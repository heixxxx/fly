// 确定性小型设计 tm_design（Nangate45 单元，途径二 TWF 生成链的时序引擎输入）：
// 时钟经 CLKBUF 缓冲驱动两 DFF；d 经反相器与 dff1 反馈经 NAND 生成 dff2 数据。
// 网名与 tm_design.def 完全一致（TWF 名字对齐的确定性来源）。
module tm_design (clk, d, q);
  input  clk;
  input  d;
  output q;
  wire nclk;
  wire n1;
  wire q1;
  wire n2;
  CLKBUF_X1 u_cb  (.A(clk), .Z(nclk));
  INV_X1     u_inv(.A(d), .ZN(n1));
  DFF_X1     u_d1 (.D(d), .CK(nclk), .Q(q1));
  NAND2_X1   u_nand(.A1(n1), .A2(q1), .ZN(n2));
  DFF_X1     u_d2 (.D(n2), .CK(nclk), .Q(q));
endmodule
