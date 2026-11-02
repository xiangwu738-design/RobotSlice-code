#!/bin/bash
# 老旧街道跑 UE 的小工具：等 Saved/ue_busy.txt 没人占（用户在玩 CS2 / 打包版也等），noclobber 拿锁，跑完只删自己的锁。
# 用法：bash tools/oldstreet_ue_run.sh "在做什么" 命令 参数...
# OS_NORES=1：这次不是带显卡的（-nullrhi / commandlet），不用等 CS2
LOCK=/d/RobotSlice/Saved/ue_busy.txt
NAME="老旧街道代理"
WHAT="$NAME $1 $(date +%H:%M:%S)"
shift
while true; do
  if [ -z "$OS_NORES" ] && tasklist 2>/dev/null | grep -qi -E "^(cs2\.exe|RobotSlice\.exe)"; then
    echo "[oldstreet_run] 用户在玩，等 60 秒 $(date +%H:%M:%S)"
    sleep 60
    continue
  fi
  if [ ! -f "$LOCK" ] && ( set -o noclobber; echo "$WHAT" > "$LOCK" ) 2>/dev/null && [ "$(cat "$LOCK")" = "$WHAT" ]; then
    break
  fi
  sleep 3
done
echo "[oldstreet_run] 拿到锁 $(date +%H:%M:%S)：$WHAT"
"$@"
RC=$?
if [ "$(cat "$LOCK" 2>/dev/null)" = "$WHAT" ]; then
  rm -f "$LOCK"
fi
echo "[oldstreet_run] 完成 $(date +%H:%M:%S) 退出码 $RC，锁已释放"
exit $RC
