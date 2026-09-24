/* dc_arm_map.cpp -- see dc_arm_map.h */
#include "dc_arm_map.h"
#include <string.h>
#include <functional>
#include <algorithm>

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

bool DcArmBus::slot_used(uint32_t step, uint64_t sample)
{
    /* sound generator fetches */
    {
        const int k = (((int)(step & 127) - sgc_step0) % 128 + 128) % 128;
        if (!(k & 1)) {
            const uint16_t r0 = store_[(0x80 * (k >> 1)) >> 2];
            if (r0 & 0x4000) {   /* KYONB */
                const uint32_t pcms = (r0 >> 7) & 3;
                if (pcms < 2) return true;
                const int ch = k >> 1;
                if (adpcm_fetch(ch, (int64_t)sample - 1024 + (ch < 7 ? 1 : 0))) return true;
            }
        }
    }
    if (slots_dirty_) {
        uint8_t dsp[128];
        for (uint32_t st = 0; st < 128; st++) dsp[st] = (store_[(0x3408 + 16 * st) >> 2] & 0x6000) != 0;   /* MRD|MWT */
        memcpy(slots_, dsp, sizeof slots_);
        /* the other user's accesses: each at its nominal step if the DSP leaves it free, else one step either side;
         * consecutive ones >= 2 apart; the earliest placement wins */
        std::vector<int> placed;
        std::vector<int> cur(fixed_steps.size());
        bool found = false;
        std::function<void(size_t)> place = [&](size_t i) {
            if (found) return;
            if (i == fixed_steps.size()) { placed = cur; found = true; return; }
            const int n = fixed_steps[i];
            const int opts[3] = {n, n - 1, n + 1};
            int order[3] = {1, 0, 2};   /* earliest first: n-1, n, n+1, but the nominal step when it is free */
            if (!dsp[n & 127]) { order[0] = 0; order[1] = 1; order[2] = 2; }
            for (int k : order) {
                const int c = opts[k];
                if (dsp[c & 127] || (k != 0 && !dsp[n & 127])) continue;
                if (i && c - cur[i - 1] < 2) continue;
                cur[i] = c;
                place(i + 1);
                if (found) return;
            }
        };
        place(0);
        if (!found) placed = fixed_steps;
        for (int c : placed) slots_[c & 127] = 1;
        slots_dirty_ = false;
    }
    return slots_[step & 127];
}

bool DcArmBus::free_at(uint64_t g)
{
    const int64_t rel = (int64_t)g - (int64_t)sample_phase - dsp_phase;
    const uint64_t smp = (uint64_t)((rel + 512 * 1024) / 512);
    if (slot_used((uint32_t)(((rel % 512) + 512) % 512) / 4, smp)) return false;
    return !std::binary_search(sh4_slots_.begin(), sh4_slots_.end(), g);
}

bool DcArmBus::adpcm_fetch(int ch, int64_t n)
{
    const uint32_t r0 = store_[(0x80 * ch) >> 2], r4 = store_[(0x80 * ch + 0x04) >> 2], r18 = store_[(0x80 * ch + 0x18) >> 2];
    const int o4 = (int)((r18 >> 11) & 15);
    if (o4 >= 3 && o4 <= 7) return false;
    const int sh = (o4 >= 8 ? o4 - 16 : o4) + 4;
    const uint64_t m = 1024 + (r18 & 0x3FF), incr = sh >= 0 ? m << sh : m >> -sh;
    const uint64_t sa = ((uint64_t)(r0 & 0x7F) << 16) | r4;
    n -= adpcm_origin;
    auto pos = [&](int64_t i) -> uint64_t { return i > 0 ? ((uint64_t)i * incr) >> 14 : 0; };
    auto word = [&](uint64_t p) -> uint64_t { return (sa + (p >> 1)) >> 1; };
    const uint64_t p1 = pos(n), p0 = pos(n - 1);
    return word(p1) != word(p0) || word(p1 + 1) != word(p1);
}

uint64_t DcArmBus::sh4_access(uint64_t t, int slots)
{
    uint64_t last = 0;
    uint64_t g = (t + 3) / 4 * 4;
    if (sh4_even_only) {   /* experiment: align to an even DSP step */
        const int64_t rel = (int64_t)g - (int64_t)sample_phase - dsp_phase;
        if (((((rel % 512) + 512) % 512) / 4) & 1) g += 4;
    }
    for (int n = 0; n < slots; n++, g += sh4_slot_spacing) {
        for (int guard = 0; guard < 256; guard++, g += 4) {
            const int64_t rel = (int64_t)g - (int64_t)sample_phase - dsp_phase;
            const uint64_t smp = (uint64_t)((rel + 512 * 1024) / 512);
            if (slot_used((uint32_t)(((rel % 512) + 512) % 512) / 4, smp)) continue;
            if (std::binary_search(sh4_slots_.begin(), sh4_slots_.end(), g)) continue;
            break;
        }
        sh4_slots_.insert(std::upper_bound(sh4_slots_.begin(), sh4_slots_.end(), g), g);
        last = g;
    }
    return last;
}

/* periodic SH4 arrivals up to time t; slots far in the past are dropped */
void DcArmBus::sh4_advance(uint64_t t)
{
    if (sh4_period > 0) {
        if (sh4_next_ < 0) sh4_next_ = sh4_phase;
        while (sh4_next_ <= (double)t) {
            const double issue = sh4_next_;
            const uint64_t last = sh4_access((uint64_t)issue, sh4_slots);
            if (sh4_read_latency > 0) sh4_next_ = std::max((double)last + 8 + sh4_read_latency, issue + sh4_period);
            else sh4_next_ += sh4_period;
        }
    }
    while (!sh4_slots_.empty() && sh4_slots_.front() + 1024 < t) sh4_slots_.erase(sh4_slots_.begin());
}

uint16_t DcArmBus::reg_read(uint32_t off) { return rreg(off & 0x7FFC); }
void DcArmBus::reg_write(uint32_t off, uint16_t v) { wreg(off & 0x7FFC, v, 0xFFFF); }

uint16_t DcArmBus::rreg(uint32_t off)
{
    if (off == REG_L) {                             /* interrupt acknowledge: releases nFIQ */
        if (fiq_) { fiq_ = false; if (core_) core_->set_fiq(false); }
        return e68k_L_;
    }
    if (off == REG_M) return 0;
    if (off >= SCILV0 && off <= SCILV0 + 8) return 0;   /* SCILV0-2 are write-only (tests/hw/fiqdiag) */
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
    if (off >= 0x3400 && off < 0x3C00) slots_dirty_ = true;   /* MPRO: the slot schedule changes */
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

/* request latch (minicast aica_mmio.cpp update_arm_interrupts / SoundCPU.cpp update_e68k), with the handshake and
 * level gating measured on the console (see the header) */
void DcArmBus::update_interrupts()
{
    const uint32_t pend = store_[SCIEB >> 2] & store_[SCIPD >> 2] & 0x7FF;
    if (pend && !e68k_out_) {
        int bit = __builtin_ctz(pend);
        if (bit > 7) bit = 7;
        uint32_t L = 0;
        for (int i = 0; i < 3; i++)
            if (store_[(SCILV0 >> 2) + i] & (1u << bit)) L |= 1u << i;
        if (L) {
            e68k_out_ = true;
            e68k_L_ = L;
            fiq_ = true;
        }
    }
    if (core_) core_->set_fiq(fiq_);
}

/* the AICA's one-sample interval: SCIPD and MCIPD bit 10, every sample_period MCLK */
void DcArmBus::sample_clock(uint64_t t)
{
    if (!sample_period) return;
    if (!sample_init_) { next_sample_ = sample_phase % sample_period; sample_init_ = true; }
    while (t >= next_sample_ + edge_latency) {
        store_[SCIPD >> 2] |= 0x400;
        store_[MCIPD >> 2] |= 0x400;
        update_interrupts();
        next_sample_ += sample_period;
    }
}

void DcArmBus::cycle(const BusCycle &c, BusResult &r)
{
    r.abort = false;
    sample_clock(c.t);
    if (c.type == CYC_I || c.type == CYC_C) { r.wait = waits.grid ? 0 : waits.internal; return; }
    const bool wr = c.flags & BF_WRITE, byte = c.flags & BF_BYTE;
    const uint32_t a = c.addr & 0x00FFFFFF;
    /* the ARM interrupt controller's L/M window (0x802D00-0x802D07) sits in the ARM interface, not behind the
     * memory arbiter: no slot, no wait (tests/hw/fiqcost: LDR L/M + ORR = 24 MCLK, STR M = 12, against 32 / 16 for
     * the other AICA registers) */
    const bool local = a >= 0x800000 && (a & 0x7FF8) == REG_L;
    if (waits.grid && local) grid_wait_ = 0;
    else if (waits.grid) {
        uint64_t start = (c.t + waits.grid - 1) / waits.grid * waits.grid;
        const uint32_t ro = a & 0x7FFF;
        const bool temp = a >= 0x800000 && ro >= 0x4000 && ro < 0x4400, efreg = a >= 0x800000 && ro >= 0x4580 && ro < 0x45C0;
        if (temp || efreg) {
            for (int guard = 0; guard < 256; guard++) {
                const int64_t rel = (int64_t)(start + dsp_port_off) - (int64_t)sample_phase - dsp_phase;
                const uint32_t step = (uint32_t)(((rel % 512) + 512) % 512) / 4;
                const uint16_t w0 = store_[(0x3400 + 16 * step) >> 2], w2 = store_[(0x3408 + 16 * step) >> 2];
                if (!((temp && (w0 & 0x100)) || (efreg && (w2 & 0x1000)))) break;
                start += waits.grid;
            }
        }
        /* the blocked slots belong to other wave RAM users: AICA register accesses are not affected
         * (tests/hw/timing ldr_reg) */
        if (a < 0x800000 && waits.dsp_slots) {
            sh4_advance(start + 64);
            for (int guard = 0; guard < 256 && !free_at(start); guard++) {
                start += waits.grid;
                blocked_hits++;
                sh4_advance(start + 64);
            }
        } else if (a < 0x800000 && waits.block_period && waits.block_set.empty())
            while ((start + waits.block_phase) % waits.block_period == 0) start += waits.grid;
        else if (a < 0x800000 && waits.block_period)
            while (waits.block_set[((start + waits.block_phase) % waits.block_period) / waits.grid]) {
                start += waits.grid;
                blocked_hits++;
            }
        const uint32_t len = (wr && (c.flags & BF_LOCK)) ? waits.locked_write : waits.access;
        grid_wait_ = (uint32_t)(start - c.t) + len - 1;
    }
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
        if (waits.grid) r.wait = grid_wait_;
        return;
    }
    /* AICA registers: 16 bits in the low half of each 32-bit slot; byte lanes 2/3 read 0, writes to them dropped */
    reg_accesses++;
    const uint32_t off = a & 0x7FFF, slot = off & 0x7FFC, lane = off & 3;
    if (wr) {
        if (slot == MCIPD && lane == 0 && (c.wdata & 0x20) && !scpu_raised) { scpu_raised = true; scpu_t = c.t; }
        if (!byte) wreg(slot, c.wdata & 0xFFFF, 0xFFFF);
        else if (lane < 2) wreg(slot, (c.wdata & 0xFF) << (8 * lane), 0xFF << (8 * lane));
        r.wait = waits.reg_w;
    } else {
        r.rdata = rreg(slot);
        r.wait = waits.reg_r;
    }
    if (waits.grid) r.wait = grid_wait_;
}

} // namespace wren7
