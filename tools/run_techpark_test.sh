#!/bin/bash
# 相位电子园区（L_TechPark）测试。跑之前先拿 UE 锁（Saved/ue_busy.txt），用户开着游戏 / CS2 时别跑。
# 用法：
#   tools/run_techpark_test.sh battle [Elim|DM] [额外参数]   3v3 全 AI 自动对战（slomo 2，离屏 1920×1080），截图在 Saved/Screenshots/Battle，日志 TechParkBattle.log
#   tools/run_techpark_test.sh perf [地图，默认 /Game/Maps/L_TechPark]   按键测试驾驶 + 帧时间统计（1600×900 离屏，和 README 里街道图的数字同条件）
#   tools/run_techpark_test.sh shots                    MechBeauty 机位截图（1920×1080），Saved/Screenshots/Beauty
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
MODE="${1:-battle}"
shift
case "$MODE" in
  battle)
    RULE="${1:-Elim}"; shift
    export MECHS_MAP=/Game/Maps/L_TechPark.L_TechPark   # ue_log_mechs.py 每 5 秒打一次所有机体位置（[mechs]），k051_mech_stats.py 统计高度分布、出没出界
    "$UE" "$PROJ" "/Game/Maps/L_TechPark?Match?Team=3?Mode=$RULE" -game -MechAutoBattle -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=slomo 2,py D:/RobotSlice/tools/ue_log_mechs.py" -log=TechParkBattle.log "$@" > /dev/null 2>&1
    py -3 -I D:/RobotSlice/tools/k051_mech_stats.py D:/RobotSlice/Saved/Logs/TechParkBattle.log
    grep -a -E "\[round\]|\[match\]|\[dm\]|击毁|掉出|\[map\]|\[arena\]|胜" D:/RobotSlice/Saved/Logs/TechParkBattle.log | cut -c30-220 | tail -60
    ;;
  perf)
    MAP="${1:-/Game/Maps/L_TechPark}"; shift
    "$UE" "$PROJ" "$MAP?Match?Team=3" -game -MechInputTest -MechPerf -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1600 -ResY=900 -windowed -log=TechParkPerf.log "$@" > /dev/null 2>&1
    grep -a "\[perf\]" D:/RobotSlice/Saved/Logs/TechParkPerf.log | cut -c30-260 | tail -8
    ;;
  shots)
    "$UE" "$PROJ" /Game/Maps/L_TechPark -game -MechBeauty -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1920 -ResY=1080 -windowed -log=TechParkShots.log "$@" > /dev/null 2>&1
    grep -a -E "\[beauty\]|Screenshot" D:/RobotSlice/Saved/Logs/TechParkShots.log | cut -c30-200 | tail -20
    ;;
esac
