#!/bin/bash
# 手感 + 帧率实测：两种重力各跑一次按键驾驶（1600×900 离屏，-MechPerf）。要先拿锁
export MSYS_NO_PATHCONV=1
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
cp "$GUS" "D:/RobotSlice/Saved/GameUserSettings.moon.bak"
for G in "$@"; do
  export MOON_TEST_G=$G
  timeout 200 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Moon?Match?Team=1" -game -MechInputTest -MechPerf -RenderOffscreen -unattended -nosound -nosplash \
    -ForceRes -ResX=1600 -ResY=900 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_moon_flight.py" -log=MoonFlight$G.log > /dev/null 2>&1
  echo "=== 重力 $G 退出码 $?"
  py -3 -I D:/RobotSlice/tools/moon_flight_stats.py "D:/RobotSlice/Saved/Logs/MoonFlight$G.log" | tail -24
  grep -a "\[perf\]" "D:/RobotSlice/Saved/Logs/MoonFlight$G.log" | cut -c30-200 | tail -5
  grep -a -E "\[pilot\] 结束|missing usage flag" "D:/RobotSlice/Saved/Logs/MoonFlight$G.log" | cut -c30-220 | head -5
done
cp "D:/RobotSlice/Saved/GameUserSettings.moon.bak" "$GUS"
