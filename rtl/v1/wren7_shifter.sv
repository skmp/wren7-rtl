//
// wren7_shifter -- the ARM7DI barrel shifter (data sheet 4.4.2), one per core, shared by operand 2 of the data
// operations, the LDR/STR register offset and the multiplier's Booth steps (Rm << 2k / 2k+1).
//
// Semantics are the C++ model's shift_imm / shift_reg (model/src/arm7di.cpp):
//   imm = 1, amount n[4:0]: LSL #0 = Rm (carry in), LSR #0 / ASR #0 = by 32, ROR #0 = RRX;
//   imm = 0, amount n[7:0]: 0 = Rm (carry in); LSL / LSR by 32 = 0 with carry Rm[0] / Rm[31], above 32 = 0 with
//   carry 0; ASR by 32 or more = sign fill; ROR by a multiple of 32 = Rm with carry Rm[31].
// One right rotator does every type (LSL n = ROR 32 - n, then the low n bits cleared; LSR / ASR clear or sign-fill
// the top n bits), so the four shift types share 5 levels of 2:1 muxes and a thermometer mask.
//
module wren7_shifter (
    input  logic [31:0] v,
    input  logic [1:0]  t,      // 0 LSL, 1 LSR, 2 ASR, 3 ROR
    input  logic [7:0]  n,
    input  logic        imm,
    input  logic        cin,
    output logic [31:0] q,
    output logic        cout
);
    // effective amount: the immediate encodings mapped onto the register-amount rules
    logic [8:0] ne;
    logic       rrx;
    always_comb begin
        rrx = imm && t == 2'd3 && n[4:0] == 5'd0;
        if (imm) ne = (n[4:0] == 5'd0 && (t == 2'd1 || t == 2'd2)) ? 9'd32 : {4'd0, n[4:0]};
        else     ne = {1'b0, n};
    end
    wire zero = ne == 9'd0;
    wire lt32 = ne < 9'd32;
    wire eq32 = ne == 9'd32;

    // right rotation by r5 (LSL: by 32 - n)
    wire [4:0] r5 = (t == 2'd0) ? (5'd0 - ne[4:0]) : ne[4:0];
    logic [31:0] s0, s1, s2, s3, rot;
    always_comb begin
        s0  = r5[0] ? {v[0],      v[31:1]}  : v;
        s1  = r5[1] ? {s0[1:0],   s0[31:2]} : s0;
        s2  = r5[2] ? {s1[3:0],   s1[31:4]} : s1;
        s3  = r5[3] ? {s2[7:0],   s2[31:8]} : s2;
        rot = r5[4] ? {s3[15:0],  s3[31:16]} : s3;
    end

    // lo[i] = i < n (n < 32): the bits an LSL clears; hi = its mirror, the bits an LSR / ASR clears or fills
    logic [31:0] lo, hi;
    always_comb begin
        for (int i = 0; i < 32; i++) begin
            lo[i]      = {27'd0, ne[4:0]} > i;
            hi[31 - i] = {27'd0, ne[4:0]} > i;
        end
    end

    always_comb begin
        q    = v;
        cout = cin;
        if (rrx) begin
            q    = {cin, v[31:1]};
            cout = v[0];
        end else if (!zero) begin
            case (t)
                2'd0: begin   // LSL
                    q    = lt32 ? (rot & ~lo) : 32'd0;
                    cout = lt32 ? rot[0] : (eq32 ? v[0] : 1'b0);
                end
                2'd1: begin   // LSR
                    q    = lt32 ? (rot & ~hi) : 32'd0;
                    cout = lt32 ? rot[31] : (eq32 ? v[31] : 1'b0);
                end
                2'd2: begin   // ASR
                    q    = lt32 ? ((rot & ~hi) | (v[31] ? hi : 32'd0)) : {32{v[31]}};
                    cout = lt32 ? rot[31] : v[31];
                end
                default: begin   // ROR (by n mod 32; a multiple of 32 leaves Rm, carry Rm[31] = rot[31])
                    q    = rot;
                    cout = rot[31];
                end
            endcase
        end
    end
endmodule
