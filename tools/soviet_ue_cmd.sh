#!/bin/bash
# 苏联板楼图：跑 UnrealEditor-Cmd 的 python 脚本（要先在 soviet_ue_lock.sh 里拿锁）。bash tools/soviet_ue_cmd.sh 日志名 "脚本 参数" [额外开关]
export MSYS_NO_PATHCONV=1
LOG=$1; SCRIPT=$2; shift 2
cd /d/RobotSlice
"/d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/RobotSlice/RobotSlice.uproject" -run=pythonscript "-script=D:/RobotSlice/tools/$SCRIPT" -unattended -nosplash -nop4 "$@" > "Inbox/_k62/$LOG.log" 2>&1
echo "exit $?" >> "Inbox/_k62/$LOG.log"
