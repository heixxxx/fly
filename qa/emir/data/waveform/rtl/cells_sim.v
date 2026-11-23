// cells_sim.v —— waveform db 测试用 Nangate45 单元行为模型（手写，非生成产物）
//
// 只覆盖本目录测试设计引用的单元子集；端口名与
// qa/emir/data/timing/NangateOpenCellLibrary.macro.lef 的 PIN 逐字一致
// （VCD 中 cell 实例 scope 下的 var 名 = 端口名 = LEF pin 名——形态②
// inst pin 直配的数据来源）。
// 语义只求功能与四态传播正确（零延迟、无时序检查）：DFFR_X1 异步低有效
// 复位；DFF_X1 无复位、上电 x；三态 TBUF_X1 使能无效时输出 z。
// 内部状态寄存器统一命名 q_r（wf_small_a2 分段续跑经 force/release 注入
// 初值时引用此名，改名会破坏 part_a2.vcd 的确定性）。

`timescale 1ns/1ns

module INV_X1 (input A, output ZN);
  assign ZN = ~A;
endmodule

module BUF_X1 (input A, output Z);
  assign Z = A;
endmodule

module CLKBUF_X1 (input A, output Z);
  assign Z = A;
endmodule

module XOR2_X1 (input A, input B, output Z);
  assign Z = A ^ B;
endmodule

module AND2_X1 (input A1, input A2, output ZN);
  assign ZN = A1 & A2;
endmodule

module DFFR_X1 (input D, input CK, input RN, output Q, output QN);
  reg q_r;
  always @(posedge CK or negedge RN) begin
    if (!RN) q_r <= 1'b0;
    else     q_r <= D;
  end
  assign Q = q_r;
  assign QN = ~q_r;
endmodule

module DFF_X1 (input D, input CK, output Q, output QN);
  reg q_r;   // 无复位——上电 x（xz.vcd 的 TX 段来源）
  always @(posedge CK) q_r <= D;
  assign Q = q_r;
  assign QN = ~q_r;
endmodule

module TBUF_X1 (input A, input EN, output Z);
  assign Z = EN ? A : 1'bz;
endmodule
