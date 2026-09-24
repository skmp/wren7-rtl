/* arm7di.cpp -- wren7 ARM7DI core model.  See arm7di.h; section / table numbers refer to DDI0027D. */
#include "arm7di.h"
#include <string.h>

namespace wren7 {

static inline uint32_t ror32(uint32_t v, unsigned n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }

/* a + b + cin with carry out and signed overflow (SUB/CMP use a + ~b + 1, so C = no borrow) */
static inline uint32_t add(uint32_t a, uint32_t b, uint32_t cin, bool &c, bool &v)
{
    uint64_t s = (uint64_t)a + b + cin;
    uint32_t r = (uint32_t)s;
    c = (s >> 32) & 1;
    v = ((~(a ^ b) & (a ^ r)) >> 31) & 1;
    return r;
}

/* Barrel shifter, immediate shift amount (section 4.4.2): amount 0 encodes LSL #0, LSR #32, ASR #32, RRX. */
static inline void shift_imm(uint32_t rm, int type, unsigned n, bool cin, uint32_t &out, bool &c)
{
    switch (type) {
    case 0: /* LSL */
        if (n == 0) { out = rm; c = cin; }
        else { out = rm << n; c = (rm >> (32 - n)) & 1; }
        break;
    case 1: /* LSR */
        if (n == 0) { out = 0; c = rm >> 31; }
        else { out = rm >> n; c = (rm >> (n - 1)) & 1; }
        break;
    case 2: /* ASR */
        if (n == 0) { out = (rm >> 31) ? 0xFFFFFFFFu : 0; c = rm >> 31; }
        else { out = (uint32_t)((int32_t)rm >> n); c = (rm >> (n - 1)) & 1; }
        break;
    default: /* ROR / RRX */
        if (n == 0) { out = ((uint32_t)cin << 31) | (rm >> 1); c = rm & 1; }
        else { out = ror32(rm, n); c = (rm >> (n - 1)) & 1; }
        break;
    }
}

/* Barrel shifter, amount = bottom byte of Rs (section 4.4.2, "Register specified shift amount"). */
static inline void shift_reg(uint32_t rm, int type, unsigned n, bool cin, uint32_t &out, bool &c)
{
    if (n == 0) { out = rm; c = cin; return; }
    switch (type) {
    case 0:
        if (n < 32) { out = rm << n; c = (rm >> (32 - n)) & 1; }
        else if (n == 32) { out = 0; c = rm & 1; }
        else { out = 0; c = 0; }
        break;
    case 1:
        if (n < 32) { out = rm >> n; c = (rm >> (n - 1)) & 1; }
        else if (n == 32) { out = 0; c = rm >> 31; }
        else { out = 0; c = 0; }
        break;
    case 2:
        if (n < 32) { out = (uint32_t)((int32_t)rm >> n); c = (rm >> (n - 1)) & 1; }
        else { out = (rm >> 31) ? 0xFFFFFFFFu : 0; c = rm >> 31; }
        break;
    default:
        n &= 31;
        if (n == 0) { out = rm; c = rm >> 31; }   /* ROR by 32, 64, ...: Rm, carry = bit 31 */
        else { out = ror32(rm, n); c = (rm >> (n - 1)) & 1; }
        break;
    }
}

const char *mode_name(uint32_t mode)
{
    switch (mode & PSR_M) {
    case M_USR: return "usr";
    case M_FIQ: return "fiq";
    case M_IRQ: return "irq";
    case M_SVC: return "svc";
    case M_ABT: return "abt";
    case M_UND: return "und";
    default: return "???";
    }
}

Arm7DI::Arm7DI(Arm7Bus *bus) : bus_(bus) { power_on(); }

int Arm7DI::bank_of(uint32_t mode)
{
    switch (mode & PSR_M) {
    case M_FIQ: return B_FIQ;
    case M_IRQ: return B_IRQ;
    case M_SVC: return B_SVC;
    case M_ABT: return B_ABT;
    case M_UND: return B_UND;
    default: return B_USR;   /* user, and the illegal mode values ("unrecoverable state", 3.3) */
    }
}

/* Physical register file: 0-7 r0-r7, 8-14 r8-r14 usr, 15-21 r8-r14 fiq, 22/23 irq, 24/25 svc, 26/27 abt, 28/29 und. */
int Arm7DI::phys(int r, int bank)
{
    if (r < 8) return r;
    if (bank == B_FIQ) return 15 + (r - 8);
    if (r < 13 || bank == B_USR) return r;
    return 22 + 2 * (bank - B_IRQ) + (r - 13);
}

void Arm7DI::power_on()
{
    memset(gpr_, 0, sizeof gpr_);
    memset(spsr_, 0, sizeof spsr_);
    pc_ = 0;
    cpsr_ = M_SVC | PSR_I | PSR_F;
    ex_ = de_ = f1_ = Slot{0, false};
    next_type_ = CYC_N;
    last_abort_ = dabort_pending_ = false;
    in_reset_ = false;
    reset_exit_ = true;
    fiq_in_ = irq_in_ = false;
    fiq_pipe_ = irq_pipe_ = 0;
    cycles = 0;
    stats = Stats();
    seq_ = SQ_IDLE;
    k_ = 0;
    x_ = InFlight();
    do bus_cycle(); while (seq_ != SQ_IDLE);   /* reset release: the vector 0 entry */
}

void Arm7DI::set_reset(bool asserted)
{
    if (asserted) in_reset_ = true;
    else if (in_reset_) { in_reset_ = false; reset_exit_ = true; }
}

uint32_t Arm7DI::reg(int r) const { return r == 15 ? pc_ : gpr_[phys(r, bank())]; }
void Arm7DI::set_reg(int r, uint32_t v) { if (r < 15) gpr_[phys(r, bank())] = v; }

void Arm7DI::undoc(const char *what)
{
    stats.undoc++;
    if (insn_trace) fprintf(insn_trace, "           undoc: %s\n", what);
}

void Arm7DI::wr(int r, uint32_t v)
{
    if (r == 15) { undoc("R15 written through a register-file-only path (ignored)"); return; }
    gpr_[phys(r, bank())] = v;
}

void Arm7DI::wr_bank(int r, int b, uint32_t v)
{
    if (r == 15) { undoc("R15 written through a register-file-only path (ignored)"); return; }
    gpr_[phys(r, b)] = v;
}

uint32_t Arm7DI::spsr_cur() const
{
    int b = bank();
    if (b == B_USR) {
        const_cast<Arm7DI *>(this)->undoc("SPSR read in user mode (returns CPSR)");
        return cpsr_;
    }
    return spsr_[b];
}

void Arm7DI::set_spsr_cur(uint32_t v, uint32_t mask)
{
    int b = bank();
    if (b == B_USR) { undoc("SPSR write in user mode (ignored)"); return; }
    spsr_[b] = ((spsr_[b] & ~mask) | (v & mask)) & PSR_IMPL;
}

void Arm7DI::write_cpsr(uint32_t v, uint32_t mask)
{
    cpsr_ = ((cpsr_ & ~mask) | (v & mask)) & PSR_IMPL;
    switch (cpsr_ & PSR_M) {
    case M_USR: case M_FIQ: case M_IRQ: case M_SVC: case M_ABT: case M_UND: break;
    default: undoc("illegal mode value written to CPSR (user bank used)"); break;
    }
}

uint32_t Arm7DI::cyc(uint32_t addr, uint8_t flags, uint32_t wdata, uint8_t next)
{
    BusCycle c{cycles, addr, (flags & BF_WRITE) ? wdata : 0, next_type_, flags};
    BusResult r{0, 0, false};
    bus_->cycle(c, r);
    switch (c.type) {
    case CYC_N: stats.n++; break;
    case CYC_S: stats.s++; break;
    case CYC_I: stats.i++; break;
    default: stats.c++; break;
    }
    stats.wait += r.wait;
    last_abort_ = r.abort && (c.type == CYC_N || c.type == CYC_S);
    if (bus_trace)
        fprintf(bus_trace, "%10llu %c %08x %c%c%c%c%c %08x w%u%s\n", (unsigned long long)cycles, "NSIC"[c.type], addr,
                (flags & BF_WRITE) ? 'W' : 'R', (flags & BF_BYTE) ? 'b' : 'w', (flags & BF_OPC) ? 'O' : '-',
                (flags & BF_LOCK) ? 'L' : '-', (flags & BF_USER) ? 'u' : 'p',
                (flags & BF_WRITE) ? wdata : r.rdata, r.wait, last_abort_ ? " ABORT" : "");
    cycles += 1 + r.wait;
    next_type_ = next;
    /* interrupt synchronisers advance with the (stretched) core clock */
    fiq_pipe_ = (fiq_pipe_ << 1) | (fiq_in_ ? 1 : 0);
    irq_pipe_ = (irq_pipe_ << 1) | (irq_in_ ? 1 : 0);
    return (flags & BF_WRITE) ? 0 : r.rdata;
}

void Arm7DI::cycle1_fetch(uint8_t next)
{
    f1_.word = cyc(pc_, BF_OPC | user_flag(), 0, next);
    f1_.abort = last_abort_;
    pc_ += 4;
}

bool Arm7DI::fiq_seen() const
{
    return irq_sync_stages == 0 ? fiq_in_ : ((fiq_pipe_ >> (irq_sync_stages - 1)) & 1);
}

bool Arm7DI::irq_seen() const
{
    return irq_sync_stages == 0 ? irq_in_ : ((irq_pipe_ >> (irq_sync_stages - 1)) & 1);
}

bool Arm7DI::cond_pass(uint32_t op) const
{
    const bool N = cpsr_ & PSR_N, Z = cpsr_ & PSR_Z, C = cpsr_ & PSR_C, V = cpsr_ & PSR_V;
    switch (op >> 28) {
    case 0x0: return Z;
    case 0x1: return !Z;
    case 0x2: return C;
    case 0x3: return !C;
    case 0x4: return N;
    case 0x5: return !N;
    case 0x6: return V;
    case 0x7: return !V;
    case 0x8: return C && !Z;
    case 0x9: return !C || Z;
    case 0xA: return N == V;
    case 0xB: return N != V;
    case 0xC: return !Z && N == V;
    case 0xD: return Z || N != V;
    case 0xE: return true;
    default: return false;   /* NV: never (4.2) */
    }
}

void Arm7DI::bus_cycle()
{
    if (seq_ == SQ_IDLE) begin();
    switch (seq_) {
    case SQ_RESET: c_reset(); break;
    case SQ_SKIP: c_skip(); break;
    case SQ_DP: c_dp(); break;
    case SQ_PSR: c_psr(); break;
    case SQ_MUL: c_mul(); break;
    case SQ_SWP: c_swp(); break;
    case SQ_SDT: c_sdt(); break;
    case SQ_BDT: c_bdt(); break;
    case SQ_BRANCH: c_branch(); break;
    case SQ_UNDEF: c_undef(); break;
    case SQ_EXC: c_exc(); break;
    default: c_refill(); break;
    }
}

void Arm7DI::begin()
{
    bus_->tick(cycles);
    k_ = 0;
    if (in_reset_) { seq_ = SQ_RESET; return; }
    if (insn_trace)
        fprintf(insn_trace, "%10llu %08x %08x %s cpsr=%08x\n", (unsigned long long)cycles, pc_ - 8, ex_.word,
                mode_name(cpsr_), cpsr_);
    if (reset_exit_) { reset_exit_ = false; start_exc(0x00, M_SVC, true); return; }
    /* exception priorities (3.4.7): reset > data abort > FIQ > IRQ > prefetch abort > undefined/SWI */
    if (dabort_pending_) { dabort_pending_ = false; start_exc(0x10, M_ABT, false); return; }
    if (fiq_seen() && !(cpsr_ & PSR_F)) { start_exc(0x1C, M_FIQ, true); return; }
    if (irq_seen() && !(cpsr_ & PSR_I)) { start_exc(0x18, M_IRQ, false); return; }
    if (ex_.abort) { start_exc(0x0C, M_ABT, false); return; }
    decode(ex_.word);
}

void Arm7DI::decode(uint32_t op)
{
    stats.instr++;
    x_.op = op;
    if (!cond_pass(op)) {   /* Table 29: one S cycle */
        stats.unexecuted++;
        seq_ = SQ_SKIP;
        return;
    }
    switch ((op >> 25) & 7) {
    case 0:
        if ((op & 0x90) == 0x90) {
            /* multiply / swap space (bit 7 and bit 4 set) */
            if ((op & 0x0FC000F0) == 0x00000090) { seq_ = SQ_MUL; return; }
            if ((op & 0x0FB00FF0) == 0x01000090) { seq_ = SQ_SWP; return; }
            /* Not a documented encoding and not the undefined trap (4.1 note: e.g. multiply with bit 6 set).
             * Hypothesis: bit 24 selects the swap datapath, else the multiply datapath; bits 6:5 ignored. */
            undoc("undocumented multiply/swap-space encoding");
            seq_ = (op & (1u << 24)) ? SQ_SWP : SQ_MUL;
            return;
        }
        seq_ = (op & 0x01900000) == 0x01000000 ? SQ_PSR : SQ_DP;   /* TST/TEQ/CMP/CMN with S = 0: MRS/MSR */
        return;
    case 1:
        seq_ = (op & 0x01900000) == 0x01000000 ? SQ_PSR : SQ_DP;
        return;
    case 2:
        seq_ = SQ_SDT;
        return;
    case 3:
        seq_ = (op & 0x10) ? SQ_UNDEF : SQ_SDT;   /* Figure 32 */
        return;
    case 4:
        seq_ = SQ_BDT;
        return;
    case 5:
        seq_ = SQ_BRANCH;
        return;
    case 6:          /* LDC/STC: no coprocessor on this bus, CPA stays high */
        seq_ = SQ_UNDEF;
        return;
    default:
        if (op & (1u << 24)) start_exc(0x08, M_SVC, false);   /* SWI (4.10, Table 22) */
        else seq_ = SQ_UNDEF;                                /* CDP / MRC / MCR, coprocessor absent */
        return;
    }
}

void Arm7DI::start_exc(uint32_t vector, uint32_t mode, bool set_f)
{
    seq_ = SQ_EXC;
    k_ = 0;
    x_.vector = vector;
    x_.mode = mode;
    x_.set_f = set_f;
}

void Arm7DI::start_refill(uint32_t target)
{
    seq_ = SQ_REFILL;
    k_ = 0;
    x_.target = target;
}

void Arm7DI::c_refill()
{
    const uint32_t t = x_.target;
    if (k_ == 0) {
        x_.fa.word = cyc(t, BF_OPC | user_flag(), 0, CYC_S);
        x_.fa.abort = last_abort_;
        k_ = 1;
        return;
    }
    Slot b;
    b.word = cyc(t + 4, BF_OPC | user_flag(), 0, CYC_S);
    b.abort = last_abort_;
    ex_ = x_.fa;
    de_ = b;
    pc_ = t + 8;
    done();
}

/* nRESET low: dummy fetches from incrementing addresses (3.5); the cycle type is not documented. */
void Arm7DI::c_reset()
{
    cyc(pc_, BF_OPC | user_flag(), 0, CYC_S);
    pc_ += 4;
    done();
}

void Arm7DI::c_skip()
{
    cycle1_fetch(CYC_S);
    advance();
    done();
}

/* Data processing (4.4), Table 15.  Immediate / immediate-shift forms: 1 cycle (+ refill for R15); register-specified
 * shift: cycle 1 latches Rs[7:0] into the shifter while fetching, cycle 2 is internal at pc+12 and reads Rn, Rm with
 * R15 = pc+12 (4.4.5). */
void Arm7DI::c_dp()
{
    const uint32_t op = x_.op;
    const bool imm = (op >> 25) & 1;
    const int opc = (op >> 21) & 15, rn = (op >> 16) & 15, rdn = (op >> 12) & 15, rm = op & 15;
    const bool test = (opc & 0xC) == 0x8;
    const bool regshift = !imm && (op & 0x10);
    const bool wpc = rdn == 15 && !test;
    uint32_t a, b;
    bool sc;
    if (k_ == 0) {
        x_.cin = cpsr_ & PSR_C;
        if (!regshift) {
            /* operands are read in cycle 1: R15 = pc+8 */
            a = rd(rn, pc_);
            if (imm) {
                unsigned rot = (op >> 7) & 0x1E;
                b = ror32(op & 0xFF, rot);
                sc = rot ? (b >> 31) : x_.cin;
            } else {
                shift_imm(rd(rm, pc_), (op >> 5) & 3, (op >> 7) & 31, x_.cin, b, sc);
            }
            cycle1_fetch(wpc ? CYC_N : CYC_S);
            dp_finish(a, b, sc);
            return;
        }
        const int rs = (op >> 8) & 15;
        if (rs == 15) undoc("register shift amount from R15");
        x_.amt = rd(rs, pc_) & 0xFF;
        cycle1_fetch(CYC_I);
        k_ = 1;
        return;
    }
    cyc(pc_, user_flag(), 0, wpc ? CYC_N : CYC_S);
    a = rd(rn, pc_);
    shift_reg(rd(rm, pc_), (op >> 5) & 3, x_.amt, x_.cin, b, sc);
    dp_finish(a, b, sc);
}

/* the ALU result and write-back, in the data operation's last cycle */
void Arm7DI::dp_finish(uint32_t a, uint32_t b, bool sc)
{
    const uint32_t op = x_.op;
    const bool S = (op >> 20) & 1;
    const int opc = (op >> 21) & 15, rdn = (op >> 12) & 15;
    const bool test = (opc & 0xC) == 0x8;
    const bool wpc = rdn == 15 && !test;
    const bool cin = x_.cin;
    uint32_t res;
    bool c = sc, v = cpsr_ & PSR_V;
    switch (opc) {
    case 0x0: case 0x8: res = a & b; break;
    case 0x1: case 0x9: res = a ^ b; break;
    case 0x2: case 0xA: res = add(a, ~b, 1, c, v); break;
    case 0x3: res = add(b, ~a, 1, c, v); break;
    case 0x4: case 0xB: res = add(a, b, 0, c, v); break;
    case 0x5: res = add(a, b, cin, c, v); break;
    case 0x6: res = add(a, ~b, cin, c, v); break;
    case 0x7: res = add(b, ~a, cin, c, v); break;
    case 0xC: res = a | b; break;
    case 0xD: res = b; break;
    case 0xE: res = a & ~b; break;
    default: res = ~b; break;
    }

    if (wpc) {
        /* 4.4.4: with S the SPSR of the current mode moves to the CPSR */
        if (S) {
            if (bank() == B_USR) undoc("data processing to R15 with S in user mode (CPSR kept)");
            else write_cpsr(spsr_[bank()], PSR_IMPL);
        }
        start_refill(res & ~3u);
        return;
    }
    if (!test) wr(rdn, res);
    if (S) {
        if (test && rdn == 15) {
            /* TSTP/TEQP/CMPP/CMNP in a 32-bit mode (4.4.6): SPSR -> CPSR if privileged, nothing in user mode */
            undoc("TEQP-style test with Rd = R15");
            if (bank() != B_USR && (cpsr_ & PSR_M) != M_USR) write_cpsr(spsr_[bank()], PSR_IMPL);
        } else {
            cpsr_ = (cpsr_ & 0x0FFFFFFFu) | (res & PSR_N) | (res == 0 ? PSR_Z : 0) | (c ? PSR_C : 0) |
                    (v ? PSR_V : 0);
        }
    }
    advance();
    done();
}

/* MRS / MSR (4.5): same timing as a data operation, 1S. */
void Arm7DI::c_psr()
{
    const uint32_t op = x_.op;
    const bool P = (op >> 22) & 1;
    if (!(op & (1u << 21))) {   /* MRS */
        const int rdn = (op >> 12) & 15;
        const uint32_t v = P ? spsr_cur() : cpsr_;
        if (rdn == 15) {
            undoc("MRS to R15 (treated as a PC write)");
            cycle1_fetch(CYC_N);
            start_refill(v & ~3u);
            return;
        }
        cycle1_fetch(CYC_S);
        wr(rdn, v);
        advance();
        done();
        return;
    }
    /* MSR.  Field decode (hypothesis): bit 19 writes the flags, bit 16 the control bits -- this covers both
     * documented forms (CPSR_all: 1001, CPSR_flg: 1000). */
    uint32_t src;
    if (op & (1u << 25)) {
        src = ror32(op & 0xFF, (op >> 7) & 0x1E);
    } else {
        const int rm = op & 15;
        if (rm == 15) undoc("MSR from R15");
        src = rd(rm, pc_);
    }
    uint32_t mask = 0;
    if (op & (1u << 19)) mask |= 0xF0000000u;
    if (op & (1u << 16)) mask |= 0x000000DFu;
    cycle1_fetch(CYC_S);
    if (P) {
        set_spsr_cur(src, mask);
    } else {
        if ((cpsr_ & PSR_M) == M_USR) mask &= 0xF0000000u;   /* 4.5.1: control bits protected in user mode */
        write_cpsr(src, mask);
    }
    advance();
    done();
}

/* MUL / MLA (4.6, 9.3, Table 16): 2-bit Booth with early termination.  Cycle 1 (fetch) initialises Rd with Rn or 0
 * and loads Rs into the Booth shifter; then one internal cycle per Booth step, each adding, subtracting or passing
 * Rm shifted by 2k (or 2k+1).  Rm is re-read every step, so Rd == Rm gives the documented zero (MUL) / garbage
 * (MLA).  Steps m: the smallest m >= 1 with Rs >> (2m-1) == 0, capped at 16 (4.6.3). */
void Arm7DI::c_mul()
{
    const uint32_t op = x_.op;
    const int rdn = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
    const bool A = (op >> 21) & 1, S = (op >> 20) & 1;
    if (k_ == 0) {
        if (rdn == 15 || rm == 15 || rs == 15 || (A && rn == 15)) undoc("MUL/MLA with R15 operand");
        if (rdn == rm) undoc("MUL/MLA with Rd == Rm");
        x_.mul = rd(rs, pc_);
        x_.acc = A ? rd(rn, pc_) : 0;
        int m = 1;
        while (m < 16 && (x_.mul >> (2 * m - 1)) != 0) m++;
        x_.m = m;
        cycle1_fetch(CYC_I);
        if (rdn != 15) gpr_[phys(rdn, bank())] = x_.acc;
        x_.borrow = 0;
        x_.alu_c = x_.sh_c = false;
        k_ = 1;
        return;
    }
    static const int digit[8] = {0, 1, 1, 2, -2, -1, -1, 0};   /* index = {b(2k+1), b(2k), borrow} */
    const int k = k_ - 1;
    const uint32_t M = rd(rm, pc_);
    const int d = digit[((x_.mul & 3) << 1) | x_.borrow];
    x_.borrow = (x_.mul >> 1) & 1;
    x_.mul >>= 2;
    const bool last = k == x_.m - 1;
    cyc(pc_, user_flag(), 0, last ? CYC_S : CYC_I);
    const int sh = 2 * k + ((d == 2 || d == -2) ? 1 : 0);
    x_.sh_c = sh ? ((M >> (32 - sh)) & 1) : ((cpsr_ & PSR_C) ? 1 : 0);   /* LSL #0 passes C through */
    if (rdn != 15) x_.acc = gpr_[phys(rdn, bank())];
    bool v;
    if (d > 0) x_.acc = add(x_.acc, M << sh, 0, x_.alu_c, v);
    else if (d < 0) x_.acc = add(x_.acc, ~(M << sh), 1, x_.alu_c, v);
    else x_.alu_c = false;
    if (rdn != 15) gpr_[phys(rdn, bank())] = x_.acc;
    if (!last) { k_++; return; }
    if (S) {
        bool c = cpsr_ & PSR_C;
        if (mul_carry == MULC_ALU_LAST) c = x_.alu_c;
        else if (mul_carry == MULC_SHIFTER_LAST) c = x_.sh_c;
        cpsr_ = (cpsr_ & 0x1FFFFFFFu) | (x_.acc & PSR_N) | (x_.acc == 0 ? PSR_Z : 0) | (c ? PSR_C : 0);
    }
    advance();
    done();
}

/* SWP / SWPB (4.9, Table 21): read (N, LOCK), write (N, LOCK), internal cycle writes Rd. */
void Arm7DI::c_swp()
{
    const uint32_t op = x_.op;
    const bool B = (op >> 22) & 1;
    const int rn = (op >> 16) & 15, rdn = (op >> 12) & 15, rm = op & 15;
    const uint8_t bf = (B ? BF_BYTE : 0) | BF_LOCK | user_flag();
    switch (k_++) {
    case 0:
        if (rn == 15 || rdn == 15 || rm == 15) undoc("SWP with R15 operand");
        x_.addr = rd(rn, pc_);
        cycle1_fetch(CYC_N);
        return;
    case 1:
        x_.data = cyc(x_.addr, bf, 0, CYC_N);
        x_.ab = last_abort_;
        return;
    case 2: {
        const uint32_t v = rd(rm, pc_);   /* Rm read after the load: Rd == Rm swaps correctly */
        cyc(x_.addr, bf | BF_WRITE, B ? (v & 0xFF) * 0x01010101u : v, CYC_I);
        x_.ab |= last_abort_;
        return;
    }
    default:
        cyc(pc_, user_flag(), 0, CYC_S);
        if (x_.ab) dabort_pending_ = true;
        else wr(rdn, B ? (x_.data >> (8 * (x_.addr & 3))) & 0xFF : ror32(x_.data, 8 * (x_.addr & 3)));
        advance();
        done();
        return;
    }
}

/* LDR / STR (4.7, Tables 17 and 18). */
void Arm7DI::c_sdt()
{
    const uint32_t op = x_.op;
    const bool B = (op >> 22) & 1, L = (op >> 20) & 1;
    const int rn = (op >> 16) & 15, rdn = (op >> 12) & 15;
    if (k_ == 0) {
        const bool I = (op >> 25) & 1, P = (op >> 24) & 1, U = (op >> 23) & 1, W = (op >> 21) & 1;
        const uint32_t base = rd(rn, pc_);
        uint32_t off;
        if (I) {
            const int rm = op & 15;
            if (rm == 15) undoc("LDR/STR offset register R15");
            bool sc;
            shift_imm(rd(rm, pc_), (op >> 5) & 3, (op >> 7) & 31, cpsr_ & PSR_C, off, sc);
        } else {
            off = op & 0xFFF;
        }
        x_.calc = U ? base + off : base - off;
        x_.addr = P ? x_.calc : base;
        x_.wb = !P || W;
        if (x_.wb && rn == 15) undoc("LDR/STR write-back to R15 (ignored)");
        x_.bf = (B ? BF_BYTE : 0) | ((!P && W) ? (uint8_t)BF_USER : user_flag());   /* LDRT/STRT force nTRANS */
        cycle1_fetch(CYC_N);
        k_ = 1;
        return;
    }
    if (!L) {
        const uint32_t v = rd(rdn, pc_);   /* read in cycle 2: R15 = pc+12 (4.7.4), base not yet written back */
        cyc(x_.addr, x_.bf | BF_WRITE, B ? (v & 0xFF) * 0x01010101u : v, CYC_N);
        if (last_abort_) dabort_pending_ = true;
        if (x_.wb && rn != 15) wr(rn, x_.calc);
        advance();
        done();
        return;
    }
    if (k_ == 1) {
        x_.data = cyc(x_.addr, x_.bf, 0, CYC_I);
        x_.ab = last_abort_;
        if (x_.wb && rn != 15) wr(rn, x_.calc);   /* base write-back in cycle 2 */
        k_ = 2;
        return;
    }
    const bool wpc = rdn == 15 && !x_.ab;
    cyc(pc_, user_flag(), 0, wpc ? CYC_N : CYC_S);   /* cycle 3: internal, Rd written */
    if (x_.ab) { dabort_pending_ = true; advance(); done(); return; }
    const uint32_t v = B ? (x_.data >> (8 * (x_.addr & 3))) & 0xFF : ror32(x_.data, 8 * (x_.addr & 3));
    if (rdn == 15) { start_refill(v & ~3u); return; }
    wr(rdn, v);
    advance();
    done();
}

/* LDM / STM (4.8, Tables 19 and 20).  The base is written back at the end of cycle 2 (4.8.6).  x_.r = the register
 * of the next transfer, 16 = the LDM's final internal cycle. */
void Arm7DI::c_bdt()
{
    const uint32_t op = x_.op;
    const bool S = (op >> 22) & 1, W = (op >> 21) & 1, L = (op >> 20) & 1;
    const int rn = (op >> 16) & 15;
    if (k_ == 0) {
        const bool P = (op >> 24) & 1, U = (op >> 23) & 1;
        uint32_t list = op & 0xFFFF;
        if (rn == 15) undoc("LDM/STM with base R15");
        x_.base = rd(rn, pc_);
        uint32_t bytes = 4 * __builtin_popcount(list);
        if (list == 0) {
            /* hypothesis (ARM7TDMI behaviour): R15 transferred, base moves by 16 words */
            undoc("LDM/STM with empty register list");
            list = 0x8000;
            bytes = 0x40;
        }
        const uint32_t base = x_.base;
        x_.list = list;
        x_.addr = U ? (P ? base + 4 : base) : (P ? base - bytes : base - bytes + 4);
        x_.wbv = U ? base + bytes : base - bytes;
        const bool pc_in = list & 0x8000;
        x_.userbank = S && !(L && pc_in);   /* 4.8.4 */
        if (S && W && x_.userbank) undoc("LDM/STM user-bank transfer with write-back");
        cycle1_fetch(CYC_N);
        x_.first = true;
        x_.ab = false;
        x_.newpc = 0;
        x_.pc_loaded = false;
        x_.r = __builtin_ctz(list);
        k_ = 1;
        return;
    }
    const uint32_t list = x_.list;
    if (x_.r == 16) {   /* LDM: final internal cycle */
        cyc(pc_, user_flag(), 0, x_.pc_loaded ? CYC_N : CYC_S);
        if (x_.ab) {
            wr(rn, W ? x_.wbv : x_.base);   /* base restored (to the written-back value if W) */
            dabort_pending_ = true;
            advance();
            done();
            return;
        }
        if (x_.pc_loaded) {
            if (S) {
                if (bank() == B_USR) undoc("LDM with R15 and S in user mode (CPSR kept)");
                else write_cpsr(spsr_[bank()], PSR_IMPL);
            }
            start_refill(x_.newpc & ~3u);
            return;
        }
        advance();
        done();
        return;
    }
    const int r = x_.r;
    const bool last = (list >> (r + 1)) == 0;
    if (L) {
        const uint32_t data = cyc(x_.addr, user_flag(), 0, last ? CYC_I : CYC_S);
        if (last_abort_) x_.ab = true;
        if (x_.first && W) wr(rn, x_.wbv);
        x_.first = false;
        if (!x_.ab) {   /* 4.8.7: register overwriting stops at the abort */
            if (r == 15) { x_.newpc = data; x_.pc_loaded = true; }
            else if (x_.userbank) wr_bank(r, B_USR, data);
            else wr(r, data);
        }
    } else {
        const uint32_t v = r == 15 ? pc_ : (x_.userbank ? gpr_[phys(r, B_USR)] : rd(r, pc_));   /* R15 = pc+12 */
        cyc(x_.addr, BF_WRITE | user_flag(), v, last ? CYC_N : CYC_S);
        if (last_abort_) x_.ab = true;
        if (x_.first && W) wr(rn, x_.wbv);
        x_.first = false;
    }
    x_.addr += 4;
    k_++;
    if (!last) { x_.r = r + 1 + __builtin_ctz(list >> (r + 1)); return; }
    if (L) { x_.r = 16; return; }
    if (x_.ab) dabort_pending_ = true;
    advance();
    done();
}

/* B / BL (4.3, Table 14): R14 = address of the following instruction. */
void Arm7DI::c_branch()
{
    const uint32_t op = x_.op;
    const uint32_t target = pc_ + ((uint32_t)((int32_t)(op << 8) >> 6));
    const uint32_t ret = pc_ - 4;
    cycle1_fetch(CYC_N);
    if (op & (1u << 24)) wr(14, ret);
    start_refill(target);
}

/* Undefined instruction / coprocessor absent (Table 28): fetch pc+8 with nCPI low, one internal cycle with the
 * address held at pc+8, then the entry fetches. */
void Arm7DI::c_undef()
{
    if (k_ == 0) {
        x_.addr = pc_;
        cyc(x_.addr, BF_OPC | user_flag(), 0, CYC_I);
        k_ = 1;
        return;
    }
    const uint32_t pc8 = x_.addr;
    cyc(pc8, BF_OPC | user_flag(), 0, CYC_N);
    spsr_[B_UND] = cpsr_;
    cpsr_ = (cpsr_ & ~PSR_M) | M_UND | PSR_I;
    gpr_[phys(14, B_UND)] = pc8 - 4;
    stats.exceptions++;
    start_refill(0x04);
}

/* Exception entry / SWI (9.9, Table 22).  pc_ = (address of the instruction in the execute stage) + 8, so R14 =
 * that address + 4: SWI/undef -> next instruction, IRQ/FIQ -> next instruction + 4, prefetch abort -> aborted + 4,
 * data abort -> aborted + 8 (the execute stage then holds the instruction after the aborted one). */
void Arm7DI::c_exc()
{
    const uint32_t pc8 = pc_;
    cyc(pc8, BF_OPC | user_flag(), 0, CYC_N);   /* fetched word discarded */
    const int nb = bank_of(x_.mode);
    spsr_[nb] = cpsr_;
    cpsr_ = (cpsr_ & ~PSR_M) | x_.mode | PSR_I | (x_.set_f ? PSR_F : 0);
    gpr_[phys(14, nb)] = pc8 - 4;
    stats.exceptions++;
    start_refill(x_.vector);
}

} // namespace wren7
