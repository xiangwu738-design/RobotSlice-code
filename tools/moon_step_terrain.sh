#!/bin/bash
# 材质 → 地形 → 建图，一次拿锁跑完
cd /d/RobotSlice
bash tools/moon_cmd.sh "ue_moon_import.py macro mat fix"
cp Saved/Logs/MoonCmd.log Saved/Logs/MoonCmd_mat.log
bash tools/moon_cmd.sh "ue_moon_landscape.py" -EnablePlugins=DatasmithImporter,DatasmithContent
cp Saved/Logs/MoonCmd.log Saved/Logs/MoonCmd_land.log
grep -a -E "Landscape|Datasmith" Saved/Logs/MoonCmd_land.log | grep -a -iE "error|warning" | cut -c1-220 | tail -12
