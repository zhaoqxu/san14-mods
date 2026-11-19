# 自动对局测试

第一版复用原版看海、AI和结算，保留原版画面。当前原生适配范围为简中传承版Build25953386，支持已挂载的双阵营预制剧本；首先使用“双城全明星对战”验证。

先在游戏内正常创建双城局面，保存到一个专用槽，并保留同名`.san14mods`。运行器只读原始起点，不重新制作或覆盖它。开始前启用完全相同的剧本包。普通／换边包的起点不可混用。

## 脚本调用

游戏停在标题页时运行：

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
- 没有承诺原生RNG确定性、精确日步进、无图形设备或加速倍数。第一版不修改倍速、既有Mod开关或玩家全局偏好。

## SDK

`San14HostApi.match`为ABI v1可选尾部，先检查Host大小、指针及`San14MatchApi.struct_size`。`San14MatchOptions`和服务表均按`struct_size`检查；现有前缀不变。

UI线程调用`start`排队；实际读档、Auto及世界观察由Host在原生manager线程执行。`status`只读缓存JSON，短缓冲不消费结果；`cancel`仅请求停止，继续查询结果确认。需要`game.commands`、`game.read.saves`及cities／forces／units读权限；句柄绑定owner，其他Mod不可控制或读取。一个进程最多保留32份请求回执，不静默淘汰；同时只运行一局。

本地CLI使用进程创建时间与随机实例身份绑定的JSON邮箱，调用同一语义服务；不接收JavaScript、地址或任意文件路径，无网络监听。该邮箱不依赖MCP逐旬调用。
