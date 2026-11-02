#!/bin/bash
# 苏联板楼图 L_Soviet 测试（先拿 UE 锁，用户在玩就别跑）。
#   tools/soviet_test.sh shots 输出子目录 [机位,机位]    环境截图（?Range 下把 ShotCam 挪到各机位，日志 SovietShots.log，每个机位记帧率）
#   tools/soviet_test.sh perf                          按键驾驶 + 帧时间统计（1600×900 离屏，和 README 里别的图同条件），日志 SovietPerf.log
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
GUS=/d/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini
BAK=/d/RobotSlice/Inbox/_k62/GameUserSettings.ini.bak
cp "$GUS" "$BAK"
trap 'cp "$BAK" "$GUS"' EXIT
MODE="${1:-shots}"
case "$MODE" in
  shots)
    export SV_SHOTS_DIR="D:/RobotSlice/Saved/Screenshots/Soviet/${2:-now}"
    export SV_SHOTS_ONLY="${3:-}"
    "$UE" "$PROJ" "/Game/Maps/L_Soviet?Range" -game -MechInfiniteEnergy -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_soviet_shots.py" -log=SovietShots.log > /dev/null 2>&1
    grep -a "\[shots\]" /d/RobotSlice/Saved/Logs/SovietShots.log | grep -a "帧率\|完成\|Error" | cut -c30-200
    grep -a -c "missing usage flag" /d/RobotSlice/Saved/Logs/SovietShots.log
    ;;
  gallery)
    export SV_SHOTS_DIR="D:/RobotSlice/Saved/Screenshots/Soviet/gallery"
    export SV_SHOTS_MAP=/Game/Env/Soviet/Test/L_SvGallery
    export SV_SHOTS_WAIT=3 SV_SHOTS_DELAY=6
    "$UE" "$PROJ" "/Game/Env/Soviet/Test/L_SvGallery?Range" -game -MechInfiniteEnergy -RenderOffscreen -unattended -nosound -nosplash       -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_soviet_shots.py" -log=SovietGallery.log > /dev/null 2>&1
    grep -a "\[shots\]" /d/RobotSlice/Saved/Logs/SovietGallery.log | tail -2 | cut -c30-200
    ;;
  battle)
    export MECHS_MAP=/Game/Maps/L_Soviet.L_Soviet
    "$UE" "$PROJ" "/Game/Maps/L_Soviet?Match?Team=3?Mode=DM" -game -MechAutoBattle -MechDMTarget=10 -MechDMTime=150 -RenderOffscreen -unattended -nosound -nosplash       -ForceRes -ResX=1280 -ResY=720 -windowed "-ExecCmds=slomo 2,py D:/RobotSlice/tools/ue_log_mechs.py" -log=SovietBattle.log > /dev/null 2>&1
    grep -a -E "\[round\]|\[match\]|\[dm\]|掉出|\[arena\]" /d/RobotSlice/Saved/Logs/SovietBattle.log | cut -c30-200 | tail -30
    ;;
  perf)
    "$UE" "$PROJ" "/Game/Maps/L_Soviet?Match?Team=3" -game -MechInputTest -MechPerf -RenderOffscreen -unattended -nosound -nosplash \
      -ForceRes -ResX=1600 -ResY=900 -windowed -log=SovietPerf.log > /dev/null 2>&1
    grep -a "\[perf\]" /d/RobotSlice/Saved/Logs/SovietPerf.log | cut -c30-260 | tail -8
    grep -a -c "missing usage flag" /d/RobotSlice/Saved/Logs/SovietPerf.log
    ;;
esac
