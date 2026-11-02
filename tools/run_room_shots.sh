#!/bin/bash
# 11-02 房间页截图（MechRoomTest.cpp 的 Shots）：单进程走一遍各种状态（1v5、1v9、个人战、夺芯、保卫战、训练场、非房主、五套配色），
# 存到 Saved/Screenshots/RoomUI/<窗口高>/。用法：tools/run_room_shots.sh [宽 高]（默认 1920 1080）。要显卡，先拿 Saved/ue_busy.txt 锁。
# -ForceRes 会改写 GameUserSettings.ini：开头备份、结尾还原。
W="${1:-1920}"; H="${2:-1080}"
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
export MSYS_NO_PATHCONV=1
cp -f "$GUS" "$GUS.roomshots_bak"
timeout 300 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Street?Room?listen" -game -MechRoomTest=Shots -MechNoIntro \
  -RenderOffscreen -windowed -ForceRes -ResX=$W -ResY=$H -unattended -nosplash -nosound -log=RoomShots$H.log > /dev/null 2>&1
mv -f "$GUS.roomshots_bak" "$GUS"
grep -a -E "\[roomtest\] 截图 通过|Fatal error|Assertion failed" D:/RobotSlice/Saved/Logs/RoomShots$H.log | cut -c1-200
ls D:/RobotSlice/Saved/Screenshots/RoomUI/$H | wc -l
