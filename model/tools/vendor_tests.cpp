/* vendor_tests -- run every vendor/arm7di-tests-dreamcast/bins/<test>.s.bin on the model.
 *
 * Each test (split from armwrestler's raw.s by vendor test.py) is linked at 0, runs from reset, and ends at a
 * `.word 0xDEADBEEF`.  r1 collects failure bits (BAD_Rd 0x10, BAD_Rn 0x20, flag bits 1/2/4/8, 0x80 memory);
 * bits 31/30 are group markers set by the LDR / LDM-STM groups, r2 is the sub-test number.  Pass = the stop word
 * was reached with (r1 & 0x3FFFFFFF) == 0 and no exception was taken after reset.
 *
 *   vendor_tests [bins-dir]      (default ../vendor/arm7di-tests-dreamcast/bins, run from wren7-rtl/model)
 * Exit status 0 iff every test passes.
 */
#include "../src/arm7di.h"
#include "../src/dc_arm_map.h"
#include <dirent.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace wren7;

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : "../vendor/arm7di-tests-dreamcast/bins";
    std::vector<std::string> names;
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0) names.push_back(n);
        }
        closedir(d);
    } else {
        perror(dir.c_str());
        return 2;
    }
    std::sort(names.begin(), names.end());

    int pass = 0, fail = 0;
    printf("%-14s %-4s %-10s %-4s %7s %7s %5s %5s %5s %5s\n", "test", "res", "r1", "r2", "instr", "cycles", "N", "S",
           "I", "undoc");
    for (auto &n : names) {
        FILE *f = fopen((dir + "/" + n).c_str(), "rb");
        std::vector<uint8_t> img;
        for (int c; (c = fgetc(f)) != EOF;) img.push_back(c);
        fclose(f);

        DcArmBus bus;
        bus.load(0, img.data(), img.size());
        Arm7DI cpu(&bus);
        bus.attach(&cpu);
        const uint64_t exc0 = cpu.stats.exceptions;
        bool stopped = false;
        while (cpu.cycles < 1000000) {
            if (cpu.exec_word() == 0xDEADBEEF) { stopped = true; break; }
            cpu.step();
        }
        const uint32_t r1 = cpu.reg(1), r2 = cpu.reg(2);
        const bool exc = cpu.stats.exceptions != exc0;
        const bool ok = stopped && !exc && (r1 & 0x3FFFFFFF) == 0;
        (ok ? pass : fail)++;
        std::string t = n.substr(0, n.find(".s.bin"));
        printf("%-14s %-4s %08x %4u %7llu %7llu %5llu %5llu %5llu %5llu%s%s\n", t.c_str(), ok ? "PASS" : "FAIL", r1,
               r2, (unsigned long long)cpu.stats.instr, (unsigned long long)cpu.cycles,
               (unsigned long long)cpu.stats.n, (unsigned long long)cpu.stats.s, (unsigned long long)cpu.stats.i,
               (unsigned long long)cpu.stats.undoc, stopped ? "" : "  (no stop word)", exc ? "  (exception)" : "");
    }
    printf("%d passed, %d failed, %zu total\n", pass, fail, names.size());
    return fail ? 1 : 0;
}
