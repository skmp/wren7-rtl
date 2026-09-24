/* wren7run -- run a raw ARM binary on the model in the Dreamcast sound-system map.
 *
 *   wren7run [options] prog.bin
 *     --load ADDR        load address in wave RAM (default 0)
 *     --stop-word W      stop when the instruction about to execute is W (default 0xDEADBEEF)
 *     --stop-addr A      stop when the instruction about to execute is at A
 *     --max-cycles N     give up after N MCLK cycles (default 10000000)
 *     --bus-trace FILE   one line per bus cycle ("-" = stdout)
 *     --insn-trace FILE  one line per instruction / exception entry
 *     --ideal            zero-wait memory (data sheet cycle counts) instead of the measured console timing
 *     --waits RN,RS,WN,WS,REGR,REGW,INT   fixed waits: wave RAM read N/S, write N/S, register read/write, internal
 *     --dump ADDR,LEN    hex-dump wave RAM after the run
 * Prints the stop reason, registers, CPSR, cycle and bus-cycle counts.
 */
#include "../src/arm7di.h"
#include "../src/dc_arm_map.h"
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace wren7;

static FILE *open_out(const char *p) { return strcmp(p, "-") ? fopen(p, "w") : stdout; }

int main(int argc, char **argv)
{
    uint32_t load = 0, stop_word = 0xDEADBEEF, stop_addr = 0xFFFFFFFF;
    uint64_t max_cycles = 10000000;
    const char *bin = nullptr, *btrace = nullptr, *itrace = nullptr;
    std::vector<std::pair<uint32_t, uint32_t>> dumps;
    DcWaits w = DcWaits::dreamcast();   /* measured console timing; --waits / --ideal override */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        auto next = [&]() -> const char * {
            if (i + 1 >= argc) { fprintf(stderr, "%s needs an argument\n", a); exit(2); }
            return argv[++i];
        };
        if (!strcmp(a, "--load")) load = strtoul(next(), 0, 0);
        else if (!strcmp(a, "--stop-word")) stop_word = strtoul(next(), 0, 0);
        else if (!strcmp(a, "--stop-addr")) stop_addr = strtoul(next(), 0, 0);
        else if (!strcmp(a, "--max-cycles")) max_cycles = strtoull(next(), 0, 0);
        else if (!strcmp(a, "--bus-trace")) btrace = next();
        else if (!strcmp(a, "--ideal")) w = DcWaits();
        else if (!strcmp(a, "--insn-trace")) itrace = next();
        else if (!strcmp(a, "--waits")) {
            unsigned v[7] = {0};
            if (sscanf(next(), "%u,%u,%u,%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) != 7) {
                fprintf(stderr, "--waits needs 7 comma-separated values\n");
                return 2;
            }
            w = DcWaits();
            w.ram_rn = v[0]; w.ram_rs = v[1]; w.ram_wn = v[2]; w.ram_ws = v[3];
            w.reg_r = v[4]; w.reg_w = v[5]; w.internal = v[6];
        } else if (!strcmp(a, "--dump")) {
            unsigned da, dl;
            if (sscanf(next(), "%i,%i", (int *)&da, (int *)&dl) != 2) { fprintf(stderr, "--dump ADDR,LEN\n"); return 2; }
            dumps.push_back({da, dl});
        } else if (a[0] == '-') { fprintf(stderr, "unknown option %s\n", a); return 2; }
        else bin = a;
    }
    if (!bin) { fprintf(stderr, "usage: wren7run [options] prog.bin\n"); return 2; }
    FILE *f = fopen(bin, "rb");
    if (!f) { perror(bin); return 1; }
    std::vector<uint8_t> img;
    for (int c; (c = fgetc(f)) != EOF;) img.push_back(c);
    fclose(f);

    DcArmBus bus;
    bus.waits = w;
    bus.load(load, img.data(), img.size());
    Arm7DI cpu(&bus);   /* power-on + reset release: the pipeline now holds 0 and 4 */
    bus.attach(&cpu);
    if (btrace) cpu.bus_trace = open_out(btrace);
    if (itrace) cpu.insn_trace = open_out(itrace);

    const char *why = "max-cycles";
    for (;;) {   /* stop conditions are checked at instruction boundaries */
        if (cpu.at_boundary()) {
            if (cpu.cycles >= max_cycles) break;
            if (cpu.exec_word() == stop_word) { why = "stop-word"; break; }
            if (cpu.exec_addr() == stop_addr) { why = "stop-addr"; break; }
        }
        cpu.bus_cycle();
    }
    printf("stop: %s at %08x (%08x)\n", why, cpu.exec_addr(), cpu.exec_word());
    for (int r = 0; r < 16; r++) printf("r%-2d=%08x%s", r, cpu.reg(r), (r & 3) == 3 ? "\n" : "  ");
    printf("cpsr=%08x (%s)\n", cpu.cpsr(), mode_name(cpu.cpsr()));
    printf("cycles=%llu instr=%llu unexec=%llu exc=%llu undoc=%llu  bus N=%llu S=%llu I=%llu C=%llu wait=%llu\n",
           (unsigned long long)cpu.cycles, (unsigned long long)cpu.stats.instr,
           (unsigned long long)cpu.stats.unexecuted, (unsigned long long)cpu.stats.exceptions,
           (unsigned long long)cpu.stats.undoc, (unsigned long long)cpu.stats.n, (unsigned long long)cpu.stats.s,
           (unsigned long long)cpu.stats.i, (unsigned long long)cpu.stats.c, (unsigned long long)cpu.stats.wait);
    for (auto &d : dumps)
        for (uint32_t o = 0; o < d.second; o += 4)
            printf("%08x%s", bus.rd32(d.first + o), ((o / 4) & 7) == 7 || o + 4 >= d.second ? "\n" : " ");
    return 0;
}
