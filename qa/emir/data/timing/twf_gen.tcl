# twf_gen.tcl —— OpenSTA → Innovus 格式 TWF 生成器（fly timing db 测试数据，途径二）
#
# 用法（cwd = 本数据目录）：
#   sta -no_init -exit twf_gen.tcl
# 输入：NangateOpenCellLibrary_typical.lib + tm_design.v + tm_design.sdc
# 产物：tm_design.twf（Innovus write_timing_windows 手册 25.10 版格式；
#       时间值 ns，TIME_SCALE 1.000E-09）
#
# 语义映射（OpenSTA 引脚属性 → TWF 八对字段）：
#   arrival_min/max_rise → RTW；arrival_min/max_fall → FTW
#   slew_min/max_rise    → Rt； slew_min/max_fall    → Ft
#   slack_min_rise/fall  → RSlk/FSlk（单标量形态）
#   RDr/FDr（源电阻）→ *（OpenSTA 无对应属性，fly 解析器弃收该列）
#   驱动引脚 clocks 属性 → CAUSED_BY 分组（时钟名，缺省 NULL 组）
#
# 已知局限（OpenSTA 3.1.0 master Tcl 属性面）：
#   - 时钟对象无 waveform 属性 → POSEDGE 0 / NEGEDGE 周期一半兜底
#     （等价于 create_clock 缺省波形）；
#   - 顶层输入端口对象的属性面缺 arrival → 普通输入端口驱动网的窗口列
#     发 *（缺省值本身即解析器需覆盖的形态；clocks 属性同缺 → 归 NULL
#     组）；slew/slack 端口可查、照常输出；
#   - 实例数据引脚的 clocks 属性为空 → 数据网归 NULL 组（Innovus 按
#     路径时钟归组，此处语义弱化——单时钟设计无影响，README 已记）。

set lib_file  NangateOpenCellLibrary_typical.lib
set ver_file  tm_design.v
set top_name  tm_design
set sdc_file  tm_design.sdc
set out_file  tm_design.twf

read_liberty $lib_file
read_verilog $ver_file
link_design $top_name
read_sdc $sdc_file

# —— 上游缺陷绕行（OpenSTA 3.1.0 master，2026-09-15 实测）——
# Properties::pinArrival 缺 ensureGraph()（pinSlew 有），直接查 arrival 属性
# 在 Graph::pinVertices 内 SIGSEGV；且 arrival 需要已完成的 min/max 路径
# 搜索。绕法：report_checks 先完成双向时序搜索 + 任意引脚 slew 查询一次
# 触发图构建，之后全部属性查询安全。
report_checks -path_delay min_max -format short -digits 4
catch {get_property [lindex [get_pins */*] 0] slew_min_rise}

# 属性安全读取（无值/不支持/返回字面 NULL → 空串）
proc prop {obj p} {
    if {[catch {set v [get_property $obj $p]}]} { return "" }
    if {$v eq "NULL"} { return "" }
    return $v
}
proc safe_name {obj} {
    if {[catch {set n [get_name $obj]}]} { return "" }
    return $n
}
proc f6 {v} { format "%.6f" $v }
# 数值有效性：空/非有限（inf/NaN，如无约束路径的 slack）→ 视为缺失
proc okv {v} {
    if {$v eq ""} { return 0 }
    if {[catch {expr {abs($v) < 1e30}} r]} { return 0 }
    return $r
}
# min:max 对；任一侧缺失 → 整字段 *
proc rng {lo hi} {
    if {![okv $lo] || ![okv $hi]} { return "*" }
    return "[f6 $lo]:[f6 $hi]"
}
proc scalar {v} {
    if {![okv $v]} { return "*" }
    return "[f6 $v]"
}

# 网驱动引脚：优先 output 方向引脚；无则顶层 input 端口。端口引脚取法：
# 时钟源网用时钟 sources 属性返回的 Pin 句柄（顶层端口本质是 Pin，该句柄
# 属性面完整）；其余端口网用 get_ports（属性面缺 arrival——窗口列发 *，
# 详见文件头「局限」）
proc net_driver {net clock_source_pin} {
    foreach pin [get_pins -of_objects $net] {
        if {[prop $pin direction] eq "output"} { return $pin }
    }
    if {$clock_source_pin ne ""} { return $clock_source_pin }
    set fallback ""
    catch {set fallback [get_ports -quiet [get_full_name $net]]}
    return $fallback
}
proc pin_net {pin} {
    set nets ""
    catch {set nets [get_nets -of_objects $pin]}
    if {$nets ne "" && $nets ne "NULL"} {
        return [get_full_name [lindex $nets 0]]
    }
    # 兜底：按引脚名查网（时钟源端口引脚 -of_objects 返回字面 NULL）
    set nm ""
    catch {set nm [get_full_name $pin]}
    if {$nm eq ""} { return "" }
    set nn ""
    catch {set nn [get_nets $nm]}
    if {[llength $nn] == 0} { return "" }
    return [get_full_name [lindex $nn 0]]
}

# 时钟源网络（标 C 类；其余 D 类）+ 源 Pin 句柄表（顶层端口的完整属性面句柄）
set clock_nets {}
array set clock_src_pin {}
foreach clk [all_clocks] {
    foreach src [prop $clk sources] {
        set n [pin_net $src]
        if {$n ne ""} {
            lappend clock_nets $n
            set clock_src_pin($n) $src
        }
    }
}

# 逐网收集（时钟名 → 网条目列表）
array set groups {}
set null_group {}
foreach net [get_nets *] {
    set nname [get_full_name $net]
    set src_pin ""
    if {[info exists clock_src_pin($nname)]} { set src_pin $clock_src_pin($nname) }
    set driver [net_driver $net $src_pin]
    if {$driver eq ""} { continue }
    set entry [list $nname $driver]
    set clks [prop $driver clocks]
    if {[llength $clks] > 0} {
        set cname [safe_name [lindex $clks 0]]
        lappend groups($cname) $entry
    } else {
        lappend null_group $entry
    }
}

set fh [open $out_file w]
puts $fh "(TIMING_WINDOWS"
puts $fh "(HEADER"
puts $fh "(VERSION \"fly-twf-gen 1.0\")"
puts $fh "(DESIGN \"$top_name\")"
puts $fh "(DELIMITERS \"/\[\]\")"
puts $fh "(TIME_SCALE 1.000E-09)"
puts $fh ")"

foreach clk [all_clocks] {
    set cname [safe_name $clk]
    set period [prop $clk period]
    set wf [prop $clk waveform]
    set pos 0.0
    set neg 0.0
    if {[llength $wf] >= 2} {
        set pos [lindex $wf 0]
        set neg [lindex $wf 1]
    } else {
        # create_clock 缺省波形 {0, 周期/2}（时钟对象无 waveform 属性时的等价兜底）
        set neg [expr {$period / 2.0}]
    }
    puts $fh "(WAVEFORM \"$cname\" [f6 $period] (POSEDGE [f6 $pos]) (NEGEDGE [f6 $neg]))"
}

proc emit_group {fh cname entries clock_nets} {
    if {[llength $entries] == 0} { return }
    if {$cname eq ""} {
        puts $fh "(CAUSED_BY NULL"
    } else {
        puts $fh "(CAUSED_BY \"$cname\" RISE"
    }
    foreach e $entries {
        lassign $e nname driver
        set rtw [rng [prop $driver arrival_min_rise] [prop $driver arrival_max_rise]]
        set rt  [rng [prop $driver slew_min_rise] [prop $driver slew_max_rise]]
        set rslk [scalar [prop $driver slack_min_rise]]
        set ftw [rng [prop $driver arrival_min_fall] [prop $driver arrival_max_fall]]
        set ft  [rng [prop $driver slew_min_fall] [prop $driver slew_max_fall]]
        set fslk [scalar [prop $driver slack_min_fall]]
        set flag D
        if {[lsearch -exact $clock_nets $nname] >= 0} { set flag C }
        puts $fh "(NET \"$nname\" $rtw $rt * $rslk $ftw $ft * $fslk $flag)"
    }
    puts $fh ")"
}

foreach clk [all_clocks] {
    set cname [safe_name $clk]
    if {[info exists groups($cname)]} {
        emit_group $fh $cname $groups($cname) $clock_nets
    }
}
emit_group $fh "" $null_group $clock_nets

puts $fh ")"
close $fh
puts "twf_gen: $out_file written"
