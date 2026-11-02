#!/bin/bash
# 峡谷截图：art = 美术图 L_Canyon（-MechBeauty，13 点晴天）；grey = 灰盒（L_Range?Core 自动对战，同一组机位，高度 +300 米）
# 用法：run_shots.sh art|grey [输出子目录]
MODE=$1; SUBDIR=${2:-$MODE}
UE="/d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
export CANYON_SHOTS_DIR="D:/RobotSlice/Saved/Screenshots/Canyon/$SUBDIR"
if [ "$MODE" = "art" ]; then
  export CANYON_SHOTS_START=8 CANYON_SHOTS_WAIT=4
  MSYS_NO_PATHCONV=1 "$UE" "D:/RobotSlice/RobotSlice.uproject" /Game/Maps/L_Canyon -game -MechBeauty -MechHour=13 -MechWeather=Clear \
    -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed \
    -ExecCmds="py D:/RobotSlice/tools/canyon/canyon_shots.py" -abslog="D:/RobotSlice/Saved/Logs/canyon_shots_art.log"
else
  export CANYON_SHOTS_START=14 CANYON_SHOTS_WAIT=3 CANYON_SHOTS_OFFSET_Z=30000
  MSYS_NO_PATHCONV=1 "$UE" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Range?Core?Team=3" -game -RenderOffscreen -unattended -nosound \
    -ForceRes -ResX=1920 -ResY=1080 -windowed -MechAutoBattle -MechCoreTime=60 \
    -ExecCmds="py D:/RobotSlice/tools/canyon/canyon_shots.py" -abslog="D:/RobotSlice/Saved/Logs/canyon_shots_grey.log"
fi
