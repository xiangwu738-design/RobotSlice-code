#!/bin/bash
# 导入街景贴片 + 搭图
cd /d/RobotSlice
bash tools/oldstreet_cmd.sh "D:/RobotSlice/tools/ue_oldstreet_backdrop.py" OSBackdrop | tail -4 | cut -c30-260
bash tools/oldstreet_cmd.sh "D:/RobotSlice/tools/ue_build_oldstreet.py" OSBuild | tail -3 | cut -c30-200
