# SAN14 Mod SDK

SDK 位于统一仓库的 `sdk/`，不依赖其它 Git 仓库即可编译插件。版本见 `project.json`，当前为 0.2.1、Host ABI v1。

- `include/`：C ABI 头文件；从 `san14_mod.h` 开始接入。
- `schemas/mod.schema.json`：Mod 清单格式。
- `testing/`：插件 UI 等合成夹具。
- `examples/`：最小插件、存储、页面和内容示例。
- `tools/build_plugin.ps1`：构建原生插件并运行其 C++ 测试。
- `tools/pack_mod.ps1`：单独校验和打包插件。

根目录的 Mod 构建入口默认使用本 SDK。外部插件仍可显式传入 `-SdkRoot`。新能力优先使用领域语义接口；扩展 ABI 时保持前缀并检查结构大小、可选指针和所有者生命周期。

本仓追加了可选的 `San14WorldApi.officer_temperament` 服务，供「一千个曹操」等插件使用；其接口见 `include/san14_officer_temperament.h`。具体内存与版本适配由私有 GameAdapter 提供。旧服务表缺少尾部能力时，插件返回不支持；已有世界快照前缀保持兼容。

SDK 的 memory-reader 示例仅通过权限受控接口验证模块 PE 头，不包含游戏数据布局或固定内存地址。正式功能应使用世界、武将、规则、UI 或自动化等语义接口。

战功和武将事迹中的战斗地点使用可选 [战功统计接口](docs/BATTLEFIELD_STATS.md)。

城市与区域的阵亡统计使用可选 [战斗页签接口](docs/TERRITORY_BATTLE_STATS.md)。
