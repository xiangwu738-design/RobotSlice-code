#!/bin/bash
# 建图 + 成图：bash tools/moon_ue_run.sh "说明" bash tools/moon_step_build.sh 版本名 [机位] [额外]
cd /d/RobotSlice
V="$1"; ONLY="$2"; shift; shift
bash tools/moon_cmd.sh "ue_build_moon.py"
bash tools/moon_shots.sh "$V" "$ONLY" "$@"
