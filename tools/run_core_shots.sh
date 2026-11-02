#!/bin/bash
# 夺芯图对比截图：run_core_shots.sh <输出目录> <场地半长米> [文件名前缀]（先 ue_add_shotcam.py 给地图加相机）
export MSYS_NO_PATHCONV=1
CORE_SHOTS_DIR="$1" CORE_SHOTS_L="$2" CORE_SHOTS_PREFIX="${3:-}" \
"D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "/Game/Maps/L_Volcano?Core?Team=2" \
  -game -MechAutoBattle -MechNoDraft -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed \
  "-ExecCmds=py D:/RobotSlice/tools/ue_core_shots.py" -log=CoreShots.log
grep -a "\[coreshots\]" D:/RobotSlice/Saved/Logs/CoreShots.log | cut -c30-260
