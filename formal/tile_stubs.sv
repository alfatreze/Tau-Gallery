// Proof-only stand-ins for tau_tgy_idct and tau_tgy_recon (the tile controller's two sub-units; their arithmetic is proved by simulation against the Python reference, the FSM proof only needs their handshakes):
// idct: busy from `start` until it chooses (freely) to pulse done; recon: busy from `start` until it chooses to pulse done or err. Pixel and data outputs are free.
module tau_tgy_idct (input wire clk, rst, clear, we, input wire [5:0] nat, input wire signed [17:0] val, input wire start, output wire busy, output wire done, output wire px_valid, output wire [2:0] px_row, output wire [63:0] px_data);
    (* anyseq *) wire fin; (* anyseq *) wire pv; (* anyseq *) wire [2:0] pr; (* anyseq *) wire [63:0] pd;
    reg b = 1'b0; reg d = 1'b0;
    always @(posedge clk) begin d <= 1'b0; if (rst) b <= 1'b0; else if (start && !b) b <= 1'b1; else if (b && fin) begin b <= 1'b0; d <= 1'b1; end end
    assign busy = b; assign done = d; assign px_valid = pv; assign px_row = pr; assign px_data = pd;
endmodule
module tau_tgy_recon #(parameter integer C444 = 0) (input wire clk, rst, we, input wire [1:0] wcomp, input wire [6:0] wx, wy, input wire [63:0] wdata, input wire start, input wire [1:0] fmt, input wire [7:0] tw, th, input wire hold,
    output wire busy, done, err, px_valid, output wire [6:0] px_x, px_y, output wire [7:0] px_r, px_g, px_b, output wire px_last);
    (* anyseq *) wire fd, fe; (* anyseq *) wire pv; (* anyseq *) wire [6:0] x, y; (* anyseq *) wire [23:0] rgb; (* anyseq *) wire pl;
    reg b = 1'b0; reg d = 1'b0, e = 1'b0;
    always @(posedge clk) begin d <= 1'b0; e <= 1'b0; if (rst) b <= 1'b0; else if (start && !b) b <= 1'b1; else if (b && (fd || fe)) begin b <= 1'b0; if (fe) e <= 1'b1; else d <= 1'b1; end end
    assign busy = b; assign done = d; assign err = e; assign px_valid = pv; assign px_x = x; assign px_y = y; assign {px_r, px_g, px_b} = rgb; assign px_last = pl;
endmodule
