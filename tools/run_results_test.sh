#!/bin/bash
# 实时结算场景测试（10-31，MechResultsStage）：假数据直接进结算场景，截 9 张到 Saved/Screenshots/Results/<模式>_NN.png，
# 日志 Saved/Logs/Results_<模式>.log（[results] 站位 / 配装 / 每张截图的镜头），约 15 秒后自动退出。
# 用法：tools/run_results_test.sh <Elim|DMTeam|DMSolo|Core> [地图，默认 /Game/Maps/L_Street] [额外参数...]
#   例：tools/run_results_test.sh DMSolo /Game/Maps/L_Volcano -MechTeam=4 "-ExecCmds=mech.Results.Exposure -4.8"
# 要显卡（离屏渲染）：用户在玩 / 开着 CS2 时别跑；跑之前先拿 Saved/ue_busy.txt 锁。
# 10-31：额外参数里有 -MechResultsRecord 时不加 -nosound，把本机主混音录到 Saved/Audio/Results/<模式>_rec.wav
#   （再加 -MechResultsMute 扬声器不出声、录音照录）；-MechResultsPoseShots 改成挨个拍每台机体正面（<模式>_pose_NN.png）。
MODE="${1:-Elim}"
MAP="${2:-/Game/Maps/L_Street}"
shift; shift
SOUND="-nosound"
for A in "$@"; do [ "$A" = "-MechResultsRecord" ] && SOUND=""; done
mkdir -p "D:/RobotSlice/Saved/Screenshots/Results"
export MSYS_NO_PATHCONV=1
timeout 300 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "$MAP" \
  -game -MechResultsTest=$MODE -RenderOffscreen -unattended -nosplash $SOUND \
  -ForceRes -ResX=1920 -ResY=1080 -windowed -log=Results_$MODE.log "$@" > /dev/null 2>&1
# [results] 行和它下面缩进的续行（站位表），外加报错
grep -a -E '\[results\]|^    |Error|Assert|Fatal' "D:/RobotSlice/Saved/Logs/Results_$MODE.log" | grep -v -E 'LogStreaming|LogTexture' | cut -c1-300 | head -90
