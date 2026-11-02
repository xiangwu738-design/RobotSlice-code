# 云端开发说明（HX-07 / RobotSlice）

UE 5.8.2 C++ 机甲 PvP 游戏。这个仓库只有 `Source`、`Config`、`tools`、README 和待办，**素材（Content、Inbox）在用户电脑上，云端没有 UE，也没法编译**。用户电脑上有一个不用 AI 的脚本负责编译和测试。

## 工作流程
1. 在 **`cloud` 分支**上改代码，提交并推送。每次推送都会触发一次本地同步、编译和测试（一次通常几分钟到十几分钟）。**攒够一批再推，不要每个小改动都推。**
2. 在仓库根目录写 `runner_request.json`，说明要跑什么：
```json
{"build": true, "tests": [
  {"kind": "click"},
  {"kind": "game", "name": "duel", "map": "/Game/Maps/L_OldStreet?Range", "args": ["-MechQuitAfter=40"], "seconds": 120},
  {"kind": "gamepy", "name": "shots", "map": "/Game/Maps/L_OldStreet?Range", "script": "tools/xxx.py", "args": ["-MechBeauty"], "seconds": 200},
  {"kind": "py", "name": "tool", "script": "tools/xxx.py", "seconds": 300}
]}
```
   - `click`：菜单真实点击回归（约 3 分钟），大改后跑。
   - `game`：`-game` 进图离屏运行。`map` 必须是 `/Game/Maps/L_名字`，可带 `?参数`；`args` 只接受 `-Mech` 开头的参数；`seconds` 上限 900。每次最多 8 项。
   - `gamepy`：进图时执行 `tools/` 下的 UE Python（可以截图）。
   - `py`：commandlet 执行 `tools/` 下的 UE Python（导入、改资源）。
3. 结果在 **`results` 分支**的 `<提交前 10 位>/` 目录：`summary.txt`、`build.txt`（编译错误）、`<name>.txt`（日志里 LogTemp/LogPython/报错行）、`<name>_NN.jpg`（新截图，最多 12 张）。
   用 `git fetch origin results` 查看，没出现就等一会儿再查。用户在玩游戏时脚本会暂停，可能等很久，这不是错误。
4. 把调试信息写成 `UE_LOG(LogTemp, Display, TEXT("[标签] ..."))`，自动测试开关写成 `-MechXxx` 命令行参数，这样才看得到。

## 必须遵守
- 只改 `Source`、`Config`、`tools`、README。其他路径不会同步到本地。
- 全局对象不能读 `FCommandLine`（打包版启动即退）。
- 其他地图的行为不能变，新规则只在对应地图生效。
- 不手搓资源：不要程序生成模型、贴图、地形。缺素材写进 `CLOUD_需要用户做.md`，由用户下载。
- 测试从简，别反复跑全套。
- 这次用的是有期限的额度（11 月 5 日 08:59 GMT+1 到期），做完就停，别做范围外的打磨。

## 待办和设计依据
`docs/本轮待办_20261102.md`（总待办）、`README.md`（各系统说明，「老旧街道」「石柱山水 / 石林」两节最相关）。
