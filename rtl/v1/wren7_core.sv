//
// wren7_core -- wren7 v1: the ARM7DI, the Dreamcast AICA's sound CPU.  Cycle-exact to the C++ model
// (model/src/arm7di.cpp, Arm7DI::bus_cycle): every bus cycle has the model's type, address, flags and write data,
// and the architectural state matches at every instruction boundary (tb/core_tb.cpp checks this in lockstep).
//
// Configuration (as the model): 32-bit modes, little-endian, no coprocessor (every coprocessor instruction takes
// the undefined trap), no debug, ISYNC high (nFIQ / nIRQ used as they are at the instruction boundary), the
// measured MUL carry (the shifter's carry out of the last Booth step).  The data sheet's "shall not" cases do what
// the model does (model/NOTES.md, "Open hardware questions").
//
// ---- Bus ------------------------------------------------------------------------------------------------------
// One bus cycle is in progress in every clock with ce = 1.  Its outputs come from registers only (no input reaches
// an output), so a bus may answer combinationally:
//   bus_type    this cycle's type, announced by the previous cycle (data sheet Table 6): 0 N, 1 S, 2 I (C unused)
//   bus_req     N or S: a memory cycle, held with bus_addr / bus_we / bus_byte / bus_lock / bus_wdata until bus_ack
//   bus_opc     instruction fetch (nOPC low); bus_user: nTRANS low (user mode, or the data cycle of LDRT / STRT)
//   bus_ack     the memory cycle ends in this clock; bus_rdata (the whole word) and bus_abort are taken with it.
//               Every clock before it is an nWAIT stretch.  I cycles last one clock and need no ack.
// The next cycle starts in the clock after the one that ended this one.  ce = 0 clocks do not exist for the core:
// nothing changes and bus_ack is ignored.  (wren7_aica maps this bus onto caique's ARM port.)
//
// ---- Inputs at instruction boundaries ---------------------------------------------------------------------------
// fiq / irq (asserted = 1, i.e. the pin low) and hold (nRESET asserted: dummy fetches; its release runs the reset
// entry) are taken in the first clock of the boundary cycle, the cycle that begins the next instruction or
// exception entry, as the model takes them in begin().  rst is the power-on reset (the model's power_on, except
// that the register file keeps its contents): after it the core performs the reset entry from address 0.
//
// ---- Structure --------------------------------------------------------------------------------------------------
// The sequencer state `st` is the model's (seq_, k_): S_BOUND is the first cycle of every instruction, exception
// entry or dummy fetch -- always a fetch from pc (= executing address + 8), so its request needs no decode; what
// happens at its end is decided during it.  The in-flight latches are shared between the instruction classes that
// never need them together (x_addr, x_data, x_calc, x_base).  One barrel shifter, one ALU adder, a branch / LDM
// address adder, and incrementers for pc and the transfer address.  The register file (wren7_regfile) has two
// read ports and one write port; an LDM writes each loaded register one cycle late (pw_*), in the next transfer
// or the final internal cycle, so its base write-back and the first loaded register never share a write.
//
module wren7_core (
    input  logic        clk,
    input  logic        rst,
    input  logic        ce,
    input  logic        hold,
    input  logic        fiq,
    input  logic        irq,

    output logic [1:0]  bus_type,
    output logic        bus_req,
    output logic [31:0] bus_addr,
    output logic        bus_we,
    output logic        bus_byte,
    output logic        bus_opc,
    output logic        bus_lock,
    output logic        bus_user,
    output logic [31:0] bus_wdata,
    input  logic        bus_ack,
    input  logic [31:0] bus_rdata,
    input  logic        bus_abort,

    // state at the boundary (testbench; unconnected in synthesis)
    output logic        dbg_boundary,
    output logic [31:0] dbg_pc,
    output logic [31:0] dbg_cpsr,
    output logic [31:0] dbg_ex
);
    localparam logic [1:0] CYC_N = 2'd0, CYC_S = 2'd1, CYC_I = 2'd2;

    localparam logic [4:0] M_USR = 5'h10, M_FIQ = 5'h11, M_IRQ = 5'h12, M_SVC = 5'h13, M_ABT = 5'h17, M_UND = 5'h1B;
    localparam logic [2:0] B_USR = 3'd0, B_FIQ = 3'd1, B_IRQ = 3'd2, B_SVC = 3'd3, B_ABT = 3'd4, B_UND = 3'd5;

    localparam logic [3:0] S_BOUND = 4'd0,    // first cycle of anything (fetch at pc)
                           S_DP2   = 4'd1,    // data operation, register-specified shift: internal cycle
                           S_MUL   = 4'd2,    // MUL / MLA Booth step mk (internal)
                           S_SWP1  = 4'd3,    // SWP read
                           S_SWP2  = 4'd4,    // SWP write
                           S_SWP3  = 4'd5,    // SWP internal (Rd written)
                           S_SDT2  = 4'd6,    // LDR / STR data cycle
                           S_SDT3  = 4'd7,    // LDR internal (Rd written)
                           S_BDTX  = 4'd8,    // LDM / STM transfer
                           S_BDTF  = 4'd9,    // LDM final internal cycle
                           S_UND2  = 4'd10,   // undefined trap: internal cycle at pc
                           S_REF0  = 4'd11,   // pipeline refill: fetch target
                           S_REF1  = 4'd12;   //                  fetch target + 4

    // ---------------------------------------------------------------------------------------------------------
    // state
    // ---------------------------------------------------------------------------------------------------------
    logic [3:0]  st;
    logic [3:0]  mk;                // MUL step
    logic [1:0]  ctype;             // this cycle's type
    logic        fc;                // no ce clock of this cycle has passed yet
    logic        hold_l, fiq_l, irq_l;
    logic [31:0] pc;                // R15: executing address + 8
    logic        f_n, f_z, f_c, f_v, f_i, f_f;
    logic [4:0]  f_m;
    logic [10:0] spsr_fiq /*verilator public_flat_rd*/, spsr_irq /*verilator public_flat_rd*/,
                 spsr_svc /*verilator public_flat_rd*/, spsr_abt /*verilator public_flat_rd*/,
                 spsr_und /*verilator public_flat_rd*/;   // {N Z C V I F M[4:0]}
    logic [31:0] ex_w, de_w, f1_w;  // execute / decode stage, the word fetched by the current instruction's cycle 1
    logic        ex_a, de_a, f1_a;  // ... and their prefetch aborts
    logic        dab;               // data abort pending
    logic        in_rst, rst_exit;  // in reset (dummy fetches) / reset entry pending
    logic [31:0] x_addr;            // data address (SWP, LDR / STR, LDM / STM running), refill target
    logic [31:0] x_data;            // loaded word (LDR, SWP), LDM: pending register / new pc, refill: first word
    logic [31:0] x_calc;            // LDR / STR write-back value, LDM / STM write-back value, MUL accumulator
    logic [31:0] x_base;            // LDM base (abort restore), MUL multiplier shift register
    logic        x_fab;             // refill: abort of the first word
    logic [7:0]  x_amt;             // register-specified shift amount
    logic        x_cin;             // C at the data operation's cycle 1
    logic        x_bbyte, x_buser;  // LDR / STR data cycle: byte, nTRANS
    logic        x_wb, x_ab, x_first, x_pcl, x_ub, x_borrow;
    logic [15:0] x_list;            // LDM / STM: registers still to transfer
    logic        pw_v;              // LDM: loaded register waiting for the write port
    logic [4:0]  pw_idx;

    // ---------------------------------------------------------------------------------------------------------
    // helpers
    // ---------------------------------------------------------------------------------------------------------
    function automatic logic [2:0] bank_of(input logic [4:0] m);
        case (m)
            M_FIQ:   bank_of = B_FIQ;
            M_IRQ:   bank_of = B_IRQ;
            M_SVC:   bank_of = B_SVC;
            M_ABT:   bank_of = B_ABT;
            M_UND:   bank_of = B_UND;
            default: bank_of = B_USR;   // user, and the illegal mode values
        endcase
    endfunction

    // physical register (R15 is never stored: reads of it take pc, writes of it are dropped)
    function automatic logic [4:0] phys(input logic [3:0] r, input logic [2:0] b);
        if (r < 4'd8)                     phys = {1'b0, r};
        else if (b == B_FIQ)              phys = {1'b0, r} + 5'd7;
        else if (r < 4'd13 || b == B_USR) phys = {1'b0, r};
        else                              phys = 5'd22 + {1'b0, b - 3'd2, 1'b0} + {4'd0, r[0] ^ 1'b1};
    endfunction

    function automatic logic [31:0] psr32(input logic [10:0] p);
        psr32 = {p[10:7], 20'd0, p[6:5], 1'b0, p[4:0]};
    endfunction

    function automatic logic [3:0] ctz16(input logic [15:0] x);
        casez (x)
            16'b???????????????1: ctz16 = 4'd0;
            16'b??????????????10: ctz16 = 4'd1;
            16'b?????????????100: ctz16 = 4'd2;
            16'b????????????1000: ctz16 = 4'd3;
            16'b???????????10000: ctz16 = 4'd4;
            16'b??????????100000: ctz16 = 4'd5;
            16'b?????????1000000: ctz16 = 4'd6;
            16'b????????10000000: ctz16 = 4'd7;
            16'b???????100000000: ctz16 = 4'd8;
            16'b??????1000000000: ctz16 = 4'd9;
            16'b?????10000000000: ctz16 = 4'd10;
            16'b????100000000000: ctz16 = 4'd11;
            16'b???1000000000000: ctz16 = 4'd12;
            16'b??10000000000000: ctz16 = 4'd13;
            16'b?100000000000000: ctz16 = 4'd14;
            default:              ctz16 = 4'd15;
        endcase
    endfunction

    function automatic logic [4:0] pop16(input logic [15:0] x);
        pop16 = 5'd0;
        for (int i = 0; i < 16; i++) pop16 = pop16 + {4'd0, x[i]};
    endfunction

    // a loaded word as LDR / SWP deliver it: byte lane, or the word rotated by the address (4.7.3)
    function automatic logic [31:0] load_val(input logic [31:0] d, input logic [1:0] a, input logic b);
        logic [31:0] r;
        case (a)
            2'd0: r = d;
            2'd1: r = {d[7:0],  d[31:8]};
            2'd2: r = {d[15:0], d[31:16]};
            default: r = {d[23:0], d[31:24]};
        endcase
        load_val = b ? {24'd0, r[7:0]} : r;
    endfunction

    wire [2:0]  bank   = bank_of(f_m);
    wire        umode  = f_m == M_USR;
    wire [10:0] cpsr11 = {f_n, f_z, f_c, f_v, f_i, f_f, f_m};
    wire [31:0] cpsr32 = psr32(cpsr11);

    logic [10:0] spsr_cur;   // SPSR of the current mode (user bank: the CPSR, as the model)
    always_comb begin
        case (bank)
            B_FIQ:   spsr_cur = spsr_fiq;
            B_IRQ:   spsr_cur = spsr_irq;
            B_SVC:   spsr_cur = spsr_svc;
            B_ABT:   spsr_cur = spsr_abt;
            B_UND:   spsr_cur = spsr_und;
            default: spsr_cur = cpsr11;
        endcase
    end

    // ---------------------------------------------------------------------------------------------------------
    // decode of the execute stage
    // ---------------------------------------------------------------------------------------------------------
    wire [2:0] c3      = ex_w[27:25];
    wire       space   = c3 == 3'd0 && ex_w[7] && ex_w[4];                        // multiply / swap space
    wire       mul_doc = (ex_w & 32'h0FC000F0) == 32'h00000090;
    wire       swp_doc = (ex_w & 32'h0FB00FF0) == 32'h01000090;
    wire       is_mul  = space && (mul_doc || (!swp_doc && !ex_w[24]));        // undocumented: bit 24 picks
    wire       is_swp  = space && !is_mul;
    wire       dpsp    = (c3 == 3'd0 && !space) || c3 == 3'd1;
    wire       is_psr  = dpsp && (ex_w & 32'h01900000) == 32'h01000000;      // TST..CMN with S = 0
    wire       is_dp   = dpsp && !is_psr;
    wire       dp_rs   = is_dp && !ex_w[25] && ex_w[4];                          // register-specified shift
    wire       is_sdt  = c3 == 3'd2 || (c3 == 3'd3 && !ex_w[4]);
    wire       is_bdt  = c3 == 3'd4;
    wire       is_br   = c3 == 3'd5;
    wire       is_swi  = c3 == 3'd7 && ex_w[24];
    // everything else (c3 = 3 with bit 4, LDC/STC, CDP/MRC/MCR) is the undefined trap

    logic cond;
    always_comb begin
        case (ex_w[31:28])
            4'h0: cond = f_z;
            4'h1: cond = !f_z;
            4'h2: cond = f_c;
            4'h3: cond = !f_c;
            4'h4: cond = f_n;
            4'h5: cond = !f_n;
            4'h6: cond = f_v;
            4'h7: cond = !f_v;
            4'h8: cond = f_c && !f_z;
            4'h9: cond = !f_c || f_z;
            4'hA: cond = f_n == f_v;
            4'hB: cond = f_n != f_v;
            4'hC: cond = !f_z && f_n == f_v;
            4'hD: cond = f_z || f_n != f_v;
            4'hE: cond = 1'b1;
            default: cond = 1'b0;   // NV
        endcase
    end

    wire [3:0] f_rn = ex_w[19:16], f_rd = ex_w[15:12];
    wire       b_P = ex_w[24], b_U = ex_w[23], b_B = ex_w[22], b_W = ex_w[21], b_L = ex_w[20];
    wire       b_S = ex_w[20];

    // ---------------------------------------------------------------------------------------------------------
    // the boundary decision (model begin(): reset > reset entry > data abort > FIQ > IRQ > prefetch abort)
    // ---------------------------------------------------------------------------------------------------------
    wire hold_s = fc ? hold : hold_l;
    wire fiq_s  = fc ? fiq  : fiq_l;
    wire irq_s  = fc ? irq  : irq_l;

    wire d_hold  = hold_s;
    wire d_rexit = !d_hold && (in_rst || rst_exit);
    wire d_dab   = !d_hold && !d_rexit && dab;
    wire d_fiq   = !d_hold && !d_rexit && !dab && fiq_s && !f_f;
    wire d_irq   = !d_hold && !d_rexit && !dab && !(fiq_s && !f_f) && irq_s && !f_i;
    wire d_pab   = !d_hold && !d_rexit && !dab && !(fiq_s && !f_f) && !(irq_s && !f_i) && ex_a;
    wire d_ins   = !d_hold && !d_rexit && !dab && !(fiq_s && !f_f) && !(irq_s && !f_i) && !ex_a;
    wire d_swi   = d_ins && cond && is_swi;
    wire d_exc   = d_rexit || d_dab || d_fiq || d_irq || d_pab || d_swi;

    logic [4:0] e_vec, e_mode;
    logic       e_setf;
    always_comb begin
        e_vec = 5'h08; e_mode = M_SVC; e_setf = 1'b0;                          // SWI
        if (d_rexit)    begin e_vec = 5'h00; e_mode = M_SVC; e_setf = 1'b1; end
        else if (d_dab) begin e_vec = 5'h10; e_mode = M_ABT; e_setf = 1'b0; end
        else if (d_fiq) begin e_vec = 5'h1C; e_mode = M_FIQ; e_setf = 1'b1; end
        else if (d_irq) begin e_vec = 5'h18; e_mode = M_IRQ; e_setf = 1'b0; end
        else if (d_pab) begin e_vec = 5'h0C; e_mode = M_ABT; e_setf = 1'b0; end
    end
    wire [2:0] e_bank = bank_of(e_mode);

    // ---------------------------------------------------------------------------------------------------------
    // register reads: port A = Rn (MLA: its Rn field), port B = Rm / Rs / STR's Rd / the STM register
    // ---------------------------------------------------------------------------------------------------------
    wire [3:0] bdt_r    = ctz16(x_list);
    wire       bdt_last = (x_list & (x_list - 16'd1)) == 16'd0;

    logic [3:0] ra_r, rb_r;
    always_comb begin
        ra_r = (st == S_BOUND && is_mul) ? ex_w[15:12] : f_rn;
        case (st)
            S_BOUND: rb_r = (is_mul || dp_rs) ? ex_w[11:8] : ex_w[3:0];
            S_SDT2:  rb_r = f_rd;
            S_BDTX:  rb_r = bdt_r;
            default: rb_r = ex_w[3:0];   // S_DP2, S_MUL, S_SWP2
        endcase
    end

    logic        w_en;
    logic [4:0]  w_idx;
    logic [31:0] w_d;
    logic [31:0] qa, qb;
    wren7_regfile u_rf (
        .clk(clk), .rst(rst), .we(w_en), .wa(w_idx), .wd(w_d),
        .ra(phys(ra_r, bank)), .rb(phys(rb_r, (st == S_BDTX && x_ub) ? B_USR : bank)),
        .qa(qa), .qb(qb)
    );
    wire [31:0] va = (ra_r == 4'd15) ? pc : qa;
    wire [31:0] vb = (rb_r == 4'd15) ? pc : qb;

    // ---------------------------------------------------------------------------------------------------------
    // barrel shifter: the rotated immediate, the immediate-shift form (data operation, LDR / STR offset), the
    // register-specified shift (cycle 2) and the Booth step's Rm << sh
    // ---------------------------------------------------------------------------------------------------------
    // Booth digit of this step: {b(2k+1), b(2k), borrow} -> 0 +1 +1 +2 -2 -1 -1 0
    wire [2:0] m_idx = {x_base[1:0], x_borrow};
    wire       m_pos = m_idx == 3'd1 || m_idx == 3'd2 || m_idx == 3'd3;
    wire       m_neg = m_idx == 3'd4 || m_idx == 3'd5 || m_idx == 3'd6;
    wire       m_two = m_idx == 3'd3 || m_idx == 3'd4;
    wire       m_last = mk == 4'd15 || x_base[31:1] == 31'd0;

    logic [31:0] sh_v;
    logic [1:0]  sh_t;
    logic [7:0]  sh_n;
    logic        sh_imm, sh_cin;
    logic [31:0] sh_o;
    logic        sh_c;
    always_comb begin
        sh_v = vb; sh_t = ex_w[6:5]; sh_n = {3'd0, ex_w[11:7]}; sh_imm = 1'b1; sh_cin = f_c;
        case (st)
            S_DP2: begin sh_n = x_amt; sh_imm = 1'b0; sh_cin = x_cin; end
            S_MUL: begin sh_t = 2'd0; sh_n = {3'd0, mk, m_two}; sh_imm = 1'b0; end
            default:
                if (ex_w[25] && dpsp) begin   // rotated 8-bit immediate (data operation, MSR)
                    sh_v = {24'd0, ex_w[7:0]}; sh_t = 2'd3; sh_n = {3'd0, ex_w[11:8], 1'b0}; sh_imm = 1'b0;
                end
        endcase
    end
    wren7_shifter u_sh (.v(sh_v), .t(sh_t), .n(sh_n), .imm(sh_imm), .cin(sh_cin), .q(sh_o), .cout(sh_c));

    // ---------------------------------------------------------------------------------------------------------
    // ALU adder: data operations, the LDR / STR address, the LDM / STM write-back value, the Booth step
    // ---------------------------------------------------------------------------------------------------------
    wire [3:0] opc   = ex_w[24:21];
    wire       test  = opc[3:2] == 2'b10;
    wire       wpc   = f_rd == 4'd15 && !test;
    wire       arith = !(opc == 4'h0 || opc == 4'h1 || opc == 4'h8 || opc == 4'h9 || opc >= 4'hC);
    wire       dp_cin = (st == S_DP2) ? x_cin : f_c;

    wire [31:0] off   = ex_w[25] ? sh_o : {20'd0, ex_w[11:0]};              // LDR / STR offset
    wire [15:0] list  = (ex_w[15:0] == 16'd0) ? 16'h8000 : ex_w[15:0];        // empty list: R15, 16 words
    wire [6:0]  bytes = (ex_w[15:0] == 16'd0) ? 7'd64 : {pop16(ex_w[15:0]), 2'b00};

    logic [31:0] ax, ay;
    logic        aci;
    always_comb begin
        if (st == S_MUL) begin
            ax = x_calc; ay = m_neg ? ~sh_o : sh_o; aci = m_neg;
        end else if (st == S_BOUND && is_sdt) begin
            ax = va; ay = b_U ? off : ~off; aci = !b_U;
        end else if (st == S_BOUND && is_bdt) begin
            ax = va; ay = b_U ? {25'd0, bytes} : ~{25'd0, bytes}; aci = !b_U;
        end else begin   // data operation
            ax = (opc == 4'h3 || opc == 4'h7) ? sh_o : va;
            ay = (opc == 4'h2 || opc == 4'h6 || opc == 4'hA) ? ~sh_o :
                 (opc == 4'h3 || opc == 4'h7) ? ~va : sh_o;
            aci = (opc == 4'h2 || opc == 4'h3 || opc == 4'hA) ? 1'b1 :
                  (opc == 4'h5 || opc == 4'h6 || opc == 4'h7) ? dp_cin : 1'b0;
        end
    end
    wire [32:0] sum = {1'b0, ax} + {1'b0, ay} + {32'd0, aci};
    wire        add_c = sum[32];
    wire        add_v = (~(ax[31] ^ ay[31])) & (ax[31] ^ sum[31]);

    logic [31:0] dp_res;
    always_comb begin
        case (opc)
            4'h0, 4'h8: dp_res = va & sh_o;
            4'h1, 4'h9: dp_res = va ^ sh_o;
            4'hC:       dp_res = va | sh_o;
            4'hD:       dp_res = sh_o;
            4'hE:       dp_res = va & ~sh_o;
            4'hF:       dp_res = ~sh_o;
            default:    dp_res = sum[31:0];
        endcase
    end
    wire dp_c = arith ? add_c : sh_c;
    wire dp_v = arith ? add_v : f_v;

    // branch target / LDM-STM start address
    logic [31:0] soff;
    always_comb begin
        if (b_U) soff = b_P ? 32'd4 : 32'd0;
        else     soff = (b_P ? 32'd0 : 32'd4) - {25'd0, bytes};
    end
    wire [31:0] boff  = {{6{ex_w[23]}}, ex_w[23:0], 2'b00};
    wire [31:0] ad2   = (is_br ? pc : va) + (is_br ? boff : soff);
    wire [31:0] pcp4  = pc + 32'd4;
    wire [31:0] pcm4  = pc - 32'd4;
    wire [31:0] xap4  = x_addr + 32'd4;

    // MUL step: the new accumulator
    wire [31:0] m_acc = (m_pos || m_neg) ? sum[31:0] : x_calc;

    // MSR: the source, the CPSR / SPSR fields (flags: bit 19, control: bit 16 -- the CPSR's are protected in
    // user mode), and the merged SPSR of the current mode
    wire [31:0] msr_src = ex_w[25] ? sh_o : vb;
    wire        msr_f   = ex_w[19];
    wire        msr_c   = ex_w[16] && !umode;
    logic [10:0] msr_spsr;
    always_comb begin
        msr_spsr = spsr_cur;
        if (msr_f)    msr_spsr[10:7] = msr_src[31:28];
        if (ex_w[16]) begin msr_spsr[6:5] = msr_src[7:6]; msr_spsr[4:0] = msr_src[4:0]; end
    end

    // ---------------------------------------------------------------------------------------------------------
    // bus (registers only)
    // ---------------------------------------------------------------------------------------------------------
    always_comb begin
        bus_type = ctype;
        bus_req  = !ctype[1];
        bus_addr = pc;
        bus_we   = 1'b0;
        bus_byte = 1'b0;
        bus_opc  = 1'b0;
        bus_lock = 1'b0;
        bus_user = umode;
        case (st)
            S_BOUND, S_UND2: bus_opc = 1'b1;
            S_REF0, S_REF1:  begin bus_addr = x_addr; bus_opc = 1'b1; end
            S_SWP1:          begin bus_addr = x_addr; bus_byte = b_B; bus_lock = 1'b1; end
            S_SWP2:          begin bus_addr = x_addr; bus_byte = b_B; bus_lock = 1'b1; bus_we = 1'b1; end
            S_SDT2:          begin bus_addr = x_addr; bus_byte = x_bbyte; bus_user = x_buser; bus_we = !b_L; end
            S_BDTX:          begin bus_addr = x_addr; bus_we = !b_L; end
            default: ;       // internal cycles at pc: S_DP2, S_MUL, S_SWP3, S_SDT3, S_BDTF
        endcase
    end
    assign bus_wdata = bus_byte ? {4{vb[7:0]}} : vb;

    assign dbg_boundary = st == S_BOUND;
    assign dbg_pc       = pc;
    assign dbg_cpsr     = cpsr32;
    assign dbg_ex       = ex_w;

    // ---------------------------------------------------------------------------------------------------------
    // end of the cycle
    // ---------------------------------------------------------------------------------------------------------
    wire cdone = ce && (bus_req ? bus_ack : 1'b1);
    wire ab_e  = bus_req && bus_abort;
    wire [31:0] ld_v = load_val(x_data, x_addr[1:0], b_B);

    logic [3:0]  n_st, n_mk;
    logic [1:0]  n_ctype;
    logic [31:0] n_pc;
    logic [10:0] n_cpsr;
    logic        sp_we;         // SPSR write: bank sp_b <- sp_v
    logic [2:0]  sp_b;
    logic [10:0] sp_v;
    logic        n_dab, n_in_rst, n_rst_exit;
    logic [31:0] n_x_addr, n_x_data, n_x_calc, n_x_base;
    logic        n_x_fab;
    logic [7:0]  n_x_amt;
    logic        n_x_cin, n_x_bbyte, n_x_buser, n_x_wb, n_x_ab, n_x_first, n_x_pcl, n_x_ub, n_x_borrow;
    logic [15:0] n_x_list;
    logic        n_pw_v;
    logic [4:0]  n_pw_idx;
    logic        cap, adv;      // cycle 1 fetch captured / pipeline advanced
    logic        w_req;         // register write: architectural register w_r of bank w_b, or physical pw_idx
    logic        w_phys;
    logic [3:0]  w_r;
    logic [2:0]  w_b;

    // a register write request: architectural register r of bank b (the port drops R15)
`define WREG(r, b, d) begin w_req = 1'b1; w_phys = 1'b0; w_r = (r); w_b = (b); w_d = (d); end

    // the data operation's last cycle (model dp_finish): the new CPSR; R15 as the destination refills instead
    logic [10:0] dpf_cpsr;
    always_comb begin
        dpf_cpsr = cpsr11;
        if (wpc) begin
            if (b_S && bank != B_USR) dpf_cpsr = spsr_cur;          // 4.4.4: SPSR -> CPSR (user bank: kept)
        end else if (b_S) begin
            if (test && f_rd == 4'd15) begin                         // TEQP-style: SPSR -> CPSR if privileged
                if (bank != B_USR) dpf_cpsr = spsr_cur;
            end else begin
                dpf_cpsr[10:7] = {dp_res[31], dp_res == 32'd0, dp_c, dp_v};
            end
        end
    end

    always_comb begin
        n_st = st; n_mk = mk; n_ctype = ctype; n_pc = pc; n_cpsr = cpsr11;
        sp_we = 1'b0; sp_b = B_UND; sp_v = cpsr11;
        n_dab = dab; n_in_rst = in_rst; n_rst_exit = rst_exit;
        n_x_addr = x_addr; n_x_data = x_data; n_x_calc = x_calc; n_x_base = x_base; n_x_fab = x_fab;
        n_x_amt = x_amt; n_x_cin = x_cin; n_x_bbyte = x_bbyte; n_x_buser = x_buser; n_x_wb = x_wb; n_x_ab = x_ab;
        n_x_first = x_first; n_x_pcl = x_pcl; n_x_ub = x_ub; n_x_borrow = x_borrow; n_x_list = x_list;
        n_pw_v = pw_v; n_pw_idx = pw_idx;
        cap = 1'b0; adv = 1'b0;
        w_req = 1'b0; w_phys = 1'b0; w_r = 4'd0; w_b = bank; w_d = 32'd0;

        case (st)
            S_BOUND: begin
                n_st = S_BOUND;
                if (d_hold) begin                                   // dummy fetch in reset
                    n_in_rst = 1'b1;
                    n_pc = pcp4;
                    n_ctype = CYC_S;
                end else if (d_exc) begin                           // exception entry (Table 22); the word is dropped
                    if (d_rexit) begin n_in_rst = 1'b0; n_rst_exit = 1'b0; end
                    if (d_dab) n_dab = 1'b0;
                    sp_we = 1'b1; sp_b = e_bank; sp_v = cpsr11;
                    n_cpsr[4:0] = e_mode;
                    n_cpsr[6] = 1'b1;
                    if (e_setf) n_cpsr[5] = 1'b1;
                    `WREG(4'd14, e_bank, pcm4)
                    n_x_addr = {27'd0, e_vec};
                    n_ctype = CYC_N;
                    n_st = S_REF0;
                end else if (!cond) begin                           // not executed: 1S
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_S; adv = 1'b1;
                end else if (is_dp && dp_rs) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_I;
                    n_x_amt = vb[7:0]; n_x_cin = f_c;
                    n_st = S_DP2;
                end else if (is_dp) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = wpc ? CYC_N : CYC_S;
                    n_cpsr = dpf_cpsr;
                    if (wpc) begin
                        n_x_addr = {dp_res[31:2], 2'b00};
                        n_st = S_REF0;
                    end else begin
                        if (!test) `WREG(f_rd, bank, dp_res)
                        adv = 1'b1;
                    end
                end else if (is_psr) begin
                    cap = 1'b1; n_pc = pcp4;
                    if (!ex_w[21]) begin                            // MRS
                        if (f_rd == 4'd15) begin                    // (undocumented: a PC write)
                            n_ctype = CYC_N;
                            n_x_addr = {(b_B ? psr32(spsr_cur) : cpsr32) & 32'hFFFFFFFC};
                            n_st = S_REF0;
                        end else begin
                            n_ctype = CYC_S;
                            `WREG(f_rd, bank, b_B ? psr32(spsr_cur) : cpsr32)
                            adv = 1'b1;
                        end
                    end else begin                                  // MSR
                        n_ctype = CYC_S;
                        if (b_B) begin
                            if (bank != B_USR) begin sp_we = 1'b1; sp_b = bank; sp_v = msr_spsr; end   // (user bank: ignored)
                        end else begin
                            if (msr_f) n_cpsr[10:7] = msr_src[31:28];
                            if (msr_c) begin n_cpsr[6:5] = msr_src[7:6]; n_cpsr[4:0] = msr_src[4:0]; end
                        end
                        adv = 1'b1;
                    end
                end else if (is_mul) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_I;
                    n_x_calc = ex_w[21] ? va : 32'd0;               // MLA: Rn
                    n_x_base = vb;                                  // Rs
                    `WREG(ex_w[19:16], bank, ex_w[21] ? va : 32'd0)
                    n_x_borrow = 1'b0;
                    n_mk = 4'd0;
                    n_st = S_MUL;
                end else if (is_swp) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_N;
                    n_x_addr = va;
                    n_st = S_SWP1;
                end else if (is_sdt) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_N;
                    n_x_calc = sum[31:0];
                    n_x_addr = b_P ? sum[31:0] : va;
                    n_x_wb = !b_P || b_W;
                    n_x_bbyte = b_B;
                    n_x_buser = (!b_P && b_W) || umode;              // LDRT / STRT force nTRANS
                    n_st = S_SDT2;
                end else if (is_bdt) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_N;
                    n_x_list = list;
                    n_x_base = va;
                    n_x_calc = sum[31:0];                           // write-back value
                    n_x_addr = ad2;                                 // first address
                    n_x_ub = ex_w[22] && !(b_L && list[15]);         // user bank transfer (4.8.4)
                    n_x_first = 1'b1; n_x_ab = 1'b0; n_x_pcl = 1'b0; n_pw_v = 1'b0;
                    n_st = S_BDTX;
                end else if (is_br) begin
                    cap = 1'b1; n_pc = pcp4; n_ctype = CYC_N;
                    if (ex_w[24]) `WREG(4'd14, bank, pcm4)
                    n_x_addr = ad2;
                    n_st = S_REF0;
                end else begin                                      // undefined (Table 28): word dropped, pc held
                    n_ctype = CYC_I;
                    n_st = S_UND2;
                end
            end

            S_DP2: begin
                n_ctype = wpc ? CYC_N : CYC_S;
                n_st = S_BOUND;
                n_cpsr = dpf_cpsr;
                if (wpc) begin
                    n_x_addr = {dp_res[31:2], 2'b00};
                    n_st = S_REF0;
                end else begin
                    if (!test) `WREG(f_rd, bank, dp_res)
                    adv = 1'b1;
                end
            end

            S_MUL: begin
                n_x_calc = m_acc;
                n_x_base = {2'b00, x_base[31:2]};
                n_x_borrow = x_base[1];
                n_mk = mk + 4'd1;
                `WREG(ex_w[19:16], bank, m_acc)
                if (m_last) begin
                    n_ctype = CYC_S;
                    if (b_S) n_cpsr[10:8] = {m_acc[31], m_acc == 32'd0, sh_c};
                    adv = 1'b1;
                    n_st = S_BOUND;
                end else begin
                    n_ctype = CYC_I;
                end
            end

            S_SWP1: begin
                n_x_data = bus_rdata;
                n_x_ab = ab_e;
                n_ctype = CYC_N;
                n_st = S_SWP2;
            end

            S_SWP2: begin
                n_x_ab = x_ab || ab_e;
                n_ctype = CYC_I;
                n_st = S_SWP3;
            end

            S_SWP3: begin
                n_ctype = CYC_S;
                if (x_ab) n_dab = 1'b1;
                else      `WREG(f_rd, bank, ld_v)
                adv = 1'b1;
                n_st = S_BOUND;
            end

            S_SDT2: begin
                if (x_wb && f_rn != 4'd15) `WREG(f_rn, bank, x_calc)   // base write-back
                if (b_L) begin
                    n_x_data = bus_rdata;
                    n_x_ab = ab_e;
                    n_ctype = CYC_I;
                    n_st = S_SDT3;
                end else begin
                    if (ab_e) n_dab = 1'b1;
                    n_ctype = CYC_N;
                    adv = 1'b1;
                    n_st = S_BOUND;
                end
            end

            S_SDT3: begin
                n_ctype = (f_rd == 4'd15 && !x_ab) ? CYC_N : CYC_S;
                n_st = S_BOUND;
                if (x_ab) begin
                    n_dab = 1'b1;
                    adv = 1'b1;
                end else if (f_rd == 4'd15) begin
                    n_x_addr = {ld_v[31:2], 2'b00};
                    n_st = S_REF0;
                end else begin
                    `WREG(f_rd, bank, ld_v)
                    adv = 1'b1;
                end
            end

            S_BDTX: begin
                n_x_ab = x_ab || ab_e;
                n_x_first = 1'b0;
                n_x_addr = xap4;
                n_x_list = x_list & (x_list - 16'd1);
                if (x_first && b_W) `WREG(f_rn, bank, x_calc)       // base write-back in the first transfer
                if (b_L) begin
                    if (pw_v) begin w_req = 1'b1; w_phys = 1'b1; w_d = x_data; end   // previous register
                    n_pw_v = 1'b0;
                    if (!(x_ab || ab_e)) begin                      // 4.8.7: nothing is loaded from the abort on
                        n_x_data = bus_rdata;
                        if (bdt_r == 4'd15) n_x_pcl = 1'b1;
                        else begin n_pw_v = 1'b1; n_pw_idx = phys(bdt_r, x_ub ? B_USR : bank); end
                    end
                    n_ctype = bdt_last ? CYC_I : CYC_S;
                    n_st = bdt_last ? S_BDTF : S_BDTX;
                end else begin
                    n_ctype = bdt_last ? CYC_N : CYC_S;
                    if (bdt_last) begin
                        if (x_ab || ab_e) n_dab = 1'b1;
                        adv = 1'b1;
                        n_st = S_BOUND;
                    end
                end
            end

            S_BDTF: begin
                n_ctype = x_pcl ? CYC_N : CYC_S;
                n_st = S_BOUND;
                n_pw_v = 1'b0;
                if (pw_v) begin w_req = 1'b1; w_phys = 1'b1; w_d = x_data; end      // the last register
                if (x_ab) begin
                    `WREG(f_rn, bank, b_W ? x_calc : x_base)        // base restored
                    n_dab = 1'b1;
                    adv = 1'b1;
                end else if (x_pcl) begin
                    if (ex_w[22] && bank != B_USR) n_cpsr = spsr_cur;   // LDM^ with R15: SPSR -> CPSR
                    n_x_addr = {x_data[31:2], 2'b00};
                    n_st = S_REF0;
                end else begin
                    adv = 1'b1;
                end
            end

            S_UND2: begin
                n_ctype = CYC_N;
                sp_we = 1'b1; sp_b = B_UND; sp_v = cpsr11;
                n_cpsr[4:0] = M_UND;
                n_cpsr[6] = 1'b1;
                `WREG(4'd14, B_UND, pcm4)
                n_x_addr = 32'h04;
                n_st = S_REF0;
            end

            S_REF0: begin
                n_x_data = bus_rdata;
                n_x_fab = ab_e;
                n_x_addr = xap4;
                n_ctype = CYC_S;
                n_st = S_REF1;
            end

            default: begin   // S_REF1
                n_pc = xap4;
                n_ctype = CYC_S;
                n_st = S_BOUND;
            end
        endcase
    end

`undef WREG

    // the register write port (R15 never written: the model's wr() drops it)
    assign w_en  = cdone && w_req && (w_phys || w_r != 4'd15);
    assign w_idx = w_phys ? pw_idx : phys(w_r, w_b);

    // ---------------------------------------------------------------------------------------------------------
    // registers
    // ---------------------------------------------------------------------------------------------------------
    always_ff @(posedge clk) begin
        if (rst) begin
            st <= S_BOUND; mk <= 4'd0; ctype <= CYC_N; fc <= 1'b1;
            hold_l <= 1'b0; fiq_l <= 1'b0; irq_l <= 1'b0;
            pc <= 32'd0;
            {f_n, f_z, f_c, f_v} <= 4'd0; f_i <= 1'b1; f_f <= 1'b1; f_m <= M_SVC;
            spsr_fiq <= 11'd0; spsr_irq <= 11'd0; spsr_svc <= 11'd0; spsr_abt <= 11'd0; spsr_und <= 11'd0;
            ex_w <= 32'd0; de_w <= 32'd0; f1_w <= 32'd0; ex_a <= 1'b0; de_a <= 1'b0; f1_a <= 1'b0;
            dab <= 1'b0; in_rst <= 1'b0; rst_exit <= 1'b1;
            x_ab <= 1'b0; pw_v <= 1'b0;
        end else if (ce) begin
            fc <= cdone;
            if (fc) begin hold_l <= hold; fiq_l <= fiq; irq_l <= irq; end
            if (cdone) begin
                st <= n_st; mk <= n_mk; ctype <= n_ctype; pc <= n_pc;
                {f_n, f_z, f_c, f_v, f_i, f_f, f_m} <= n_cpsr;
                if (sp_we)
                    case (sp_b)
                        B_FIQ:   spsr_fiq <= sp_v;
                        B_IRQ:   spsr_irq <= sp_v;
                        B_SVC:   spsr_svc <= sp_v;
                        B_ABT:   spsr_abt <= sp_v;
                        default: spsr_und <= sp_v;
                    endcase
                dab <= n_dab; in_rst <= n_in_rst; rst_exit <= n_rst_exit;
                x_ab <= n_x_ab; pw_v <= n_pw_v;
                if (cap) begin f1_w <= bus_rdata; f1_a <= ab_e; end
                if (adv) begin
                    ex_w <= de_w; ex_a <= de_a;
                    de_w <= cap ? bus_rdata : f1_w;
                    de_a <= cap ? ab_e : f1_a;
                end
                if (st == S_REF1) begin
                    ex_w <= x_data; ex_a <= x_fab;
                    de_w <= bus_rdata; de_a <= ab_e;
                end
            end
        end
    end

    // the in-flight latches need no reset
    always_ff @(posedge clk) begin
        if (ce && cdone) begin
            x_addr <= n_x_addr; x_data <= n_x_data; x_calc <= n_x_calc; x_base <= n_x_base; x_fab <= n_x_fab;
            x_amt <= n_x_amt; x_cin <= n_x_cin; x_bbyte <= n_x_bbyte; x_buser <= n_x_buser; x_wb <= n_x_wb;
            x_first <= n_x_first; x_pcl <= n_x_pcl; x_ub <= n_x_ub; x_borrow <= n_x_borrow; x_list <= n_x_list;
            pw_idx <= n_pw_idx;
        end
    end
endmodule
