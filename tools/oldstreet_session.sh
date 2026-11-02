#!/bin/bash
# 连跑：导入（OS_STEPS 给 import 的参数，空 = 不导入）+ 搭图。锁由 oldstreet_ue_run.sh 拿
cd /d/RobotSlice
if [ -n "$OS_STEPS" ]; then bash tools/oldstreet_cmd.sh "D:/RobotSlice/tools/ue_oldstreet_import.py $OS_STEPS" OSImport | tail -${OS_TAIL:-12} | cut -c30-300; fi
bash tools/oldstreet_cmd.sh "D:/RobotSlice/tools/ue_build_oldstreet.py" OSBuild | tail -5 | cut -c30-500
