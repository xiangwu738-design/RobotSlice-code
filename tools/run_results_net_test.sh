#!/bin/bash
# 实时结算场景的联机检查（10-31）：本机双开——房主 -nullrhi 开房跑全 AI 自动对战（一局定胜负），客户端连进来当观战者，
# 比赛结束后客户端在本机生成结算场景、截图到 Saved/Screenshots/Results/client_auto_*.png，截完自己退出。
# 对时：房主日志「比赛结束」那一行和客户端日志「切到结算场景」那一行的真实时间（日志行首）应差 2 秒左右（PreRoll）。
# 用法：tools/run_results_net_test.sh [地图，默认 /Game/Maps/L_Street] [sound]；要显卡（客户端离屏渲染），先拿 Saved/ue_busy.txt 锁。
#   第二个参数写 sound（10-31 配乐 / 音效）：房主也离屏渲染（960×540）、也起结算场景截图（auto_*.png），两边都不加 -nosound，
#   各自把本机主混音录到 Saved/Audio/Results/<auto|client_auto>_<地图>_<模式>_rec.wav（扬声器静音），日志「[results] 声音」两边对时。
MAP="${1:-/Game/Maps/L_Street}"
MODE="${2:-}"
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
export MSYS_NO_PATHCONV=1
if [ "$MODE" = "sound" ]; then
  HOSTR="-RenderOffscreen -ForceRes -ResX=960 -ResY=540 -windowed -MechResultsShots -MechResultsRecord -MechResultsMute"
  SND="-MechResultsRecord -MechResultsMute"
else
  HOSTR="-nullrhi -nosound"
  SND="-nosound"
fi
timeout 600 "$UE" "$PROJ" "$MAP?Match?listen" -game $HOSTR -MechAutoBattle -MechWinRounds=1 -MechWaitPlayers=2 \
  -unattended -nosplash -log=ResultsNetHost.log > /dev/null 2>&1 &
HOST=$!
sleep 25
timeout 560 "$UE" "$PROJ" 127.0.0.1 -game -MechResultsShots -RenderOffscreen -unattended -nosplash $SND \
  -ForceRes -ResX=1280 -ResY=720 -windowed -log=ResultsNetClient.log > /dev/null 2>&1 &
CLIENT=$!
wait $HOST $CLIENT
echo "== 房主 =="
grep -a -E "比赛结束|\[results\]" D:/RobotSlice/Saved/Logs/ResultsNetHost.log | cut -c1-200
echo "== 客户端 =="
grep -a -E "\[results\]|Fatal|Assert" D:/RobotSlice/Saved/Logs/ResultsNetClient.log | cut -c1-220
