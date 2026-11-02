#!/bin/bash
# 城区图（K059）跑 UE 的小工具：等 UE 锁空出来（没有锁文件、没有 UnrealEditor 进程），原子拿锁，跑命令，跑完删自己的锁。
# 用法：bash tools/city_ue_run.sh "在做什么" 命令 参数...
#   例：bash tools/city_ue_run.sh "导入城区素材" /d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe D:/RobotSlice/RobotSlice.uproject -run=pythonscript ...
LOCK=/d/RobotSlice/Saved/ue_busy.txt
WHAT="K059地图代理 $1"
shift
while true; do
  if [ ! -f "$LOCK" ] && ! tasklist | grep -qi "UnrealEditor"; then
    if ( set -o noclobber; echo "$WHAT" > "$LOCK" ) 2>/dev/null; then
      break
    fi
  fi
  sleep 3
done
echo "[city_ue_run] 拿到锁 $(date +%H:%M:%S)：$WHAT"
"$@"
RC=$?
if [ "$(cat "$LOCK" 2>/dev/null)" = "$WHAT" ]; then
  rm -f "$LOCK"
fi
echo "[city_ue_run] 完成 $(date +%H:%M:%S) 退出码 $RC，锁已释放"
exit $RC
