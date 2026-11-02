#!/bin/bash
# 石柱山水工作用的小工具：拿 UE 锁（Saved/ue_busy.txt）跑一条命令，跑完放锁。别人占着就等（最多 30 分钟），不删别人的锁。
# 用法：bash tools/pl_ue.sh "在做什么" 命令...
WHAT="$1"; shift
LOCK=D:/RobotSlice/Saved/ue_busy.txt
export MSYS_NO_PATHCONV=1
for i in $(seq 1 180); do
  if tasklist 2>/dev/null | grep -qiE "^(cs2|RobotSlice)\.exe"; then
    echo "[pl_ue] 用户在玩（cs2 / RobotSlice），等 10 秒"; sleep 10; continue
  fi
  if ( set -o noclobber; echo "石柱山水代理 $(date) $WHAT" > "$LOCK" ) 2>/dev/null; then
    trap 'rm -f "$LOCK"' EXIT
    "$@"
    RC=$?
    rm -f "$LOCK"
    trap - EXIT
    exit $RC
  fi
  echo "[pl_ue] 锁被占：$(cat "$LOCK" 2>/dev/null)，等 10 秒"; sleep 10
done
echo "[pl_ue] 等了 30 分钟还拿不到锁，放弃"
exit 99
