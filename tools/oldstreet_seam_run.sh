#!/bin/bash
# 瞬移实测：跑 ue_oldstreet_seamtest.py，把 Shot 截的帧按顺序挪进结果目录。锁由 oldstreet_ue_run.sh 拿。
# 用法：OS_SEAM_TAG=lumen [OS_SEAM_CUT=1] [OS_SEAM_EXEC="..."] bash tools/oldstreet_ue_run.sh "瞬移实测" bash tools/oldstreet_seam_run.sh [小时]
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
HOUR="${1:-16.3}"
TAG="${OS_SEAM_TAG:-lumen}"
OUT="/d/RobotSlice/Saved/Screenshots/OldStreet/seam_$TAG"; rm -f "$OUT"/f*.png "$OUT"/seq.json
SHOTS=/d/RobotSlice/Saved/Screenshots/WindowsEditor
mkdir -p "$OUT" "$SHOTS"
# 记下跑之前已有的截图，只挪这次新出的
MARK=$(mktemp); touch "$MARK"; sleep 1
INI=/d/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini
BAK="$INI.oldstreet_bak"
cp "$INI" "$BAK"
"$UE" "$PROJ" "/Game/Maps/L_OldStreet?Range" -game -MechHour=$HOUR -MechWeather=${OS_WEATHER:-Haze} -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed \
  "-ExecCmds=py D:/RobotSlice/tools/ue_oldstreet_seamtest.py" -log=OSSeam.log > /dev/null 2>&1
echo "退出码 $?"
cp "$BAK" "$INI" && rm -f "$BAK"
n=0
for f in $(find "$SHOTS" -maxdepth 1 -name "ScreenShot*.png" -newer "$MARK" | sort); do
  mv "$f" "$OUT/f$(printf %03d $n).png"; n=$((n+1))
done
rm -f "$MARK"
echo "截到 $n 帧"
grep -a -E "\[osseam\]" /d/RobotSlice/Saved/Logs/OSSeam.log | cut -c1-260 | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | tail -12
