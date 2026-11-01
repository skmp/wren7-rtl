//
// wren7_regfile -- the ARM7DI's 30 banked general registers (wren7 v1; R15 lives in the core).
//
// Two asynchronous read ports and one write port, sized for Cyclone V MLABs: each read port has its own 32 x 32
// copy (an MLAB pair) and both copies take every write, so no read port needs a multi-port memory.  A write lands
// on the clock edge that ends the core's bus cycle; the core reads in the following cycles.  MLABs commit a write
// during the clock after that edge, so the last write is also kept in a bypass register for that one clock: reads
// never depend on the MLAB's write-to-read path, and `no_rw_check` is safe (a same-clock read wants the old value,
// which the MLAB still holds).
//
// Physical numbering (the C++ model's Arm7DI::phys): 0-7 r0-r7, 8-14 r8-r14 usr, 15-21 r8-r14 fiq, 22/23 r13/r14
// irq, 24/25 svc, 26/27 abt, 28/29 und.  Contents are not reset (the console keeps them across ARMRST); simulation
// starts them at zero like the model's power_on.
//
module wren7_regfile (
    input  logic        clk,
    input  logic        rst,
    input  logic        we,
    input  logic [4:0]  wa,
    input  logic [31:0] wd,
    input  logic [4:0]  ra,
    input  logic [4:0]  rb,
    output logic [31:0] qa,
    output logic [31:0] qb
);
    (* ramstyle = "MLAB, no_rw_check" *) logic [31:0] mem_a [0:31] /*verilator public_flat_rd*/;
    (* ramstyle = "MLAB, no_rw_check" *) logic [31:0] mem_b [0:31];

`ifndef SYNTHESIS
    initial begin
        for (int i = 0; i < 32; i++) begin
            mem_a[i] = 32'd0;
            mem_b[i] = 32'd0;
        end
    end
`endif

    always_ff @(posedge clk) begin
        if (we) begin
            mem_a[wa] <= wd;
            mem_b[wa] <= wd;
        end
    end

    // the last write, for the clock after it
    logic        lw_v;
    logic [4:0]  lw_a;
    logic [31:0] lw_d;
    always_ff @(posedge clk) begin
        lw_v <= we && !rst;
        lw_a <= wa;
        lw_d <= wd;
    end

    assign qa = (lw_v && lw_a == ra) ? lw_d : mem_a[ra];
    assign qb = (lw_v && lw_a == rb) ? lw_d : mem_b[rb];
endmodule
