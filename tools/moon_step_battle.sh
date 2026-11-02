#!/bin/bash
# 3v3 全 AI 死斗走一遍（slomo 2，离屏 1600×900）：看有没有掉出地图、重生点能不能用。要先拿锁
export MSYS_NO_PATHCONV=1
export MECHS_MAP=/Game/Maps/L_Moon.L_Moon
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
cp "$GUS" "D:/RobotSlice/Saved/GameUserSettings.moon.bak"
timeout 240 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Moon?Match?Team=3?Mode=DM" -game -MechAutoBattle -MechDMTarget=12 -MechDMTime=150 -RenderOffscreen -unattended -nosound -nosplash \
  -ForceRes -ResX=1600 -ResY=900 -windowed "-ExecCmds=slomo 2,py D:/RobotSlice/tools/ue_log_mechs.py" -log=MoonBattle.log > /dev/null 2>&1
echo "退出码 $?"
cp "D:/RobotSlice/Saved/GameUserSettings.moon.bak" "$GUS"
L=D:/RobotSlice/Saved/Logs/MoonBattle.log
grep -a -c "击毁" $L
grep -a -E "掉出|\[dm\]|\[match\]|missing usage flag" $L | cut -c30-200 | tail -20
grep -a "\[mechs\]" $L | tail -4 | cut -c30-260
