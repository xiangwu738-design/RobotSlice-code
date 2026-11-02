#!/bin/bash
# 老旧街道截图：备份 GameUserSettings.ini（-ForceRes 退出时会改它），跑完还原。锁由 oldstreet_ue_run.sh 拿。
# 用法：bash tools/oldstreet_ue_run.sh "截图" bash tools/oldstreet_shots.sh 输出子目录 [小时] [额外参数...]
# 环境变量见 tools/ue_oldstreet_shots.py；OS_MAP 换地图（默认 /Game/Maps/L_OldStreet）
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
SUB="${1:-now}"; shift
HOUR="${1:-16.3}"; shift
MAP="${OS_MAP:-/Game/Maps/L_OldStreet}"
export OS_SHOTS_DIR="D:/RobotSlice/Saved/Screenshots/OldStreet/$SUB"
INI=/d/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini
BAK=/d/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini.oldstreet_bak
cp "$INI" "$BAK"
"$UE" "$PROJ" "$MAP" -game -MechBeauty -MechHour=$HOUR -MechWeather=${OS_WEATHER:-Haze} -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed \
  "-ExecCmds=py D:/RobotSlice/tools/ue_oldstreet_shots.py" -log=OSShots.log "$@" > /dev/null 2>&1
echo "退出码 $?"
cp "$BAK" "$INI" && rm -f "$BAK"
grep -a -E "\[osshots\]|missing usage|Failed to compile|\[lighting\]" /d/RobotSlice/Saved/Logs/OSShots.log | cut -c1-230 | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | tail -${OS_TAIL:-90}
ls "$OS_SHOTS_DIR" | wc -l
