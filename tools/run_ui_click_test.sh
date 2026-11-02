#!/bin/bash
# 菜单点击测试（MechUIClickTest.cpp）：真鼠标事件点一遍主菜单 → 设置 → 开房 → 房间 → 选配 → 回房间。
# 会改写设置（滑条、配色、开始时存房间选择）：GameUserSettings.ini 开头备份、结尾还原。要显卡，先拿 Saved/ue_busy.txt 锁。日志 [uiclick]，截图 Saved/Screenshots/UIClick。
W="${1:-1920}"; H="${2:-1080}"
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
export MSYS_NO_PATHCONV=1
cp -f "$GUS" "$GUS.uiclick_bak"
timeout 500 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Street" -game -UIClickTest \
  -RenderOffscreen -windowed -ForceRes -ResX=$W -ResY=$H -unattended -nosplash -nosound -log=UIClick.log > /dev/null 2>&1
mv -f "$GUS.uiclick_bak" "$GUS"
grep -a -E "\[uiclick\]|Fatal error|Assertion failed" D:/RobotSlice/Saved/Logs/UIClick.log | cut -c30-180
