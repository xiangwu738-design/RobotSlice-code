#!/bin/bash
# 开场运镜联机测试（10-31）：本机双开，房主 listen + 客户端连 127.0.0.1，两边都播运镜、逐秒截图（客户端文件名前缀 client_），开打后各自退出。
# 客户端在第 5 秒模拟按键跳过：应该只有客户端自己回到机体，房主照常播完（不是所有真人都跳过，服务器不提前开打）。
# 用法：tools/run_intro_nettest.sh [键=core]   日志 Saved/Logs/IntroNetHost.log / IntroNetClient.log，搜 [intro]
# 要显卡：用户在玩 / 开着 CS2 时别跑；先拿 Saved/ue_busy.txt 锁。
KEY="${1:-core}"
case "$KEY" in
  street)   URL="/Game/Maps/L_Street?Match?listen" ;;
  nature)   URL="/Game/Maps/L_Nature?Match?listen" ;;
  valley)   URL="/Game/Maps/L_Volcano?Core?listen" ;;
  core)     URL="/Game/Maps/L_Range?Core?listen" ;;
  *) echo "不认识的键 $KEY"; exit 1 ;;
esac
export MSYS_NO_PATHCONV=1
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
COMMON="-game -RenderOffscreen -unattended -nosplash -nosound -ForceRes -ResX=960 -ResY=540 -windowed"
timeout 420 "$UE" "D:/RobotSlice/RobotSlice.uproject" "$URL" -MechIntroTest=$KEY -MechIntroVariant=b -MechNoLobby -MechNoDraft -MechWaitPlayers=2 $COMMON -log=IntroNetHost.log > /dev/null 2>&1 &
sleep 25
timeout 400 "$UE" "D:/RobotSlice/RobotSlice.uproject" 127.0.0.1 -MechIntroTest=$KEY -MechIntroSkipAt=5 $COMMON -log=IntroNetClient.log > /dev/null 2>&1
wait
echo "== 房主"; grep -a "\[intro\]" "D:/RobotSlice/Saved/Logs/IntroNetHost.log" | grep -v "截图" | cut -c31-220
echo "== 客户端"; grep -a "\[intro\]" "D:/RobotSlice/Saved/Logs/IntroNetClient.log" | grep -v "截图" | cut -c31-220
