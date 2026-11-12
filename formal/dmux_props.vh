// Proof-only properties of tau_sdram_dmux (included by the RTL under `ifdef FORMAL; formal/dmux.sby, unbounded k-induction). Not synthesised. All port inputs are free; the only environment contract is the
// scheduler's: `done` only while a request is outstanding, and the port does not accept while C's 4-bit outstanding counter is full. Proved:
//  (1) while the secondary request is in flight no second request reaches the port and C is not ready;   (2) the secondary is granted only with C offering nothing, nothing outstanding and nothing in flight,
//  and is always granted when those hold and the port is ready;   (3) every `done` goes to exactly one client, the owner of the oldest request (C is held while the secondary runs, so that is the secondary);
//  (4) address / length / direction / write data / byte enables at the port come from the owner; next, index and read data go to the owner only;   (5) C's outstanding count cannot underflow
//  (the assumption that `done` needs an outstanding request is what keeps it exact);   (6) D's direction and byte enables pass through unforced, and read data reaches D only while D owns the port.
    wire [3:0] f_tot = c_out + {3'd0, sub_busy};
    always @(*) if (!rst) begin
        assume (!s_done || f_tot != 4'd0);
        assume (!(s_valid && s_ready) || c_out != 4'd15);
    end
    always @(*) if (!rst) begin
        assert (!sub_busy || c_out == 4'd0);                                                  // helper invariant: C is held while the secondary is in flight, so none of C's requests is outstanding
        if (sub_busy) begin assert (!s_valid); assert (!c_ready); end                          // (1)
        if (grant_d) begin assert (!sub_busy); assert (!c_valid); assert (c_out == 4'd0); end   // (2)
        if (grant_d && s_ready) assert (d_ready);
        assert (!(c_done && d_done));                                                        // (3)
        if (s_done) begin assert (c_done != d_done); assert (d_done == sub_busy); end else begin assert (!c_done); assert (!d_done); end
        if (grant_d) begin assert (s_addr == d_addr); assert (s_len == d_len); assert (s_valid); end   // (4)
        else begin assert (s_addr == c_addr); assert (s_len == c_len); assert (s_we == c_we); end
        if (sub_busy) begin assert (s_wdata == d_wdata); assert (d_next == s_next); assert (!c_next); end
        else begin assert (s_wdata == c_wdata); assert (s_be == c_be); assert (c_next == s_next); assert (!d_next); end
        assert (c_idx == s_idx); assert (d_idx == s_idx);
        if (sub_busy) begin assert (!c_r_valid); assert (!c_r_last); end else begin assert (c_r_valid == s_r_valid); assert (c_r_last == s_r_last); end
        assert (c_r_data == s_r_data);
        if (grant_d) assert (s_we == d_we);
        if (sub_busy) assert (s_be == d_be);
        if (sub_busy) begin assert (d_r_valid == s_r_valid); assert (d_r_last == s_r_last); end else begin assert (!d_r_valid); assert (!d_r_last); end
        assert (d_r_data == s_r_data);
    end
