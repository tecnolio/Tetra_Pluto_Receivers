`timescale 1ns/100ps

// TETRA DDC + CIC lane, timing-optimized v04.
//
// v03 remaining timing failures came from the NCO address fanout/LUT decode.
// v04 duplicates the NCO ROM into two independent synchronous single-port
// block ROMs (SIN and COS). This removes the shared dual-read decode tree and
// gives Vivado a much cleaner BRAM inference.
//
// Target:
//   l_clk constraint: 250 MHz / 4 ns
//   sample-valid rate: 12.5 MS/s
//   DECIM=50 -> 250 kS/s
module tetra_ddc_cic_lane #(
  parameter integer PHASE_STEP = 0,
  parameter integer DECIM = 50,
  parameter integer SHIFT = 28,
  parameter MEMFILE = "tetra_sin500.mem"
) (
  input                         clk,
  input                         rst,
  input                         valid_in,
  input [8:0]                   phase_step,
  input      signed [15:0]      i_in,
  input      signed [15:0]      q_in,
  output reg                    valid_out,
  output reg signed [15:0]      i_out,
  output reg signed [15:0]      q_out,
  output reg        [32:0]      power_out
);

  localparam integer ACC_W = 56;
  wire [8:0] wrap_at = 9'd500 - phase_step;

  // Two independent single-port synchronous ROMs.
  // Duplicating 500x16 is cheap on XC7Z020 and avoids a shared address/decode
  // network with >100 fanout seen in v03.
  (* rom_style = "block" *)
  reg signed [15:0] sin_rom [0:499];

  (* rom_style = "block" *)
  reg signed [15:0] cos_rom [0:499];

  reg [8:0] phase = 0;

  // A: capture sample + addresses
  (* max_fanout = 16 *) reg [8:0] a_sin_addr=0;
  (* max_fanout = 16 *) reg [8:0] a_cos_addr=0;
  reg signed [15:0] a_i=0, a_q=0;
  reg a_valid=0;

  // B: synchronous BRAM outputs
  reg signed [15:0] b_i=0, b_q=0;
  reg signed [15:0] b_sin=0, b_cos=0;
  reg b_valid=0;

  // C: multiplier pipeline
  reg signed [31:0] c_ic=0, c_qs=0, c_is=0, c_qc=0;
  reg c_valid=0;

  // D: complex mixer pipeline
  reg signed [32:0] d_i=0, d_q=0;
  reg d_valid=0;

  reg signed [ACC_W-1:0] i_int1=0,i_int2=0,i_int3=0;
  reg signed [ACC_W-1:0] q_int1=0,q_int2=0,q_int3=0;
  reg v_int1=0,v_int2=0,v_int3=0;
  reg [5:0] decim_count=0;

  reg signed [ACC_W-1:0] i_d1=0,i_d2=0,i_d3=0;
  reg signed [ACC_W-1:0] q_d1=0,q_d2=0,q_d3=0;
  reg signed [ACC_W-1:0] c1_i=0,c1_q=0,c2_i=0,c2_q=0,c3_i=0,c3_q=0;
  reg v_c1=0,v_c2=0,v_c3=0;

  wire signed [ACC_W-1:0] d_i_ext = {{(ACC_W-33){d_i[32]}},d_i};
  wire signed [ACC_W-1:0] d_q_ext = {{(ACC_W-33){d_q[32]}},d_q};

  wire signed [ACC_W-1:0] scaled_i_w = c3_i >>> SHIFT;
  wire signed [ACC_W-1:0] scaled_q_w = c3_q >>> SHIFT;
  wire signed [15:0] scaled_i = scaled_i_w[15:0];
  wire signed [15:0] scaled_q = scaled_q_w[15:0];

  wire [15:0] abs_i = scaled_i[15] ? (~scaled_i + 16'd1) : scaled_i;
  wire [15:0] abs_q = scaled_q[15] ? (~scaled_q + 16'd1) : scaled_q;
  wire [16:0] mag = {1'b0,abs_i}+{1'b0,abs_q};

  initial begin
    $readmemh(MEMFILE, sin_rom);
    $readmemh(MEMFILE, cos_rom);
  end

  always @(posedge clk) begin
    if (rst) begin
      phase<=0;
      a_sin_addr<=0;a_cos_addr<=0;a_i<=0;a_q<=0;a_valid<=0;
      b_i<=0;b_q<=0;b_sin<=0;b_cos<=0;b_valid<=0;
      c_ic<=0;c_qs<=0;c_is<=0;c_qc<=0;c_valid<=0;
      d_i<=0;d_q<=0;d_valid<=0;
      i_int1<=0;i_int2<=0;i_int3<=0;q_int1<=0;q_int2<=0;q_int3<=0;
      v_int1<=0;v_int2<=0;v_int3<=0;decim_count<=0;
      i_d1<=0;i_d2<=0;i_d3<=0;q_d1<=0;q_d2<=0;q_d3<=0;
      c1_i<=0;c1_q<=0;c2_i<=0;c2_q<=0;c3_i<=0;c3_q<=0;
      v_c1<=0;v_c2<=0;v_c3<=0;
      i_out<=0;q_out<=0;power_out<=0;valid_out<=0;
    end else begin
      valid_out <= 1'b0;

      a_valid <= valid_in;
      if (valid_in) begin
        a_i <= i_in;
        a_q <= q_in;
        a_sin_addr <= phase;

        if (phase >= 9'd375)
          a_cos_addr <= phase - 9'd375;
        else
          a_cos_addr <= phase + 9'd125;

        if (phase >= wrap_at)
          phase <= phase - wrap_at;
        else
          phase <= phase + phase_step;
      end

      // Single-port synchronous ROM reads.
      b_valid <= a_valid;
      if (a_valid) begin
        b_i <= a_i;
        b_q <= a_q;
        b_sin <= sin_rom[a_sin_addr];
        b_cos <= cos_rom[a_cos_addr];
      end

      c_valid <= b_valid;
      if (b_valid) begin
        c_ic <= b_i * b_cos;
        c_qs <= b_q * b_sin;
        c_is <= b_i * b_sin;
        c_qc <= b_q * b_cos;
      end

      d_valid <= c_valid;
      if (c_valid) begin
        d_i <= $signed({c_ic[31],c_ic}) - $signed({c_qs[31],c_qs});
        d_q <= $signed({c_is[31],c_is}) + $signed({c_qc[31],c_qc});
      end

      v_int1 <= d_valid;
      if (d_valid) begin
        i_int1 <= i_int1 + d_i_ext;
        q_int1 <= q_int1 + d_q_ext;
      end

      v_int2 <= v_int1;
      if (v_int1) begin
        i_int2 <= i_int2 + i_int1;
        q_int2 <= q_int2 + q_int1;
      end

      v_int3 <= v_int2;
      if (v_int2) begin
        i_int3 <= i_int3 + i_int2;
        q_int3 <= q_int3 + q_int2;
      end

      v_c1 <= 1'b0;
      if (v_int3) begin
        if (decim_count == DECIM-1) begin
          decim_count <= 0;
          c1_i <= i_int3 - i_d1;
          c1_q <= q_int3 - q_d1;
          i_d1 <= i_int3;
          q_d1 <= q_int3;
          v_c1 <= 1'b1;
        end else begin
          decim_count <= decim_count + 1'b1;
        end
      end

      v_c2 <= v_c1;
      if (v_c1) begin
        c2_i <= c1_i - i_d2;
        c2_q <= c1_q - q_d2;
        i_d2 <= c1_i;
        q_d2 <= c1_q;
      end

      v_c3 <= v_c2;
      if (v_c2) begin
        c3_i <= c2_i - i_d3;
        c3_q <= c2_q - q_d3;
        i_d3 <= c2_i;
        q_d3 <= c2_q;
      end

      if (v_c3) begin
        i_out <= scaled_i;
        q_out <= scaled_q;
        power_out <= {{16{1'b0}},mag};
        valid_out <= 1'b1;
      end
    end
  end
endmodule
