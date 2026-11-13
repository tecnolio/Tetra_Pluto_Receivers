`timescale 1ns/100ps
// 47-tap Blackman low-pass, 250 kS/s -> 125 kS/s, Q17.
// One tap per clock. A full MAC finishes within 49 clocks, before the next
// CIC output even at valid_in every clock (CIC decimation 50).
module tetra_fir_dec2(input clk,input rst,input valid_in,input signed[15:0] i_in,q_in,output reg valid_out,output reg signed[15:0] i_out,q_out);
 reg signed[15:0] hi[0:46],hq[0:46];reg[5:0] wr=0,base=0,tap=0;reg dec=0,reading=0,v0=0,v1=0,last0=0,last1=0;
 reg signed[15:0] xi=0,xq=0;reg signed[17:0] co=0;reg signed[33:0] pi=0,pq=0;reg signed[41:0] ai=0,aq=0;
 wire[6:0] address=base>=tap?base-tap:base+7'd47-tap;
 wire signed[41:0] sumi=ai+{{8{pi[33]}},pi},sumq=aq+{{8{pq[33]}},pq};
 wire signed[41:0] oi=sumi>>>17,oq=sumq>>>17;
 function signed[17:0] coefficient(input[5:0] index);begin case(index)
 0:coefficient=18'sd0;
 1:coefficient=18'sd3;
 2:coefficient=18'sd8;
 3:coefficient=18'sd0;
 4:coefficient=-18'sd38;
 5:coefficient=-18'sd107;
 6:coefficient=-18'sd171;
 7:coefficient=-18'sd161;
 8:coefficient=18'sd0;
 9:coefficient=18'sd334;
 10:coefficient=18'sd748;
 11:coefficient=18'sd1014;
 12:coefficient=18'sd836;
 13:coefficient=18'sd0;
 14:coefficient=-18'sd1432;
 15:coefficient=-18'sd2995;
 16:coefficient=-18'sd3864;
 17:coefficient=-18'sd3089;
 18:coefficient=18'sd0;
 19:coefficient=18'sd5420;
 20:coefficient=18'sd12344;
 21:coefficient=18'sd19242;
 22:coefficient=18'sd24337;
 23:coefficient=18'sd26214;
 24:coefficient=18'sd24337;
 25:coefficient=18'sd19242;
 26:coefficient=18'sd12344;
 27:coefficient=18'sd5420;
 28:coefficient=18'sd0;
 29:coefficient=-18'sd3089;
 30:coefficient=-18'sd3864;
 31:coefficient=-18'sd2995;
 32:coefficient=-18'sd1432;
 33:coefficient=18'sd0;
 34:coefficient=18'sd836;
 35:coefficient=18'sd1014;
 36:coefficient=18'sd748;
 37:coefficient=18'sd334;
 38:coefficient=18'sd0;
 39:coefficient=-18'sd161;
 40:coefficient=-18'sd171;
 41:coefficient=-18'sd107;
 42:coefficient=-18'sd38;
 43:coefficient=18'sd0;
 44:coefficient=18'sd8;
 45:coefficient=18'sd3;
 46:coefficient=18'sd0;
 default:coefficient=0;endcase end endfunction
 integer j;initial begin for(j=0;j<47;j=j+1)begin hi[j]=0;hq[j]=0;end end
 always @(posedge clk)begin
  valid_out<=0;
  if(rst)begin wr<=0;base<=0;tap<=0;dec<=0;reading<=0;v0<=0;v1<=0;last0<=0;last1<=0;ai<=0;aq<=0;i_out<=0;q_out<=0;end
  else begin
   v0<=reading;v1<=v0;last0<=reading&&tap==46;last1<=last0;
   if(valid_in)begin
    hi[wr]<=i_in;hq[wr]<=q_in;wr<=wr==46?0:wr+1'b1;dec<=~dec;
    if(dec)begin base<=wr;tap<=0;ai<=0;aq<=0;reading<=1;end
   end
   if(reading)begin xi<=hi[address];xq<=hq[address];co<=coefficient(tap);if(tap==46)reading<=0;else tap<=tap+1'b1;end
   if(v0)begin pi<=xi*co;pq<=xq*co;end
   if(v1)begin
    ai<=sumi;aq<=sumq;
    if(last1)begin
     i_out<=oi>32767?16'sh7fff:oi< -32768?16'sh8000:oi[15:0];q_out<=oq>32767?16'sh7fff:oq< -32768?16'sh8000:oq[15:0];valid_out<=1;
    end
   end
  end
 end
endmodule
