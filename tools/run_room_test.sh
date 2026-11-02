#!/bin/bash
# 房间流程联机测试（Source/RobotSlice/MechRoomTest.cpp）：本机双开——房主开房间图（L_Street?Room?listen），客户端连 127.0.0.1，
# 走一遍：开房 → 客户端进房间、换到蓝队 → 客户端偷改设置（应被忽略）→ 房主换两次地图（客户端跟着变）→ 开始 → 两边进对战图（等人页）→
# 选配 → 开打 3 秒后房主把对面打爆 → 结算 → 房主点「回房间」→ 两边回到同一个房间（队伍、地图、规则、人机名单都还在）→ 退出。
# 用法：tools/run_room_test.sh [开打地图序号，MechMaps 表，默认 0 街道] [种类]
#   种类：Elim（默认，2 真人 v 5 人机）/ DMTeam / DMSolo / Core / Defense / Training：带画面双开
#         big：一个人 v 9 台人机（不要客户端，看 1v9 的选配页、比分条、结算）
#         Challenge：一个人 1v5，打开挑战倍数，真打一下核对装甲和伤害的倍数（不要客户端）
#         midjoin：中途加入接管 AI、掉线交 AI、结算表到时自动回房间（两边 -nullrhi）
#         nullrhi 版：种类后面加 " n"（第三个参数写 n），两边 -nullrhi，不用显卡，只看日志（测夺芯 / 死斗 / 保卫战能开打能回房间）
# 要显卡（两边都离屏渲染截图到 Saved/Screenshots/Room），跑之前先拿 Saved/ue_busy.txt 锁，用户在玩时别跑。
# -ForceRes 会改写 Saved/Config/WindowsEditor/GameUserSettings.ini（用户自己的画质设置）：开头备份、结尾还原。
MAP="${1:-0}"
KIND="${2:-Elim}"
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
LOGS="D:/RobotSlice/Saved/Logs"
export MSYS_NO_PATHCONV=1
NULL="-game -MechNoIntro -nullrhi -nosound -unattended -nosplash"
if [ "$KIND" = "midjoin" ]; then
  # 房主不等人直接开始，日志出现「开打」后才启动客户端（普通客户端，25 秒后退出）
  LOG="$LOGS/RoomMidJoinHost.log"
  timeout 400 "$UE" "$PROJ" "/Game/Maps/L_Street?Room?listen" $NULL -MechRoomTest=Host -MechRoomTestMidJoin -MechRoomTestMap=$MAP \
    -log=RoomMidJoinHost.log > /dev/null 2>&1 &
  HOST=$!
  sleep 10  # 等引擎把上一次的日志改名，免得读到旧的「开打」
  until grep -a -q "\[roomtest\] 开打" "$LOG" 2>/dev/null; do
    kill -0 $HOST 2>/dev/null || break
    sleep 1
  done
  timeout 150 "$UE" "$PROJ" 127.0.0.1 $NULL -MechQuitAfter=25 -log=RoomMidJoinClient.log > /dev/null 2>&1 &
  CLIENT=$!
  wait $HOST $CLIENT
  echo "== 房主 =="
  grep -a -E "\[roomtest\]|\[room\]|\[battle\].*(接管|掉线|加入)|Fatal error|Assertion failed" "$LOG" | cut -c1-220
  echo "== 客户端 =="
  grep -a -E "\[pilot\]|\[room\]|Fatal error|Assertion failed" "$LOGS/RoomMidJoinClient.log" | cut -c1-220
  exit 0
fi
if [ "$3" = "n" ]; then
  COMMON="$NULL"
else
  cp -f "$GUS" "$GUS.roomtest_bak"
  COMMON="-game -MechNoIntro -RenderOffscreen -windowed -ForceRes -ResX=1920 -ResY=1080 -unattended -nosplash -nosound"
fi
if [ "$KIND" = "big" ] || [ "$KIND" = "Challenge" ]; then
  EXTRA="-MechRoomTestKind=$KIND"
  [ "$KIND" = "big" ] && EXTRA="-MechRoomTestBig"
  timeout 400 "$UE" "$PROJ" "/Game/Maps/L_Street?Room?listen" $COMMON -MechRoomTest=Host -MechRoomTestAlone $EXTRA -MechRoomTestMap=$MAP \
    -log=RoomTestHost_$KIND.log > /dev/null 2>&1
  [ "$3" = "n" ] || mv -f "$GUS.roomtest_bak" "$GUS"
  grep -a -E "\[roomtest\]|\[room\]|Fatal error|Assertion failed" "$LOGS/RoomTestHost_$KIND.log" | cut -c1-220
  exit 0
fi
timeout 700 "$UE" "$PROJ" "/Game/Maps/L_Street?Room?listen" $COMMON -MechRoomTest=Host -MechRoomTestKind=$KIND -MechRoomTestMap=$MAP \
  -log=RoomTestHost_$KIND.log > /dev/null 2>&1 &
HOST=$!
sleep 25
timeout 680 "$UE" "$PROJ" 127.0.0.1 $COMMON -MechRoomTest=Client -MechRoomTestKind=$KIND -log=RoomTestClient_$KIND.log > /dev/null 2>&1 &
CLIENT=$!
wait $HOST $CLIENT
[ "$3" = "n" ] || mv -f "$GUS.roomtest_bak" "$GUS"
echo "== 房主 =="
grep -a -E "\[roomtest\]|\[room\]|Fatal error|Assertion failed" "$LOGS/RoomTestHost_$KIND.log" | cut -c1-220
echo "== 客户端 =="
grep -a -E "\[roomtest\]|Fatal error|Assertion failed" "$LOGS/RoomTestClient_$KIND.log" | cut -c1-220
