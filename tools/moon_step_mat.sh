#!/bin/bash
# 重导月壤 + 重建材质 + 建图 + 成图：bash tools/moon_ue_run.sh "说明" bash tools/moon_step_mat.sh 版本名 [机位] [额外]
cd /d/RobotSlice
V="$1"; ONLY="$2"; shift; shift
bash tools/moon_cmd.sh "ue_moon_import.py ${MOON_IMPORT_STEPS:-mat}"
grep -a -iE "material.*error|\[SM6\]|error X|Failed to compile" Saved/Logs/MoonCmd.log | cut -c1-240 | head -10
bash tools/moon_cmd.sh "ue_build_moon.py"
bash tools/moon_shots.sh "$V" "$ONLY" "$@"
