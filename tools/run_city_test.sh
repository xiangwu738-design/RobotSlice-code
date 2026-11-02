#!/bin/bash
# 城区街口（L_City）测试。跑之前先拿 UE 锁（Saved/ue_busy.txt；或用 tools/city_ue_run.sh 包一层），用户开着游戏 / CS2 时别跑。
# 用法：
#   tools/run_city_test.sh battle [DM|Elim] [额外参数]   3v3 全 AI 自动对战（slomo 2，离屏 1920×1080，-MechMapTest 顺便拍小地图），
#                                                       截图在 Saved/Screenshots/Battle 和 Saved/Screenshots/Map，日志 CityBattle.log
#   tools/run_city_test.sh perf [额外参数]               按键测试驾驶 + 帧时间统计（1600×900 离屏，和 README 里街道图的数字同条件）
#   tools/run_city_test.sh shots                         ShotCam 10 个机位成图（1920×1080），Saved/Screenshots/City/beauty
#   tools/run_city_test.sh tex                           贴图预算（ue_texture_budget.py report）
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
UEC="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
LOGS="D:/RobotSlice/Saved/Logs"
MODE="${1:-battle}"
shift
case "$MODE" in
  battle)
    RULE="${1:-DM}"; shift
    export MECHS_MAP=/Game/Maps/L_City.L_City   # ue_log_mechs.py 每 5 秒打一次所有机体位置（[mechs]）
    "$UE" "$PROJ" "/Game/Maps/L_City?Match?Team=3?Mode=$RULE" -game -MechAutoBattle -MechMapTest -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=slomo 2,py D:/RobotSlice/tools/ue_log_mechs.py" -log=CityBattle.log "$@" > /dev/null 2>&1
    py -3 -I D:/RobotSlice/tools/city_mech_stats.py $LOGS/CityBattle.log
    grep -a -E "\[round\]|\[match\]|\[dm\]|掉出|\[map\]|\[arena\]|获胜|整场" $LOGS/CityBattle.log | cut -c30-220 | tail -40
    ;;
  perf)
    "$UE" "$PROJ" "/Game/Maps/L_City?Match?Team=3" -game -MechInputTest -MechPerf -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1600 -ResY=900 -windowed -log=CityPerf.log "$@" > /dev/null 2>&1
    grep -a "\[perf\]" $LOGS/CityPerf.log | cut -c30-260 | tail -12
    ;;
  shots)
    CITY_SHOTS_DIR=D:/RobotSlice/Saved/Screenshots/City/beauty "$UE" "$PROJ" /Game/Maps/L_City -game -MechBeauty -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_city_shots.py" -log=CityShots.log "$@" > /dev/null 2>&1
    grep -a "\[shots\]" $LOGS/CityShots.log | cut -c30-220 | tail -24
    ;;
  tex)
    "$UEC" "$PROJ" -run=pythonscript "-script=D:/RobotSlice/tools/ue_texture_budget.py ${1:-report} /Game/Maps/L_City" -unattended -nosplash > $LOGS/city_texbudget.log 2>&1
    grep -a "\[texbudget\]" $LOGS/RobotSlice.log | cut -c30-200   # commandlet 里 LogPython 不进 stdout，在引擎日志里
    ;;
esac
