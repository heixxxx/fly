// wf_small_a2 —— waveform db 测试设计（gen_design.py 生成，重跑原样再生成，勿手改）
// 分段 a2：全局 [100,200]ns 平移到 0 起点（count 初值 8=a1 末值，rn 恒 1，有效沿 5..95 共 10 个；时间戳从 0 重新开始——场景 a 偏移对齐测试点）
// 网名/实例名与 design/wf_small_a2.def 逐字一致（名字对齐的确定性来源）。
`timescale 1ns/1ns
module wf_small_a2;
  reg clk;
  reg rn;
  wire [3:0] count;
  wire clk_div2;
  wire n_and, n_xor;
  wire n_cnt0_d, n_cnt1_d, n_cnt2_d, n_cnt3_d;
  wire n_b1, n_b2, n_div_d;
  INV_X1 u_inv_c0 (.A(count[0]), .ZN(n_cnt0_d));
  XOR2_X1 u_xor_c1 (.A(count[0]), .B(count[1]), .Z(n_cnt1_d));
  AND2_X1 u_and_c2 (.A1(count[0]), .A2(count[1]), .ZN(n_b1));
  XOR2_X1 u_xor_c2 (.A(n_b1), .B(count[2]), .Z(n_cnt2_d));
  AND2_X1 u_and_c3a (.A1(n_b1), .A2(count[2]), .ZN(n_b2));
  XOR2_X1 u_xor_c3 (.A(n_b2), .B(count[3]), .Z(n_cnt3_d));
  DFFR_X1 u_cnt0 (.D(n_cnt0_d), .CK(clk), .RN(rn), .Q(count[0]));
  DFFR_X1 u_cnt1 (.D(n_cnt1_d), .CK(clk), .RN(rn), .Q(count[1]));
  DFFR_X1 u_cnt2 (.D(n_cnt2_d), .CK(clk), .RN(rn), .Q(count[2]));
  DFFR_X1 u_cnt3 (.D(n_cnt3_d), .CK(clk), .RN(rn), .Q(count[3]));
  INV_X1 u_inv_div (.A(clk_div2), .ZN(n_div_d));
  DFFR_X1 u_div (.D(n_div_d), .CK(clk), .RN(rn), .Q(clk_div2));
  AND2_X1 u_and1 (.A1(count[3]), .A2(count[2]), .ZN(n_and));
  XOR2_X1 u_xor1 (.A(count[1]), .B(count[0]), .Z(n_xor));

  initial begin
    clk = 1'b0; rn = 1'b1;
    force u_cnt0.q_r = 1'b0;
    force u_cnt1.q_r = 1'b0;
    force u_cnt2.q_r = 1'b0;
    force u_cnt3.q_r = 1'b1;
    force u_div.q_r = 1'b0;
    release u_cnt0.q_r;
    release u_cnt1.q_r;
    release u_cnt2.q_r;
    release u_cnt3.q_r;
    release u_div.q_r;
    #101 $finish;
  end
  always #5 clk = ~clk;
  initial begin
    $dumpfile("part_a2.vcd");
    $dumpvars(0, wf_small_a2);
  end
endmodule
