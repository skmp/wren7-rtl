/* core_tb -- wren7 v1 (rtl/v1/wren7_core.sv) in lockstep with the C++ model (model/src/arm7di.cpp).
 *
 * The model runs on its bus and every bus cycle it makes is replayed on the RTL in the same clock: the RTL's request
 * (type, address, flags, write data) must equal the model's, and the RTL gets the model's answer (wait clocks, read
 * data, abort).  At every instruction boundary pc, CPSR, the execute-stage word, the 30 registers and the SPSRs
 * must match.  Sources:
 *   vendor        the vendor suite bins, zero waits and the console's bus timing (DcWaits::dreamcast)
 *   qemu          the kept qemu_diff batches (../build/model/work/qemu_diff/s<n>/prog.bin, built by `make -C model test`)
 *   jobs S[,S..]  every job of tests/hw/S/jobs.txt on DcArmBus as the console suites run it (sample FIQ, SH4
 *                 traffic, DSP slots, channels), until the done stub
 *   bin FILE [MAX] a raw binary at 0 (zero waits) until 0xDEADBEEF or MAX clocks
 *   chaos N [S]   N seeded runs (seeds S..S+N-1): vendor and qemu_diff programs and random instruction soup, with random
 *                 wait states, aborts, FIQ / IRQ, nRESET pulses, ce = 0 clocks and stray bus_ack / bus_abort
 * Run from wren7-rtl/model (job paths are relative to it).  Prints one line per mode; exit status 0 iff no mismatch.
 */
#include "Vwren7_core.h"
#include "Vwren7_core___024root.h"
#include "verilated.h"
#include "arm7di.h"
#include "dc_arm_map.h"
#include "hwjob.h"
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace wren7;

struct Mismatch { std::string what; };

static uint32_t psr11(uint32_t p) { return ((p >> 28) << 7) | (((p >> 6) & 3) << 5) | (p & 0x1F); }

/* The model's bus with the RTL slaved to it. */
struct Lock : Arm7Bus {
    Arm7Bus *inner;
    Arm7DI *cpu = nullptr;
    std::unique_ptr<VerilatedContext> ctx;
    std::unique_ptr<Vwren7_core> T;
    uint64_t now = 0;                 /* RTL clocks with ce = 1 since the reset release = model MCLK */
    bool bound = false;               /* the next cycle begins an instruction / entry */
    bool full_check = true;           /* registers at every boundary (else at the end only) */
    bool chaos = false;
    bool hold_lvl = false;
    std::mt19937_64 rng;
    struct H { uint64_t t; uint32_t addr, wdata, rdata, wait; uint8_t type, flags; bool abort; };
    H hist[32];
    unsigned hn = 0;

    Lock(Arm7Bus *in, uint64_t seed) : inner(in), ctx(new VerilatedContext), rng(seed) {
        T.reset(new Vwren7_core(ctx.get()));
        T->clk = 0; T->rst = 1; T->ce = 1; T->hold = 0; T->fiq = 0; T->irq = 0;
        T->bus_ack = 0; T->bus_rdata = 0; T->bus_abort = 0;
        clock(); clock();
        T->rst = 0;
        T->eval();
    }
    void clock() { T->clk = 0; T->eval(); T->clk = 1; T->eval(); }

    [[noreturn]] void fail(const char *fmt, ...) {
        char b[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        std::string s = b;
        s += "\n    last model cycles (t type addr flags wdata rdata wait abort):";
        for (unsigned i = hn > 12 ? hn - 12 : 0; i < hn; i++) {
            const H &h = hist[i % 32];
            snprintf(b, sizeof b, "\n      %10llu %c %08x %02x %08x %08x %u%s", (unsigned long long)h.t, "NSIC"[h.type],
                     h.addr, h.flags, h.wdata, h.rdata, h.wait, h.abort ? " ABORT" : "");
            s += b;
        }
        throw Mismatch{s};
    }

    /* the RTL's architectural state against the model's (both at a boundary) */
    void check_state(const char *where) {
        if (!cpu) return;
        if (T->dbg_pc != cpu->reg(15)) fail("%s: pc rtl %08x model %08x", where, T->dbg_pc, cpu->reg(15));
        if (T->dbg_cpsr != cpu->cpsr()) fail("%s: cpsr rtl %08x model %08x", where, T->dbg_cpsr, cpu->cpsr());
        if (T->dbg_ex != cpu->exec_word()) fail("%s: ex rtl %08x model %08x", where, T->dbg_ex, cpu->exec_word());
        if (!full_check && strcmp(where, "end")) return;
        auto *R = T->rootp;
        for (int i = 0; i < 30; i++) {
            int r, b;
            if (i < 15) { r = i; b = B_USR; }
            else if (i < 22) { r = 8 + i - 15; b = B_FIQ; }
            else { r = 13 + ((i - 22) & 1); b = B_IRQ + (i - 22) / 2; }
            const uint32_t m = cpu->reg_bank(r, b), v = R->wren7_core__DOT__u_rf__DOT__mem_a[i];
            if (m != v) fail("%s: r%d bank %d (phys %d) rtl %08x model %08x", where, r, b, i, v, m);
        }
        const uint32_t sp[5] = {R->wren7_core__DOT__spsr_fiq, R->wren7_core__DOT__spsr_irq, R->wren7_core__DOT__spsr_svc,
                                R->wren7_core__DOT__spsr_abt, R->wren7_core__DOT__spsr_und};
        for (int b = B_FIQ; b <= B_UND; b++)
            if (sp[b - 1] != psr11(cpu->spsr(b)))
                fail("%s: spsr bank %d rtl %03x model %03x", where, b, sp[b - 1], psr11(cpu->spsr(b)));
    }

    void tick(uint64_t t) override {
        inner->tick(t);
        if (chaos && cpu) {
            const uint64_t x = rng();
            if ((x & 63) == 0) cpu->set_fiq(!cpu->fiq_line());
            if (((x >> 6) & 63) == 0) cpu->set_irq(!cpu->irq_line());
            if (!hold_lvl && ((x >> 12) & 4095) == 0) { hold_lvl = true; cpu->set_reset(true); }
            else if (hold_lvl && ((x >> 24) & 7) == 0) { hold_lvl = false; cpu->set_reset(false); }
        }
        bound = true;
    }

    void cycle(const BusCycle &c, BusResult &r) override {
        inner->cycle(c, r);
        const bool mem = c.type == CYC_N || c.type == CYC_S;
        if (chaos && mem) {
            const uint64_t x = rng();
            if ((x & 15) == 0) r.wait += (x >> 4) % 6;
            if (((x >> 8) & 255) == 0) r.abort = true;
        }
        hist[hn++ % 32] = H{c.t, c.addr, c.wdata, r.rdata, r.wait, c.type, c.flags, r.abort};
        if (!mem && r.wait) fail("t %llu: I cycle with %u wait clocks (the RTL has none)", (unsigned long long)c.t, r.wait);
        if (now != c.t) fail("t %llu: the RTL is at clock %llu", (unsigned long long)c.t, (unsigned long long)now);
        if (bound) {
            T->fiq = cpu ? cpu->fiq_line() : 0;
            T->irq = cpu ? cpu->irq_line() : 0;
            T->hold = hold_lvl;
        }
        T->bus_ack = 0; T->bus_abort = 0; T->bus_rdata = 0;
        if (chaos && (rng() & 7) == 0) {   /* a ce = 0 clock before the cycle's first one, with a stray ack */
            T->ce = 0; T->bus_ack = rng() & 1; T->bus_abort = rng() & 1;
            clock();
            T->ce = 1; T->bus_ack = 0; T->bus_abort = 0;
        }
        T->eval();
        const uint8_t f = (T->bus_we ? BF_WRITE : 0) | (T->bus_byte ? BF_BYTE : 0) | (T->bus_opc ? BF_OPC : 0) |
                          (T->bus_lock ? BF_LOCK : 0) | (T->bus_user ? BF_USER : 0);
        if (T->bus_type != c.type || T->bus_req != mem || T->bus_addr != c.addr || f != c.flags ||
            ((c.flags & BF_WRITE) && T->bus_wdata != c.wdata))
            fail("t %llu: rtl %c %08x flags %02x wdata %08x, model %c %08x flags %02x wdata %08x",
                 (unsigned long long)c.t, "NSIC"[T->bus_type & 3], T->bus_addr, f, T->bus_wdata, "NSIC"[c.type],
                 c.addr, c.flags, c.wdata);
        if (T->dbg_boundary != bound) fail("t %llu: rtl boundary %d, model %d", (unsigned long long)c.t, T->dbg_boundary, bound);
        if (bound) check_state("boundary");
        bound = false;
        for (uint32_t w = 0;; w++) {
            const bool last = w == r.wait;
            T->bus_ack = mem ? last : (rng() & 1);
            T->bus_rdata = (mem && last) ? r.rdata : (uint32_t)rng();
            T->bus_abort = (mem && last) ? r.abort : (rng() & 1);
            clock();
            now++;
            if (last) break;
            if (T->bus_addr != c.addr || T->bus_req != mem) fail("t %llu: request changed in wait clock %u", (unsigned long long)c.t, w);
            if (chaos && (rng() & 7) == 0) {   /* a ce = 0 clock inside the cycle */
                T->ce = 0; T->bus_ack = rng() & 1;
                clock();
                T->ce = 1;
            }
        }
        T->bus_ack = 0; T->bus_abort = 0;
    }
};

struct Totals {
    uint64_t runs = 0, bad = 0, clocks = 0, insns = 0, excs = 0;
};

static bool read_file(const std::string &p, std::vector<uint8_t> &out) { return read_all(p, out); }

/* One lockstep run: the model from power-on on `bus` until stop() at a boundary (or max_cycles). */
template <class Stop>
static bool run_one(const std::string &name, DcArmBus &bus, Stop stop, uint64_t max_cycles, bool chaos, uint64_t seed,
                    bool full_check, Totals &tot)
{
    Lock L(&bus, seed);
    L.chaos = chaos;
    L.full_check = full_check;
    bool ok = true;
    try {
        Arm7DI cpu(&L);   /* power-on: the reset entry runs through the lockstep already */
        L.cpu = &cpu;
        bus.attach(&cpu);
        while (!cpu.at_boundary() || (!stop(cpu, bus) && cpu.cycles < max_cycles)) cpu.bus_cycle();
        L.T->eval();
        if (!L.T->dbg_boundary) L.fail("end: the RTL is not at a boundary");
        L.check_state("end");
        tot.clocks += cpu.cycles;
        tot.insns += cpu.stats.instr;
        tot.excs += cpu.stats.exceptions;
        bus.attach(nullptr);
    } catch (Mismatch &e) {
        printf("MISMATCH %s: %s\n", name.c_str(), e.what.c_str());
        bus.attach(nullptr);
        ok = false;
    }
    tot.runs++;
    tot.bad += !ok;
    return ok;
}

static void report(const char *mode, const Totals &t)
{
    printf("%-18s %6llu runs, %6llu mismatches, %12llu clocks, %11llu instructions, %8llu exceptions\n", mode,
           (unsigned long long)t.runs, (unsigned long long)t.bad, (unsigned long long)t.clocks,
           (unsigned long long)t.insns, (unsigned long long)t.excs);
    fflush(stdout);
}

static std::vector<std::string> vendor_bins()
{
    std::vector<std::string> v;
    const std::string dir = "../vendor/arm7di-tests-dreamcast/bins";
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0) v.push_back(dir + "/" + n);
        }
        closedir(d);
    }
    std::sort(v.begin(), v.end());
    return v;
}

static std::vector<std::string> qemu_bins()
{
    std::vector<std::string> v;
    for (int s = 1; s < 100; s++) {
        const std::string p = "../build/model/work/qemu_diff/s" + std::to_string(s) + "/prog.bin";
        if (FILE *f = fopen(p.c_str(), "rb")) { fclose(f); v.push_back(p); }
    }
    return v;
}

static const uint32_t QBASE = 0x10000;

static void load_qemu(DcArmBus &bus, const std::vector<uint8_t> &img)
{
    bus.load(QBASE, img.data(), img.size());
    bus.wr32(0, 0xE51FF004);   /* reset vector: ldr pc, [pc, #-4] */
    bus.wr32(4, QBASE);
}

static int mode_vendor()
{
    Totals t;
    const auto bins = vendor_bins();
    if (bins.empty()) { fprintf(stderr, "no vendor bins\n"); return 2; }
    for (int dc = 0; dc < 2; dc++)
        for (auto &p : bins) {
            std::vector<uint8_t> img;
            read_file(p, img);
            DcArmBus bus;
            if (dc) bus.waits = DcWaits::dreamcast();
            bus.load(0, img.data(), img.size());
            run_one(p + (dc ? " (dreamcast)" : " (ideal)"), bus,
                    [](Arm7DI &c, DcArmBus &) { return c.exec_word() == 0xDEADBEEF; }, 1000000, false, 0, true, t);
        }
    report("vendor", t);
    return t.bad ? 1 : 0;
}

static int mode_qemu()
{
    Totals t;
    const auto bins = qemu_bins();
    if (bins.empty()) { fprintf(stderr, "no qemu_diff images (make -C model test)\n"); return 2; }
    for (int dc = 0; dc < 2; dc++)
        for (auto &p : bins) {
            std::vector<uint8_t> img;
            read_file(p, img);
            DcArmBus bus;
            if (dc) bus.waits = DcWaits::dreamcast();
            load_qemu(bus, img);
            run_one(p + (dc ? " (dreamcast)" : " (ideal)"), bus,
                    [](Arm7DI &c, DcArmBus &) { return c.exec_word() == 0xEF000000; }, 200000000ull, false, 0, true, t);
        }
    report("qemu_diff", t);
    return t.bad ? 1 : 0;
}

static int mode_jobs(const std::string &suite)
{
    HwJobFile jf;
    if (!parse_jobs("tests/hw/" + suite + "/jobs.txt", jf)) return 2;
    Totals t;
    for (auto &j : jf.jobs) {
        DcArmBus bus;
        bus.waits = DcWaits::dreamcast();
        sh4_setup(bus, j, 0);
        bool loaded = true;
        for (auto &l : j.loads) {
            std::vector<uint8_t> img;
            if (!read_all(l.second, img)) { fprintf(stderr, "%s: cannot read %s\n", j.name.c_str(), l.second.c_str()); loaded = false; break; }
            bus.load(l.first, img.data(), img.size());
        }
        if (!loaded) { t.runs++; t.bad++; continue; }
        for (auto &p : j.words) bus.wr32(p.first, p.second);
        for (auto &p : j.aregs) bus.reg_write(p.first, (uint16_t)p.second);
        run_one(suite + "/" + j.name, bus, [](Arm7DI &, DcArmBus &b) { return b.scpu_raised; }, 400000000ull, false, 0,
                false, t);
    }
    report(("jobs " + suite).c_str(), t);
    return t.bad ? 1 : 0;
}

/* Random instruction soup: 2 MB of instructions drawn from a weighted mix (most with condition AL), so that runs
 * stay on straight-line code long enough for flags and registers to carry edge values into later instructions:
 * data operations of every form (Rd = R15 now and then), shift-edge immediates (0, 1, 31, 32, 33, 64, 255 ..)
 * moved mostly into r0-r3, which also give most register-specified shifts their amount (half of those shifts
 * come right after such a MOV),
 * MUL / MLA, LDR / STR, LDM / STM, SWP, MSR / MRS (modes, flags, SPSR), TEQP-style tests, a few SWI / undefined /
 * coprocessor words, and jumps that keep moving forward: short branches, ADD pc, pc, Rm, LSL #2, and ADDS pc, pc, #k
 * (the SPSR -> CPSR return form).  Backward jumps (B back, MOV pc, Rm with the small edge values, returns through
 * lr) trapped most runs in loops of a few dozen instructions. */
static void soup(DcArmBus &bus, std::mt19937_64 &rng)
{
    static const uint8_t edge[12] = {0, 1, 2, 31, 32, 33, 63, 64, 65, 128, 224, 255};
    static const uint8_t modes[6] = {0x10, 0x11, 0x12, 0x13, 0x17, 0x1B};
    std::vector<uint8_t> img(DcArmBus::RAM_SIZE);
    for (size_t i = 0; i < img.size(); i += 4) {
        const uint64_t x = rng(), y = rng();
        const uint32_t r = (uint32_t)y;
        auto reg = [&](int sh) { return (uint32_t)(((x >> sh) & 15) == 15 && ((x >> 40) & 7) ? (x >> (sh + 4)) & 7 : (x >> sh) & 15); };
        const uint32_t hot = ((x >> 44) & 3) ? (uint32_t)((x >> 46) & 3) : reg(24);   /* shift amounts meet edge values in r0-r3 */
        const uint32_t cond = (x & 7) ? 0xE0000000u : (uint32_t)(r & 0xF0000000u);
        const uint32_t pick = (uint32_t)((x >> 3) % 100);
        uint32_t w;
        if (pick < 25)      w = 0x02000000 | (r & 0x01F000FF & ~0x0F0000u) | reg(8) << 16 | reg(12) << 12 | (r & 0xF00);   /* DP immediate */
        else if (pick < 42) w = (r & 0x01F00F60) | reg(8) << 16 | reg(12) << 12 | reg(16);                                /* DP, imm shift */
        else if (pick < 50) {                                                                                         /* DP, reg shift */
            w = (r & 0x01F00060) | 0x10 | reg(8) << 16 | reg(12) << 12 | hot << 8 | reg(20);
            if ((y >> 36) & 1 && i + 4 < img.size()) {   /* half of them right after a MOV of an edge value into Rs */
                const uint32_t m = 0xE3A00000 | hot << 12 | edge[(y >> 40) % 12];
                memcpy(&img[i], &m, 4);
                i += 4;
            }
        }
        else if (pick < 56) w = 0x03A00000 | hot << 12 | edge[(y >> 40) % 12];                                           /* MOV #edge */
        else if (pick < 60) w = (r & 0x00300000) | 0x90 | reg(8) << 16 | reg(12) << 12 | reg(16) << 8 | reg(20);          /* MUL / MLA */
        else if (pick < 70) w = 0x04000000 | (r & 0x03F00FFF & ~0x0F0000u) | reg(8) << 16 | reg(12) << 12;                /* LDR / STR */
        else if (pick < 75) w = 0x08000000 | (r & 0x01F0FFFF & ~0x0F0000u) | reg(8) << 16;                                /* LDM / STM */
        else if (pick < 77) w = 0x01000090 | (r & 0x00400000) | reg(8) << 16 | reg(12) << 12 | reg(16);                   /* SWP */
        else if (pick < 84) w = 0x0A000000 | (r & 0x01000000) | (r & 0x3FF);                                            /* B / BL forward */
        else if (pick < 87) w = 0x0321F000 | (r & 0x00400000) | ((r >> 8) & 3) << 6 | modes[(y >> 40) % 6];             /* MSR c */
        else if (pick < 89) w = 0x0328F400 | (r & 0x004000FF);                                                           /* MSR f */
        else if (pick < 91) w = 0x010F0000 | (r & 0x00400000) | reg(12) << 12;                                           /* MRS */
        else if (pick < 93) w = 0x029FF000 | (r & 0xFC);                                                               /* ADDS pc, pc, #k */
        else if (pick < 95) w = 0x0130F000 | (r & 0x02600FFF & ~0x0F0000u) | reg(8) << 16;                                /* TSTP..CMNP */
        else if (pick < 97) w = 0x008FF100 | reg(8);                                                                      /* ADD pc, pc, Rm, LSL #2 */
        else                w = r & 0x0FFFFFFF;                                                                           /* anything */
        w = (w & 0x0FFFFFFF) | cond;
        memcpy(&img[i], &w, 4);
    }
    /* vectors: reset branches to a random start; the others return (MOVS pc, lr / SUBS pc, lr, #4), continue a little
     * further on in the exception's mode (ADD pc, lr, #k) or are soup, so exceptions do not replay one code path */
    for (uint32_t v = 0; v < 8; v++) {
        const uint64_t x = rng();
        uint32_t w;
        if (v == 0) w = 0xEA000000 | (uint32_t)(x % 0x40000);
        else if ((x & 3) == 0) w = 0xE1B0F00E;
        else if ((x & 3) == 1) w = 0xE25EF004;
        else if ((x & 3) == 2) w = 0xE28EF000 | (uint32_t)((x >> 8) & 0xFC);
        else continue;
        memcpy(&img[4 * v], &w, 4);
    }
    bus.load(0, img.data(), img.size());
}

static int mode_chaos(uint64_t n, uint64_t first)
{
    const auto vb = vendor_bins(), qb = qemu_bins();
    Totals t;
    for (uint64_t s = first; s < first + n; s++) {
        std::mt19937_64 rng(s * 0x9E3779B97F4A7C15ull);
        DcArmBus bus;
        if (rng() & 1) bus.waits = DcWaits::dreamcast();
        std::string name;
        const unsigned kind = rng() % 4;
        if (kind == 0 && !vb.empty()) {   /* the programs over a soup background: it runs on after they end */
            name = vb[rng() % vb.size()];
            std::vector<uint8_t> img;
            read_file(name, img);
            soup(bus, rng);
            bus.load(0, img.data(), img.size());
        } else if (kind == 1 && !qb.empty()) {
            name = qb[rng() % qb.size()];
            std::vector<uint8_t> img;
            read_file(name, img);
            soup(bus, rng);
            load_qemu(bus, img);
        } else {
            name = "soup";
            soup(bus, rng);
        }
        run_one("chaos seed " + std::to_string(s) + " " + name, bus, [](Arm7DI &, DcArmBus &) { return false; }, 300000,
                true, rng(), true, t);
    }
    report("chaos", t);
    return t.bad ? 1 : 0;
}

/* a raw binary at 0, zero waits, until MAX clocks or the word 0xDEADBEEF */
static int mode_bin(const std::string &path, uint64_t max)
{
    Totals t;
    std::vector<uint8_t> img;
    if (!read_file(path, img)) { fprintf(stderr, "cannot read %s\n", path.c_str()); return 2; }
    DcArmBus bus;
    bus.load(0, img.data(), img.size());
    run_one(path, bus, [](Arm7DI &c, DcArmBus &) { return c.exec_word() == 0xDEADBEEF; }, max, false, 0, true, t);
    report("bin", t);
    return t.bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    Verilated::commandArgs(argc, argv);
    int st = 0;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "vendor") st |= mode_vendor();
        else if (a == "qemu") st |= mode_qemu();
        else if (a == "jobs" && i + 1 < argc) {   /* jobs SUITE[,SUITE...] */
            std::string l = argv[++i];
            for (size_t p = 0; p <= l.size();) {
                const size_t q = std::min(l.find(',', p), l.size());
                if (q > p) st |= mode_jobs(l.substr(p, q - p));
                p = q + 1;
            }
        } else if (a == "chaos") {
            const uint64_t n = i + 1 < argc ? strtoull(argv[++i], 0, 0) : 100;
            uint64_t first = 1;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) first = strtoull(argv[++i], 0, 0);
            st |= mode_chaos(n, first);
        } else if (a == "bin" && i + 1 < argc) {
            const std::string f = argv[++i];
            uint64_t max = 100000;
            if (i + 1 < argc && isdigit((unsigned char)argv[i + 1][0])) max = strtoull(argv[++i], 0, 0);
            st |= mode_bin(f, max);
        } else if (a[0] == '+') continue;   /* Verilator plusargs */
        else { fprintf(stderr, "usage: core_tb [vendor] [qemu] [jobs SUITE[,SUITE...]] [chaos N [FIRST]] [bin FILE [MAX]]\n"); return 2; }
    }
    return st;
}
