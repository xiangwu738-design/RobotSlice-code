#!/bin/bash
# 石柱山水（L_Pillars）和落水规则的测试。自己拿 UE 锁（tools/pl_ue.sh），用户开着 CS2 / 游戏时等。-ForceRes 前备份 GameUserSettings.ini、结束还原。
#   tools/run_pillars_test.sh shots [输出目录] [只拍的机位，逗号分隔]    成图截图（史诗画质，记帧率）
#   tools/run_pillars_test.sh battle Elim|DM [地图名，默认 L_Pillars] [额外参数...]   全 AI 对战打到结束，数落水（日志 Pillars_<规则>.log）
#   tools/run_pillars_test.sh water [地图名]    落水规则自测（归属、浅水不误杀，MechWaterTest.cpp）
#   tools/run_pillars_test.sh perf [地图名]     按键测试驾驶 + 帧时间统计（1600×900）
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
MODE="${1:-shots}"; shift
run_ue() {
  cp -f "$GUS" "$GUS.pl_bak"
  "$@"
  mv -f "$GUS.pl_bak" "$GUS"
}
case "$MODE" in
  shots)
    export PL_SHOTS_DIR="${1:-D:/RobotSlice/Saved/Screenshots/Pillars/now}"
    export PL_SHOTS_ONLY="${2:-}"
    export PL_SHOTS_RES="${PL_SHOTS_RES:-1920x1080}"
    bash D:/RobotSlice/tools/pl_ue.sh "石柱山水截图" bash -c "cp -f '$GUS' '$GUS.pl_bak'; timeout 500 '$UE' '$PROJ' /Game/Maps/L_Pillars -game -MechBeauty -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed '-ExecCmds=py D:/RobotSlice/tools/ue_pillars_shots.py' -log=PillarsShots.log > /dev/null 2>&1; mv -f '$GUS.pl_bak' '$GUS'"
    grep -a -E "\[plshots\]|missing usage|Fatal error|Assertion failed" D:/RobotSlice/Saved/Logs/PillarsShots.log | cut -c30-240 | tail -16
    ;;
  battle)
    RULE="${1:-Elim}"; shift
    MAP="${1:-L_Pillars}"; shift
    LOG="Pillars_${MAP}_${RULE}${PL_TAG:-}"
    bash D:/RobotSlice/tools/pl_ue.sh "$MAP 全 AI 对战 $RULE" bash -c "cp -f '$GUS' '$GUS.pl_bak'; timeout 590 '$UE' '$PROJ' '/Game/Maps/$MAP?Match?Team=4?Mode=$RULE' -game -MechAutoBattle -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1280 -ResY=720 -windowed '-ExecCmds=slomo 2' -log=$LOG.log $* > /dev/null 2>&1; mv -f '$GUS.pl_bak' '$GUS'"
    py -3 -I D:/RobotSlice/tools/pillars_ai_stats.py "D:/RobotSlice/Saved/Logs/$LOG.log"
    ;;
  water)
    MAP="${1:-L_Pillars}"; shift
    bash D:/RobotSlice/tools/pl_ue.sh "$MAP 落水自测" bash -c "cp -f '$GUS' '$GUS.pl_bak'; timeout 300 '$UE' '$PROJ' '/Game/Maps/$MAP?Match?Team=4?Mode=Elim' -game -MechAutoBattle -MechWaterTest -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1280 -ResY=720 -windowed -log=PillarsWater_$MAP.log $* > /dev/null 2>&1; mv -f '$GUS.pl_bak' '$GUS'"
    grep -a -E "\[watertest\]|\[water\]|击毁了|Fatal error|Assertion failed" "D:/RobotSlice/Saved/Logs/PillarsWater_$MAP.log" | cut -c30-200 | tail -30
    ;;
  perf)
    MAP="${1:-L_Pillars}"; shift
    bash D:/RobotSlice/tools/pl_ue.sh "$MAP 帧率" bash -c "cp -f '$GUS' '$GUS.pl_bak'; timeout 300 '$UE' '$PROJ' '/Game/Maps/$MAP?Match?Team=3' -game -MechInputTest -MechPerf -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1600 -ResY=900 -windowed -log=PillarsPerf.log $* > /dev/null 2>&1; mv -f '$GUS.pl_bak' '$GUS'"
    grep -a -E "\[perf\]|missing usage|Fatal error" D:/RobotSlice/Saved/Logs/PillarsPerf.log | cut -c30-260 | tail -8
    ;;
esac
