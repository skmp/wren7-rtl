/* dc_arm_map.h -- the AICA ARM7DI's view of the Dreamcast sound system, as an Arm7Bus.
 *
 * Map (from minicast libswirl/hw/arm7/SoundCPU.cpp scpu_ReadMemArm/scpu_WriteMemArm and hw/aica/aica_mmio.cpp):
 *   A[31:24] ignored (addr &= 0x00FFFFFF)
 *   0x000000-0x7FFFFF  wave RAM, 2 MB, mirrored every 2 MB (addr & 0x1FFFFF)
 *   0x800000-0xFFFFFF  AICA registers, offset = addr & 0x7FFF (mirrored every 32 KB):
 *                        0x0000-0x1FFF channel data (64 x 0x80), 0x2000-0x27FF (channel data tail),
 *                        0x2800-0x2FFF common data, 0x3000-0x7FFF DSP data
 *                      ARM-only: 0x2D00 L (read: latched interrupt level), 0x2D04 M (write bit 0: accept)
 * Interrupt path (minicast): SCIEB 0x289C, SCIPD 0x28A0, SCIRE 0x28A4, SCILV0-2 0x28A8/0x28AC/0x28B0.  Pending &
 * enabled -> lowest set bit picks L (bits >= 7 share SCILV bit 7) -> "e68k" latch -> nFIQ.  nIRQ is not driven.
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

    /* The console as measured (NOTES.md "Bus timing", tests/hw/timing), sound generator idle: MCLK = 22.5792 MHz =
     * 512 per sample; a memory cycle starts on a 4-MCLK grid and lasts 8 (a SWP's locked write 4); per 512-MCLK frame
     * two adjacent phase-4 half slots belong to another wave RAM user.  Their phase relative to the sample edge is
     * not known yet (the averages the fit used do not depend on it). */
    static DcWaits dreamcast()
    {
        DcWaits w;
        w.grid = 4;
        w.access = 8;
        w.locked_write = 4;
        w.block_period = 512;
        w.block_set.assign(512 / 4, 0);
        w.block_set[52 / 4] = w.block_set[60 / 4] = 1;
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

    uint64_t reg_accesses = 0;
    /* ARM -> SH4 interrupt: set when the ARM writes MCIPD with bit 5 (SCPU), as the console jobs' done stub does */
    static const uint32_t MCIPD = 0x28B8;
    bool scpu_raised = false;
    uint64_t scpu_t = 0;   /* MCLK count at the start of that write cycle */

private:
    std::vector<uint8_t> ram_;
    uint16_t store_[0x8000 / 4];
    Arm7DI *core_ = nullptr;
    bool e68k_out_ = false;
    uint32_t grid_wait_ = 0;
    uint32_t e68k_L_ = 0;

    uint16_t rreg(uint32_t off);
    void wreg(uint32_t off, uint16_t v, uint16_t mask);
    void update_interrupts();
};

} // namespace wren7
