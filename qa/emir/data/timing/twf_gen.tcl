# twf_gen.tcl —— OpenSTA → Innovus 格式 TWF 生成器（fly timing db 测试数据，途径二）
#
# 用法（cwd = 本数据目录）：
#   sta -no_init -exit twf_gen.tcl
# 输入：NangateOpenCellLibrary_typical.lib + tm_design.v + tm_design.sdc
# 产物（一次运行产出三份，均为 Innovus write_timing_windows 手册 25.10 版
#       格式，时间值 ns、TIME_SCALE 1.000E-09）：
#   tm_design.twf        网络维度（Innovus 缺省风味，键 = 网名，值取自驱动引脚）
#   tm_design_pins.twf   引脚维度（-pin 风味，键 = 实例/引脚名，值取自该引脚）
#   tm_design_mixed.twf  混合维度（同一 CAUSED_BY 分组内 NET 与 PIN 条目并存——
#                        解析器必须支持的形态，2026-09-15 裁定）
#
# 语义映射（OpenSTA 引脚属性 → TWF 八对字段）：
#   arrival_min/max_rise → RTW；arrival_min/max_fall → FTW
#   slew_min/max_rise    → Rt； slew_min/max_fall    → Ft
#   slack_min_rise/fall  → RSlk/FSlk（单标量形态）
#   RDr/FDr（源电阻）→ *（OpenSTA 无对应属性，fly 解析器弃收该列）
#   条目引脚 clocks 属性 → CAUSED_BY 分组（时钟名，缺省 NULL 组）
#   C|D 结尾标记：条目所在网属时钟源网络 → C，否则 D
#
# 已知局限（OpenSTA 3.1.0 master Tcl 属性面）：
#   - 时钟对象无 waveform 属性 → POSEDGE 0 / NEGEDGE 周期一半兜底
#     （等价于 create_clock 缺省波形）；
#   - 顶层输入端口对象的属性面缺 arrival → 网络维度中普通输入端口驱动网
#     的窗口列发 *（缺省值本身即解析器需覆盖的形态；引脚维度不含顶层
#     端口条目——get_pins */* 仅匹配实例引脚）；clocks 属性同缺 → 归
#     NULL 组；
#   - 实例数据引脚的 clocks 属性为空 → 数据条目归 NULL 组（Innovus 按
#     路径时钟归组，此处语义弱化——单时钟设计无影响，README 已记）。

# —— 参数化（环境变量；缺省复现 tm_design 三件套，逐字节兼容）——
#   TWF_TOP       顶层模块名（默认 tm_design）
#   TWF_PREFIX    文件名前缀（默认 tm_design；读入 ${prefix}.v/.sdc）
#   TWF_SKIP_READ =1 时跳过读入（设计已由驱动加载——pg_grid_twf.tcl 复用；
#                 驱动须先行完成 read_liberty/read_verilog/link_design/read_sdc
#                 + set_propagated_clock）
#   TWF_ONLY      输出维度（NET / PIN / "NET PIN"；默认两者 → 三件全出）
proc env_or {key default} {
    if {[info exists ::env($key)]} { return $::env($key) }
    return $default
}
set twf_prefix   [env_or TWF_PREFIX tm_design]
set top_name     [env_or TWF_TOP tm_design]
set twf_skipread [env_or TWF_SKIP_READ 0]
set twf_only     [env_or TWF_ONLY {NET PIN}]

if {$twf_skipread ne "1"} {
    read_liberty NangateOpenCellLibrary_typical.lib
    read_verilog ${twf_prefix}.v
    link_design $top_name
    read_sdc ${twf_prefix}.sdc
}

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
# 柄属性面完整）；其余端口网用 get_ports（属性面缺 arrival——窗口列发
# *，详见文件头「局限」）
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

# 时钟源网络（C 类判定）+ 源 Pin 句柄表（顶层端口的完整属性面句柄）
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

# —— 条目收集（统一形态：{kind name obj netname}；kind = NET / PIN）——
set all_entries {}

# 网络维度：逐网取驱动引脚（电源网 VDD/VSS 无时序，不收）
foreach net [get_nets *] {
    set nname [get_full_name $net]
    if {$nname eq "VDD" || $nname eq "VSS"} { continue }
    set src_pin ""
    if {[info exists clock_src_pin($nname)]} { set src_pin $clock_src_pin($nname) }
    set driver [net_driver $net $src_pin]
    if {$driver eq ""} { continue }
    lappend all_entries [list NET $nname $driver $nname]
}

# 引脚维度：逐实例引脚（get_pins */* 不含顶层端口；电源引脚 <inst>/VDD|VSS 不收）
foreach pin [get_pins */*] {
    set pname [get_full_name $pin]
    if {[string match "*/VDD" $pname] || [string match "*/VSS" $pname]} { continue }
    set nname [pin_net $pin]
    lappend all_entries [list PIN $pname $pin $nname]
}

# 单条目输出行（值取自 obj；kind/netname 决定键与 C|D 标记）
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

# 写一份 TWF：kinds = 本份文件收录的条目类别（{NET} / {PIN} / {NET PIN}）
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
    puts $fh "(VERSION \"fly-twf-gen 1.1\")"
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
    puts "twf_gen: $fname written"
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
