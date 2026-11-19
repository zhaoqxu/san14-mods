# 原生武将名单与筹略地图入口

`navigation` 追加 `open_roster`、`roster_status`、`close_roster`，`San14HostApi`
提供可选的 `roster_view`。两者独立检查 Host／服务表 `struct_size`、表指针及所需函数。
首版适配简中传承版 Build 25953386。接口只在 UI 线程供已启用 Mod 使用，要求
`ui.panel` 和 `game.read.officers`。

- 在地图筹略阶段调用 `open_roster(..., SAN14_NAVIGATION_OWN_FORCE_OFFICERS, &ticket)`。
  Host 在原生更新点走「情报 → 本势力武将」，由 `roster_status` 取得 `PENDING`、
  `OPEN`、`CLOSED` 或 `FAILED`。终态读取一次后释放票据。`close_roster` 排队返回，
  保留其它原生窗口。读档后一律重新获取票据。
- `roster_view.capture` 读取当前可操作的原生情报武将表或已适配的原生选将表。
  传初始化过 `struct_size` 的 `San14RosterViewInfo`，用 `row_stride = sizeof(San14RosterViewRow)`
  和容量零获取所需行数，按 `candidate_count` 分配后再捕获。每条记录给出武将 ID、
  可见／勾选／禁用位，`visible_index` 为原生顺序，隐藏时为 `UINT32_MAX`。
- `apply_projection` 使用最新 `view`、`revision` 及唯一的候选武将 ID。传零项可显示空集，
  游戏中已勾选的人仍由 Host 保留在可见集合。`reset` 恢复全部当前原生候选。排序、
  勾选或投影变化后重新捕获，遇到 `STALE_GENERATION` 丢弃旧快照。一次由一个 Mod 持有
  同一个原生名单的筛选权，关页、重新读档或停用后回收。
- `create_filter_panel` 在 Host 能验证的名单边缘创建非模态筛选区。使用 `flags=0`，
  窄栏可用约 240×36 的面板并留空 `title`，在其中通过 `ui_extended` 加文字输入，
  通过 `ui.show_panel` 显示。名单遮挡、关闭或读档时释放面板；回到名单后可按需重新创建。
- `register_hotkey` 限定筹略地图、情报名单或选将名单，支持未加修饰键的 F2–F12 并
  额外要求 `input.hotkey`。例如在 `SAN14_ROSTER_KEY_PLANNING_MAP` 上登记 F9，
  回调里请求打开本势力名单，成功受理时返回 1；不适用时返回 0 交回原生输入。
