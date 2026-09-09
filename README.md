# Fly

Fly 是一个分布式任务执行框架，采用 C++20 核心 + Python 流程控制 + nanobind 桥接的架构，面向大规模数值求解与 EDA 数据处理场景（RAS 稀疏矩阵迭代求解器、EMIR 电迁移分析工具链）。

## 快速开始

```bash
./fly.sh build               # 构建 + 刷新 clangd（禁止直接使用 bazel build）
./fly.sh test                # 单元测试
./fly.sh install             # 创建 build/ 目录并部署 fly 二进制
```

详细构建与测试说明见 [CLAUDE.md](CLAUDE.md)，文档索引见 [docs/README.md](docs/README.md)。

## 许可证（License）

本仓库整体采用 **双许可** 模式：

| 使用方式 | 授权 |
|----------|------|
| 学习、研究、个人与非商业用途 | **免费**，遵循 [AGPL-3.0](LICENSE) |
| 开源使用（衍生作品以 AGPL-3.0 兼容的开源协议发布全部源代码） | **免费**，遵循 [AGPL-3.0](LICENSE) |
| 闭源商用（将本软件或其衍生作品用于闭源商业产品或服务） | **须事先获得商业授权**，联系：[GitHub 个人主页](https://github.com/heixxxx)（请通过主页上的联系方式沟通，或在仓库开 issue 注明「商业授权咨询」） |

即：仓库主体以 [GNU Affero General Public License v3.0（AGPL-3.0）](LICENSE) 授权。您可以免费学习、研究、修改和再分发，但由此产生的衍生作品必须以 AGPL-3.0 开源；若希望闭源商用（包括以未开源修改版本对外提供网络服务），请先取得书面商业授权。

### 第三方代码豁免

以下目录**不受**仓库 AGPL-3.0 声明约束，各自遵循其原始许可证（均为 AGPL-3.0 兼容协议）：

| 目录 | 许可证 |
|------|--------|
| `src/lefdef/` | Apache License 2.0（Cadence LEF/DEF 解析器 fork，修改部分同样按 Apache 2.0 对外，见其目录内 `README.md`） |
| `third_party/lefdef-6.1-mod/` | Apache License 2.0（Cadence 原始声明，见 `lef/LICENSE.TXT`、`def/LICENSE.TXT`） |
| `third_party/sqlite/` | SQLite 公有领域（Public Domain） |

其余通过 Bazel 外部依赖（fmt、nanobind、bitsery、Eigen、googletest、robin_map 等）引入的第三方库不进入本仓库，其许可证随各自发布物分发。
