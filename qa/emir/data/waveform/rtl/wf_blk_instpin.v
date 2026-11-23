// wf_blk_top —— waveform db 测试设计（gen_design.py 生成，重跑原样再生成，勿手改）
// 形态②直配：$dumpvars(1, u_blk_a.u_inv1)——var 名=cell 端口名（A/ZN=LEF pin 名），scope=实例层级路径；net 名未命中路径
// 网名/实例名与 design/wf_blk_top.def 逐字一致（名字对齐的确定性来源）。
`timescale 1ns/1ns
module wf_blk_top;
  reg clk;
  reg rn;
  wire n_top;
  wire n_top2;
  wf_blk_a u_blk_a (.clk(clk), .rn(rn), .n_out(n_top));
  wf_blk_b u_blk_b (.clk(clk), .rn(rn), .n_out(n_top2));

  initial begin
    clk = 1'b0; rn = 1'b0;
    #23 rn = 1'b1;
    #61 $finish;
  end
  always #5 clk = ~clk;
  initial begin
    $dumpfile("instpin.vcd");
    $dumpvars(1, u_blk_a.u_inv1);
  end
endmodule
module wf_blk_a (clk, rn, n_out);
  input clk;
  input rn;
  output n_out;
  wire n_a1;
  wire n_a2;
  DFFR_X1 u_dff_a (.D(n_a2), .CK(clk), .RN(rn), .Q(n_a1));
  INV_X1 u_inv1 (.A(n_a1), .ZN(n_a2));
  INV_X1 u_inv_out (.A(n_a1), .ZN(n_out));
  wf_sub u_sub (.clk(clk), .rn(rn), .n_in(n_a1));
endmodule
module wf_sub (clk, rn, n_in);
  input clk;
  input rn;
  input n_in;
  wire n1;
  wire n2;
  wire n3;
  DFFR_X1 u_dff_s (.D(n2), .CK(clk), .RN(rn), .Q(n1));
  XOR2_X1 u_xor_s (.A(n1), .B(n_in), .Z(n3));
  INV_X1 u_inv_s (.A(n3), .ZN(n2));
endmodule
module wf_blk_b (clk, rn, n_out);
  input clk;
  input rn;
  output n_out;
  wire n_b1;
  wire n_b2;
  DFFR_X1 u_dff_b1 (.D(n_b2), .CK(clk), .RN(rn), .Q(n_b1));
  BUF_X1 u_buf_b (.A(n_b1), .Z(n_b2));
  INV_X1 u_inv_b (.A(n_b1), .ZN(n_out));
endmodule
