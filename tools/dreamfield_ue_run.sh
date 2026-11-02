#!/bin/bash
# 梦核旷野图（L_Dreamfield）跑 UE 的小工具，照 docs/代理协作规则.md：先在 Saved/ue_queue.txt 排队，
# 等到「没有锁文件、自己排第一（或第一名 10 分钟没动静）」时原子拿锁 Saved/ue_busy.txt，拿到后把自己从队列删掉，跑完马上删自己的锁。
# 用法：bash tools/dreamfield_ue_run.sh "在做什么" 命令 参数...
LOCK=/d/RobotSlice/Saved/ue_busy.txt
QUEUE=/d/RobotSlice/Saved/ue_queue.txt
NAME="地图代理"
WHAT="$NAME $1 $(date +%H:%M)"
shift
touch "$QUEUE"
grep -qx "$NAME" "$QUEUE" || echo "$NAME" >> "$QUEUE"
first_seen=""
first_since=$(date +%s)
while true; do
  first=$(head -n 1 "$QUEUE" | tr -d '\r')
  if [ "$first" != "$first_seen" ]; then first_seen="$first"; first_since=$(date +%s); fi
  stale=$(( $(date +%s) - first_since ))
  if [ ! -f "$LOCK" ] && { [ "$first" = "$NAME" ] || [ $stale -gt 600 ]; }; then
    if ( set -o noclobber; echo "$WHAT" > "$LOCK" ) 2>/dev/null && [ "$(cat "$LOCK")" = "$WHAT" ]; then
      break
    fi
  fi
  sleep 15
done
sed -i "/^$NAME$/d" "$QUEUE"
echo "[dreamfield_ue_run] 拿到锁 $(date +%H:%M:%S)：$WHAT"
"$@"
RC=$?
if [ "$(cat "$LOCK" 2>/dev/null)" = "$WHAT" ]; then
  rm -f "$LOCK"
fi
echo "[dreamfield_ue_run] 完成 $(date +%H:%M:%S) 退出码 $RC，锁已释放"
exit $RC
