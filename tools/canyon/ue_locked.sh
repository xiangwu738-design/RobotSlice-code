#!/bin/bash
# 轮流用 UE：等 Saved/ue_busy.txt 消失、没有编译进程，再独占写入「峡谷子代理 <在做什么>」，跑完立刻删掉。
# 用法：ue_locked.sh "在做什么" 命令 参数...
DESC="$1"; shift
BUSY=/d/RobotSlice/Saved/ue_busy.txt
while true; do
  if [ ! -f "$BUSY" ] && ! tasklist | grep -q -i -E "UnrealBuildTool|link.exe|cl.exe"; then
    if ( set -o noclobber; echo "峡谷子代理 $DESC" > "$BUSY" ) 2>/dev/null; then break; fi
  fi
  sleep 10
done
echo "[lock] 拿到 $(date +%T)：$DESC"
"$@"; rc=$?
rm -f "$BUSY"
echo "[lock] 释放 $(date +%T) rc=$rc"
