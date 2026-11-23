// wf_xz —— waveform db 测试设计（gen_design.py 生成，重跑原样再生成，勿手改）
// x/z 统计载体：无复位 DFF 初始 x、TBUF 三态 z、n_f 经 force x/1/z+release
// 网名/实例名与 design/wf_xz.def 逐字一致（名字对齐的确定性来源）。
`timescale 1ns/1ns
module wf_xz;
  reg clk;
  reg en;
  reg n_seed;
  wire n_q;
  wire n_inv;
  wire n_f;
  wire n_tri;
  DFF_X1 u_dff (.D(n_seed), .CK(clk), .Q(n_q));
  INV_X1 u_inv1 (.A(n_q), .ZN(n_inv));
  BUF_X1 u_buf (.A(n_inv), .Z(n_f));
  TBUF_X1 u_tri (.A(n_inv), .EN(en), .Z(n_tri));

  initial begin
    clk = 1'b0; en = 1'b0;
    #8  n_seed = 1'b0;
    #12 n_seed = 1'b1;
    #10 en = 1'b1;
    #10 n_seed = 1'b0;
    #20 en = 1'b0;
    #10 n_seed = 1'b1;
    #31 $finish;
  end
  always #5 clk = ~clk;
  initial begin
    #18 force n_f = 1'bx;
    #20 force n_f = 1'b1;
    #20 force n_f = 1'bz;
    #20 release n_f;
  end
  initial begin
    $dumpfile("xz.vcd");
    $dumpvars(0, wf_xz);
  end
endmodule
