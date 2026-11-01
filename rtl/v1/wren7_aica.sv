//
// wren7_aica -- wren7 v1 as the AICA instantiates it: wren7_core on caique's ARM7DI port (caique-rtl rtl/v1/README.md,
// "ARM7DI port contract"; wren7-rtl/INTEGRATION.md).
//
//   arm_req    the core's memory cycle (N or S), held with the other arm_* outputs until arm_ack
//   arm_ram    wave RAM (A[23] = 0; 2 MB mirrored), else the register window (A[14:0])
//   arm_addr   wave RAM: A[20:2]; registers: A[14:2] (word addresses, low two bits zero)
//   arm_be     byte cycles (LDRB / STRB / SWPB): the addressed lane; words: all four
//   arm_lock   SWP's two cycles (the bus shortens the locked write to 4 clocks)
//   arm_ack    the cycle's last clock, with arm_rdata (the whole word; the core picks the byte and rotates)
//   arm_fiq    nFIQ asserted, from the AICA's interrupt controller
// The AICA bus has no ABORT and does not drive nIRQ; nRESET is ARMRST: rst holds the core and its release runs the
// reset entry from address 0 (the register file keeps its contents, as on the console).  ce: one AICA clock
// (22.5792 MHz MCLK) per ce = 1; arm_ack is only seen in ce clocks.
//
module wren7_aica (
    input  logic        clk,
    input  logic        rst,
    input  logic        ce,

    output logic        arm_req,
    output logic        arm_we,
    output logic        arm_ram,
    output logic        arm_lock,
    output logic [20:0] arm_addr,
    output logic [31:0] arm_wdata,
    output logic [3:0]  arm_be,
    input  logic [31:0] arm_rdata,
    input  logic        arm_ack,
    input  logic        arm_fiq
);
    logic [31:0] a;
    logic        byte_c;

    wren7_core u_core (
        .clk(clk), .rst(rst), .ce(ce),
        .hold(1'b0), .fiq(arm_fiq), .irq(1'b0),
        .bus_type(), .bus_req(arm_req), .bus_addr(a), .bus_we(arm_we), .bus_byte(byte_c), .bus_opc(),
        .bus_lock(arm_lock), .bus_user(), .bus_wdata(arm_wdata),
        .bus_ack(arm_ack), .bus_rdata(arm_rdata), .bus_abort(1'b0),
        .dbg_boundary(), .dbg_pc(), .dbg_cpsr(), .dbg_ex()
    );

    assign arm_ram  = !a[23];
    assign arm_addr = arm_ram ? {a[20:2], 2'b00} : {6'd0, a[14:2], 2'b00};
    assign arm_be   = byte_c ? (4'b0001 << a[1:0]) : 4'b1111;
endmodule
