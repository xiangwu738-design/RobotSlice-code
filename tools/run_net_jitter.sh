#!/bin/bash
# 本机双开测联机抖动：房主 + 客户端都带 -MechNetTest（测试驾驶员自己打）和 -MechJitterLog（每 5 秒打 [jitter] 统计），
# 两边都模拟网络延迟 / 抖动 / 丢包（默认 45±15 毫秒、1%，可用环境变量 LAG / VAR / LOSS 改）。
# 房主默认不出画面（-nullrhi：本机双开时后台窗口抢不到显卡只有十几帧，不像真联机），两边都限 60 帧。
# 用法：tools/run_net_jitter.sh <标签>   结果在 Saved/Logs/jit_<标签>_host.log / _client.log，最后打印两边的 [jitter] 行。
# 跑之前看 Saved/ue_busy.txt（有就等），跑时自己写一行、跑完删。
set -u
TAG=${1:-run}
LAG=${LAG:-45}
VAR=${VAR:-15}
LOSS=${LOSS:-1}
MAP=${MAP:-/Game/Maps/L_Street}
HOSTARGS=${HOSTARGS:--nullrhi}
UE="/d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
LOGS="D:/RobotSlice/Saved/Logs"
NET="-PktLag=$LAG -PktLagVariance=$VAR -PktLoss=$LOSS"
COMMON="-game -windowed -ForceRes -ResX=960 -ResY=540 -nosound -MechNetTest -MechJitterLog $NET ${EXTRA:-}"
CAP="-ExecCmds=t.MaxFPS 60"
export MSYS_NO_PATHCONV=1
"$UE" "$PROJ" "$MAP?listen?Match" $COMMON $HOSTARGS "$CAP" -MechWaitPlayers=2 -abslog="$LOGS/jit_${TAG}_host.log" &
HOST=$!
sleep 25
"$UE" "$PROJ" 127.0.0.1 $COMMON "$CAP" -WinX=980 -abslog="$LOGS/jit_${TAG}_client.log" &
CLIENT=$!
# 测试驾驶员打满 75 秒自己退出；最多等 4 分钟
for i in $(seq 1 240); do
  if ! kill -0 $HOST 2>/dev/null && ! kill -0 $CLIENT 2>/dev/null; then break; fi
  sleep 1
done
kill $HOST $CLIENT 2>/dev/null
for side in host client; do
  echo "== $side"
  # 客户端在房主退出后会掉回单机继续跑，只看断开前的
  awk '/HostClosedConnection/{exit} /\[jitter\]|纠正/' "$LOGS/jit_${TAG}_${side}.log" | sed 's/^.*LogTemp: Display: //' | tail -60
done
