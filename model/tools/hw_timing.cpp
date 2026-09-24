/* hw_timing -- ARM7DI bus timing on the console: kernel generator and analysis.
 *
 *   hw_timing gen     write tests/hw/timing/src/<kernel>.s (kept), build them to ../build/hw/timing/, write
 *                     tests/hw/timing/jobs.txt (each variant at ITER = N1 and N2, 3 repeats, plus a sample-clock
 *                     calibration).  Then: ./run_hw.sh timing
 *   hw_timing check   read tests/hw/timing/hw/results.txt, print per-iteration cost in ARM clocks next to the model's
 *                     cycle breakdown; writes tests/hw/timing/SUMMARY.txt.
 *
 * A kernel is a loop: U copies of BODY, then subs r12 / bne (the loop overhead is part of every variant and
 * cancels in differences).  The per-iteration console cost is (T(N2) - T(N1)) / (N2 - N1) SH4 cycles, converted to
 * ARM clocks with the measured SH4 cycles per AICA sample and the assumption 512 ARM clocks per sample
 * (22.5792 MHz); the model's figures are datasheet cycles (zero wait states).
 * Registers: r1 multiplicand, r2 = P1 (Rs / shift amount), r3 = 0x100000 (wave RAM data), r4 = 0x802800 (AICA
 * register, read), r5 = 0x803000 (DSP COEF 0, written), r7 = 0x180000 (second RAM area), r12 counter, r0 and
 * r6, r8-r11, r13, r14 free.
 */
#include "hwjob.h"
#include <math.h>
#include <algorithm>
#include <map>

using namespace wren7;

static const unsigned N1 = 64, N2 = 576;

struct Kernel {
    std::string name, body, what;
    int unroll;
    std::vector<std::pair<std::string, uint32_t>> vars;   /* variant suffix, P1 */
};

/* A random loop body of 32 instructions from every class the timing covers (r13 = P1-independent Rs from the seed). */
static std::string mix_body(int seed)
{
    uint64_t x = 0x243F6A8885A308D3ull * (uint64_t)seed;
    auto rnd = [&](uint32_t n) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; return (uint32_t)((x >> 11) % n); };
    static const char *lists[] = {"r6", "r6, r8", "r6, r8-r10", "r6, r8-r11", "r0, r6, r8-r11, r14"};
    std::string b;
    char line[96];
    for (int i = 0; i < 32; i++) {
        switch (rnd(15)) {
        case 0: snprintf(line, sizeof line, "add r0, r0, #%u", rnd(256)); break;
        case 1: snprintf(line, sizeof line, "eors r6, r6, r8, ror #%u", 1 + rnd(31)); break;
        case 2: snprintf(line, sizeof line, "add r9, r9, r1, lsl r2"); break;
        case 3: snprintf(line, sizeof line, "%s", rnd(2) ? "mul r10, r1, r13" : "mla r10, r1, r13, r9"); break;
        case 4: snprintf(line, sizeof line, "ldr%s r0, [r%u, #%u]", rnd(2) ? "b" : "", rnd(2) ? 3 : 7, 4 * rnd(64)); break;
        case 5: snprintf(line, sizeof line, "str%s r0, [r%u, #%u]", rnd(2) ? "b" : "", rnd(2) ? 3 : 7, 4 * rnd(64)); break;
        case 6: snprintf(line, sizeof line, "ldmia r%u, {%s}", rnd(2) ? 3 : 7, lists[rnd(5)]); break;
        case 7: snprintf(line, sizeof line, "stmia r%u, {%s}", rnd(2) ? 3 : 7, lists[rnd(5)]); break;
        case 8: snprintf(line, sizeof line, "swp%s r11, r11, [r%u]", rnd(2) ? "b" : "", rnd(2) ? 3 : 7); break;
        case 9: snprintf(line, sizeof line, "ldr r0, [r4]"); break;
        case 10: snprintf(line, sizeof line, "str r0, [r5]"); break;
        case 11: snprintf(line, sizeof line, "b 1f\n1:"); break;
        case 12: snprintf(line, sizeof line, "addeq r0, r0, #1"); break;
        case 13: snprintf(line, sizeof line, "mov r0, r0"); break;
        default: snprintf(line, sizeof line, "movs r8, r8, lsr r2"); break;
        }
        b += line;
        b += '\n';
    }
    /* Rs for the multiplies: a seed-dependent value, loaded once (r13 is otherwise unused) */
    char setup[64];
    snprintf(setup, sizeof setup, "@RS 0x%08x\n", (uint32_t)(x >> 20) >> rnd(32));
    return setup + b;
}

static std::vector<Kernel> kernels()
{
    std::vector<Kernel> k;
    auto one = [](uint32_t p) { return std::vector<std::pair<std::string, uint32_t>>{{"", p}}; };
    k.push_back({"nop", "mov r0, r0", "S fetch", 64, one(0)});
    std::vector<std::pair<std::string, uint32_t>> mv;
    for (int m = 1; m <= 16; m++) mv.push_back({"m" + std::to_string(m), m == 1 ? 1u : 1u << (2 * m - 3)});
    k.push_back({"mul", "mul r0, r1, r2", "S fetch + m I", 16, mv});
    k.push_back({"rsh", "add r0, r0, r1, lsl r2", "S fetch + I", 32, one(1)});
    k.push_back({"br", "b 1f\n1:", "S, N, S fetches", 16, one(0)});
    k.push_back({"ldr", "ldr r0, [r3]", "S fetch, N read, I", 16, one(0)});
    k.push_back({"ldr_seq", "ldr r0, [r3], #4", "S fetch, N read (sequential addresses), I", 16, one(0)});
    k.push_back({"ldr_far", "ldr r0, [r3]\nldr r0, [r7]", "two LDRs 512 KB apart", 8, one(0)});
    k.push_back({"ldrb", "ldrb r0, [r3]", "S fetch, N byte read, I", 16, one(0)});
    k.push_back({"str", "str r0, [r3]", "S fetch, N write", 16, one(0)});
    k.push_back({"str_seq", "str r0, [r3], #4", "S fetch, N write (sequential)", 16, one(0)});
    k.push_back({"strb", "strb r0, [r3]", "S fetch, N byte write", 16, one(0)});
    k.push_back({"swp", "swp r0, r0, [r3]", "S fetch, N read, N write, I", 16, one(0)});
    static const char *lists[] = {"r6", "r6, r8", "r6, r8, r9", "r6, r8-r10", "r6, r8-r11", "r6, r8-r11, r13",
                                  "r6, r8-r11, r13, r14", "r0, r6, r8-r11, r13, r14"};
    for (int n = 1; n <= 8; n++) {
        k.push_back({"ldm" + std::to_string(n), std::string("ldmia r3, {") + lists[n - 1] + "}",
                     "S fetch, N + (n-1) S reads, I", 8, one(0)});
        k.push_back({"stm" + std::to_string(n), std::string("stmia r3, {") + lists[n - 1] + "}",
                     "S fetch, N + (n-1) S writes", 8, one(0)});
    }
    k.push_back({"ldr_reg", "ldr r0, [r4]", "LDR from an AICA register", 16, one(0)});
    k.push_back({"str_reg", "str r0, [r5]", "STR to an AICA register (DSP COEF 0)", 16, one(0)});
    std::vector<std::pair<std::string, uint32_t>> lm;
    for (int m = 1; m <= 8; m++) lm.push_back({"m" + std::to_string(m), m == 1 ? 1u : 1u << (2 * m - 3)});
    k.push_back({"ldr_mul", "ldr r0, [r3]\nmul r6, r1, r2", "LDR then MUL (m I cycles)", 8, lm});
    k.push_back({"str_mul", "str r0, [r3]\nmul r6, r1, r2", "STR then MUL (m I cycles)", 8, lm});
    /* batch 2 */
    std::vector<std::pair<std::string, uint32_t>> neg;
    for (uint32_t rs : {0xFFFFFFFFu, 0xFFFFFFFEu, 0xFFFFFF80u, 0xFFFF8000u, 0xFF800000u, 0x80000000u, 0x7FFFFFFFu,
                        0x00800000u, 0x007FFFFFu, 0x00008000u, 0x00007FFFu, 0x00000080u, 0x0000007Fu}) {
        char b[16];
        snprintf(b, sizeof b, "%08x", rs);
        neg.push_back({b, rs});
    }
    k.push_back({"mulx", "mul r0, r1, r2", "MUL, Rs sweep incl. negative values", 16, neg});
    std::vector<std::pair<std::string, uint32_t>> mlav;
    for (uint32_t rs : {0x1u, 0x80u, 0x8000u, 0x800000u, 0xFFFFFFFFu}) {
        char b[16];
        snprintf(b, sizeof b, "%08x", rs);
        mlav.push_back({b, rs});
    }
    k.push_back({"mla", "mla r0, r1, r2, r6", "MLA, Rs sweep", 16, mlav});
    k.push_back({"swpb", "swpb r0, r0, [r3]", "SWPB", 16, one(0)});
    k.push_back({"swp_rsh", "swp r0, r0, [r3]\nadd r6, r6, r1, lsl r2", "SWP then a 1-I instruction", 8, one(1)});
    k.push_back({"rsh_ldr", "add r6, r6, r1, lsl r2\nldr r0, [r3]", "1-I instruction then LDR", 8, one(1)});
    k.push_back({"ldr_ldr", "ldr r0, [r3]\nldr r6, [r7]", "LDR pair", 8, one(0)});
    k.push_back({"swp_swp", "swp r0, r0, [r3]\nswp r6, r6, [r7]", "SWP pair", 8, one(0)});
    /* program flow: PC writes and exceptions ("prologue @@ body": the prologue runs once before the loop) */
    k.push_back({"swi", "ldr r0, =0xE1B0F00E\nmov r6, #8\nstr r0, [r6]\n@@\nswi 0", "SWI + movs pc, lr", 8, one(0)});
    k.push_back({"und", "ldr r0, =0xE1B0F00E\nmov r6, #4\nstr r0, [r6]\n@@\n.word 0xE7F000F0", "undefined trap + movs pc, lr",
                 8, one(0)});
    k.push_back({"ldrpc", "ldr pc, [pc, #-4]\n.word 1f\n1:", "LDR PC (PC-relative literal)", 8, one(0)});
    k.push_back({"addpc", "add pc, pc, #0\nmov r0, r0", "data op to PC (skips one)", 8, one(0)});
    k.push_back({"ldmpc", "ldr r0, =3f\nstr r0, [r3, #64]\n@@\nadd r6, r3, #64\nldmia r6, {pc}\n3:",
                 "LDM {PC} to the loop tail", 1, one(0)});
    /* validation (not used by the fits): random instruction mixes, predicted before measuring */
    for (int seed = 1; seed <= 16; seed++) k.push_back({"mix" + std::to_string(seed), mix_body(seed), "random mix", 1, one(3)});
    return k;
}

static const char *T_DEFAULT = "/opt/toolchains/dc-dca3/arm-eabi/bin";
static std::string tool(const char *t)
{
    const char *b = getenv("DC_ARM_BIN");
    return std::string(b ? b : T_DEFAULT) + "/arm-eabi-" + t;
}

static bool sh(const std::string &c)
{
    int rc = system(c.c_str());
    if (rc) fprintf(stderr, "failed (%d): %s\n", rc, c.c_str());
    return rc == 0;
}

static int gen()
{
    const std::string src = "tests/hw/timing/src", bld = "../build/hw/timing";
    if (!sh("mkdir -p " + src + " " + bld + " tests/hw/timing/hw")) return 2;
    FILE *jf = fopen("tests/hw/timing/jobs.txt", "w");
    fprintf(jf, "# generated by tools/hw_timing gen -- ARM bus timing kernels, ITER %u and %u, 3 repeats\n", N1, N2);
    fprintf(jf, "out tests/hw/timing/hw\nchstate\nsamples 44100\n");
    for (auto &k : kernels()) {
        const std::string s = src + "/" + k.name + ".s";
        FILE *f = fopen(s.c_str(), "w");
        fprintf(f, "@ generated by tools/hw_timing -- kernel %s: %s, %d per iteration\n", k.name.c_str(), k.what.c_str(),
                k.unroll);
        std::string body = k.body, rs = "0", pro;
        if (body.find("\n@@\n") != std::string::npos) {   /* one-off prologue before the loop */
            pro = body.substr(0, body.find("\n@@\n"));
            body = body.substr(body.find("\n@@\n") + 4);
        }
        if (body.compare(0, 4, "@RS ") == 0) {   /* mix kernels: r13 = Rs for their multiplies, set before the loop */
            const size_t e = body.find('\n');
            rs = body.substr(4, e - 4);
            body = body.substr(e + 1);
        }
        fprintf(f, "    .include \"crt.inc\"\n_start:\n    ldr r12, ITER\n    ldr r1, =0x12345679\n    ldr r2, P1\n"
                   "    ldr r3, =0x100000\n    ldr r4, =0x802800\n    ldr r5, =0x803000\n    ldr r7, =0x180000\n"
                   "    ldr r13, =%s\n", rs.c_str());
        for (size_t p = 0, q; p < pro.size(); p = q + 1) {
            q = pro.find('\n', p);
            if (q == std::string::npos) q = pro.size();
            fprintf(f, "    %s\n", pro.substr(p, q - p).c_str());
        }
        fprintf(f, "    b loop\n    .ltorg\n    .balign 64\nloop:\n    .rept %d\n", k.unroll);
        for (size_t p = 0, q; p < body.size(); p = q + 1) {
            q = body.find('\n', p);
            if (q == std::string::npos) q = body.size();
            if (q > p) fprintf(f, "    %s\n", body.substr(p, q - p).c_str());
        }
        fprintf(f, "    .endr\n    subs r12, r12, #1\n    bne loop\n    b STUB\n");
        fclose(f);
        const std::string o = bld + "/" + k.name;
        if (!sh(tool("as") + " -mcpu=arm7 -Ihw/arm " + s + " -o " + o + ".o")) return 2;
        if (!sh(tool("ld") + " -Ttext=0 -N -e 0 --no-warn-rwx-segments -o " + o + ".elf " + o + ".o")) return 2;
        if (!sh(tool("objcopy") + " -O binary " + o + ".elf " + o + ".bin")) return 2;
        for (auto &v : k.vars)
            for (unsigned it : {N1, N2}) {
                fprintf(jf, "job %s%s%s_n%u 2000 3\nload 0 ../build/hw/timing/%s.bin\nload 0x1FE000 ../build/hw/arm/stub.bin\n"
                            "word 0x20 %u\nword 0x24 0x%x\nrun\n",
                        k.name.c_str(), v.first.empty() ? "" : "_", v.first.c_str(), it, k.name.c_str(), it, v.second);
            }
    }
    for (const char *k : {"k_smpswp64", "k_smpswp72"})
        for (unsigned it : {1024u, 8192u})
            fprintf(jf, "job %s_n%u 3000 3\nload 0 ../build/hw/arm/%s.bin\nload 0x1FE000 ../build/hw/arm/stub.bin\n"
                        "word 0x20 %u\nread 0x1FF000 0x60\nrun\n", k + 2, it, k, it);
    for (unsigned it : {1024u, 4096u})
        fprintf(jf, "job smpcount_n%u 2000 3\nload 0 ../build/hw/arm/k_smpcount.bin\nload 0x1FE000 ../build/hw/arm/stub.bin\n"
                    "word 0x20 %u\nread 0x1FF000 0x60\nrun\n", it, it);
    fclose(jf);
    printf("wrote tests/hw/timing/jobs.txt\n");
    return 0;
}

static double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? NAN : v[v.size() / 2];
}

static int check()
{
    HwJobFile jf;
    if (!parse_jobs("tests/hw/timing/jobs.txt", jf)) return 2;
    std::map<std::string, std::vector<double>> hw;
    std::map<std::string, std::string> status;
    double smp_n = 0, smp_c = 0;
    {
        FILE *f = fopen("tests/hw/timing/hw/results.txt", "r");
        if (!f) { perror("tests/hw/timing/hw/results.txt"); return 2; }
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char name[128], st[32];
            unsigned long long a, b;
            if (sscanf(line, "samples %llu %llu", &a, &b) == 2) { smp_n = a; smp_c = b; continue; }
            if (sscanf(line, "%127s %31s %*x", name, st) != 2) continue;
            status[name] = st;
            char *p = line;
            for (int i = 0; i < 3; i++) { p = strchr(p, ' '); if (p) p++; }
            std::vector<double> v;
            while (p && *p) { char *e; double x = strtod(p, &e); if (e == p) break; v.push_back(x); p = e; }
            hw[name] = v;
        }
        fclose(f);
    }
    const double sh4_per_sample = smp_n ? smp_c / smp_n : NAN;
    FILE *out = fopen("tests/hw/timing/SUMMARY.txt", "w");
    auto both = [&](const char *fmt, auto... a) { printf(fmt, a...); fprintf(out, fmt, a...); };
    auto per_iter = [&](const std::string &base, double &v) {
        const std::string j1 = base + "_n" + std::to_string(N1), j2 = base + "_n" + std::to_string(N2);
        if (!hw.count(j1) || !hw.count(j2) || status[j1] != "done" || status[j2] != "done") return false;
        v = (median(hw[j2]) - median(hw[j1])) / (N2 - N1);
        return true;
    };
    /* unit = one MCLK of the slot-grid model = (nop kernel SH4 cycles per iteration) / (68 accesses x 8) */
    double nop_sh4;
    if (!per_iter("nop", nop_sh4)) { fprintf(stderr, "no nop kernel\n"); return 2; }
    const double U = nop_sh4 / (68 * 8);
    const DcWaits grid = DcWaits::dreamcast();
    both("unit U = %.5f SH4 cycles (nop kernel / 544); SH4-side sample calibration: %.3f SH4 cycles per sample = "
         "%.3f U\n", U, sh4_per_sample, sh4_per_sample / U);
    both("%s", "model = DcWaits::dreamcast() (4-MCLK grid, 8-MCLK access, locked write 4, 2 blocked half slots per frame)\n");
    both("%-15s %9s %8s %8s %7s | %6s %5s %5s %5s\n", "variant", "SH4/iter", "meas U", "model U", "delta", "dsheet",
         "N", "S", "I");
    int nbad = 0;
    for (auto &k : kernels())
        for (auto &v : k.vars) {
            const std::string base = k.name + (v.first.empty() ? "" : "_" + v.first);
            double per;
            if (!per_iter(base, per)) { both("%-15s missing or not done\n", base.c_str()); continue; }
            const HwJob *a = nullptr, *b = nullptr;
            for (auto &j : jf.jobs) {
                if (j.name == base + "_n" + std::to_string(N1)) a = &j;
                if (j.name == base + "_n" + std::to_string(N2)) b = &j;
            }
            ModelRun ma = run_job_model(*a, DcWaits()), mb = run_job_model(*b, DcWaits());
            ModelRun ga = run_job_model(*a, grid), gb = run_job_model(*b, grid);
            const double d = N2 - N1;
            const double mc = (mb.cycles - ma.cycles) / d, mn = (mb.stats.n - ma.stats.n) / d,
                         ms = (mb.stats.s - ma.stats.s) / d, mi = (mb.stats.i - ma.stats.i) / d;
            const double gu = (gb.cycles - ga.cycles) / d, meas = per / U;
            const bool bad = fabs(meas - gu) > 0.1;
            nbad += bad;
            both("%-15s %9.2f %8.2f %8.2f %7.2f%s| %6.2f %5.2f %5.2f %5.2f\n", base.c_str(), per, meas, gu, meas - gu,
                 bad ? "*" : " ", mc, mn, ms, mi);
        }
    both("%d variants off the model by more than 0.1 U per iteration\n", nbad);
    /* ARM-side sample clock */
    /* exact poll loops: model the job itself with a synthetic sample clock of S MCLK to find S */
    for (const char *k : {"smpswp64", "smpswp72"})
        for (unsigned it : {1024u, 8192u}) {
            const std::string n = std::string(k) + "_n" + std::to_string(it);
            std::vector<uint8_t> r;
            if (!read_all("tests/hw/timing/hw/" + n + ".bin", r) || r.size() < 0x48 || status[n] != "done") {
                both("%s: no result\n", n.c_str());
                continue;
            }
            const uint32_t polls = r[8 + 32] | (r[9 + 32] << 8) | (r[10 + 32] << 16) | ((uint32_t)r[11 + 32] << 24);
            both("%s: %u polls over %u samples = %.5f polls/sample\n", n.c_str(), polls, it, (double)polls / it);
        }
    for (unsigned it : {1024u, 4096u}) {
        const std::string n = "smpcount_n" + std::to_string(it);
        std::vector<uint8_t> r;
        if (!read_all("tests/hw/timing/hw/" + n + ".bin", r) || r.size() < 0x48 || status[n] != "done") {
            both("%s: no result\n", n.c_str());
            continue;
        }
        const uint32_t polls = r[8 + 32] | (r[9 + 32] << 8) | (r[10 + 32] << 16) | ((uint32_t)r[11 + 32] << 24);
        both("%s: %u polls over %u samples = %.4f polls/sample; poll loop 60 U + 32 U per edge (grid model) -> %.3f U "
             "per sample\n", n.c_str(), polls, it, (double)polls / it, (60.0 * polls + 32.0 * it) / it);
    }
    fclose(out);
    return 0;
}

/* fit: blocked phase-4 point every R MCLK (R = 8..2048, multiple of 8); prediction per variant averaged over 8 start
 * phases; least squares against the measured per-iteration cost. */
static int fit()
{
    HwJobFile jf;
    if (!parse_jobs("tests/hw/timing/jobs.txt", jf)) return 2;
    std::map<std::string, std::vector<double>> hw;
    {
        FILE *f = fopen("tests/hw/timing/hw/results.txt", "r");
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char name[128], st[32];
            if (sscanf(line, "%127s %31s %*x", name, st) != 2 || strcmp(st, "done")) continue;
            char *p = line;
            for (int i = 0; i < 3; i++) { p = strchr(p, ' '); if (p) p++; }
            std::vector<double> v;
            while (p && *p) { char *e; double x = strtod(p, &e); if (e == p) break; v.push_back(x); p = e; }
            hw[name] = v;
        }
        fclose(f);
    }
    struct V { std::string name; const HwJob *a, *b; double meas; };
    std::vector<V> vs;
    double U = 0;
    for (auto &k : kernels())
        for (auto &v : k.vars) {
            const std::string base = k.name + (v.first.empty() ? "" : "_" + v.first);
            const std::string j1 = base + "_n" + std::to_string(N1), j2 = base + "_n" + std::to_string(N2);
            if (!hw.count(j1) || !hw.count(j2)) continue;
            V x{base, nullptr, nullptr, (median(hw[j2]) - median(hw[j1])) / (N2 - N1)};
            for (auto &j : jf.jobs) { if (j.name == j1) x.a = &j; if (j.name == j2) x.b = &j; }
            if (base == "nop") U = x.meas / 544.0;
            vs.push_back(x);
        }
    for (auto &x : vs) x.meas /= U;
    /* candidates: period R, blocked point at t = p (mod R), p = 4 (mod 8), t counted from the reset release */
    std::vector<std::pair<uint32_t, uint32_t>> Rs;
    for (uint32_t R : {64u, 128u, 256u, 512u, 1024u, 2048u})
        for (uint32_t p = 4; p < R; p += 8) Rs.push_back({R, p});
    double base = 0;
    for (auto &x : vs) {
        DcWaits w;
        w.grid = 4;
        ModelRun ma = run_job_model(*x.a, w), mb = run_job_model(*x.b, w);
        const double d = (double)(mb.cycles - ma.cycles) / (N2 - N1) - x.meas;
        base += d * d;
    }
    std::vector<double> err(Rs.size());
    std::vector<std::vector<double>> pred(Rs.size(), std::vector<double>(vs.size()));
#pragma omp parallel for schedule(dynamic)
    for (size_t ri = 0; ri < Rs.size(); ri++) {
        double e = 0;
        for (size_t vi = 0; vi < vs.size(); vi++) {
            DcWaits w;
            w.grid = 4;
            w.block_period = Rs[ri].first;
            w.block_phase = (Rs[ri].first - Rs[ri].second) % Rs[ri].first;
            ModelRun ma = run_job_model(*vs[vi].a, w), mb = run_job_model(*vs[vi].b, w);
            pred[ri][vi] = (double)(mb.cycles - ma.cycles) / (N2 - N1);
            e += (pred[ri][vi] - vs[vi].meas) * (pred[ri][vi] - vs[vi].meas);
        }
        err[ri] = e;
    }
    std::vector<size_t> order(Rs.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return err[a] < err[b]; });
    FILE *out = fopen("tests/hw/timing/FIT.txt", "w");
    auto both = [&](const char *fmt, auto... a) { printf(fmt, a...); fprintf(out, fmt, a...); };
    both("blocked phase-4 point at t = p (mod R) from the reset release; least squares over %zu variants (U^2); "
         "no blocking: %.3f\n", vs.size(), base);
    for (int i = 0; i < 12; i++) both("  R = %4u p = %4u  err = %9.3f\n", Rs[order[i]].first, Rs[order[i]].second, err[order[i]]);
    const size_t b = order[0];
    both("best R = %u p = %u: per-variant measured vs predicted (U per iteration)\n", Rs[b].first, Rs[b].second);
    for (size_t vi = 0; vi < vs.size(); vi++)
        both("  %-15s %8.2f %8.2f %6.2f\n", vs[vi].name.c_str(), vs[vi].meas, pred[b][vi], vs[vi].meas - pred[b][vi]);
    fclose(out);
    return 0;
}

/* greedy set fit: blocked phase-4 grid points within a 512-MCLK frame (counted from the reset release) */
static int fitset()
{
    HwJobFile jf;
    if (!parse_jobs("tests/hw/timing/jobs.txt", jf)) return 2;
    std::map<std::string, std::vector<double>> hw;
    {
        FILE *f = fopen("tests/hw/timing/hw/results.txt", "r");
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char name[128], st[32];
            if (sscanf(line, "%127s %31s %*x", name, st) != 2 || strcmp(st, "done")) continue;
            char *p = line;
            for (int i = 0; i < 3; i++) { p = strchr(p, ' '); if (p) p++; }
            std::vector<double> v;
            while (p && *p) { char *e; double x = strtod(p, &e); if (e == p) break; v.push_back(x); p = e; }
            hw[name] = v;
        }
        fclose(f);
    }
    struct V { std::string name; const HwJob *a, *b; double meas; };
    std::vector<V> vs;
    double U = 0;
    for (auto &k : kernels())
        for (auto &v : k.vars) {
            const std::string base = k.name + (v.first.empty() ? "" : "_" + v.first);
            const std::string j1 = base + "_n" + std::to_string(N1), j2 = base + "_n" + std::to_string(N2);
            if (!hw.count(j1) || !hw.count(j2)) continue;
            V x{base, nullptr, nullptr, (median(hw[j2]) - median(hw[j1])) / (N2 - N1)};
            for (auto &j : jf.jobs) { if (j.name == j1) x.a = &j; if (j.name == j2) x.b = &j; }
            if (base == "nop") U = x.meas / 544.0;
            vs.push_back(x);
        }
    for (auto &x : vs) x.meas /= U;
    const uint32_t R = 512;
    auto eval = [&](const std::vector<uint8_t> &set, std::vector<double> *pr) {
        DcWaits w;
        w.grid = 4;
        w.block_period = R;
        w.block_set = set;
        double e = 0;
        for (size_t vi = 0; vi < vs.size(); vi++) {
            ModelRun ma = run_job_model(*vs[vi].a, w), mb = run_job_model(*vs[vi].b, w);
            const double p = (double)(mb.cycles - ma.cycles) / (N2 - N1);
            if (pr) (*pr)[vi] = p;
            e += (p - vs[vi].meas) * (p - vs[vi].meas);
        }
        return e;
    };
    std::vector<uint8_t> set(R / 4, 0);
    FILE *out = fopen("tests/hw/timing/FITSET.txt", "w");
    auto both = [&](const char *fmt, auto... a) { printf(fmt, a...); fprintf(out, fmt, a...); };
    double cur = eval(set, nullptr);
    both("greedy blocked-set fit, frame %u MCLK from the reset release, %zu variants; empty set err %.3f U^2\n", R, vs.size(), cur);
    for (int step = 0; step < 12; step++) {
        std::vector<double> e(R / 8);
#pragma omp parallel for schedule(dynamic)
        for (uint32_t c = 0; c < R / 8; c++) {
            std::vector<uint8_t> s2 = set;
            const uint32_t pt = (8 * c + 4) / 4;
            if (s2[pt]) { e[c] = 1e30; continue; }
            s2[pt] = 1;
            e[c] = eval(s2, nullptr);
        }
        uint32_t best = 0;
        for (uint32_t c = 1; c < R / 8; c++) if (e[c] < e[best]) best = c;
        if (e[best] >= cur - 0.05) { both("%s", "no further improvement\n"); break; }
        set[(8 * best + 4) / 4] = 1;
        cur = e[best];
        both("  + t = %3u (mod %u): err %.3f\n", 8 * best + 4, R, cur);
    }
    std::vector<double> pr(vs.size());
    eval(set, &pr);
    both("%s", "per variant: measured, predicted, delta (U per iteration)\n");
    for (size_t vi = 0; vi < vs.size(); vi++)
        both("  %-15s %8.2f %8.2f %6.2f\n", vs[vi].name.c_str(), vs[vi].meas, pr[vi], vs[vi].meas - pr[vi]);
    fclose(out);
    return 0;
}

static int predict()
{
    HwJobFile jf;
    if (!parse_jobs("tests/hw/timing/jobs.txt", jf)) return 2;
    FILE *out = fopen("tests/hw/timing/PREDICT.txt", "w");
    fprintf(out, "# model prediction (DcWaits::dreamcast), MCLK per iteration, written before the console run\n");
    for (auto &k : kernels())
        for (auto &v : k.vars) {
            const std::string base = k.name + (v.first.empty() ? "" : "_" + v.first);
            const HwJob *a = nullptr, *b = nullptr;
            for (auto &j : jf.jobs) {
                if (j.name == base + "_n" + std::to_string(N1)) a = &j;
                if (j.name == base + "_n" + std::to_string(N2)) b = &j;
            }
            if (!a || !b) continue;
            ModelRun ma = run_job_model(*a, DcWaits::dreamcast()), mb = run_job_model(*b, DcWaits::dreamcast());
            fprintf(out, "%-15s %9.3f\n", base.c_str(), (double)(mb.cycles - ma.cycles) / (N2 - N1));
        }
    fclose(out);
    printf("wrote tests/hw/timing/PREDICT.txt\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "predict")) return predict();
    if (argc > 1 && !strcmp(argv[1], "fitset")) return fitset();
    if (argc > 1 && !strcmp(argv[1], "fit")) return fit();
    if (argc > 1 && !strcmp(argv[1], "gen")) return gen();
    if (argc > 1 && !strcmp(argv[1], "check")) return check();
    fprintf(stderr, "usage: hw_timing gen | check   (from wren7-rtl/model)\n");
    return 2;
}
