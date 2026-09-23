/* dc_arm_map.cpp -- see dc_arm_map.h */
#include "dc_arm_map.h"
#include <string.h>

namespace wren7 {

DcArmBus::DcArmBus() : ram_(RAM_SIZE, 0) { memset(store_, 0, sizeof store_); }

void DcArmBus::load(uint32_t addr, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) ram_[(addr + i) & (RAM_SIZE - 1)] = p[i];
}

uint32_t DcArmBus::rd32(uint32_t addr) const
{
    const uint32_t a = addr & (RAM_SIZE - 4);
    return ram_[a] | (ram_[a + 1] << 8) | (ram_[a + 2] << 16) | ((uint32_t)ram_[a + 3] << 24);
}

void DcArmBus::wr32(uint32_t addr, uint32_t v)
{
    const uint32_t a = addr & (RAM_SIZE - 4);
    ram_[a] = v;
    ram_[a + 1] = v >> 8;
    ram_[a + 2] = v >> 16;
    ram_[a + 3] = v >> 24;
}

uint16_t DcArmBus::reg_read(uint32_t off) { return rreg(off & 0x7FFC); }
void DcArmBus::reg_write(uint32_t off, uint16_t v) { wreg(off & 0x7FFC, v, 0xFFFF); }

uint16_t DcArmBus::rreg(uint32_t off)
{
    if (off == REG_L) return e68k_L_;
    if (off == REG_M) return 0;
    return regs ? regs->read16(off) : store_[off >> 2];
}

void DcArmBus::wreg(uint32_t off, uint16_t v, uint16_t mask)
{
    if (off == REG_L) return;                       /* read only */
    if (off == REG_M) {                             /* interrupt accept */
        if (v & mask & 1) { e68k_out_ = false; update_interrupts(); }
        return;
    }
    if (regs) { regs->write16(off, v, mask); update_interrupts(); return; }
    uint16_t &s = store_[off >> 2];
    if (off == SCIPD) {                             /* only SCPU (bit 5) is writable, as a set */
        if (v & mask & 0x20) s |= 0x20;
    } else if (off == SCIRE) {                      /* write-1-to-clear SCIPD, reads back 0 */
        store_[SCIPD >> 2] &= ~(v & mask);
    } else {
        s = (s & ~mask) | (v & mask);
    }
    update_interrupts();
}

/* minicast aica_mmio.cpp update_arm_interrupts() + SoundCPU.cpp update_e68k() */
void DcArmBus::update_interrupts()
{
    const uint32_t pend = rreg(SCIEB) & rreg(SCIPD) & 0x7FF;
    if (pend && !e68k_out_) {
        int bit = __builtin_ctz(pend);
        if (bit > 7) bit = 7;
        uint32_t L = 0;
        for (int i = 0; i < 3; i++)
            if (rreg(SCILV0 + 4 * i) & (1u << bit)) L |= 1u << i;
        e68k_out_ = true;
        e68k_L_ = L;
    }
    if (core_) core_->set_fiq(e68k_out_);
}

void DcArmBus::cycle(const BusCycle &c, BusResult &r)
{
    r.abort = false;
    if (c.type == CYC_I || c.type == CYC_C) { r.wait = waits.internal; return; }
    const bool wr = c.flags & BF_WRITE, byte = c.flags & BF_BYTE;
    const uint32_t a = c.addr & 0x00FFFFFF;
    if (a < 0x800000) {
        const uint32_t ra = a & (RAM_SIZE - 1);
        if (wr) {
            if (byte) ram_[ra] = c.wdata >> (8 * (ra & 3));
            else wr32(ra, c.wdata);
            r.wait = c.type == CYC_N ? waits.ram_wn : waits.ram_ws;
        } else {
            r.rdata = rd32(ra);
            r.wait = c.type == CYC_N ? waits.ram_rn : waits.ram_rs;
        }
        return;
    }
    /* AICA registers: 16 bits in the low half of each 32-bit slot; byte lanes 2/3 read 0, writes to them dropped */
    reg_accesses++;
    const uint32_t off = a & 0x7FFF, slot = off & 0x7FFC, lane = off & 3;
    if (wr) {
        if (!byte) wreg(slot, c.wdata & 0xFFFF, 0xFFFF);
        else if (lane < 2) wreg(slot, (c.wdata & 0xFF) << (8 * lane), 0xFF << (8 * lane));
        r.wait = waits.reg_w;
    } else {
        r.rdata = rreg(slot);
        r.wait = waits.reg_r;
    }
}

} // namespace wren7
