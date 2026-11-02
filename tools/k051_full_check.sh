#!/bin/bash
# 相位电子园区一键验证（先自己拿好 UE 锁；跑完不删锁，调用方删）：
# 导入资源 → 搭地图 → 编译 → 10 机位环境截图 → 3v3 团队死斗全 AI（4 分钟、先到 15）+ 位置统计 → 帧率（园区、街道同条件各一次）
# 用法：bash tools/k051_full_check.sh 截图目录名  → 结果汇总在 Inbox/_k051/full_check.txt
set -u
cd /d/RobotSlice
export MSYS_NO_PATHCONV=1
UEC="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
OUT=Inbox/_k051/full_check.txt
SHOTS="${1:-final}"
: > "$OUT"
log() { echo "$@" | tee -a "$OUT"; }
log "== 开始 $(date +%H:%M:%S)"
"$UEC" "D:/RobotSlice/RobotSlice.uproject" -run=pythonscript -script="D:/RobotSlice/tools/ue_techpark_import.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash > /dev/null 2>&1
grep -a -E "\[techpark\]|Traceback|LogPython: Error" Saved/Logs/RobotSlice.log | cut -c40-200 | tee -a "$OUT"
"$UEC" "D:/RobotSlice/RobotSlice.uproject" -run=pythonscript -script="D:/RobotSlice/tools/ue_techpark_build.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash > /dev/null 2>&1
grep -a -E "\[techpark\]|Traceback|LogPython: Error" Saved/Logs/RobotSlice.log | cut -c40-200 | tee -a "$OUT"
"D:/EPIC/UE_5.8/Engine/Build/BatchFiles/Build.bat" RobotSliceEditor Win64 Development -Project="D:\RobotSlice\RobotSlice.uproject" -WaitMutex > Inbox/_k051/build.log 2>&1
log "== 编译 退出码 $? $(grep -E 'Result:' Inbox/_k051/build.log)"
grep -E " error " Inbox/_k051/build.log | head -5 | tee -a "$OUT"
unset TP_SHOTS_ONLY
export TP_SHOTS_DIR="D:/RobotSlice/Saved/Screenshots/TechPark/$SHOTS"
"$UE" "D:/RobotSlice/RobotSlice.uproject" /Game/Maps/L_TechPark -game -MechBeauty -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_techpark_shots.py" -log=TechParkShots.log > /dev/null 2>&1
log "== 截图 $(ls "$TP_SHOTS_DIR" | wc -l) 张 → $TP_SHOTS_DIR"
grep -a "帧率" Saved/Logs/TechParkShots.log | cut -c40-200 | tee -a "$OUT"
log "== 死斗 $(date +%H:%M:%S)"
bash tools/run_techpark_test.sh battle DM -MechDMTarget=15 -MechDMTime=240 2>&1 | grep -a -E "\[dm\] 结束|比赛结束|\[stats\]|击毁 .* 阵亡" | tee -a "$OUT"
log "== 帧率 $(date +%H:%M:%S)（同时在跑的重进程：$(powershell -NoProfile -Command "(Get-Process blender,python,ffmpeg -ErrorAction SilentlyContinue | ForEach-Object { \$_.ProcessName }) -join ','")）"
for MAP in /Game/Maps/L_TechPark /Game/Maps/L_Street; do
  bash tools/run_techpark_test.sh perf $MAP 2>&1 | grep -a "汇总" | sed "s#^#$MAP #" | tee -a "$OUT"
done
log "== 结束 $(date +%H:%M:%S)"
