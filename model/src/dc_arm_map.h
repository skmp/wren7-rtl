/* dc_arm_map.h -- the AICA ARM7DI's view of the Dreamcast sound system, as an Arm7Bus.
 *
 * Map (from minicast libswirl/hw/arm7/SoundCPU.cpp scpu_ReadMemArm/scpu_WriteMemArm and hw/aica/aica_mmio.cpp):
 *   A[31:24] ignored (addr &= 0x00FFFFFF)
 *   0x000000-0x7FFFFF  wave RAM, 2 MB, mirrored every 2 MB (addr & 0x1FFFFF)
 *   0x800000-0xFFFFFF  AICA registers, offset = addr & 0x7FFF (mirrored every 32 KB):
 *                        0x0000-0x1FFF channel data (64 x 0x80), 0x2000-0x27FF (channel data tail),
 *                        0x2800-0x2FFF common data, 0x3000-0x7FFF DSP data
 *                      ARM-only: 0x2D00 L (read: latched interrupt level), 0x2D04 M (write bit 0: accept)
 * Interrupt path: SCIEB 0x289C, SCIPD 0x28A0, SCIRE 0x28A4, SCILV0-2 0x28A8/0x28AC/0x28B0 (write-only).  Pending &
 * enabled -> lowest set bit picks the level L from SCILV0-2 (bits >= 7 share SCILV bit 7).  Measured on the console
 * (tests/hw/fiqdiag), a 68000-style handshake: a request with L != 0 is latched and drives nFIQ; the ARM's read of
 * L (0x2D00) is the acknowledge that releases nFIQ; the write of M (0x2D04) bit 0 ends the service and lets the
 * next request in.  Level 0 never interrupts.  (minicast: any enabled pending bit, released by the M write only.)
 * nIRQ is not driven.
 *
 * Registers are 16 bits in 32-bit slots (the SH4 view, caique NOTES.md).  The register block here is a plain store
 * plus the interrupt logic above; timers, channel and DSP behaviour belong to caique's AICA model and can be hooked
 * in through AicaRegs.  How the ARM's byte/word accesses land on the 16-bit registers is an open question.
 *
 * Timing: the whole sound-system bus behaviour is carried by DcWaits (nWAIT cycles per access).  Nothing is
 * measured yet; the default is zero wait states.  See NOTES.md "Dreamcast bus timing".
 */
#pragma once
#include "arm7di.h"
#include <vector>

namespace wren7 {

struct DcWaits {
    uint32_t ram_rn = 0, ram_rs = 0;   /* wave RAM read, N / S cycle */
    uint32_t ram_wn = 0, ram_ws = 0;   /* wave RAM write, N / S cycle */
    uint32_t reg_r = 0, reg_w = 0;     /* AICA register read / write */
    uint32_t internal = 0;             /* I and C cycles */
    /* Slot-grid mode (grid > 0): a memory cycle (N or S, any region) may only start on a multiple of `grid` MCLK
     * and then lasts `access` MCLK; the LOCKed write of a SWP lasts `locked_write`.  I/C cycles take one MCLK.
     * The fixed waits above are ignored in this mode. */
    uint32_t grid = 0, access = 8, locked_write = 4;
    /* Slot-grid mode: a memory cycle may not start at t with (t + block_phase) % block_period == 0 (another bus
     * user's half slot); it moves to the next grid point.  block_period = 0: no blocking. */
    uint32_t block_period = 0, block_phase = 0;
    /* alternatively a set of blocked grid points within the period: block_set[((t + block_phase) % block_period) / grid] */
    std::vector<uint8_t> block_set;

    /* The console as measured (NOTES.md "Bus timing", tests/hw/timing, dspmem), sound generator idle: MCLK = 22.5792
     * MHz = 512 per sample = 128 DSP steps of 4 MCLK; a memory cycle starts at a step boundary whose wave RAM slot is
     * free and lasts 8 (a SWP's locked write 4); DSP steps 109 and 111 are always taken; L/M (0x802D00-07) take
     * 1 MCLK (tests/hw/fiqcost).  Their phase relative to the sample edge is
     * not known yet (the averages the fit used do not depend on it). */
    /* dsp_slots: the wave RAM slots come from the DSP step schedule (see DcArmBus): the grid points are DSP step
     * boundaries, a step's slot is free unless a fixed user (steps 109, 111) or a DSP MRD/MWT of that step takes it.
     * block_period / block_set are ignored then. */
    bool dsp_slots = false;
    static DcWaits dreamcast()
    {
        DcWaits w;
        w.grid = 4;
        w.access = 8;
        w.locked_write = 4;
        w.dsp_slots = true;
        return w;
    }
};

/* Register-block hook; the default implementation is AicaRegStub. */
struct AicaRegs {
    virtual uint16_t read16(uint32_t off) = 0;             /* off: byte offset of the 32-bit slot, & 0x7FFC */
    virtual void write16(uint32_t off, uint16_t v, uint16_t mask) = 0;
    virtual ~AicaRegs() {}
};

class DcArmBus : public Arm7Bus {
public:
    static const uint32_t RAM_SIZE = 2u << 20;
    static const uint32_t REG_L = 0x2D00, REG_M = 0x2D04;
    static const uint32_t SCIEB = 0x289C, SCIPD = 0x28A0, SCIRE = 0x28A4, SCILV0 = 0x28A8;

    DcArmBus();
    void attach(Arm7DI *core) { core_ = core; }

    void cycle(const BusCycle &c, BusResult &r) override;
    void tick(uint64_t t) override { sample_clock(t); }

    /* wave RAM access from the harness (the SH4 side) */
    void load(uint32_t addr, const void *data, size_t len);
    uint32_t rd32(uint32_t addr) const;
    void wr32(uint32_t addr, uint32_t v);
    uint8_t *ram() { return ram_.data(); }

    /* AICA registers from the harness (the SH4 side): same store as the ARM sees */
    uint16_t reg_read(uint32_t off);
    void reg_write(uint32_t off, uint16_t v);

    DcWaits waits;
    AicaRegs *regs = nullptr;   /* nullptr: internal store */

    /* Sample clock: SCIPD/MCIPD bit 10 (one-sample interval) is set every sample_period MCLK, first at sample_phase
     * (MCLK counted from the reset release).  512 MCLK per sample is measured (tests/hw/timing smpswp); the phase
     * relative to the reset release is not known.  0 = off. */
    uint32_t sample_period = 512, sample_phase = 0;

    /* DSP step clock (slot-grid mode): 128 steps of 4 MCLK per sample, step 0 at sample_phase + dsp_phase.  Per the
     * AICA manual's buffer timing table, a step's second half is the CPU port of TEMP / EFREG, shared with the DSP's
     * own writes (TWT, EWT): a CPU access to TEMP (0x4000-0x43FF) or EFREG (0x4580-0x45BF) whose port time
     * (start + dsp_port_off) falls in a step that writes that buffer waits for the next step (tests/hw/dspport).
     * The steps are decoded from MPRO as the CPU wrote it (the DSP itself is not modelled here). */
    int32_t dsp_phase = 40, dsp_port_off = 0;   /* measured: step 0 = edge + 40 MCLK = 10 steps (the joint fit of
                                                 * tests/hw/dspport, blkshot, blkphase, fiqcost: unique) */
    /* Wave RAM slots (waits.dsp_slots): one per DSP step.  Every DSP MRD / MWT takes the slot of its own step.
     * Another user takes two slots per sample, nominally steps 109 and 111 (tests/hw/dspmem1); where the DSP holds
     * one of them it moves one step earlier or later, the two staying >= 2 steps apart, earliest placement first
     * (fits: DSP at 109 -> 108/111, at 111 -> 109/112, every odd step -> 108/110; tests/hw/dspmem, dspmem1).  An ARM
     * wave RAM access may start only at the boundary of a free step.  Built from MPRO as written. */
    std::vector<int> fixed_steps = {109, 111};
    /* Sound generator (tests/hw/sgc): channel K, when keyed on (KYONB), fetches its sample data in the slot of DSP
     * step (2K + sgc_step0) mod 128 -- every sample for PCM16 / PCM8 at any pitch measured (OCT -1..7).  The channel's
     * own play state (loop end, envelope off) is not modelled here: keyed on = fetching. */
    int sgc_step0 = -14;
    /* ADPCM (PCMS 2/3, tests/hw/sgcadp): the channel holds one 16-bit word of its stream; a sample fetches when the
     * play position enters another word, or when the look-ahead nibble (position + 1, the interpolation's second
     * sample) lies in the next word -- at most one fetch per sample.  This reproduces the console's shares exactly
     * (OCT -2 5/16, -1 3/8, 0 and +1 1/2, +2 1) and the per-iteration pattern of the register-shift kernels, which
     * the earlier evenly spread rates (0.31 / 0.37 / 0.5 / 1) missed by up to 0.35 MCLK (sgc f_adp_o0_rsh).  OCT 3..7:
     * no fetch (the channel stops).  Derived with caique-rtl rtl/v1, which fetches this way (aica_sgc claim).
     * Position: nibbles from SA, advancing (1024 + FNS) << (OCT + 4) / 2^14 per sample (the pitch accumulator), 0 at
     * the SGC sample adpcm_origin.  The SGC sample of a slot is its DSP sample, except channels 0..6 (steps 114..126,
     * the end of the DSP sample) which already belong to the next one.  Loops are not modelled (LEA far away). */
    int64_t adpcm_origin = 0;
    bool adpcm_fetch(int ch, int64_t sgc_sample);
    bool slot_used(uint32_t step, uint64_t sample = 0);

    /* SH4 (G2) wave RAM accesses: each takes the slot of the first step boundary at or after its arrival that no
     * DSP / sound-generator / fixed user holds, ahead of the ARM (tests/hw/sh4load).  G2 is not synchronous to the
     * AICA, so the arrival times are an input: sh4_access(t) from a system model, or the periodic generator
     * (sh4_period MCLK apart from sh4_phase; 0 = none) for tests.  AICA register traffic from the SH4 costs the ARM
     * nothing (measured) and is not modelled. */
    uint64_t sh4_access(uint64_t t, int slots = 1);   /* returns the last slot taken */
    double sh4_period = 0, sh4_phase = 0;
    int sh4_slots = 1;             /* slots per access for the periodic generator */
    int sh4_slot_spacing = 4;      /* MCLK between the slots of one access */
    bool sh4_even_only = false;    /* experiment: SH4 accesses only on even steps */
    /* SH4 read loop (a CPU polling wave RAM): each read takes sh4_slots slots sh4_slot_spacing apart, its data is
     * back sh4_read_latency MCLK after the last one, and the next read is issued then or sh4_period after the
     * previous issue, whichever is later.  0 = off (the plain periodic generator above is used). */
    double sh4_read_latency = 0;
    /* a sample edge is visible (SCIPD bit 10, and through it nFIQ) from edge + edge_latency MCLK on; it is
     * evaluated at every bus cycle start and every instruction boundary */
    uint32_t edge_latency = 0;

    uint64_t reg_accesses = 0;
    uint64_t blocked_hits = 0;   /* memory cycles moved by a blocked grid point (slot-grid mode) */
    /* ARM -> SH4 interrupt: set when the ARM writes MCIPD with bit 5 (SCPU), as the console jobs' done stub does */
    static const uint32_t MCIPD = 0x28B8;
    bool scpu_raised = false;
    uint64_t scpu_t = 0;   /* MCLK count at the start of that write cycle */

private:
    std::vector<uint8_t> ram_;
    uint16_t store_[0x8000 / 4];
    Arm7DI *core_ = nullptr;
    bool e68k_out_ = false;      /* the controller holds a request (in service until the M write) */
    bool fiq_ = false;           /* nFIQ asserted: from the request until the ARM reads L */
    uint32_t grid_wait_ = 0;
    uint64_t next_sample_ = 0;
    bool slots_dirty_ = true;
    std::vector<uint64_t> sh4_slots_;   /* taken step boundaries (MCLK), sorted, pruned as time passes */
    double sh4_next_ = -1;
    bool free_at(uint64_t g);          /* slot at step boundary g free for a wave RAM access */
    void sh4_advance(uint64_t t);
    uint8_t slots_[128];
    bool sample_init_ = false;
    uint32_t e68k_L_ = 0;

    uint16_t rreg(uint32_t off);
    void wreg(uint32_t off, uint16_t v, uint16_t mask);
    void update_interrupts();
    void sample_clock(uint64_t t);
};

} // namespace wren7
