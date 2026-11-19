# 自动对局测试

第一版复用原版看海、AI和结算，保留原版画面。当前原生适配范围为简中传承版Build25953386，支持已挂载的双阵营预制剧本；首先使用“双城全明星对战”验证。

先在游戏内正常创建双城局面，保存到一个专用槽，并保留同名`.san14mods`。运行器只读原始起点，不重新制作或覆盖它。开始前启用完全相同的剧本包。普通／换边包的起点不可混用。

## 脚本调用

游戏完成片头，进入可点击的主菜单后运行（不要停在“请点击鼠标”画面）：

```powershell
python tools/run_match.py --save svdexceSC17 --max-turns 360 --output results/match-001.json
```

使用安装包时，在游戏目录运行`python SAN14Base/tools/run_match.py`，其余参数相同。脚本仅需Python标准库。

本机验证使用修正后双城包新建的17号档。旧生成器曾将未启用的特殊人物改成死亡，导致原版拒绝部分旧起点；应保留旧档，用修正包重新建局保存。运行器不改写存档，也不跳过原版DLC校验。

`--save`使用真实文件名去掉`.s14`，不要仅凭UI槽号推算。也可显式使用`--current`从当前已载入的匹配双城筹略地图开始。运行器核对存档本体、旁存绑定、内容指纹、双城身份和加载完成，再调用原版Auto。

`--max-turns`为日期截止，默认360旬；`--wall-timeout`为真实秒数，默认3600；`--stall-timeout`为日期无变化时的等待秒数，默认180。既有结果不会覆盖。截止在可验证的原生筹略边界停住，结果标为截断，不宣布赢家。

同目录会写入`.state.json`和`.jsonl`轨迹。客户端中断或提交回执超时时，使用原有状态文件继续观察：

```powershell
python tools/run_match.py --resume results/match-001.json.state.json --output results/match-001-resumed.json
```

同请求ID与相同参数重试去重。不要在不确定时另发新请求。Ctrl+C请求安全停止；若客户端已断线，使用状态文件继续确认。状态文件含本机进程绑定，只用于本机，不随结果分享。

## 结果解释

- `force_eliminated`：在匹配局面中，原生势力活动判定明确一方不活动、另一方活动，双首府归胜方，败方无城／活动队且无第三方／未知归属。`winner_force_id`给出胜方；不承诺已经观察到原版胜利画面。
- `date_limit`／`wall_time_limit`／`cancelled`：截断，无赢家。以`paused_at_boundary`判断是否已经安全停住。
- `stalled`／`environment_error`／`ended_unverified`：环境未得到可验证结果，不算AI输赢。保留最后观测及原因；未知事件不擅自选择。
- `initial`／`final`包含双方、城池及活动队状态；兵力差包含募兵／伤兵等影响，不等于真实阵亡。
- 没有承诺原生RNG确定性、精确日步进、无图形设备或固定加速倍数。仍保留原版画面、AI和全部战斗计算。

## 对局倍速与采样

```powershell
python tools/run_match.py --save svdexceSC17 --speed 400 --max-turns 36 --output results/speed-400.json
```

`--speed`以百分之一倍为单位，100为原速、400为4倍时钟，范围100..800；省略时继承既有速度，不申请控制权。实际模拟吞吐必须实测，不等于时钟倍率。仅在完成存档／内容校验后取得独占倍速；若其他Mod正在持有倍速，运行器明确报错，先由原控制方释放。例如“优化伴侣 → 常规 → 游戏加速”设为标准1倍。运行期间其他控制方修改倍速返回BUSY。标题／加载维持1倍；显式倍速局结束、取消、错误、owner卸载或适配器故障均释放并回到1倍，不写玩家偏好。省略倍速的旧脚本／旧SDK参数不接管、释放或重设其他控制方的速度。

日常监测只读取当前日期、局身份与原生势力活动状态；全量城池／势力／部队快照默认每1000毫秒采集。`--full-snapshot-interval 0`可恢复每次观察全量采样作对照，合法范围0..5000毫秒。初始状态、正常日期／时间／取消停止均取得新的完整快照；任何一方活动状态异常立即尝试全量核验，终局条件没有放宽。最终采样失败时保持已经取得的停止边界并报告环境错误。

`end_date`是当前轻量监测日期；`final_snapshot_date`和`final.date`是完整快照日期，运行中可能较早，不能把旧快照当作当前世界。`performance`包含观察／全量次数、真实观察耗时及原生状态抽样墙钟分布；分布包含状态间的等待，不是函数CPU剖析，不能直接当作可删除时间。真实超时不随游戏倍速缩短。

## SDK

`San14HostApi.match`为ABI v1可选尾部，先检查Host大小、指针及`San14MatchApi.struct_size`。`San14MatchOptions`和服务表均按`struct_size`检查；现有前缀不变。

`San14MatchApi.capabilities`是可选尾部；读取前检查服务表大小，再要求`SAN14_MATCH_CAP_SPEED`／`SAN14_MATCH_CAP_PERIODIC_SNAPSHOTS`后使用对应参数。旧服务表可继续调用原72字节参数。

UI线程调用`start`排队；实际读档、Auto及世界观察由Host在原生manager线程执行。`status`只读缓存JSON，短缓冲不消费结果；`cancel`仅请求停止，继续查询结果确认。需要`game.commands`、`game.read.saves`及cities／forces／units读权限；使用新倍速尾部另需`game.rules`，原72字节参数前缀保持兼容、继承原有速度与默认1000毫秒采样；句柄绑定owner，其他Mod不可控制或读取。一个进程最多保留32份请求回执，不静默淘汰；同时只运行一局。

本地CLI使用进程创建时间与随机实例身份绑定的JSON邮箱，调用同一语义服务；不接收JavaScript、地址或任意文件路径，无网络监听。该邮箱不依赖MCP逐旬调用。

## 小窗口渲染预设

关闭游戏和启动器后，可使用1280×720窗口减少绘制负载；这是保留图形设备的模式，仍不是无头：

```powershell
python tools/match_render_profile.py apply --backup results/render-backup.json
```

然后正常通过Steam启动游戏，进入主菜单并运行对局。结束、关闭游戏后恢复：

```powershell
python tools/match_render_profile.py restore --backup results/render-backup.json
```

安装包中的路径为`SAN14Base/tools/match_render_profile.py`。备份在首次写入前持久化且不能覆盖；恢复时检查当前三项仍与预设相符，用户另行改过则拒绝覆盖。仅修改游戏自己的WIDTH、HEIGHT和WINDOW_MODE，不改系统显示设置、画质、模拟或存档。实际收益依赖机器与原分辨率，用同档对照衡量；`status`可只读查看当前三项。
