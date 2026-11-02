#!/bin/bash
# 城区图：一次拿锁里依次 导入素材（ue_import_city.py 全部步骤）→ 搭地图（ue_build_city.py）。日志在 Saved/Logs/city_import.log / city_build.log
export MSYS_NO_PATHCONV=1
UE=/d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe
"$UE" D:/RobotSlice/RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_import_city.py ${CITY_STEPS:-tex mat mesh fix}" \
  -AllowCommandletRendering -RenderOffscreen -unattended -nosplash > /d/RobotSlice/Saved/Logs/city_import.log 2>&1
echo "[city] 导入退出码 $?"
"$UE" D:/RobotSlice/RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_build_city.py" \
  -AllowCommandletRendering -RenderOffscreen -unattended -nosplash > /d/RobotSlice/Saved/Logs/city_build.log 2>&1
echo "[city] 搭图退出码 $?"
