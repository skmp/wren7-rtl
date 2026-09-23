/* arm7di.h -- wren7 ARM7DI core model (the Dreamcast AICA sound CPU).
 *
 * Bus-cycle accurate: every instruction (and every exception entry) is executed as the exact sequence of memory
 * cycles given in the data sheet (docs/DDI0027D_7di_ds.pdf, chapter 9 "Instruction Cycle Operations").  Each MCLK
 * cycle is presented to an Arm7Bus with the signals the real core drives for it (address, N/S/I/C type from the
 * previous cycle's nMREQ/SEQ, nRW, nBW, nOPC, LOCK, nTRANS, write data); the memory system answers with the read
 * data, the number of nWAIT stretch cycles and ABORT.  Timing therefore comes only from the cycle sequence plus
 * the memory system's wait states -- there is no per-instruction latency table.
 *
 * Configuration modelled: PROG32 = DATA32 = 1 (32-bit modes), BIGEND = 0, no coprocessors (CPA/CPB high: every
 * coprocessor instruction takes the undefined trap), debug/ICEbreaker absent (DBGEN low).
 *
 * Where the data sheet says "shall not" / "meaningless" the model picks one behaviour and counts the event in
 * Arm7DI::stats.undoc; every such choice is listed in model/NOTES.md as an open hardware question.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>

namespace wren7 {

/* Cycle type of a bus cycle, encoded as {nMREQ, SEQ} (data sheet Table 6). */
enum CycleType : uint8_t { CYC_N = 0, CYC_S = 1, CYC_I = 2, CYC_C = 3 };

enum BusFlags : uint8_t {
    BF_WRITE = 1,   /* nRW high */
    BF_BYTE  = 2,   /* nBW low */
    BF_OPC   = 4,   /* nOPC low: instruction fetch */
    BF_LOCK  = 8,   /* LOCK high (SWP) */
    BF_USER  = 16,  /* nTRANS low: user mode, or the data cycle of LDRT/STRT */
};

struct BusCycle {
    uint64_t t;       /* MCLK count at the start of this cycle */
    uint32_t addr;    /* A[31:0] */
    uint32_t wdata;   /* D[31:0] driven on writes (STRB/SWPB replicate the byte over all four lanes) */
    uint8_t type;     /* CycleType, as announced by nMREQ/SEQ during the previous cycle */
    uint8_t flags;    /* BusFlags */
};

struct BusResult {
    uint32_t rdata;   /* D[31:0] sampled on reads: the whole word containing addr (the core selects bytes and
                       * rotates misaligned words itself, sections 4.7.3 / 5.2) */
    uint32_t wait;    /* extra MCLK cycles with nWAIT low */
    bool abort;       /* ABORT high for this cycle */
};

struct Arm7Bus {
    virtual void cycle(const BusCycle &c, BusResult &r) = 0;
    virtual ~Arm7Bus() {}
};

/* Processor modes (Table 2) */
enum Mode : uint32_t { M_USR = 0x10, M_FIQ = 0x11, M_IRQ = 0x12, M_SVC = 0x13, M_ABT = 0x17, M_UND = 0x1B };

/* PSR bits.  Only N Z C V I F M[4:0] exist (section 4.5.2); reserved bits are not stored and read as 0
 * (unverified, NOTES.md). */
constexpr uint32_t PSR_N = 1u << 31, PSR_Z = 1u << 30, PSR_C = 1u << 29, PSR_V = 1u << 28;
constexpr uint32_t PSR_I = 1u << 7, PSR_F = 1u << 6, PSR_M = 0x1F;
constexpr uint32_t PSR_IMPL = 0xF00000DFu;

/* Register banks: physical register file of 31 GPRs (section 3.3) + PC. */
enum Bank { B_USR = 0, B_FIQ, B_IRQ, B_SVC, B_ABT, B_UND, B_COUNT };

class Arm7DI {
public:
    explicit Arm7DI(Arm7Bus *bus);

    /* Power-on state (register contents are not defined by the data sheet; the model zeroes them) followed by a
     * reset release: SVC mode, I = F = 1, execution from address 0. */
    void power_on();

    /* nRESET: while asserted, step() performs dummy fetches from incrementing addresses (section 3.5); the release
     * runs the reset exception entry.  Takes effect at the next instruction boundary. */
    void set_reset(bool asserted);
    /* nFIQ / nIRQ, level sensitive (true = request asserted, i.e. pin LOW).  Sampled once per MCLK cycle through a
     * synchroniser of irq_sync_stages cycles (0 = ISYNC high), checked at the end of each instruction. */
    void set_fiq(bool asserted) { fiq_in_ = asserted; }
    void set_irq(bool asserted) { irq_in_ = asserted; }
    int irq_sync_stages = 0;

    /* Execute one instruction, one exception entry, or (in reset) one dummy fetch. */
    void step();

    /* ---- state access (harness / tests) ---- */
    uint32_t reg(int r) const;               /* R0..R14 of the current mode; R15 reads as executing address + 8 */
    void set_reg(int r, uint32_t v);         /* R0..R14 of the current mode */
    uint32_t reg_bank(int r, int bank) const { return gpr_[phys(r, bank)]; }
    uint32_t cpsr() const { return cpsr_; }
    uint32_t spsr(int bank) const { return spsr_[bank]; }
    void set_cpsr(uint32_t v) { cpsr_ = v & PSR_IMPL; }
    uint32_t exec_addr() const { return pc_ - 8; }   /* address of the instruction about to execute */
    uint32_t exec_word() const { return ex_.word; }  /* its instruction word */
    bool in_reset() const { return in_reset_; }

    uint64_t cycles = 0;   /* MCLK cycles, including nWAIT stretch */

    struct Stats {
        uint64_t instr = 0, unexecuted = 0, exceptions = 0, undoc = 0;
        uint64_t n = 0, s = 0, i = 0, c = 0, wait = 0;
    } stats;

    /* Optional traces: one line per bus cycle / per instruction or exception entry. */
    FILE *bus_trace = nullptr;
    FILE *insn_trace = nullptr;

    /* MUL/MLA with S: the data sheet leaves C "meaningless" (4.6.2).  Measured (tests/hw/mulsem, 1024/1024): C is the
     * barrel shifter's carry out of the last Booth step (Rm << 2k or 2k+1; LSL #0 passes the old C).  The other rules
     * stay selectable for comparisons. */
    enum MulCarry { MULC_KEEP = 0, MULC_ALU_LAST, MULC_SHIFTER_LAST };
    MulCarry mul_carry = MULC_SHIFTER_LAST;

private:
    struct Slot { uint32_t word; bool abort; };

    Arm7Bus *bus_;
    uint32_t gpr_[31];
    uint32_t pc_;              /* R15 = address register/incrementer: next fetch address = executing address + 8 */
    uint32_t cpsr_;
    uint32_t spsr_[B_COUNT];   /* [B_USR] unused */
    Slot ex_, de_, f1_;        /* execute stage, decode stage, the word fetched in the current cycle 1 */
    uint8_t next_type_;        /* type of the next bus cycle (nMREQ/SEQ output of the current one) */
    bool last_abort_;
    bool dabort_pending_;
    bool in_reset_, reset_exit_;
    bool fiq_in_, irq_in_;
    uint32_t fiq_pipe_, irq_pipe_;

    static int bank_of(uint32_t mode);
    static int phys(int r, int bank);
    int bank() const { return bank_of(cpsr_); }
    uint8_t user_flag() const { return (cpsr_ & PSR_M) == M_USR ? BF_USER : 0; }

    uint32_t rd(int r, uint32_t r15) const { return r == 15 ? r15 : gpr_[phys(r, bank())]; }
    void wr(int r, uint32_t v);
    void wr_bank(int r, int bank, uint32_t v);
    uint32_t spsr_cur() const;
    void set_spsr_cur(uint32_t v, uint32_t mask);
    void write_cpsr(uint32_t v, uint32_t mask);
    void undoc(const char *what);

    /* one MCLK cycle: type = next_type_, then next_type_ = next */
    uint32_t cyc(uint32_t addr, uint8_t flags, uint32_t wdata, uint8_t next);
    void cycle1_fetch(uint8_t next);   /* fetch pc+8 into f1_, pc_ += 4 */
    void advance() { ex_ = de_; de_ = f1_; }
    void refill(uint32_t target);      /* fetch target (N), target+4 (S): pipeline = [target, target+4] */
    bool cond_pass(uint32_t op) const;
    bool fiq_seen() const;
    bool irq_seen() const;

    void exec(uint32_t op);
    void exec_dp(uint32_t op);
    void exec_psr(uint32_t op);
    void exec_mul(uint32_t op);
    void exec_swp(uint32_t op);
    void exec_sdt(uint32_t op);
    void exec_bdt(uint32_t op);
    void exec_branch(uint32_t op);
    void exec_undef();
    void exception(uint32_t vector, uint32_t mode, bool set_f);  /* Table 22 entry sequence */
    void reset_step();
};

const char *mode_name(uint32_t mode);

} // namespace wren7
