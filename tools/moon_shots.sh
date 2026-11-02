#!/bin/bash
# 月球基地成图：bash tools/moon_ue_run.sh "成图" bash tools/moon_shots.sh 版本名 [只拍的机位,逗号隔开] [额外命令行]
# 带 -ForceRes：先备份 GameUserSettings.ini，结束还原
export MSYS_NO_PATHCONV=1
V="$1"; ONLY="$2"; shift; shift
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
cp "$GUS" "D:/RobotSlice/Saved/GameUserSettings.moon.bak"
export MOON_SHOTS_DIR="D:/RobotSlice/Saved/Screenshots/Moon/$V"
export MOON_SHOTS_ONLY="$ONLY"
mkdir -p "$MOON_SHOTS_DIR"
timeout 480 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" /Game/Maps/L_Moon -game -MechBeauty -RenderOffscreen -unattended -nosplash -nosound \
  -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_moon_shots.py" -log=MoonShots.log "$@" > /dev/null 2>&1
echo "退出码 $?"
cp "D:/RobotSlice/Saved/GameUserSettings.moon.bak" "$GUS"
grep -a -E "\[shots\]|missing usage flag|LogPython: Error|Traceback" D:/RobotSlice/Saved/Logs/MoonShots.log | cut -c30-230 | tail -50
ls -la "$MOON_SHOTS_DIR" | tail -20
