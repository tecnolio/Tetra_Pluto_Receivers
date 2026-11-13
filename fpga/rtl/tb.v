`timescale 1ns/1ps
module tb;
reg clk=0;always #5 clk=~clk;reg rst=1,valid=0;reg signed[15:0] i=0,q=0;wire vo;wire signed[15:0] io,qo;wire[15:0] m0,m1;
reg[127:0] cfg=0;reg[31:0] tones[0:499];integer n,j,records=0,fail=0,slot,seq,aux,count,expected,primary;reg single=0,dl_single=0;
tetra_channelizer #(.AUX_HOLD_FRAMES(64)) dut(clk,rst,valid,cfg,i,q,vo,io,qo,m0,m1);
always @(posedge clk)if(vo)begin
 slot=records%(count+2);seq=(records/(count+2))%256;aux=((records/(count+2))/64)%count;
 if(m0!==(16'ha700|(aux<<4)|slot)||m1!==(16'h5c00|seq))begin fail=fail+1;if(fail<6)$display("metadata %d %h %h",records,m0,m1);end
 if(records>120*(count+2)&&slot<count+1)begin
  expected=dl_single ? (slot==count&&primary==5 ? 1220 : 0) : single&&slot!=0 ? 0 : 1220;
  if(io<expected-40||io>expected+40||qo< -40||qo>40)begin fail=fail+1;if(fail<6)$display("tone %d slot%d %d %d expected%d",records,slot,io,qo,expected);end
 end
 if(records>120*(count+2)&&slot==count+1&&((records/(count+2))%64)>40)begin
  if((dl_single&&aux!=5)||single ? (io*io+qo*qo)>1600 : ((io*io+qo*qo)<1392400||(io*io+qo*qo)>1587600))begin fail=fail+1;if(fail<6)$display("aux power frame%d aux%d I%d Q%d",records/(count+2),aux,io,qo);end
 end
 records=records+1;
end
initial begin
 primary=0;if($value$plusargs("primary=%d",primary))begin end
 count=8;if($value$plusargs("count=%d",count))begin end
 dl_single=$test$plusargs("dl_single");single=$test$plusargs("single");if(dl_single)$readmemh("tones_dl5.mem",tones);else if(single)$readmemh("tones_single.mem",tones);else $readmemh("tones_all.mem",tones);
 cfg=128'h0000000000014c542c180c668b5dc4e8;cfg[84:81]=count;cfg[85]=1;cfg[88:86]=primary;
 repeat(10)@(negedge clk);rst=0;
 for(n=0;n<60000;n=n+1)begin @(negedge clk);valid=1;i=tones[n%500][31:16];q=tones[n%500][15:0];for(j=0;j<3;j=j+1)begin @(negedge clk);valid=0;end end
 @(negedge clk);valid=0;repeat(600)@(negedge clk);$display("RESULT records=%d failures=%d count=%d single=%d",records,fail,count,single);if(records!=600*(count+2)||fail!=0)$fatal(1,"profile channelizer simulation failed");$finish;
end
endmodule
