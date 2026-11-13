`timescale 1ns/100ps
// Profile-driven 1..8 UL lanes, primary DL and eight settled DL filters and one rotating seed output.
// cfg[71:0]: 8 x 9-bit NCO steps; [80:72]: DL step; [84:81]: UL count; [85]: enable; [88:86]: primary DL index.
module tetra_channelizer #(parameter AUX_HOLD_FRAMES=250000)(input clk,input rst,input valid_in,input [127:0] cfg,input signed[15:0] i_in,q_in,output reg valid_out,output reg signed[15:0] i_out,q_out,output reg[15:0] meta0_out,meta1_out);
 (* ASYNC_REG="TRUE" *) reg[127:0] cfg_meta=0,cfg_sync=0;
 always @(posedge clk)begin cfg_meta<=cfg;cfg_sync<=cfg_meta;end
 wire[3:0] count=cfg_sync[84:81];
 wire reset_lanes=rst||!cfg_sync[85]||count<1||count>8;
 wire[15:0] v;wire signed[15:0] fi[0:15],fq[0:15];
 reg[2:0] aux=0,baux=0;
 genvar n;generate for(n=0;n<16;n=n+1)begin:lanes
  wire[8:0] phase_step;
  if(n<8)assign phase_step=cfg_sync[n*9+:9];
  else begin:downlink
   wire[8:0] ul_phase=cfg_sync[(n-8)*9+:9];
   assign phase_step=ul_phase>=400 ? ul_phase-9'd400 : ul_phase+9'd100;
  end
  wire cv;wire signed[15:0] ci,cq;wire[32:0] power_unused;
  tetra_ddc_cic_lane lane(.clk(clk),.rst(reset_lanes),.valid_in(valid_in),.phase_step(phase_step),.i_in(i_in),.q_in(q_in),.valid_out(cv),.i_out(ci),.q_out(cq),.power_out(power_unused));
  tetra_fir_dec2 filter(clk,reset_lanes,cv,ci,cq,v[n],fi[n],fq[n]);
 end endgenerate
 reg signed[15:0] bi[0:9],bq[0:9];reg[3:0] slot=0;reg[7:0] seq=0,bseq=0;reg active=0;reg[17:0] hold_count=0;integer j;
 always @(posedge clk)begin
  valid_out<=0;
  if(reset_lanes)begin slot<=0;aux<=0;baux<=0;seq<=0;bseq<=0;active<=0;hold_count<=0;i_out<=0;q_out<=0;meta0_out<=0;meta1_out<=0;end
  else if(v[0])begin
   for(j=0;j<8;j=j+1)begin bi[j]<=fi[j];bq[j]<=fq[j];end
   bi[count]<=fi[8+cfg_sync[88:86]];bq[count]<=fq[8+cfg_sync[88:86]];bi[count+1]<=fi[8+aux];bq[count+1]<=fq[8+aux];
   baux<=aux;bseq<=seq;seq<=seq+1'b1;slot<=0;active<=1;
   if(hold_count==AUX_HOLD_FRAMES-1)begin hold_count<=0;aux<=aux==count-1?0:aux+1'b1;end else hold_count<=hold_count+1'b1;
  end else if(active)begin
   valid_out<=1;i_out<=bi[slot];q_out<=bq[slot];meta0_out<=16'ha700|{9'd0,baux,slot};meta1_out<=16'h5c00|bseq;
   if(slot==count+1)active<=0;else slot<=slot+1'b1;
  end
 end
endmodule
