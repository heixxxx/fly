# EMIR 子模块开发规则（通用参考）

> **定位**：emir 模块族下**任意子模块**的开发规则——通用约束，与具体业务无关。lib 库 db（首个立项）中的具体实现仅作为示例出现（标「例」），不是规则本身。后续子模块立项按本规则执行；偏离需在立项方案中显式提出并获裁定。
> 总流程与依赖关系见 [emir-data-flow.md](emir-data-flow.md)；模块结构见 [module.md](module.md)。

---

## 1. 模块结构：三段式

每个子模块是独立完整模块，标准布局（无 C++ 需求的子模块，`cpp/`/`export/` 待需要时补全，结构标准不变）：

```
src/emir/<子模块>/
├── cpp/        # 计算核心（解析器、领域数据结构、算法；C++ 实现）
├── export/     # nanobind 绑定（产物 _fly_emir_<子模块>.so）
├── py/         # 流程编排（六文件，见 §2）
├── tests/      # 单测（bazel cc_test / py_test）+ 测试数据子目录
└── third_party/<解析器>/   # 第三方解析器（如有），见 §4.3
```

### 4.3 第三方解析器引入规则（如有）

- 不使用 Python 解析器；
- 以独立第三方库形式引入，**许可须验证无传染**；上游源码原样保留于 `third_party/`（以便后续优化），以**上游原构建方式**编译出 `.a`/`.so` 签入，bazel 引入预编译库、不在 bazel 内编译上游 C 源；
- fly 侧改动集中在适配层与 `src_local/`（对上游源码的最小补丁须记录于 `src_local/PATCHES.md`）。

## 2. Python 目录构成（六文件规范）

`py/` 目录固定六文件（前缀 = 模块简写小写）：

| 文件 | 内容 |
|------|------|
| `<前缀>_db.py` | 子 db 类型声明（`role` 子类）、header（UserDoc）、校验器（Schema）、**入口函数**（`build_<角色>_db`，`@register_flow(EMIRProject)` 注册至 project）、读取方法 |
| `<前缀>_flow.py` | 具体 db 创建流程执行体：freeze task、MapReduce 各阶段装配、领域对象方法调用 |
| `<前缀>_register_msg.py` | 本模块消息注册。**注册语句直书全局区**（无函数/类包装）——`__init__` 加载完毕即注册完毕，master 与 worker 以相同状态启动 |
| `<前缀>_utils.py` | 本模块内部工具函数（worker 任务函数等）。**不对外导出** |
| `<前缀>_functions.py` | **供其他模块使用的**与本模块相关的 API。仅放其他模块消费本模块 db 数据的接口；**本模块内部流程不得放此处对外**（见 §9） |
| `__init__.py` | 初始化聚合：导出 db/flow/functions 公开符号、副作用加载 register_msg；utils 不导出 |

## 3. Project 归属与 flow 范式

- 全部数据库与创建 API 归属 **EMIRProject**：`from emir import EMIRProject`。
- 建库 API 统一命名 `build_<角色>_db(name, ...)`。
- **直接前驱显式传入**（建数据库链），**间接前置一律经数据库链 `find_db(role=...)` 获取**——不得跨级显式传参。
- **建库 API 流程标准：三段式 + 根任务自包含目录（2026-09-17 用户裁定）**
  ——`build_<角色>_db` 函数体恒为三段：
  1. **轻量参数预处理**：纯参数与元数据——schema 校验（header 框架已自动）+ 文件存在性（`ensure_readable_file`，isfile/access 级）+ 建库 + alpha_settings 写入；**不读文件内容**——文件级错误（不可读/形态错）由文件首个解析任务失败透出，纯参数错误仍提交即 raise；
  2. **提交唯一 flow 根任务**（如 `_timing_flow_task`/`_design_flow_task`）：全部 flow 编排在根任务函数体内声明式呈现——阶段顺序、分派粒度（每文件/每块/每分区）、依赖锚一眼可读；维护者读这一个函数 + 各 task 函数名即可画出完整流程图。深层动态提交仅限「任务图形状运行时才知」场景（注释一行声明）；
  3. **顶层提交 freeze 任务**：freeze 依赖**几个固定标记对象**（权威终态锚——数据规模变化收敛在 finalize/汇总任务里，不随切块/分区数变化；蕴含链必须成立：freeze 不可能早于任一正式产物）；临时键清理清单**静态化**（按输入数量枚举——master 已知；运行时规模的键——分片/冲突/校验结果——清理责任单化到链上任务自清理）。
  **编排四判据**（方案评审与自查逐项过，DEVELOPMENT_GUIDELINES §20 判据的实施面）：
  ① master（提交线程）只做元数据级操作，读文件内容类 IO（嗅探/切块/解析）一律下放 worker 任务并行；② 预处理段只随输入**数量**线性、与输入**字节数**无关；③ 动态提交仅限「形状运行时才知」且注释声明；④ freeze 顶层可见固定标记依赖。
  **task 命名模式**：`<动作>_<对象>[_<粒度>]_task`（如 `_parse_chunk_task`/`_merge_partition_task`/`_plan_file_chunks_task`），与根任务目录逐行对应；编排提交调用按流程顺序直书，不做工厂/间接层包装。
  提交后立即返回 db（异步语义不变）。
- **校验前置（2026-09-17 三段式裁定修订）**：文件存在性/可读性在预处理段（第①段，master 侧）同步拦截——不建库、不起任务；文件内容形态（嗅探/格式）在 worker 任务内发现，文件级错误由该文件首个解析任务失败透出（库不冻结）。
- **建库 API 配置参数标准（2026-09-09 裁定；2026-09-17 裁定修订入口校验范式）**：全部 `build_<角色>_db` 统一追加两个 dict 参数——`settings` 收纳**稳定**的创建流程配置项（不同 key = 不同特殊设置，令用户对创建流程具有一定控制权）；`alpha` 收纳**未稳定**（尚未完全开发完毕）的配置项，成熟后迁入 `settings`。用户面配置一律经这两个参数表达，不再为单个配置项增加独立关键字参数；各子模块在立项方案中定义自己的 settings/alpha 键表、默认值与迁移状态。已实施子模块随后补齐这两个参数（默认空 dict，向后兼容）。
- **入口参数校验范式（2026-09-17 裁定，翻转 2026-09-13 裁定 5 的对外语义）**——header 直接校验、错误直接 raise 终止、白名单严格模式：
  - **header（UserDoc `add_param` 的 Schema）是入口参数的唯一校验声明面**：调用时由 `@document` 装饰器按 schema 校验，失败聚合抛 `ValueError`（含参数名 + 期望 + 实得值）——不走 user message、不提醒忽略、不回退默认继续。
  - **Schema 结构化声明**：一律容器工厂（`Schema.dict` / `Schema.list` / `Schema.any_of`）+ **命名 validator**（`src/emir/common/validators.py` 的 `is_*` 函数库，或各模块 `alpha_settings.py` 的键级 validator），**禁止内联 lambda**。值域单一来源：header schema 与 `AlphaSetting` validator 引用同一函数，改值域只动一处。
  - **白名单严格模式**：dict 一律 `allow_extra=False`（未知键报错）；「当前无可用键」的 settings/alpha 用 `extra_error` 定制文案。未知键不再「提醒忽略」、非法值不再「回退默认继续」。
  - **文件参数显式可读校验**：入口 Step 1 对每个路径调 `ensure_readable_file(path, api_name, param_name)`（不存在 → `FileNotFoundError`；不可读 → `PermissionError`；统一文案含 api 名/参数名/路径）+ 保留既有格式嗅探（`sniff_*_header`）。
  - **描述用户视角**：UserDoc 与 docstring 用户段只写「参数是什么/格式/约束/默认值/合法值集/报错行为」，禁止设计叙事（裁定编号引用、内部文件路径、消息码名、实现内部构件名、迁移历史）。代码内 `#` 实现注释与模块/类 docstring（开发者视角）不受此限。
  - **参数描述禁实现手段（2026-09-17 用户裁定：参数介绍不需要实现细节）**：参数描述四要素——参数是什么（语义）/格式/约束/默认值/合法值集/传错报错行为/结果可预期行为（如冲突数据取舍）；**禁实现手段**：任务、并行、合并、分区、落库组织、快照、对象名、消息码——除非参数本身是该机制的调节钮（如 chunk_size_mb 之于切块：描述效果不描述过程）。
- **alpha 项声明式定义（2026-09-13 裁定；接线语义 2026-09-17 裁定更新）**：每个 alpha 项包含**五要素**——setting 名 / 默认值 / 值类型及约束介绍 / 值校验器（validator）/ setting 介绍。基座在 `src/emir/common/`（`AlphaSetting` 单项描述符 + `AlphaSettings` 基类，纯 Python 包）；各子模块建自己的 `alpha_settings.py` 定义子类（如 design 的 `DSAlphaSettings` 八键）+ 模块级 `get_default_alpha_settings()` 工厂（deepcopy 语义——多次创建同种 db 互不污染默认值，dict 型默认值关键）。建库入口接线（2026-09-17）：header 的 alpha `Schema.dict` 引用各键 validator（值域单一来源）**拦截未知键/非法值直接 raise** → 默认实例 `apply(alpha)` **防御性**覆盖（纯逻辑，schema 拦截后理论不再拒绝；返回 `{"rejected", "unknown"}` 结构）→ settings 对象以固定对象名 `"alpha_settings"` 写入 db → 消费点 `read_object` 读回后 `normalize()` 兜底（旧对象缺键补默认、未知属性丢弃，向前兼容）。原「问题一次汇总 message 提醒（design 复用 DSGN::0013、lib 用 LIBR::0005）」接线已删除：LIBR::0005 / TIMG::0005 注册删除；DSGN::0013 保留注册（C++ S8 分区决策运行期回退仍使用，`ds_partition.cpp`）。

## 4. 实现语言边界

- 4.1 文件解析器一律 **C/C++**，不使用 Python 解析器。
- 4.2 **解析与领域对象构建均在 C++ 进行**：领域类是 C++ 类（含序列化能力），C++ 内构建完成后以对象返回 Python；`write_object` 落库的即 C++ 对象（pickle 协议）。
- 4.3 高频使用、后续算法引擎需要的结构**必须 C++ 实现**（语言间零开销传递）。
- 4.4 MapReduce 各阶段的**处理逻辑在 C++（领域对象自身导出的方法）**，Python 编排层一行调用。
- 4.5 每个输入文件解析产出的独立对象，应**导出自身的方法**供 MapReduce 各阶段调用（例：lib 子模块的解析产物导出 `merge`，供合并阶段两两调用）。

## 5. 命名规范

- C++ 类名 = 模块简写**大写**前缀 + 类名（例：lib 子模块的 `LIBCell`）；
- 独立函数 = 模块简写**小写**前缀 + 下划线动词短语（例：`lib_parse_lib_file`）；
- 模块简写两字母优先、无法准确表达放宽三字母，全库冲突检测通过后方可使用（须避开 export 目录与导出符号 `EX+模块缩写` 体系、`FLY_` 宏、`fly_*` BUILD 目标）；已分配：LIB / TC / DS / PEX / SP / MX / TM / VCD / SW / PWR / CUR / ANS / EM / CM；
- `CM` — emir 公共强类型 id 族（src/emir/common/cpp/emir_ids.h；与 container 模块 CM 容器别名前缀双归属，2026-09-16 裁定——id 族一律 Id 后缀值类型、容器族一律容器类型名，以后缀可辨）；
- Python 绑定产物：`_fly_emir_<子模块>.so`；
- **消息前缀独立于模块简写**，各子模块注册自己的前缀（例：lib 的消息前缀为 `LIBR`）。

## 6. 消息规范

- 面向用户的提示（业务异常提醒、状态通报）使用 **message 系统**（`MSG()` 宏 + 消息 id 注册），不使用裸 print / 裸日志；
- 注册写在 `<前缀>_register_msg.py` 全局区：`fly.register_message_id("<消息前缀>::NNNN", "级别")`；
- 消息 id 格式 `<本模块消息前缀>::NNNN`；每条消息语义（触发场景、级别）在立项方案中定义。

## 7. 异常与健壮性语义（核心规则）

解析/处理阶段**仅两类场景可 raise**：
1. 输入不可读；
2. 输入存在语法/格式错误无法解析。

其余**能正常处理完成的情况，一律不得造成崩溃退出，也不得 raise**——视为业务异常场景，用兜底手段直接处理，并经 message 系统提醒用户。各子模块在立项方案中逐场景定义自己的兜底策略，通用模式包括：

| 兜底模式 | 说明 |
|---------|------|
| 抛弃并提醒 | 违规条目不入库，message 说明条目标识与两处来源（保留首次、抛弃后续为默认策略） |
| 空结果放行 | 处理成功但结果为空：message 提醒 + 返回空容器（下游完整性校验会再暴露） |
| 跳过并计数 | 未收录/不完整的结构：跳过 + 统计（发现遗漏可扩展处理） |
| 静默放行 | 确认为正常现象的形态差异（例：多库混用时库名不一致是正常现象，不是异常，不做任何提示——一致性交由用户判断） |

补充规则：
- 无意义的字段/中间产物**直接删除**，不以"保留以防万一"为由留存；
- 领域对象记录自身**来源可追溯信息**（如来源库名、来源文件完整路径），多源合并后仍可回答"来自哪里"。

### 7.1 第三类处置：不可恢复数据/结构错误 → fatal message（2026-09-12）

上述两类 raise 之外，还存在第三类场景——**不可恢复的数据/结构损坏**（层级树
多根/环、name hasher 权威段损坏、存储校验预算耗尽等）：数据完整性已破坏，
兜底放行只会把坏数据带进下游，此时进程应以 fatal message 退出：

- 处置方式：`MSG_FATAL_EXIT("DSGN::0011", 0, 80, ...)`（C++）/ 
  `fly.fatal_message("DSGN::0011", 0, "...")`（Python）——本地落盘（FATAL 级
  别、立即 flush、豁免配额）后进程以码 80 退出，worker 触发时 master 联动
  fast_exit（失败在途任务 + StopNow 停全部 worker）并同码退出。
- 机制详情见 [docs/message-system.md](../message-system.md) §14「fatal message」；
  退出码 80 保留原因（避开 77=std::terminate / 78=signal）同文 §14.6。
- 已按此处置的场景：DSGN::0011（层级树构建失败，原 raise 改 fatal）、
  DSGN::0012（hasher 权威段损坏）、STOR::0005（存储数据损坏）。
- **编程错误守卫不适用本类**：如 `DSNameHasherT::check_not_sealed` 等接口
  契约违反（`std::logic_error`）保留 throw——那是代码缺陷，不是数据损坏。

### 7.2 流程级错误处理：二元处置范式（2026-09-13）

§7.1 覆盖「不可恢复数据损坏」；对**流程任务**（建库 flow 的解析/汇总任务）
的运行期错误，另按二元处置范式执行——(a) 错误导致后续流程完全无法推进 →
fatal message（码 80）结束整个 run；(b) 可继续 → 任务内兜底、**仍产出下游
依赖的数据对象**（依赖链保持满足）+ error message 提醒。**禁止第三态**
（任务失败且不产出数据、下游依赖断裂的失败悬挂）。判定标准、emir 各场景
处置实例（lib/cell lef 单文件失败兜底、全败/DEF/tech lef fatal）与判死
闭环说明见 [docs/DEVELOPMENT_GUIDELINES.md](../DEVELOPMENT_GUIDELINES.md)
§18「流程级错误处理」。文件不可读属第一类 raise 场景——预处理段同步
拦截（不建库、不起任务）；文件内容形态错（嗅探/解析失败）由文件首个
解析任务失败透出（库不冻结，2026-09-17 三段式裁定口径）。

## 8. 加载语义

- **emir 一次性加载全部子模块**：`import emir` 后全部功能可用；新子模块立项即在 `emir/__init__.py` 追加聚合；
- **fly 启动即 import emir**（main.cpp `setup_sys_path`，在对应 `_fly_*.so` 之后）：master 与每个 worker 开启即可用，且以相同状态启动（消息注册完毕）；
- 消息注册直书全局区：注册完成时机 = `__init__` 加载完毕，无时序差异。

## 9. 职责边界

- 本模块的文件解析/处理是**内部流程**：对外可见面收敛为「建库入口（`build_<角色>_db`）+ 读库 API（`<前缀>_functions.py`）」，处理函数不对外导出；
- 其他模块消费本模块数据一律经数据库链读 db 对象，不重复实现本模块内部流程。

---

## 附：规则溯源

以上规则来自 lib 库 db 立项（2026-09-06/07）开发过程中的用户裁定提炼，与 [emir-data-flow.md](emir-data-flow.md) §4 的架构裁定互补——该文档定"做什么与依赖关系"，本文档定"怎么开发"。lib 立项中的具体兜底语义实例见 [emir-data-flow.md](emir-data-flow.md) 与 lib 子模块实现。
