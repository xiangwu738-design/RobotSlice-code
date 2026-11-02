#!/bin/bash
# 月球基地：排队拿 UE 锁（Saved/ue_busy.txt）→ 跑命令 → 删自己的锁。bash tools/moon_ue_run.sh "在做什么" 命令...
# 用户在玩（cs2.exe / RobotSlice.exe）时等。规则见 docs/代理协作规则.md
LOCK=/d/RobotSlice/Saved/ue_busy.txt
ME="月球基地代理 $1"
shift
n=0
while true; do
  if tasklist | grep -iqE "cs2\.exe|RobotSlice\.exe"; then
    echo "[锁] 用户在玩（cs2 / RobotSlice），等 30 秒"; sleep 30; continue
  fi
  if ( set -o noclobber; echo "$ME $(date +%H:%M)" > "$LOCK" ) 2>/dev/null; then
    break
  fi
  n=$((n+1))
  if [ $((n % 10)) = 1 ]; then echo "[锁] 被占：$(cat $LOCK 2>/dev/null)"; fi
  sleep 15
done
echo "[锁] 拿到：$(cat $LOCK)"
trap 'grep -q "^月球基地代理" "$LOCK" 2>/dev/null && rm -f "$LOCK"; echo "[锁] 已释放"' EXIT
"$@"
