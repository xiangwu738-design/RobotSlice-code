#!/bin/bash
# 第一阶段收尾一轮：重搭、全机位、瞬移实测（镜头切换 + 不切换）、同条件城区 / 园区帧率
cd /d/RobotSlice
OS_NORES=1 OS_STEPS="" bash tools/oldstreet_ue_run.sh "老街重搭" bash tools/oldstreet_session.sh 2>&1 | tail -3 | cut -c1-300
OS_SHOTS_WAIT=6 OS_TAIL=1 bash tools/oldstreet_ue_run.sh "老街终版全机位" bash tools/oldstreet_shots.sh final 16.3 2>&1 | tail -3 | cut -c1-700
grep -a -E "missing usage|Failed to compile" Saved/Logs/OSShots.log | wc -l
grep -a -E "\[osshots\] (碰撞自检|楼顶 B|隐形墙)" Saved/Logs/OSShots.log | cut -c30-120
OS_SHOTS_MAP=/Game/Maps/L_City.L_City OS_SHOTS_TABLE=city OS_MAP=/Game/Maps/L_City OS_WEATHER=Overcast OS_SHOTS_WAIT=6 OS_TAIL=1 bash tools/oldstreet_ue_run.sh "城区对比帧率" bash tools/oldstreet_shots.sh cmp_city 11 2>&1 | tail -3 | cut -c1-300
OS_SHOTS_MAP=/Game/Maps/L_TechPark.L_TechPark OS_SHOTS_TABLE=techpark OS_MAP=/Game/Maps/L_TechPark OS_WEATHER=Clear OS_SHOTS_WAIT=6 OS_TAIL=1 bash tools/oldstreet_ue_run.sh "园区对比帧率" bash tools/oldstreet_shots.sh cmp_tp 15 2>&1 | tail -3 | cut -c1-300
for v in finalcut finalnocut; do
  [ $v = finalcut ] && export OS_SEAM_CUT=1 || export OS_SEAM_CUT=0
  export OS_SEAM_TAG=$v OS_SEAM_EXEC=""
  bash tools/oldstreet_ue_run.sh "老街瞬移实测 $v" bash tools/oldstreet_seam_run.sh 16.3 2>&1 | grep -a -E "退出码|截到"
  grep -a "osseam\] 帧时间毫秒" Saved/Logs/OSSeam.log | cut -c30-170 | head -6
  py -3 tools/oldstreet_seam_compare.py Saved/Screenshots/OldStreet/seam_$v "docs/老旧街道_20261102/瞬移实测_$v.jpg"
done
echo 全部完成
