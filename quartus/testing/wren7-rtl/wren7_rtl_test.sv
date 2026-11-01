//
// wren7_rtl_test - standalone synthesis / timing harness around the wren7 v1 ARM7DI as the AICA instantiates it
// (rtl/v1/wren7_aica.sv: wren7_core -- sequencer, banked register file in MLABs, barrel shifter, ALU, multiplier
// steps -- on caique's ARM7DI port). A self-contained place-and-route + timing-close context, so the fitter reports
// Fmax and area for the core alone, decoupled from any board top -- the wren7 sibling of shrike4-rtl's
// quartus/testing harness, with the same pattern:
//   * ALL inputs are driven from a free-running input register bank (in_reg) the HPS pokes via wr_en / wr_addr /
//     wr_data, so every input bit has a real register source and the fitter cannot fold the core away against
//     constant inputs;
//   * ALL outputs are captured into RAW registers with NO logic in between (pure core timing), then XOR-folded to a
//     SINGLE `digest` pin one cycle LATER (the fold tree sits off the capture registers, never in a measured path).
//
// The core is the AICA configuration: no ABORT, nIRQ not driven, nRESET = ARMRST (rst). The testbench-only outputs
// (bus type / opcode / user flags, the dbg_* state) are not connected, so only logic that reaches the AICA's port is
// kept. One clock: `clk`; ce is a clock enable (one AICA MCLK per ce = 1).
//
// Build: cd quartus/testing/wren7-rtl && quartus_sh --flow compile wren7-rtl
// Fmax:  output_files/wren7-rtl.sta.rpt
//
module wren7_rtl_test (
    input             clk,
    input             reset,

    // ---- input register load (driven by the HPS) ----
    input             wr_en,        // 1: in_reg[wr_addr] <= wr_data
    input       [3:0] wr_addr,
    input      [31:0] wr_data,

    // ---- single folded output pin (keeps every output bit alive) ----
    output reg        digest
);
    // ---- input register bank ----
    //   0 : arm_rdata
    //   1 : control bits: 0 rst (ARMRST), 1 ce, 2 arm_ack, 3 arm_fiq
    localparam integer NREG = 2;
    reg [31:0] in_reg [0:NREG-1];
    integer ir;
    always @(posedge clk) begin
        if (reset) begin
            for (ir=0; ir<NREG; ir=ir+1) in_reg[ir] <= 32'd0;
        end else if (wr_en && wr_addr < 4'd2) begin
            in_reg[wr_addr[0]] <= wr_data;
        end
    end

    wire [31:0] w_ctl = in_reg[1];

    // ---- DUT outputs ----
    wire        d_req, d_we, d_ram, d_lock;
    wire [20:0] d_addr;
    wire [31:0] d_wdata;
    wire  [3:0] d_be;

    // ---- DUT ----
    wren7_aica u_arm (
        .clk(clk),
        .rst(w_ctl[0]),
        .ce(w_ctl[1]),
        .arm_req(d_req), .arm_we(d_we), .arm_ram(d_ram), .arm_lock(d_lock),
        .arm_addr(d_addr), .arm_wdata(d_wdata), .arm_be(d_be),
        .arm_rdata(in_reg[0]),
        .arm_ack(w_ctl[2]),
        .arm_fiq(w_ctl[3])
    );

    // ---- RAW capture (no logic before the flops -> pure core timing) ----
    reg [31:0] cap_wdata;
    reg [28:0] cap_bus;                   // req, we, ram, lock, addr, be
    always @(posedge clk) begin
        if (reset) begin cap_wdata <= '0; cap_bus <= '0; end
        else begin
            cap_wdata <= d_wdata;
            cap_bus   <= {d_req, d_we, d_ram, d_lock, d_addr, d_be};
        end
    end

    // ---- next cycle: XOR-fold to one pin (off cap_*, not the DUT) ----
    always @(posedge clk) begin
        if (reset) digest <= 1'b0;
        else       digest <= ^{ cap_wdata, cap_bus };
    end
endmodule
