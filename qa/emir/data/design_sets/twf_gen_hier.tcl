# twf_gen_hier.tcl —— OpenSTA → Innovus 格式 TWF 生成器（design_sets 族）
#
# 与 data/timing/twf_gen.tcl 同源的语义映射（arrival→RTW / slew→Rt /
# slack→RSlk / clocks 属性→CAUSED_BY / C|D 标记；上游缺陷绕行内置：
# report_checks 预热 + 字面 NULL 回退 + waveform 周期一半兜底），差异点：
#   - 多文件 Verilog 读入（层级网表：块定义文件 + 顶层文件，顺序任意）；
#   - 引脚枚举 pattern 参数化——层级顶层用 get_pins -hierarchical *
#     （get_pins */* 只匹配两段，2026-09-15 实测）；
#   - 输出前缀独立参数（顶层与块会话共用本脚本）。
#
# 用法（cwd = 本目录；环境变量驱动）：
#   TWF_LIB       Liberty 路径（缺省 ../timing/NangateOpenCellLibrary_typical.lib）
#   TWF_V_FILES   Verilog 文件列表（空格分隔；缺省 <top>.v 单文件）
#   TWF_TOP       顶层模块名（必传）
#   TWF_SDC       SDC 路径（缺省 <top>.sdc）
#   TWF_OUT_BASE  输出文件名前缀（缺省 = TWF_TOP）
#   TWF_ONLY      输出维度 NET / PIN / "NET PIN"（缺省两者→三份全出）
#   TWF_PIN_HIER  =1 时 get_pins -hierarchical *（层级顶层；缺省 */*）
#   TWF_SKIP_READ =1 跳过读入（外层驱动已加载设计）
#   sta -no_init -exit twf_gen_hier.tcl

proc env_or {key default} {
    if {[info exists ::env($key)]} { return $::env($key) }
    return $default
}
set top_name     [env_or TWF_TOP ""]
if {$top_name eq ""} { error "twf_gen_hier: TWF_TOP required" }
set twf_prefix   [env_or TWF_OUT_BASE $top_name]
set twf_skipread [env_or TWF_SKIP_READ 0]
set twf_only     [env_or TWF_ONLY {NET PIN}]
set twf_pin_hier [env_or TWF_PIN_HIER 0]

if {$twf_skipread ne "1"} {
    read_liberty [env_or TWF_LIB ../timing/NangateOpenCellLibrary_typical.lib]
    foreach vf [env_or TWF_V_FILES ${top_name}.v] { read_verilog $vf }
    link_design $top_name
    read_sdc [env_or TWF_SDC ${top_name}.sdc]
}

# —— 上游缺陷绕行（同 twf_gen.tcl：pinArrival 缺 ensureGraph 的 SIGSEGV）——
report_checks -path_delay min_max -format short -digits 4
catch {get_property [lindex [get_pins */*] 0] slew_min_rise}

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
proc okv {v} {
    if {$v eq ""} { return 0 }
    if {[catch {expr {abs($v) < 1e30}} r]} { return 0 }
    return $r
}
proc rng {lo hi} {
    if {![okv $lo] || ![okv $hi]} { return "*" }
    return "[f6 $lo]:[f6 $hi]"
}
proc scalar {v} {
    if {![okv $v]} { return "*" }
    return "[f6 $v]"
}
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
    set nm ""
    catch {set nm [get_full_name $pin]}
    if {$nm eq ""} { return "" }
    set nn ""
    catch {set nn [get_nets $nm]}
    if {[llength $nn] == 0} { return "" }
    return [get_full_name [lindex $nn 0]]
}

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

set all_entries {}
foreach net [get_nets *] {
    set nname [get_full_name $net]
    if {$nname eq "VDD" || $nname eq "VSS"} { continue }
    set src_pin ""
    if {[info exists clock_src_pin($nname)]} { set src_pin $clock_src_pin($nname) }
    set driver [net_driver $net $src_pin]
    if {$driver eq ""} { continue }
    lappend all_entries [list NET $nname $driver $nname]
}
if {$twf_pin_hier eq "1"} {
    set pin_list [get_pins -hierarchical *]
} else {
    set pin_list [get_pins */*]
}
foreach pin $pin_list {
    set pname [get_full_name $pin]
    if {[string match "*/VDD" $pname] || [string match "*/VSS" $pname]} { continue }
    set nname [pin_net $pin]
    lappend all_entries [list PIN $pname $pin $nname]
}

proc entry_line {e} {
    global clock_nets
    lassign $e kind name obj netname
    set rtw [rng [prop $obj arrival_min_rise] [prop $obj arrival_max_rise]]
    set rt  [rng [prop $obj slew_min_rise] [prop $obj slew_max_rise]]
    set rslk [scalar [prop $obj slack_min_rise]]
    set ftw [rng [prop $obj arrival_min_fall] [prop $obj arrival_max_fall]]
    set ft  [rng [prop $obj slew_min_fall] [prop $obj slew_max_fall]]
    set fslk [scalar [prop $obj slack_min_fall]]
    set flag D
    if {$netname ne "" && [lsearch -exact $clock_nets $netname] >= 0} {
        set flag C
    }
    return "($kind \"$name\" $rtw $rt * $rslk $ftw $ft * $fslk $flag)"
}

proc write_twf {fname kinds} {
    global top_name all_entries
    array set groups {}
    foreach e $all_entries {
        set kind [lindex $e 0]
        if {[lsearch -exact $kinds $kind] < 0} { continue }
        set obj [lindex $e 2]
        set clks [prop $obj clocks]
        set cname ""
        if {[llength $clks] > 0} { set cname [safe_name [lindex $clks 0]] }
        if {$cname eq ""} {
            lappend groups(_null_) $e
        } else {
            lappend groups($cname) $e
        }
    }
    set fh [open $fname w]
    puts $fh "(TIMING_WINDOWS"
    puts $fh "(HEADER"
    puts $fh "(VERSION \"fly-twf-gen 1.2\")"
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
    foreach clk [all_clocks] {
        set cname [safe_name $clk]
        if {$cname ne "" && [info exists groups($cname)]} {
            puts $fh "(CAUSED_BY \"$cname\" RISE"
            foreach e $groups($cname) { puts $fh [entry_line $e] }
            puts $fh ")"
        }
    }
    if {[info exists groups(_null_)]} {
        puts $fh "(CAUSED_BY NULL"
        foreach e $groups(_null_) { puts $fh [entry_line $e] }
        puts $fh ")"
    }
    puts $fh ")"
    close $fh
    puts "twf_gen_hier: $fname written ([llength $all_entries] entries collected)"
}

if {[lsearch -exact $twf_only NET] >= 0} {
    write_twf ${twf_prefix}.twf {NET}
}
if {[lsearch -exact $twf_only PIN] >= 0} {
    write_twf ${twf_prefix}_pins.twf {PIN}
}
if {[lsearch -exact $twf_only NET] >= 0 && [lsearch -exact $twf_only PIN] >= 0} {
    write_twf ${twf_prefix}_mixed.twf {NET PIN}
}
