// Testbench of src/fpga/core/tau_tgy_wfifo.sv: two UNRELATED clocks (100 MHz writer, 50 MHz reader with an odd phase), random pushes and pops, clears at random: every word that is read is the next word that was pushed since the
// last clear (nothing lost, repeated, reordered or taken from before a clear), `full` is never ignored (no overflow), a cleared FIFO reads empty, the clear handshake completes. Mutants (-P BUG=n) must fail.
`timescale 1ns/1ps
module tb_tau_tgy_wfifo;
    parameter integer BUG = 0;
    reg clk_w = 0, clk_r = 0; always #5 clk_w = ~clk_w; initial #3.3 forever #10 clk_r = ~clk_r;
    reg clear_req = 0, push = 0, pop = 0; wire [31:0] wdata; wire clear_ack, full, rvalid; wire [31:0] rdata;
    tau_tgy_wfifo #(.AW(4), .BUG(BUG)) dut (.clk_w(clk_w), .clk_r(clk_r), .clear_req(clear_req), .clear_ack(clear_ack), .push(push), .wdata(wdata), .full(full), .rvalid(rvalid), .rdata(rdata), .pop(pop));
    integer seed = 32'h1badf00d; function integer rnd(input integer n); rnd = $unsigned($random(seed)) % n; endfunction
    integer errors = 0, pushed = 0, popped = 0, nclear = 0, ovf = 0, cyc; reg [31:0] expect_next = 1; reg in_clear = 0; integer push_pct = 50, pop_pct = 50;
    // writer: `val` is the word presented while push is up; it is accepted when push && !full at a clock edge
    reg [31:0] val = 1; assign wdata = val;
    always @(posedge clk_w) begin
        if (in_clear || clear_req) push <= 0;
        else if (push && !full) begin val <= val + 1; pushed <= pushed + 1; push <= (rnd(100) < push_pct); end
        else if (!push) push <= (rnd(100) < push_pct);
    end
    // reader: pops when valid, at random
    always @(posedge clk_r) begin
        pop <= 0;
        if (rvalid && !pop && rnd(100) < pop_pct) pop <= 1;
        if (rvalid && pop) begin
            popped <= popped + 1;
            if (rdata !== expect_next) begin errors <= errors + 1; if (errors < 5) $display("word %0d: got %0d want %0d (t=%0t)", popped, rdata, expect_next, $time); end
            expect_next <= expect_next + 1;
        end
        if (clear_req && clear_ack && rvalid) begin errors <= errors + 1; if (errors < 5) $display("valid output during a clear (t=%0t)", $time); end
    end
    integer round;
    initial begin
        repeat (10) @(posedge clk_w);
        for (round = 0; round < 40; round = round + 1) begin
            push_pct = 20 + rnd(80); pop_pct = 10 + rnd(90);
            repeat (200 + rnd(600)) @(posedge clk_w);
            // clear: the writer stops, the request goes up, the handshake must complete
            in_clear = 1; repeat (4) @(posedge clk_w); clear_req = 1; cyc = 0;
            while (!clear_ack && cyc < 200) begin @(posedge clk_w); cyc = cyc + 1; end
            if (cyc >= 200) begin errors = errors + 1; $display("clear ack never came"); end
            repeat (8) @(posedge clk_w); clear_req = 0; cyc = 0;
            while (clear_ack && cyc < 200) begin @(posedge clk_w); cyc = cyc + 1; end
            if (cyc >= 200) begin errors = errors + 1; $display("clear ack never dropped"); end
            repeat (8) @(posedge clk_w);
            // after a clear: the sequence restarts: the next word pushed is 'expect' again
            pushed = 0; val = 1; expect_next = 1; popped = 0; in_clear = 0; nclear = nclear + 1;
        end
        repeat (400) @(posedge clk_w);
        if (errors == 0) $display("WFIFO PASSED (%0d clears)", nclear); else $display("WFIFO FAILED %0d", errors);
        $finish;
    end
endmodule
