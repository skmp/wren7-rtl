/* runner.c -- wren7 console job runner (KOS).  Runs every job of MODEL_ROOT/../build/hw/jobs.txt on the AICA ARM and
 * writes the results next to it through dcload /pc/.  Run only through ../run_hw.sh (which goes through hwrun.sh).
 *
 * Job file (one directive per line, '#' comments; paths relative to MODEL_ROOT = wren7-rtl/model):
 *   out DIR                      output directory for everything below
 *   job NAME TIMEOUT_MS REPEAT   start a job
 *   clear                        zero all 2 MB of wave RAM before loading (default: only the result block)
 *   load ADDR FILE               copy FILE into wave RAM at ARM address ADDR
 *   word ADDR VALUE              patch one 32-bit word (after the loads)
 *   read ADDR LEN                wave RAM region saved to DIR/NAME.bin after the run (regions concatenated)
 *   rreg OFF N                   AICA register OFF read N times ~1 sample (23 us) apart, after the run with the ARM
 *                                in reset and 5 ms of settling; the words are appended to DIR/NAME.bin after the reads
 *   quiet                        before the loads: every channel keyed off and zeroed (20 ms), MPRO and MEMS cleared
 *                                (first, so the previous job's DSP program cannot write the freshly loaded RAM)
 *   areg OFF VALUE               AICA register write from the SH4 (0x700000 + OFF) after the loads, in order
 *   sh4load MODE ADDR GAP        during the run the SH4 accesses G2 (1 read / 2 write wave RAM at ADDR, 3 read / 4
 *                                write AICA register ADDR) at most every GAP SH4 cycles; count logged as sh4acc=N
 *   run                          execute the job
 *   chstate                      results.txt line "chstate EG:CA x64" (channel monitors, MSLC 0..63)
 *   samples N                    calibration: SH4 cycles over N AICA sample intervals (MCIPD bit 10), ARM stopped;
 *                                results.txt line "samples N CYCLES"
 * Per repeat: ARM held in reset (ARMRST), RAM prepared, MCIEB = SCPU only, pending bits cleared, then with interrupts
 * off: t0 = PRFC0 (SH4 CPU cycles), ARMRST released, poll SB_ISTEXT bit 1 (the AICA interrupt -- raised when the ARM
 * writes MCIPD.SCPU in stub.s), t1.  Polling Holly keeps the SH4 off the G2/AICA bus during the run.
 * DIR/results.txt: "NAME STATUS MCIPD T1 T2 ..." with STATUS done / timeout / stuck (interrupt already pending) and
 * Tn = t1 - t0 in SH4 cycles for repeat n.
 */
#include <kos.h>
#include <dc/g2bus.h>
#include <dc/perfctr.h>
#include <dc/spu.h>
#include <arch/irq.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "/pc" MODEL_ROOT "/"
#define REG(o) (0xA0700000u + (o))
#define RAM(o) (0xA0800000u + (o))
#define SB_ISTEXT (*(volatile uint32_t *)0xA05F6904)
#define MCIEB 0x28B4
#define MCIPD 0x28B8
#define MCIRE 0x28BC
#define RES 0x1FF000

typedef struct { uint32_t addr; char path[160]; } load_t;
typedef struct { uint32_t addr, val; } word_t;
typedef struct { uint32_t addr, len; } rd_t;
typedef struct {
    char name[64];
    uint32_t timeout_ms, repeat;
    int clear, nl, nw, nr;
    load_t l[8];
    word_t w[64];
    rd_t r[8];
    rd_t g[8];   /* rreg: addr = register offset, len = count */
    int ng;
    int quiet, na;
    uint32_t sh4mode, sh4addr, sh4gap;   /* SH4 G2 traffic during the run (sh4load) */
    word_t a[1536];   /* AICA register writes (offset, value) from the SH4, after the loads */
} job_t;

typedef struct { char path[160]; uint8_t *data; size_t len; } img_t;
static img_t cache[48];
static int ncache;

static uint8_t *slurp(const char *rel, size_t *len)
{
    for (int i = 0; i < ncache; i++)
        if (!strcmp(cache[i].path, rel)) { *len = cache[i].len; return cache[i].data; }
    char p[256];
    snprintf(p, sizeof p, ROOT "%s", rel);
    FILE *f = fopen(p, "rb");
    if (!f) { printf("cannot open %s\n", p); return NULL; }
    size_t cap = 4096, n = 0;
    uint8_t *buf = malloc(cap);
    for (;;) {
        if (n + 4096 > cap) { cap *= 2; buf = realloc(buf, cap); }
        size_t got = fread(buf + n, 1, 4096, f);
        n += got;
        if (got == 0) break;
    }
    fclose(f);
    if (ncache < 48) {
        strncpy(cache[ncache].path, rel, sizeof cache[ncache].path - 1);
        cache[ncache].data = buf;
        cache[ncache].len = n;
        ncache++;
    }
    *len = n;
    return buf;
}

static void arm_stop(void)
{
    g2_write_32(REG(0x2C00), g2_read_32(REG(0x2C00)) | 1);
    g2_fifo_wait();
}

static void ram_write(uint32_t addr, const uint8_t *d, size_t len)
{
    for (size_t i = 0; i < len; i += 4) {
        uint32_t v = 0;
        for (int b = 0; b < 4 && i + b < len; b++) v |= (uint32_t)d[i + b] << (8 * b);
        if ((i & 31) == 0) g2_fifo_wait();
        g2_write_32(RAM(addr + i), v);
    }
    g2_fifo_wait();
}

/* poll MCIPD until (value & mask) != 0, at most max_cycles SH4 cycles; returns -1 on timeout */
static int wait_bit(uint32_t mask, uint64_t max_cycles)
{
    const uint64_t t0 = perf_cntr_count(PRFC0);
    while (!(g2_read_32(REG(MCIPD)) & mask))
        if (perf_cntr_count(PRFC0) - t0 > max_cycles) return -1;
    return 0;
}

/* every channel keyed off and its registers zeroed, as caique's aica_quiet(); the ARM is already in reset */
static void aica_quiet(void)
{
    for (int c = 0; c < 64; c++) {
        g2_write_32(REG(0x80 * c), 0);
        g2_write_32(REG(0x80 * c + 0x14), 0x1F);
        if ((c & 7) == 7) g2_fifo_wait();
    }
    g2_write_32(REG(0), 0x8000);   /* KYONEX: key everything off */
    for (uint32_t a = 0x3400; a < 0x3C00; a += 4) {   /* and an empty DSP program, MEMS cleared (+4 = bits 23:8) */
        g2_write_32(REG(a), 0);
        if ((a & 31) == 28) g2_fifo_wait();
    }
    for (uint32_t a = 0x4400; a < 0x4500; a += 8) g2_write_32(REG(a + 4), 0);
    g2_fifo_wait();
    timer_spin_sleep(20);
    for (int c = 0; c < 64; c++)
        for (int r = 0; r < 0x80; r += 4) {
            g2_write_32(REG(0x80 * c + r), 0);
            if ((r & 31) == 28) g2_fifo_wait();
        }
    g2_fifo_wait();
}

static uint32_t sh4acc;
static char outdir[160];
static FILE *results;

static int run_job(job_t *j)
{
    uint64_t t[16];
    const char *status = "done";
    uint32_t mcipd = 0;
    uint32_t reps = j->repeat < 1 ? 1 : (j->repeat > 16 ? 16 : j->repeat);
    for (uint32_t rep = 0; rep < reps; rep++) {
        arm_stop();
        if (j->quiet) aica_quiet();
        if (j->clear) spu_memset_sq(0, 0, 0x200000);
        else for (uint32_t a = RES; a < RES + 0x100; a += 4) g2_write_32(RAM(a), 0);
        for (int i = 0; i < j->nl; i++) {
            size_t len;
            uint8_t *d = slurp(j->l[i].path, &len);
            if (!d) return -1;
            ram_write(j->l[i].addr, d, len);
        }
        for (int i = 0; i < j->nw; i++) g2_write_32(RAM(j->w[i].addr), j->w[i].val);
        for (int i = 0; i < j->na; i++) {
            g2_write_32(REG(j->a[i].addr), j->a[i].val);
            if ((i & 7) == 7) g2_fifo_wait();
        }
        g2_write_32(REG(MCIEB), 0x20);
        g2_write_32(REG(MCIRE), 0x7FF);
        g2_fifo_wait();
        const uint32_t rst = g2_read_32(REG(0x2C00)) & ~1u;
        if (SB_ISTEXT & 2) status = "stuck";
        const uint64_t tmo = (uint64_t)j->timeout_ms * 200000u;
        int old = irq_disable();
        const uint64_t t0 = perf_cntr_count(PRFC0);
        g2_write_32(REG(0x2C00), rst);
        uint64_t t1, tl = t0;
        uint32_t nacc = 0;
        volatile uint32_t *const sp = (volatile uint32_t *)((j->sh4mode <= 2 ? 0xA0800000u : 0xA0700000u) + j->sh4addr);
        for (;;) {
            t1 = perf_cntr_count(PRFC0);
            if (SB_ISTEXT & 2) break;
            if (t1 - t0 > tmo) { status = "timeout"; break; }
            if (j->sh4mode && t1 - tl >= j->sh4gap) {   /* 1 read RAM, 2 write RAM, 3 read register, 4 write register */
                if (j->sh4mode & 1) (void)*sp;
                else *sp = nacc;
                nacc++;
                tl = t1;
            }
        }
        sh4acc = nacc;
        irq_restore(old);
        arm_stop();
        mcipd = g2_read_32(REG(MCIPD));
        g2_write_32(REG(MCIRE), 0x20);
        g2_write_32(REG(MCIEB), 0);
        g2_fifo_wait();
        t[rep] = t1 - t0;
    }
    if (j->nr || j->ng) {
        char p[320];
        snprintf(p, sizeof p, ROOT "%s/%s.bin", outdir, j->name);
        FILE *f = fopen(p, "wb");
        if (!f) { printf("cannot write %s\n", p); return -1; }
        for (int i = 0; i < j->nr; i++) {
            uint32_t n = (j->r[i].len + 3) / 4;
            uint32_t *buf = malloc(n * 4);
            for (uint32_t k = 0; k < n; k++) buf[k] = g2_read_32(RAM(j->r[i].addr + 4 * k));
            fwrite(buf, 1, j->r[i].len, f);
            free(buf);
        }
        if (j->ng) timer_spin_sleep(5);   /* the ARM is in reset: only the channels and the DSP touch wave RAM */
        for (int i = 0; i < j->ng; i++)
            for (uint32_t k = 0; k < j->g[i].len; k++) {
                const uint64_t w0 = perf_cntr_count(PRFC0);
                const uint32_t v = g2_read_32(REG(j->g[i].addr));
                fwrite(&v, 1, 4, f);
                while (perf_cntr_count(PRFC0) - w0 < 4600) ;
            }
        fclose(f);
    }
    fprintf(results, "%s %s %08lx", j->name, status, (unsigned long)mcipd);
    for (uint32_t rep = 0; rep < reps; rep++) fprintf(results, " %llu", (unsigned long long)t[rep]);
    if (j->sh4mode) fprintf(results, " sh4acc=%lu", (unsigned long)sh4acc);
    fprintf(results, "\n");
    printf("%-32s %-7s %llu\n", j->name, status, (unsigned long long)t[reps - 1]);
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t len;
    uint8_t *txt = slurp("../build/hw/jobs.txt", &len);
    if (!txt) return 1;
    char *s = malloc(len + 1);
    memcpy(s, txt, len);
    s[len] = 0;
    ncache = 0;   /* the job file itself is not an image */

    const uint64_t c0 = perf_cntr_count(PRFC0);
    arm_stop();
    spu_memset_sq(0, 0, 0x200000);
    printf("clear 2 MB wave RAM: %llu SH4 cycles\n", (unsigned long long)(perf_cntr_count(PRFC0) - c0));
    /* silence the DSP: MPRO (0x3400-0x3BFC) and the ring buffer control stay zero */
    for (uint32_t a = 0x3400; a < 0x3C00; a += 4) g2_write_32(REG(a), 0);
    g2_fifo_wait();

    static job_t job;
    int njobs = 0, err = 0;
    char *save = NULL;
    for (char *line = strtok_r(s, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char cmd[16], a1[160], a2[160], a3[32];
        int n = sscanf(line, "%15s %159s %159s %31s", cmd, a1, a2, a3);
        if (n < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "out") && n >= 2) {
            strncpy(outdir, a1, sizeof outdir - 1);
            char p[320];
            snprintf(p, sizeof p, ROOT "%s/results.txt", outdir);
            results = fopen(p, "w");
            if (!results) { printf("cannot write %s\n", p); return 1; }
        } else if (!strcmp(cmd, "job") && n >= 4) {
            memset(&job, 0, sizeof job);
            strncpy(job.name, a1, sizeof job.name - 1);
            job.timeout_ms = strtoul(a2, 0, 0);
            job.repeat = strtoul(a3, 0, 0);
        } else if (!strcmp(cmd, "clear")) {
            job.clear = 1;
        } else if (!strcmp(cmd, "sh4load") && n >= 4) {
            job.sh4mode = strtoul(a1, 0, 0);
            job.sh4addr = strtoul(a2, 0, 0);
            job.sh4gap = strtoul(a3, 0, 0);
        } else if (!strcmp(cmd, "quiet")) {
            job.quiet = 1;
        } else if (!strcmp(cmd, "areg") && n >= 3 && job.na < 1536) {
            job.a[job.na].addr = strtoul(a1, 0, 0);
            job.a[job.na].val = strtoul(a2, 0, 0);
            job.na++;
        } else if (!strcmp(cmd, "load") && n >= 3 && job.nl < 8) {
            job.l[job.nl].addr = strtoul(a1, 0, 0);
            strncpy(job.l[job.nl].path, a2, sizeof job.l[0].path - 1);
            job.nl++;
        } else if (!strcmp(cmd, "word") && n >= 3 && job.nw < 64) {
            job.w[job.nw].addr = strtoul(a1, 0, 0);
            job.w[job.nw].val = strtoul(a2, 0, 0);
            job.nw++;
        } else if (!strcmp(cmd, "read") && n >= 3 && job.nr < 8) {
            job.r[job.nr].addr = strtoul(a1, 0, 0);
            job.r[job.nr].len = strtoul(a2, 0, 0);
            job.nr++;
        } else if (!strcmp(cmd, "rreg") && n >= 3 && job.ng < 8) {
            job.g[job.ng].addr = strtoul(a1, 0, 0);
            job.g[job.ng].len = strtoul(a2, 0, 0) > 64 ? 64 : strtoul(a2, 0, 0);
            job.ng++;
        } else if (!strcmp(cmd, "samples") && n >= 2) {
            /* every wait is bounded (1 ms per sample edge): a register that never changes must not hang the console
             * (2026-09-23: waiting for bit 10 to read back 0 after the MCIRE clear never returned) */
            if (!results) { printf("no out directive\n"); return 1; }
            const uint32_t N = strtoul(a1, 0, 0);
            arm_stop();
            int old = irq_disable();   /* an interrupt handler longer than a sample would hide an edge */
            int ok = wait_bit(0x400, 200000) >= 0;
            g2_write_32(REG(MCIRE), 0x400);
            const uint64_t t0 = perf_cntr_count(PRFC0);
            for (uint32_t k = 0; k < N && ok; k++) {
                ok = wait_bit(0x400, 200000) >= 0;
                g2_write_32(REG(MCIRE), 0x400);
            }
            const uint64_t t1 = perf_cntr_count(PRFC0);
            irq_restore(old);
            fprintf(results, "samples %lu %llu%s\n", (unsigned long)N, (unsigned long long)(t1 - t0), ok ? "" : " timeout");
            printf("samples %lu: %llu SH4 cycles%s\n", (unsigned long)N, (unsigned long long)(t1 - t0), ok ? "" : " TIMEOUT");
        } else if (!strcmp(cmd, "chstate")) {
            /* every channel's monitor word (MSLC = channel, 0x2810: LP, SGC state, EG) and CA (0x2814) */
            if (!results) { printf("no out directive\n"); return 1; }
            fprintf(results, "chstate");
            for (uint32_t ch = 0; ch < 64; ch++) {
                g2_write_32(REG(0x280C), ch << 8);
                g2_fifo_wait();
                const uint32_t eg = g2_read_32(REG(0x2810)) & 0xFFFF, ca = g2_read_32(REG(0x2814)) & 0xFFFF;
                fprintf(results, " %04lx:%04lx", (unsigned long)eg, (unsigned long)ca);
            }
            fprintf(results, "\n");
        } else if (!strcmp(cmd, "run")) {
            if (!results) { printf("no out directive\n"); return 1; }
            if (run_job(&job)) err = 1;
            njobs++;
        } else {
            printf("bad line: %s\n", line);
            err = 1;
        }
    }
    if (results) fclose(results);
    printf("%d jobs\n", njobs);
    return err;
}
