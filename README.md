# SAN14 Mods

三国志14 Mod 的统一源码仓库。管理器、SDK、MCP Worker 和各 Mod 都在这里修改、测试、提交，一次克隆即可取得完整公共源码，不使用子模块。

```text
manager/                 通用 Host、运行包打包和地址无关的 MCP Worker
sdk/                     SDK 头文件、Schema、示例和插件构建工具
mods/
  default/               优化伴侣、存档数据统计、电脑智商优化、增强字体、增强设定与编辑、MCP
  optional/              一千个曹操、双城全明星对战
tools/                   统一打包工具
tests/                   默认／可选分组和打包检查
```

`default` 表示常规打包范围，各 Mod 的启用偏好仍由各自清单和用户设置决定。`optional` 不进入默认 Mod 包。组件独立版本记录在各自 `mod.json` 或 `project.json`，源码来源提交见 [sources.lock.json](sources.lock.json)。

实际游戏内存地址、版本布局、Hook 和原生命令适配源码位于独立的私有 GameAdapter 仓库。本仓不包含游戏文件、存档或该适配源码。默认构建会从公开 [0.3.0 Release](https://github.com/zhaoqxu/san14-mods/releases/tag/v0.3.0) 下载匹配编译件，不需要私有仓权限。

## 构建

需要 Windows x64、PowerShell 7、MSVC x64、Windows SDK 和 Python 3.12。可用 `-CompilerRoot` 或 `SAN14_MSVC_ROOT` 指定 MSVC 版本目录。

```powershell
git clone https://github.com/zhaoqxu/san14-mods.git
cd san14-mods
py -3.12 -m venv .venv
./.venv/Scripts/python.exe -m pip install -r requirements-build.txt
./build.ps1 -Tests -ArtifactRoot build/first
```

准备好构建依赖后，一次运行 `build.ps1` 即可生成安装器。默认构建管理器、单 MCP Worker 和六个默认 Mod，并自动下载、校验及解压 GameAdapter。

- `build/first/SAN14-ModManager-Setup-<版本>.exe` 内嵌管理器运行包、许可证和六个默认 Mod。MCP 随包安装，默认关闭。
- `build/first/SAN14-runtime.zip`、`SAN14-mods-default.zip` 和 `mods/default/*.s14mod` 供单独使用。

`-IncludeOptional` 额外生成可选 Mod 包，安装器仍只包含默认分组。仅构建组件子集或用 `-Mod` 选择 Mod 时，跳过安装器打包。

固定版本、URL、大小与 SHA-256 在 `manager/adapter.lock.json`，不会自动追踪 `latest`。下载缓存位于 `build/.cache/gameadapter/`，适配包解压到对应 `ArtifactRoot/adapter-package/`。缓存校验失败会停止；删除损坏的缓存 ZIP 后可重新下载。已解压的受管文件损坏时会从已验证缓存修复，额外的用户文件会阻止覆盖。

缓存齐全后可离线构建；也可从 Release 手动下载 ZIP 后提供给脚本：

```powershell
./build.ps1 -Offline -Tests -ArtifactRoot build/first
./build.ps1 -Offline -AdapterArchive './SAN14-GameAdapter-0.1.0-for-0.3.0-win64.zip' -Tests
```

只研究通用 Host、希望跳过二进制获取时，使用 `-SkipGameAdapter`，此时不生成完整运行包和安装器。`-Target mods` 或 `-Target worker` 本身不下载适配器。构建只生成文件，安装时请手动运行 Setup。

只改一个 Mod 时：

```powershell
./build.ps1 -Target mods -Mod mcp-mod -Tests -ArtifactRoot build/mcp-change
```

加入可选曹操 Mod：

```powershell
./build.ps1 -Target mods -Mod officer-temperament-mod -Tests -ArtifactRoot build/temperament
```

同时构建默认与全部可选 Mod：

```powershell
./build.ps1 -IncludeOptional -Tests -ScenarioFile '<你的原版 CE 剧本01文件>' -ArtifactRoot build/with-optional
```

双城制作器只读取用户提供的匹配原始剧本，生成的原生内容位于 `build/`，不进入源码仓。没有原始剧本仍可编辑、构建其它七个原生插件。

曹操 Mod 需要运行时提供 `world.officer_temperament`；SDK 使用兼容的可选表尾接口，旧适配器缺少该能力时明确拒绝加载此可选插件。当前锁定的单 Worker 预览适配器尚不提供该可选服务，不能运行曹操插件；它不影响六个默认 Mod 的构建和使用。此组合的游戏内验收仍待完成。

## 协作

所有贡献必须新开分支，不直接向 `main` 提交：

- **常规需求与修复**：从最新 `main` 新开分支（如 `feat/<主题>`、`fix/<主题>`），在本仓需要的目录修改后向 `main` 提交 PR，跨管理器、SDK 和 Mod 的公共改动可一起审查。
- **未发版共享专题**：参与无头模拟器等大型未入版专题时，从对应的专题基线分支新开子分支，PR 先向该专题分支收口（最多两级堆叠；若顺手修了 `main` 也存在的通用问题，单独基于 `main` 开分支）。
- **跨两仓配套修改**：同时改动本仓与私有 `san14-game-adapter` 时，无论基于 `main` 还是专题分支，**两个仓库新开的分支名必须完全一致**，并在两侧 PR 中互相引用。联调时用 `-AdapterPackage` 传入本地适配包，不在功能分支修改 `manager/adapter.lock.json`。
- **发版收录（Cherry-pick）**：`main` 仅保留稳定发布基线。平时开发提交留在各自线性分支（一个需求整理为一个独立提交或最小有序提交组），发 Release 时由维护者统一 `cherry-pick` 已验收提交进 `main`、更新适配包锁文件并打包发布；分支被收录后不再继续堆叠新需求。

详细流程、修改位置与验证要求见 [CONTRIBUTING.md](CONTRIBUTING.md)。源码合入不代表新的玩家版本已经发布；安装和游戏内验收分别记录。

安装前退出游戏，在 Setup 中选择游戏目录。升级或卸载时 Setup 使用本机安装记录备份、核对和恢复受管文件。先在隔离测试目录验收，再替换实际游戏安装。
