# 电脑智商优化

面向电脑势力的战术、行为树与外交 AI 增强：

- 原有功能（[清都jsl](https://space.bilibili.com/3690994744691528)）：优化攻城阵型配兵、动态同盟与敌对主敌评估、智能水域防御调度、应急增援重调、敌方攻城器械时间窗截击与电脑施政自动排序。
- 移植与融合增强的 8 项电脑 AI 行为树模块（移植来源：[coocookie123/SAN14-Optimizer-Launcher](https://github.com/coocookie123/SAN14-Optimizer-Launcher)，MIT License）：
  1. **出兵编成**（`bt_deploy`）：迎击出兵按强将平分兵力，自动配属锋矢侧翼与雁行辅助。
  2. **集结齐进**（`bt_rally`）：迎击与攻城多路军势同步会合、近处晚出与防先头部队落单送死。
  3. **会战协同**（`bt_engage`）：近战按负载分流避免扎堆、锋矢优先突击远程/器械，并令贴敌待命部队还手。
  4. **野战撤退**（`bt_retreat`）：迎击军势在野外被敌力明显压制时整军势协同撤退并统一回城目标。
  5. **雁行站位**（`bt_stance`）：电脑雁行保持 2 格射击距离，被贴脸时优先撤至友军盾后安全格。
  6. **城下迎击**（`bt_walls`）：城下放开迎击限制，整合水上战力折算、半渡而击、港口交换比决策与援军留守。
  7. **攻城评估**（`bt_siegeplan`）：攻城决策折算邻城援军与府加成、兵力不足不强攻，并先占周边府及保护粮道。
  8. **守关调度**（`bt_gate`）：受攻关隘仅派雁行据关射击、近战回城防排队，并豁免隔关不可达敌军触发的误撤退。

版本 `0.3.0`，内部 ID `ai-iq-mod`。本目录属于 `default` 分组。

从仓库根目录构建与打包：

```powershell
./build.ps1 -Target mods -Mod ai-iq-mod -Tests -ArtifactRoot build/ai-iq-mod
```

## 作者

- 第一作者：[清都jsl](https://space.bilibili.com/3690994744691528)
- 第二作者：[黑色曲奇](https://steamcommunity.com/profiles/76561198090135353)（8 项电脑 AI 行为树模块原始设计）

## 第三方许可（MIT License）

本 Mod 中的 8 项电脑 AI 行为树策略模块移植并适配自 [SAN14-Optimizer-Launcher](https://github.com/coocookie123/SAN14-Optimizer-Launcher)，遵循 MIT 许可证：

```text
MIT License

Copyright (c) 2026 coocookie123

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
