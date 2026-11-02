#!/bin/bash
# 导入 + 建图 + 成图 + 手感帧率 + AI 对战，一次拿锁
cd /d/RobotSlice
bash tools/moon_step_mat.sh "$1" ""
bash tools/moon_step_flight.sh -327 -163
bash tools/moon_step_battle.sh
