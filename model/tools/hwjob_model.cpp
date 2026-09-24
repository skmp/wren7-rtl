/* hwjob_model -- execute a console job file on the model: same inputs as hw/runner.c, same outputs.
 *   hwjob_model [--dc] tests/hw/SUITE/jobs.txt [OUTDIR]   (default OUTDIR: the job file's "out" with /hw -> /model)
 * Writes OUTDIR/results.txt ("NAME STATUS 0 CYCLES", CYCLES in ARM MCLK) and OUTDIR/NAME.bin for jobs with read
 * regions.  Bus: zero waits (data sheet cycles), or with --dc the measured console timing DcWaits::dreamcast(). */
#include "hwjob.h"

using namespace wren7;

int main(int argc, char **argv)
{
    DcWaits w;
    if (argc > 1 && !strcmp(argv[1], "--dc")) { w = DcWaits::dreamcast(); argv++; argc--; }
    if (argc < 2) { fprintf(stderr, "usage: hwjob_model [--dc] JOBS [OUTDIR]\n"); return 2; }
    HwJobFile jf;
    if (!parse_jobs(argv[1], jf)) return 2;
    std::string out = argc > 2 ? argv[2] : jf.out;
    if (argc <= 2 && out.size() > 3 && out.compare(out.size() - 3, 3, "/hw") == 0) out = out.substr(0, out.size() - 3) + "/model";
    if (system(("mkdir -p " + out).c_str())) return 2;
    FILE *res = fopen((out + "/results.txt").c_str(), "w");
    int bad = 0;
    for (auto &j : jf.jobs) {
        ModelRun r = run_job_model(j, w);
        fprintf(res, "%s %s 0 %llu\n", j.name.c_str(), r.done ? "done" : "timeout", (unsigned long long)r.cycles);
        if (!j.reads.empty()) {
            FILE *f = fopen((out + "/" + j.name + ".bin").c_str(), "wb");
            fwrite(r.readback.data(), 1, r.readback.size(), f);
            fclose(f);
        }
        if (!r.done) bad++;
    }
    fclose(res);
    printf("%zu jobs, %d without completion -> %s\n", jf.jobs.size(), bad, out.c_str());
    return 0;
}
