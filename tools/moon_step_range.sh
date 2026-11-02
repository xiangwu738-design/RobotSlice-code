#!/bin/bash
export MSYS_NO_PATHCONV=1
cd /d/RobotSlice
sed -i "s/    ('f_dome_in', .*/    ('f_dome_in', (-40, 6, 'g3.5'), (12, -4, 12), 80, None),               # 穹顶里（地面高 5 米）往中间看/" tools/ue_moon_shots.py
bash tools/moon_shots.sh v7 "f_dome_in" | tail -8 | cut -c1-160
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
cp "$GUS" "D:/RobotSlice/Saved/GameUserSettings.moon.bak"
timeout 180 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Moon?Range" -game -MechInfiniteEnergy -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_moon_range_check.py" -log=MoonRange.log > /dev/null 2>&1
echo "退出码 $?"
cp "D:/RobotSlice/Saved/GameUserSettings.moon.bak" "$GUS"
grep -a -E "\[rangecheck\]|\[range\]|missing usage flag|Datasmith.*Error|LogPython: Error" Saved/Logs/MoonRange.log | cut -c30-260
for f in MoonRange MoonBattle MoonFlight-327 MoonFlight-163 MoonShots; do echo "$f missing usage flag: $(grep -a -c 'missing usage flag' Saved/Logs/$f.log)"; done
