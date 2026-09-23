/* hwjob.h -- the console job file (hw/runner.c) parsed and executed on the model.  Header-only, used by the tools.
 * A job runs from reset until the ARM writes MCIPD.SCPU (the done stub), like the runner's SB_ISTEXT poll. */
#pragma once
#include "../src/arm7di.h"
#include "../src/dc_arm_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace wren7 {

struct HwJob {
    std::string name;
    unsigned timeout_ms = 0, repeat = 1;
    bool clear = false;
    std::vector<std::pair<uint32_t, std::string>> loads;
    std::vector<std::pair<uint32_t, uint32_t>> words, reads;
};

struct HwJobFile {
    std::string out;
    std::vector<HwJob> jobs;
};

inline bool read_all(const std::string &p, std::vector<uint8_t> &out)
{
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

inline bool parse_jobs(const std::string &path, HwJobFile &jf)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) { perror(path.c_str()); return false; }
    char line[512];
    HwJob cur;
    while (fgets(line, sizeof line, f)) {
        char cmd[16], a1[256], a2[256], a3[64];
        int n = sscanf(line, "%15s %255s %255s %63s", cmd, a1, a2, a3);
        if (n < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "out")) jf.out = a1;
        else if (!strcmp(cmd, "job") && n >= 4) {
            cur = HwJob();
            cur.name = a1;
            cur.timeout_ms = strtoul(a2, 0, 0);
            cur.repeat = strtoul(a3, 0, 0);
        } else if (!strcmp(cmd, "clear")) cur.clear = true;
        else if (!strcmp(cmd, "load") && n >= 3) cur.loads.push_back({(uint32_t)strtoul(a1, 0, 0), a2});
        else if (!strcmp(cmd, "word") && n >= 3) cur.words.push_back({(uint32_t)strtoul(a1, 0, 0), (uint32_t)strtoul(a2, 0, 0)});
        else if (!strcmp(cmd, "read") && n >= 3) cur.reads.push_back({(uint32_t)strtoul(a1, 0, 0), (uint32_t)strtoul(a2, 0, 0)});
        else if (!strcmp(cmd, "run")) jf.jobs.push_back(cur);
        else if (!strcmp(cmd, "samples") || !strcmp(cmd, "chstate")) continue;   /* console-only diagnostics */
        else { fprintf(stderr, "%s: bad line: %s", path.c_str(), line); fclose(f); return false; }
    }
    fclose(f);
    return true;
}

struct ModelRun {
    bool done = false;
    uint64_t cycles = 0;   /* MCLK from reset release to the SCPU write */
    std::vector<uint8_t> readback;
    Arm7DI::Stats stats;
};

/* Paths in the job are relative to wren7-rtl/model (the tools' working directory). */
inline ModelRun run_job_model(const HwJob &j, const DcWaits &w, uint64_t max_cycles = 400000000ull,
                              FILE *bus_trace = nullptr, int mul_carry = -1)
{
    ModelRun r;
    DcArmBus bus;
    bus.waits = w;
    for (auto &l : j.loads) {
        std::vector<uint8_t> img;
        if (!read_all(l.second, img)) { fprintf(stderr, "cannot read %s\n", l.second.c_str()); return r; }
        bus.load(l.first, img.data(), img.size());
    }
    for (auto &p : j.words) bus.wr32(p.first, p.second);
    Arm7DI cpu(&bus);   /* power-on + reset release */
    bus.attach(&cpu);
    cpu.bus_trace = bus_trace;
    if (mul_carry >= 0) cpu.mul_carry = (Arm7DI::MulCarry)mul_carry;
    while (!bus.scpu_raised && cpu.cycles < max_cycles) cpu.step();
    r.done = bus.scpu_raised;
    r.cycles = bus.scpu_raised ? bus.scpu_t : cpu.cycles;
    r.stats = cpu.stats;
    for (auto &rd : j.reads)
        for (uint32_t i = 0; i < rd.second; i++) r.readback.push_back(bus.ram()[(rd.first + i) & (DcArmBus::RAM_SIZE - 1)]);
    return r;
}

} // namespace wren7
