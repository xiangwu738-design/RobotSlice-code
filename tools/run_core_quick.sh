#!/bin/bash
# 夺芯快速检查（用户 10-30：「AI 对战测试费时间太多，简化一下」）：2v2、先到 1 分、限时 4 分钟游戏时间、slomo 3（3 倍速）、
# 960×540 离屏、跳过选配；只看日志：场地有没有搭好、谁拿到核心、交没交芯、最后比分
# 用法：tools/run_core_quick.sh [地图，默认 /Game/Maps/L_Volcano] [额外参数...]
MAP="${1:-/Game/Maps/L_Volcano}"
shift
export MSYS_NO_PATHCONV=1
"D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "$MAP?Core?Team=2" \
  -game -MechAutoBattle -MechNoDraft -MechCoreTarget=1 -MechCoreTime=240 -RenderOffscreen -unattended -nosound -nosplash \
  -ForceRes -ResX=960 -ResY=540 -windowed "-ExecCmds=slomo 3" -log=CoreQuick.log "$@" > /dev/null 2>&1
grep -a "\[core\]" D:/RobotSlice/Saved/Logs/CoreQuick.log | grep -v "带芯 .* 在 (" | cut -c40-240
echo "带芯过程日志 $(grep -a -c '带芯 .* 在 (' D:/RobotSlice/Saved/Logs/CoreQuick.log) 条"
