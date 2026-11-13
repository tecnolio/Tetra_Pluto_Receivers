`timescale 1ns/100ps
module tetra_channelizer #(parameter AUX_HOLD_FRAMES=250000)(input clk,input rst,input valid_in,input signed[15:0] i_in,q_in,output reg valid_out,output reg signed[15:0] i_out,q_out,output reg[15:0] meta0_out,meta1_out);
 wire[9:0] v;wire signed[15:0] fi[0:9],fq[0:9];
 wire cv0;wire signed[15:0] ci0,cq0;wire[32:0] p0;
 tetra_ddc_cic_lane #(.PHASE_STEP(220)) lane0(clk,rst,valid_in,i_in,q_in,cv0,ci0,cq0,p0);
 tetra_fir_dec2 filter0(clk,rst,cv0,ci0,cq0,v[0],fi[0],fq[0]);
 wire cv1;wire signed[15:0] ci1,cq1;wire[32:0] p1;
 tetra_ddc_cic_lane #(.PHASE_STEP(214)) lane1(clk,rst,valid_in,i_in,q_in,cv1,ci1,cq1,p1);
 tetra_fir_dec2 filter1(clk,rst,cv1,ci1,cq1,v[1],fi[1],fq[1]);
 wire cv2;wire signed[15:0] ci2,cq2;wire[32:0] p2;
 tetra_ddc_cic_lane #(.PHASE_STEP(203)) lane2(clk,rst,valid_in,i_in,q_in,cv2,ci2,cq2,p2);
 tetra_fir_dec2 filter2(clk,rst,cv2,ci2,cq2,v[2],fi[2],fq[2]);
 wire cv3;wire signed[15:0] ci3,cq3;wire[32:0] p3;
 tetra_ddc_cic_lane #(.PHASE_STEP(186)) lane3(clk,rst,valid_in,i_in,q_in,cv3,ci3,cq3,p3);
 tetra_fir_dec2 filter3(clk,rst,cv3,ci3,cq3,v[3],fi[3],fq[3]);
 wire cv4;wire signed[15:0] ci4,cq4;wire[32:0] p4;
 tetra_ddc_cic_lane #(.PHASE_STEP(180)) lane4(clk,rst,valid_in,i_in,q_in,cv4,ci4,cq4,p4);
 tetra_fir_dec2 filter4(clk,rst,cv4,ci4,cq4,v[4],fi[4],fq[4]);
 wire cv5;wire signed[15:0] ci5,cq5;wire[32:0] p5;
 tetra_ddc_cic_lane #(.PHASE_STEP(320)) lane5(clk,rst,valid_in,i_in,q_in,cv5,ci5,cq5,p5);
 tetra_fir_dec2 filter5(clk,rst,cv5,ci5,cq5,v[5],fi[5],fq[5]);
 wire cv6;wire signed[15:0] ci6,cq6;wire[32:0] p6;
 tetra_ddc_cic_lane #(.PHASE_STEP(314)) lane6(clk,rst,valid_in,i_in,q_in,cv6,ci6,cq6,p6);
 tetra_fir_dec2 filter6(clk,rst,cv6,ci6,cq6,v[6],fi[6],fq[6]);
 wire cv7;wire signed[15:0] ci7,cq7;wire[32:0] p7;
 tetra_ddc_cic_lane #(.PHASE_STEP(303)) lane7(clk,rst,valid_in,i_in,q_in,cv7,ci7,cq7,p7);
 tetra_fir_dec2 filter7(clk,rst,cv7,ci7,cq7,v[7],fi[7],fq[7]);
 wire cv8;wire signed[15:0] ci8,cq8;wire[32:0] p8;
 tetra_ddc_cic_lane #(.PHASE_STEP(286)) lane8(clk,rst,valid_in,i_in,q_in,cv8,ci8,cq8,p8);
 tetra_fir_dec2 filter8(clk,rst,cv8,ci8,cq8,v[8],fi[8],fq[8]);
 wire cv9;wire signed[15:0] ci9,cq9;wire[32:0] p9;
 tetra_ddc_cic_lane #(.PHASE_STEP(280)) lane9(clk,rst,valid_in,i_in,q_in,cv9,ci9,cq9,p9);
 tetra_fir_dec2 filter9(clk,rst,cv9,ci9,cq9,v[9],fi[9],fq[9]);
 reg signed[15:0] bi[0:6],bq[0:6];reg[2:0] slot=0,aux=1,baux=1;reg[7:0] seq=0,bseq=0;reg active=0;reg[17:0] hold_count=0;integer j;
 always @(posedge clk)begin
  valid_out<=0;
  if(rst)begin slot<=0;aux<=1;baux<=1;seq<=0;bseq<=0;active<=0;hold_count<=0;i_out<=0;q_out<=0;meta0_out<=0;meta1_out<=0;end
  else if(v[0])begin
   for(j=0;j<6;j=j+1)begin bi[j]<=fi[j];bq[j]<=fq[j];end
   bi[6]<=fi[5+aux];bq[6]<=fq[5+aux];baux<=aux;bseq<=seq;seq<=seq+1'b1;slot<=0;active<=1;
   if(hold_count==AUX_HOLD_FRAMES-1)begin hold_count<=0;aux<=aux==4?1:aux+1'b1;end else hold_count<=hold_count+1'b1;
  end else if(active)begin
   valid_out<=1;i_out<=bi[slot];q_out<=bq[slot];meta0_out<=16'ha600|{9'd0,baux,1'b0,slot};meta1_out<=16'h5b00|bseq;
   if(slot==6)active<=0;else slot<=slot+1'b1;
  end
 end
endmodule
