#!/bin/bash
# 苏联板楼图：等 UE 锁 -> 跑命令 -> 删锁（只删自己的）。bash tools/soviet_ue_lock.sh "在做什么" 命令...
LOCK=/d/RobotSlice/Saved/ue_busy.txt
WHAT="$1"; shift
for i in $(seq 1 2000); do
  if ( set -o noclobber; echo "苏联板楼代理 $(date) $WHAT" > "$LOCK" ) 2>/dev/null; then got=1; break; fi
  sleep 15
done
[ "$got" = 1 ] || { echo "等不到 UE 锁"; exit 99; }
trap 'rm -f "$LOCK"' EXIT
"$@"
