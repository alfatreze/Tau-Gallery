// altera_pll_model.v -- a behavioural stand-in for Intel's altera_pll megafunction for the whole-core simulation (docs/FULLCORE_SIM.md). Written from the parameters the core's PLL wrappers pass
// (reference_clock_frequency, number_of_clocks, output_clock_frequencyN, phase_shiftN, duty_cycleN as strings such as "66.666667 MHz" and "6250 ps"): each output is a free running clock of that frequency
// and phase, `locked` rises after 2 microseconds. The refclk is not used (the real PLL locks to it; the ratios the core relies on come from the parameters). Not Intel's code.
`timescale 1ps/1ps
module altera_pll #(
    parameter fractional_vco_multiplier = "false", parameter reference_clock_frequency = "74.25 MHz", parameter operation_mode = "normal", parameter number_of_clocks = 1,
    parameter pll_type = "General", parameter pll_subtype = "General",
    parameter output_clock_frequency0 = "0 MHz", parameter phase_shift0 = "0 ps", parameter duty_cycle0 = 50,
    parameter output_clock_frequency1 = "0 MHz", parameter phase_shift1 = "0 ps", parameter duty_cycle1 = 50,
    parameter output_clock_frequency2 = "0 MHz", parameter phase_shift2 = "0 ps", parameter duty_cycle2 = 50,
    parameter output_clock_frequency3 = "0 MHz", parameter phase_shift3 = "0 ps", parameter duty_cycle3 = 50,
    parameter output_clock_frequency4 = "0 MHz", parameter phase_shift4 = "0 ps", parameter duty_cycle4 = 50,
    parameter output_clock_frequency5 = "0 MHz", parameter phase_shift5 = "0 ps", parameter duty_cycle5 = 50,
    parameter output_clock_frequency6 = "0 MHz", parameter phase_shift6 = "0 ps", parameter duty_cycle6 = 50,
    parameter output_clock_frequency7 = "0 MHz", parameter phase_shift7 = "0 ps", parameter duty_cycle7 = 50,
    parameter output_clock_frequency8 = "0 MHz", parameter phase_shift8 = "0 ps", parameter duty_cycle8 = 50,
    parameter output_clock_frequency9 = "0 MHz", parameter phase_shift9 = "0 ps", parameter duty_cycle9 = 50,
    parameter output_clock_frequency10 = "0 MHz", parameter phase_shift10 = "0 ps", parameter duty_cycle10 = 50,
    parameter output_clock_frequency11 = "0 MHz", parameter phase_shift11 = "0 ps", parameter duty_cycle11 = 50,
    parameter output_clock_frequency12 = "0 MHz", parameter phase_shift12 = "0 ps", parameter duty_cycle12 = 50,
    parameter output_clock_frequency13 = "0 MHz", parameter phase_shift13 = "0 ps", parameter duty_cycle13 = 50,
    parameter output_clock_frequency14 = "0 MHz", parameter phase_shift14 = "0 ps", parameter duty_cycle14 = 50,
    parameter output_clock_frequency15 = "0 MHz", parameter phase_shift15 = "0 ps", parameter duty_cycle15 = 50,
    parameter output_clock_frequency16 = "0 MHz", parameter phase_shift16 = "0 ps", parameter duty_cycle16 = 50,
    parameter output_clock_frequency17 = "0 MHz", parameter phase_shift17 = "0 ps", parameter duty_cycle17 = 50
) (
    input  wire rst, refclk, fbclk,
    output wire [number_of_clocks-1:0] outclk,
    output wire fboutclk,
    output reg  locked
);
    function real mhz(input [8*24-1:0] s); real v; integer n; begin n = $sscanf(s, "%f", v); mhz = (n == 1) ? v : 0.0; end endfunction
    function real ps(input [8*24-1:0] s); real v; integer n; begin n = $sscanf(s, "%f", v); ps = (n == 1) ? v : 0.0; end endfunction
    real f [0:17]; real ph [0:17]; reg clk [0:17]; integer i;
    initial begin
        f[0] = mhz(output_clock_frequency0); f[1] = mhz(output_clock_frequency1); f[2] = mhz(output_clock_frequency2); f[3] = mhz(output_clock_frequency3); f[4] = mhz(output_clock_frequency4); f[5] = mhz(output_clock_frequency5);
        ph[0] = ps(phase_shift0); ph[1] = ps(phase_shift1); ph[2] = ps(phase_shift2); ph[3] = ps(phase_shift3); ph[4] = ps(phase_shift4); ph[5] = ps(phase_shift5);
        locked = 1'b0; #2000000 locked = 1'b1;
    end
    genvar g;
    generate for (g = 0; g < 6; g = g + 1) begin : gen
        reg c = 1'b0; real half;
        initial begin
            #1; half = (f[g] > 0.0) ? 500000.0 / f[g] : 0.0;                    // half period in ps
            if (half > 0.0) begin #(ph[g]); forever begin #(half) c = ~c; end end
        end
        if (g < number_of_clocks) assign outclk[g] = c;
    end endgenerate
    assign fboutclk = 1'b0;
endmodule
