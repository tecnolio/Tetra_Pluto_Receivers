// ***************************************************************************
// ***************************************************************************
// Copyright (C) 2026 fishball7020-fpga-devkit contributors
//
// Distributed under the GNU General Public License version 2, which can be
// found in the top level directory of this repository (LICENSE), and also
// online at: <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>
//
// This module is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
// ***************************************************************************
// ***************************************************************************

// tx_gpio_bitmap - drive header pins from the low bits of the transmit sample.
//
// WHY THIS EXISTS
// ---------------
// The AD9361's transmit DAC is 12 bits wide, but the sample words you write
// over DMA are 16 bits. The converter takes the top 12 (`dma_data[15:4]`) and
// the bottom 4 (`dma_data[3:0]`) are thrown away inside the FPGA. They cost
// nothing and reach nothing.
//
// This module takes those four discarded bits and puts them on four header
// pins instead. Because they ride along with the sample that produced them,
// each pin transition is locked to the RF that left the antenna port on that
// same sample clock - no drift, no unknown delay. That is what makes them
// usable as a master clock, a frame clock or an external sync line for a
// separate receiver in a multi-channel radar or MIMO setup.
//
// The FPGA does not *generate* anything here; it transports. Whatever pattern
// you author into the low nibble of the samples you transmit is what appears
// on the pins. Alternate bit 0 between 0 and 1 every sample and that pin is a
// square wave at half the sample rate; set bit 1 once every 16 samples and
// that pin is a frame marker. See docs/tx-gpio-bitmap.md.
//
// CONTROL
// -------
// `flag` selects what owns the pins:
//
//   flag = 0   the pins are ordinary EMIO GPIO, driven by Linux (`gpio_*_in`)
//   flag = 1   the fabric claims them and drives the registered sample bits
//
// `flag` comes from bit 1 of the DAC core's GP_CONTROL register
// (`up_dac_gpio_out`, AXI offset 0xBC); bit 0 is already the interpolator
// bypass. It is asynchronous to `clk`, so it is synchronised here.
//
// GAPPED VALID MATTERS
// --------------------
// `sample_in` is only meaningful on clocks where `valid_in` is high - in
// 2R2T mode the datapath presents a new sample every second `l_clk`. The
// nibble is captured on those clocks only and held in between, so a pattern
// authored per SAMPLE comes out per sample. Capturing every clock would
// double the apparent rate and is the mistake the testbench exists to catch.
//
// `valid_in` must mean "a new word is standing at `sample_in` NOW", not "a
// word has been requested". In this design it is
// `fifo_rd_valid | fifo_rd_underflow` from util_upack2, which is exactly
// that; `fifo_rd_en` is one clock too early and would hand the pins the
// previous sample forever. See system_bd.tcl.
//
// LATENCY
// -------
// The nibble reaches the pin one clock after the sample leaves the unpacker.
// The matching RF leaves the antenna port much later - through the fabric
// interpolator, the AD9361's own digital filters and the DAC. So the pins
// LEAD the RF by a fixed offset. Fixed is what matters: it does not drift and
// it repeats run to run for a given configuration, so it can be calibrated
// out once. It is not zero, and nothing here pretends otherwise.

`timescale 1ns/100ps

module tx_gpio_bitmap #(

  parameter integer NBITS = 4
) (

  input                    clk,
  input                    rst,

  // datapath: the raw DMA sample and its valid strobe.
  //
  // X_INTERFACE_IGNORE keeps Vivado's IP packager from deciding these ports
  // are a bus. Left alone it sees a vector next to a port whose name ends in
  // _valid, infers a data/valid signal interface, and turns the port into an
  // interface pin - at which point ad_connect refuses to wire a plain slice
  // output to it ("Cannot connect non-interface to interface"). The names
  // below also follow ad_fs4_ddc's valid_in/*_in convention, which packages
  // as plain pins. Belt and braces: this costs one build to discover.

  (* X_INTERFACE_IGNORE = "true" *) input   [NBITS-1:0]  sample_in,
  (* X_INTERFACE_IGNORE = "true" *) input                valid_in,

  // control: bit 1 of up_dac_gpio_out

  (* X_INTERFACE_IGNORE = "true" *) input                flag,

  // standard-GPIO source, used when flag = 0

  (* X_INTERFACE_IGNORE = "true" *) input   [NBITS-1:0]  gpio_o_in,
  (* X_INTERFACE_IGNORE = "true" *) input   [NBITS-1:0]  gpio_t_in,

  // to the pins (via an ad_iobuf at the top level)

  (* X_INTERFACE_IGNORE = "true" *) output  [NBITS-1:0]  pin_o,
  (* X_INTERFACE_IGNORE = "true" *) output  [NBITS-1:0]  pin_t
);

  reg     [NBITS-1:0]      sample_d = {NBITS{1'b0}};
  reg                      flag_m   = 1'b0;
  reg                      flag_s   = 1'b0;

  // Capture the nibble once per sample, not once per clock.

  always @(posedge clk) begin
    if (rst == 1'b1) begin
      sample_d <= {NBITS{1'b0}};
    end else if (valid_in == 1'b1) begin
      sample_d <= sample_in;
    end
  end

  // Two flip-flops across the clock-domain boundary. The flag is set once by
  // software and then left alone, so all this has to do is keep a metastable
  // level out of the fabric.

  always @(posedge clk) begin
    if (rst == 1'b1) begin
      flag_m <= 1'b0;
      flag_s <= 1'b0;
    end else begin
      flag_m <= flag;
      flag_s <= flag_m;
    end
  end

  assign pin_o = (flag_s == 1'b1) ? sample_d : gpio_o_in;
  assign pin_t = (flag_s == 1'b1) ? {NBITS{1'b0}} : gpio_t_in;

endmodule
