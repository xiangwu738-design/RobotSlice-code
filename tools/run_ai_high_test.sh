#!/bin/bash
# AI 用高处的改前改后对比（10-31，搁置中）：同一份程序，不带开关 = 原来那套（AI 离场地中心高 12 米就往回走），-MechHighGround = 高处逻辑。
# 每轮同时开两个 -nullrhi 的 3v3 全 AI 自动对战（一个原来那套、一个新的，条件一样），slomo 3，-MechAIStats 打统计。
# 跑之前先拿 UE 锁（Saved/ue_busy.txt）；-nullrhi 不占显卡，用户玩 CS2 时也能跑。
# 用法：tools/run_ai_high_test.sh <city|techpark> <DM|Elim> <轮数> [起始编号] [额外参数]
#   日志 Saved/Logs/AIHigh_<图>_<规则>_<old|new>_<编号>.log；统计 py -3 -I tools/ai_high_stats.py Saved/Logs/AIHigh_*.log
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
MAPKEY="${1:-city}"; RULE="${2:-DM}"; RUNS="${3:-1}"; START="${4:-1}"
shift 4 2>/dev/null || shift $#
case "$MAPKEY" in
  city) MAP=/Game/Maps/L_City; NAME=City ;;
  techpark) MAP=/Game/Maps/L_TechPark; NAME=TechPark ;;
  *) echo "地图写 city 或 techpark"; exit 1 ;;
esac
if [ "$RULE" = "DM" ]; then RULEARGS="-MechDMTime=180"; else RULEARGS="-MechWinRounds=2"; fi
for ((i = START; i < START + RUNS; i++)); do
  for COND in old new; do
    EXTRA=""; [ "$COND" = "new" ] && EXTRA="-MechHighGround"
    timeout 900 "$UE" "$PROJ" "$MAP?Match?Team=3?Mode=$RULE" -game -MechAutoBattle -MechAIStats $EXTRA $RULEARGS -nullrhi -unattended -nosound -nosplash \
      "-ExecCmds=slomo 3" -log=AIHigh_${NAME}_${RULE}_${COND}_$i.log "$@" > /dev/null 2>&1 &
    sleep 20   # 错开加载
  done
  wait
  py -3 -I D:/RobotSlice/tools/ai_high_stats.py D:/RobotSlice/Saved/Logs/AIHigh_${NAME}_${RULE}_old_$i.log D:/RobotSlice/Saved/Logs/AIHigh_${NAME}_${RULE}_new_$i.log
done
