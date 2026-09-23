/* qemu_diff -- differential fuzz of instruction semantics: the model vs qemu-arm (-cpu sa1100, ARMv4, no Thumb).
 *
 * Only behaviour that ARMv3 (ARM7DI) and ARMv4 define identically is generated:
 *   data processing (all opcodes, all operand-2 forms, random condition and flags; R15 only as an
 *   immediate-shift operand), MUL/MLA (C flag masked: "meaningless" on the ARM7DI), LDR/STR[B] (all addressing
 *   modes, aligned words -- qemu-user does not rotate misaligned loads), LDM/STM (all four modes, write-back, S = 0,
 *   no R15), SWP/SWPB (aligned).  R13 is the harness pointer and never appears in a generated instruction; Rd = R15,
 *   write-back onto Rd, base-in-list with write-back, Rm == Rn and the other "shall not" cases are excluded.
 *
 * Each case: load r0-r12, r14 and NZCV from a table, execute the instruction, store r0-r12, r14 and the CPSR.
 * Memory cases get a private 128-byte scratch region, compared too.
 *
 * The generated batches are kept as tests (tests/qemu_diff/s<seed>/): prog.s (the program), cases.tsv (per-case
 * metadata and compare masks), expected.bin (qemu's output = the reference).  prog.s is also the console test: it
 * is linked at 0x10000, runs from there in any privileged or user mode, and ends with SWI 0 -- qemu-user takes it
 * as write()/exit() syscalls, on the console (and in the model) it traps to vector 0x08.  See tests/qemu_diff/README.md.
 *
 *   qemu_diff --gen [--seed S] [--batches B] [--cases N]   generate + run qemu, save under --tests, then check
 *   qemu_diff [--show K]                                   check every saved batch against the model (no qemu)
 *     --tests DIR (default tests/qemu_diff)   --work DIR (default ../build/model/work/qemu_diff, build outputs only)
 * Needs arm-eabi binutils ($DC_ARM_BIN, default /opt/toolchains/dc-dca3/arm-eabi/bin); --gen also qemu-arm-static.
 * Exit status 0 iff no mismatch.
 */
#include "../src/arm7di.h"
#include "../src/dc_arm_map.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace wren7;

enum Cls { C_DP, C_MUL, C_SDT, C_BDT, C_SWP, C_COUNT };
static const char *cls_name[C_COUNT] = {"data-proc", "mul/mla", "ldr/str", "ldm/stm", "swp"};

struct Case {
    int cls;
    uint32_t op;
    uint32_t in[16];     /* r0..r12, r14, -, flags */
    int region;          /* scratch region index, -1 = none */
    uint32_t cpsr_mask;
};

static const uint32_t BASE = 0x10000, REC = 64, REGION = 128;

struct Gen {
    std::mt19937_64 rng;
    std::vector<Case> cases;
    int regions = 0;
    std::vector<uint8_t> scratch_init;

    explicit Gen(uint64_t seed) : rng(seed) {}
    uint32_t u32() { return (uint32_t)rng(); }
    uint32_t below(uint32_t n) { return (uint32_t)(rng() % n); }
    bool coin() { return rng() & 1; }
    int reg() { int r = below(14); return r == 13 ? 14 : r; }   /* r0-r12, r14 */
    int reg_not(std::initializer_list<int> ex) {
        for (;;) { int r = reg(); bool ok = true; for (int e : ex) ok &= r != e; if (ok) return r; }
    }
    uint32_t value() {
        static const uint32_t special[] = {0, 1, 2, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF, 0xFFFFFFFE,
                                           31, 32, 33, 0x100, 0xFF, 0x1F, 0x20};
        switch (below(6)) {
        case 0: return special[below(sizeof special / sizeof special[0])];
        case 1: return below(64);
        case 2: return u32() & 0xFF;
        default: return u32();
        }
    }
    uint32_t cond() { uint32_t c = below(16); return c == 15 ? 14 : (below(3) ? 14 : c); }
    Case base_case(int cls) {
        Case c{};
        c.cls = cls;
        for (int i = 0; i < 14; i++) c.in[i] = value();
        c.in[15] = (uint32_t)below(16) << 28;
        c.region = -1;
        c.cpsr_mask = 0xF0000000;
        return c;
    }
    static int slot(int r) { return r == 14 ? 13 : r; }
    int new_region() {
        int k = regions++;
        for (uint32_t i = 0; i < REGION; i++) scratch_init.push_back(u32());
        return k;
    }

    void gen_dp() {
        Case c = base_case(C_DP);
        const uint32_t opc = below(16);
        const bool test = (opc & 0xC) == 0x8;
        const uint32_t S = test ? 1 : coin();
        const int form = below(3);   /* 0 imm, 1 reg imm-shift, 2 reg reg-shift */
        int rn = reg(), rd = reg(), rm = reg(), rs = reg();
        if (form != 2 && below(12) == 0) rn = 15;
        if (form == 1 && below(12) == 0) rm = 15;
        if (test) rd = 0;                          /* SBZ fields: qemu UNDEFs when they are not zero */
        if (opc == 0xD || opc == 0xF) rn = 0;
        uint32_t op2;
        if (form == 0) op2 = (below(16) << 8) | below(256);
        else if (form == 1) op2 = (below(32) << 7) | (below(4) << 5) | rm;
        else {
            op2 = (rs << 8) | (below(4) << 5) | 0x10 | rm;
            static const uint32_t amts[] = {0, 1, 2, 7, 31, 32, 33, 63, 64, 100, 255, 256, 257, 0xFFFFFF20};
            if (below(3)) c.in[slot(rs)] = (u32() & ~0xFFu) | (below(2) ? amts[below(14)] & 0xFF : below(256));
        }
        c.op = (cond() << 28) | ((form == 0) << 25) | (opc << 21) | (S << 20) | (rn << 16) | (rd << 12) | op2;
        cases.push_back(c);
    }
    void gen_mul() {
        Case c = base_case(C_MUL);
        int rm = reg(), rd = reg_not({rm}), rs = reg(), rn = reg();
        const uint32_t A = coin(), S = coin();
        if (!A) rn = 0;                            /* MUL: Rn "should be zero" (qemu UNDEFs otherwise) */
        c.op = (cond() << 28) | (A << 21) | (S << 20) | (rd << 16) | (rn << 12) | (rs << 8) | 0x90 | rm;
        c.cpsr_mask = 0xD0000000;   /* N Z V: C is left undefined */
        if (below(2)) c.in[slot(rs)] = below(2) ? below(64) : (1u << below(32)) - below(2);
        cases.push_back(c);
    }
    void gen_sdt() {
        Case c = base_case(C_SDT);
        c.region = new_region();
        const uint32_t P = coin(), U = coin(), B = coin(), L = coin(), I = coin();
        const uint32_t W = P ? coin() : (below(4) == 0);   /* post-indexed W = LDRT/STRT */
        const bool wb = !P || W;
        const int rn = reg();
        const int rd = wb ? reg_not({rn}) : reg();
        uint32_t off, op2;
        if (!I) { off = below(61); op2 = off; }
        else {
            const int rm = reg_not({rn, rd});
            const uint32_t type = below(4);
            uint32_t amt, rmv;
            switch (type) {
            case 0: amt = below(4); rmv = below(16); off = rmv << amt; break;
            case 1: amt = 26 + below(7); rmv = u32(); off = amt == 32 ? 0 : rmv >> amt; break;
            case 2: amt = 26 + below(7); rmv = u32() & 0x7FFFFFFF; off = amt == 32 ? 0 : rmv >> amt; break;
            default:
                amt = below(32);
                if (amt == 0) { rmv = below(31) * 2; off = rmv >> 1; c.in[15] &= ~PSR_C; }   /* RRX, C = 0 */
                else { off = below(61); rmv = (off << amt) | (off >> (32 - amt)); }
                break;
            }
            c.in[slot(rm)] = rmv;
            op2 = ((amt & 31) << 7) | (type << 5) | rm;
        }
        uint32_t A = BASE_SCRATCH_PLACEHOLDER + REGION * c.region + 64 + below(4);
        if (!B) A &= ~3u;
        c.in[slot(rn)] = P ? (U ? A - off : A + off) : A;
        c.op = (cond() << 28) | (1u << 26) | (I << 25) | (P << 24) | (U << 23) | (B << 22) | (W << 21) | (L << 20) |
               (rn << 16) | (rd << 12) | op2;
        cases.push_back(c);
    }
    void gen_bdt() {
        Case c = base_case(C_BDT);
        c.region = new_region();
        const uint32_t P = coin(), U = coin(), W = coin(), L = coin();
        const int rn = reg();
        uint32_t list = 0;
        while (!list) {
            list = u32() & 0x5FFF;   /* r0-r12, r14 */
            if (below(3) == 0) list &= u32() & u32();
            if (W) list &= ~(1u << rn);
        }
        c.in[slot(rn)] = BASE_SCRATCH_PLACEHOLDER + REGION * c.region + 64;
        c.op = (cond() << 28) | (4u << 25) | (P << 24) | (U << 23) | (W << 21) | (L << 20) | (rn << 16) | list;
        cases.push_back(c);
    }
    void gen_swp() {
        Case c = base_case(C_SWP);
        c.region = new_region();
        const uint32_t B = coin();
        const int rn = reg(), rd = reg_not({rn}), rm = reg_not({rn});
        uint32_t A = BASE_SCRATCH_PLACEHOLDER + REGION * c.region + 64 + below(4);
        if (!B) A &= ~3u;
        c.in[slot(rn)] = A;
        c.op = (cond() << 28) | 0x01000090 | (B << 22) | (rn << 16) | (rd << 12) | rm;
        cases.push_back(c);
    }
    /* scratch addresses are generated relative to the scratch label and relocated by the linker */
    static const uint32_t BASE_SCRATCH_PLACEHOLDER = 0;

    void generate(int n) {
        for (int i = 0; i < n; i++) {
            uint32_t k = below(100);
            if (k < 55) gen_dp();
            else if (k < 67) gen_mul();
            else if (k < 82) gen_sdt();
            else if (k < 94) gen_bdt();
            else gen_swp();
        }
    }
};

/* The base register of a memory case holds a scratch-relative address in the input table. */
static int base_slot(const Case &c)
{
    if (c.region < 0) return -1;
    int rn = (c.op >> 16) & 15;
    return rn == 14 ? 13 : rn;
}

static std::string hex(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08x", v); return b; }

static bool run(const std::string &cmd)
{
    int rc = system(cmd.c_str());
    if (rc != 0) fprintf(stderr, "command failed (%d): %s\n", rc, cmd.c_str());
    return rc == 0;
}

static bool read_file(const std::string &p, std::vector<uint8_t> &out)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    for (int c; (c = fgetc(f)) != EOF;) out.push_back(c);
    fclose(f);
    return true;
}

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static std::string T;   /* binutils prefix */

static void write_batch(const std::string &tdir, const Gen &g, uint64_t seed)
{
    FILE *f = fopen((tdir + "/prog.s").c_str(), "w");
    fprintf(f, "@ wren7 qemu_diff batch, seed %llu, %zu cases -- generated by model/tools/qemu_diff --gen, do not edit.\n"
               "@ Link .text at 0x10000 and run from 0x10000.  Results: out_tab (64 bytes per case: r0-r12, r14, CPSR),\n"
               "@ scratch (128 bytes per memory case).  Ends with three SWI 0 (qemu-user: write, write, exit).\n",
            (unsigned long long)seed, g.cases.size());
    fprintf(f, ".macro CASE i, op\n\tldr r13, =in_tab+(\\i*64)\n\tldr r0, [r13, #60]\n\tmsr cpsr_f, r0\n"
               "\tldmia r13, {r0-r12, r14}\n\t.word \\op\n\tldr r13, =out_tab+(\\i*64)\n\tstmia r13, {r0-r12, r14}\n"
               "\tmrs r0, cpsr\n\tstr r0, [r13, #56]\n.endm\n.macro POOL\n\tb 1f\n\t.ltorg\n1:\n.endm\n");
    fprintf(f, ".text\n.global _start\n_start:\n");
    for (size_t i = 0; i < g.cases.size(); i++) {
        fprintf(f, "\tCASE %zu, 0x%08x\n", i, g.cases[i].op);
        if (i % 32 == 31) fprintf(f, "\tPOOL\n");
    }
    fprintf(f, "\tmov r0, #1\n\tldr r1, =out_tab\n\tldr r2, =%zu\n\tmov r7, #4\n\tswi 0\n", g.cases.size() * REC);
    fprintf(f, "\tmov r0, #1\n\tldr r1, =scratch\n\tldr r2, =%zu\n\tmov r7, #4\n\tswi 0\n",
            (size_t)g.regions * REGION);
    fprintf(f, "\tmov r0, #0\n\tmov r7, #1\n\tswi 0\n\t.ltorg\n.data\n.align 4\nin_tab:\n");
    for (auto &c : g.cases) {
        const int bs = base_slot(c);
        fprintf(f, "\t.word ");
        for (int k = 0; k < 16; k++) {
            if (k == bs) fprintf(f, "scratch+0x%x", c.in[k]);
            else fprintf(f, "0x%08x", c.in[k]);
            fprintf(f, k == 15 ? "\n" : ",");
        }
    }
    fprintf(f, ".align 4\nout_tab:\n\t.space %zu\n.align 4\nscratch:\n", g.cases.size() * REC);
    for (size_t i = 0; i < g.scratch_init.size(); i += 32) {
        fprintf(f, "\t.word ");
        for (size_t j = i; j < i + 32; j += 4) fprintf(f, "0x%08x%s", le32(&g.scratch_init[j]), j + 4 < i + 32 ? "," : "\n");
    }
    fclose(f);

    f = fopen((tdir + "/cases.tsv").c_str(), "w");
    fprintf(f, "# case class op cpsr_mask region (inputs: in_tab in prog.s; region = 128-byte scratch block, -1 none)\n");
    for (size_t i = 0; i < g.cases.size(); i++) {
        const Case &c = g.cases[i];
        fprintf(f, "%zu\t%s\t%08x\t%08x\t%d\n", i, cls_name[c.cls], c.op, c.cpsr_mask, c.region);
    }
    fclose(f);
}

static bool load_cases(const std::string &tdir, std::vector<Case> &cases, int &regions)
{
    FILE *f = fopen((tdir + "/cases.tsv").c_str(), "r");
    if (!f) return false;
    char line[512];
    cases.clear();
    regions = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        Case c{};
        char cls[32];
        unsigned idx;
        if (sscanf(line, "%u %31s %x %x %d", &idx, cls, &c.op, &c.cpsr_mask, &c.region) != 5) continue;
        c.cls = 0;
        for (int k = 0; k < C_COUNT; k++) if (!strcmp(cls, cls_name[k])) c.cls = k;
        if (c.region >= 0) regions = std::max(regions, c.region + 1);
        cases.push_back(c);
    }
    fclose(f);
    return true;
}

struct Totals { uint64_t total = 0, bad = 0, per[C_COUNT] = {0}, bad_per[C_COUNT] = {0}; int shown = 0; };

/* Assemble a saved batch, run it on the model, compare with expected.bin. */
static bool check_batch(const std::string &tdir, const std::string &wdir, Totals &tot, int show)
{
    std::vector<Case> cases;
    int regions;
    if (!load_cases(tdir, cases, regions)) { fprintf(stderr, "%s: no cases.tsv\n", tdir.c_str()); return false; }
    const std::string o = wdir + "/prog.o", elf = wdir + "/prog.elf", bin = wdir + "/prog.bin", sym = wdir + "/prog.sym";
    if (!run("mkdir -p " + wdir)) return false;
    if (!run(T + "as -mcpu=arm7 " + tdir + "/prog.s -o " + o)) return false;
    if (!run(T + "ld -Ttext=0x10000 -z max-page-size=0x1000 -e _start --no-warn-rwx-segments -o " + elf + " " + o))
        return false;
    if (!run(T + "objcopy -O binary " + elf + " " + bin)) return false;
    if (!run(T + "nm " + elf + " > " + sym)) return false;
    uint32_t in_tab = 0, out_tab = 0, scratch = 0;
    {
        FILE *sf = fopen(sym.c_str(), "r");
        char line[256];
        while (fgets(line, sizeof line, sf)) {
            unsigned a;
            char t, name[128];
            if (sscanf(line, "%x %c %127s", &a, &t, name) != 3) continue;
            if (!strcmp(name, "in_tab")) in_tab = a;
            if (!strcmp(name, "out_tab")) out_tab = a;
            if (!strcmp(name, "scratch")) scratch = a;
        }
        fclose(sf);
    }
    std::vector<uint8_t> img, q;
    read_file(bin, img);
    read_file(tdir + "/expected.bin", q);
    const size_t qneed = cases.size() * REC + (size_t)regions * REGION;
    if (!in_tab || !out_tab || !scratch || q.size() != qneed) {
        fprintf(stderr, "%s: expected.bin has %zu bytes, want %zu (or symbols missing)\n", tdir.c_str(), q.size(), qneed);
        return false;
    }
    if (BASE + img.size() > DcArmBus::RAM_SIZE) { fprintf(stderr, "program too large for wave RAM\n"); return false; }

    DcArmBus bus;
    bus.load(BASE, img.data(), img.size());
    bus.wr32(0, 0xE51FF004);   /* reset vector: ldr pc, [pc, #-4] */
    bus.wr32(4, BASE);
    Arm7DI cpu(&bus);
    bus.attach(&cpu);
    while (cpu.exec_word() != 0xEF000000 && cpu.cycles < 200000000ull) cpu.step();
    if (cpu.exec_word() != 0xEF000000) { fprintf(stderr, "%s: model did not reach the SWI\n", tdir.c_str()); return false; }

    uint64_t bad0 = tot.bad;
    for (size_t i = 0; i < cases.size(); i++) {
        const Case &c = cases[i];
        const uint8_t *qr = &q[i * REC];
        std::string diff;
        for (int k = 0; k < 15; k++) {
            const uint32_t mv = bus.rd32(out_tab + i * REC + 4 * k), qv = le32(qr + 4 * k);
            const uint32_t mask = k == 14 ? c.cpsr_mask : 0xFFFFFFFF;
            if ((mv & mask) != (qv & mask))
                diff += std::string(" ") + (k == 14 ? "cpsr" : k == 13 ? "r14" : ("r" + std::to_string(k))) +
                        " model=" + hex(mv) + " ref=" + hex(qv);
        }
        if (c.region >= 0) {
            const uint8_t *qs = &q[cases.size() * REC + (size_t)c.region * REGION];
            for (uint32_t b = 0; b < REGION; b += 4) {
                const uint32_t mv = bus.rd32(scratch + c.region * REGION + b), qv = le32(qs + b);
                if (mv != qv) diff += " mem+" + std::to_string(b) + " model=" + hex(mv) + " ref=" + hex(qv);
            }
        }
        tot.total++;
        tot.per[c.cls]++;
        if (!diff.empty()) {
            tot.bad++;
            tot.bad_per[c.cls]++;
            if (tot.shown++ < show) {
                printf("MISMATCH %s case %zu %s op=%08x flags=%x r0-r12,r14:", tdir.c_str(), i, cls_name[c.cls], c.op,
                       bus.rd32(in_tab + i * REC + 60) >> 28);
                for (int k = 0; k < 14; k++) printf(" %08x", bus.rd32(in_tab + i * REC + 4 * k));
                printf("\n   %s\n", diff.c_str());
            }
        }
    }
    printf("%-28s %6zu cases %6llu mismatches  (%llu model cycles)\n", tdir.c_str(), cases.size(),
           (unsigned long long)(tot.bad - bad0), (unsigned long long)cpu.cycles);
    return true;
}

int main(int argc, char **argv)
{
    int ncases = 1000, batches = 4, show = 20;
    uint64_t seed = 1;
    bool gen = false;
    std::string tests = "tests/qemu_diff", work = "../build/model/work/qemu_diff";
    for (int i = 1; i < argc; i++) {
        auto val = [&]() -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "%s needs an argument\n", argv[i]); exit(2); }
            return argv[++i];
        };
        if (!strcmp(argv[i], "--gen")) gen = true;
        else if (!strcmp(argv[i], "--cases")) ncases = atoi(val());
        else if (!strcmp(argv[i], "--batches")) batches = atoi(val());
        else if (!strcmp(argv[i], "--seed")) seed = strtoull(val(), 0, 0);
        else if (!strcmp(argv[i], "--tests")) tests = val();
        else if (!strcmp(argv[i], "--work")) work = val();
        else if (!strcmp(argv[i], "--show")) show = atoi(val());
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    const char *tb = getenv("DC_ARM_BIN");
    T = std::string(tb ? tb : "/opt/toolchains/dc-dca3/arm-eabi/bin") + "/arm-eabi-";

    std::vector<std::string> dirs;
    if (gen) {
        for (int bt = 0; bt < batches; bt++) {
            const uint64_t sd = seed + bt;
            const std::string name = "s" + std::to_string(sd), tdir = tests + "/" + name, wdir = work + "/" + name;
            if (!run("mkdir -p " + tdir + " " + wdir)) return 2;
            Gen g(sd);
            g.generate(ncases);
            write_batch(tdir, g, sd);
            if (!run(T + "as -mcpu=arm7 " + tdir + "/prog.s -o " + wdir + "/prog.o")) return 2;
            if (!run(T + "ld -Ttext=0x10000 -z max-page-size=0x1000 -e _start --no-warn-rwx-segments -o " + wdir +
                     "/prog.elf " + wdir + "/prog.o"))
                return 2;
            if (!run("qemu-arm-static -cpu sa1100 " + wdir + "/prog.elf > " + tdir + "/expected.bin")) return 2;
            dirs.push_back(name);
        }
    } else {
        if (DIR *d = opendir(tests.c_str())) {
            while (dirent *e = readdir(d))
                if (e->d_name[0] == 's') dirs.push_back(e->d_name);
            closedir(d);
        }
        std::sort(dirs.begin(), dirs.end());
        if (dirs.empty()) { fprintf(stderr, "no batches under %s (run with --gen)\n", tests.c_str()); return 2; }
    }
    Totals tot;
    for (auto &n : dirs)
        if (!check_batch(tests + "/" + n, work + "/" + n, tot, show)) return 2;
    for (int k = 0; k < C_COUNT; k++)
        printf("%-10s %8llu cases, %llu mismatches\n", cls_name[k], (unsigned long long)tot.per[k],
               (unsigned long long)tot.bad_per[k]);
    printf("total %llu cases, %llu mismatches\n", (unsigned long long)tot.total, (unsigned long long)tot.bad);
    return tot.bad ? 1 : 0;
}
