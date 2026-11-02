#!/bin/bash
# 跑一个 UE 命令行 Python 脚本（commandlet）。用法：bash tools/oldstreet_cmd.sh "脚本 参数" 日志名
export MSYS_NO_PATHCONV=1
UE=/d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe
"$UE" "D:/RobotSlice/RobotSlice.uproject" -run=pythonscript -script="$1" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash -nop4 -abslog="D:/RobotSlice/Saved/Logs/$2.log" > /dev/null 2>&1
echo "退出码 $?"
grep -a -E "\[oldstreet\]|Error: |Traceback|\.py\", line" "D:/RobotSlice/Saved/Logs/$2.log" | grep -a -v -E "LogPython: Error: *$" | tail -80
