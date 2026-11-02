#!/bin/bash
# 跑 UnrealEditor-Cmd 的 Python 脚本：bash tools/moon_cmd.sh "ue_moon_import.py tex mat" [额外参数]
export MSYS_NO_PATHCONV=1
S="$1"; shift
"D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/RobotSlice/RobotSlice.uproject" -run=pythonscript "-script=D:/RobotSlice/tools/$S" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash -nosound -log=MoonCmd.log "$@" > /d/RobotSlice/Saved/Logs/MoonCmd.out 2>&1
echo "退出码 $?"
grep -a -E "\[moon\]|Traceback|Error:.*Python|LogPython: Error|断言|AssertionError" /d/RobotSlice/Saved/Logs/MoonCmd.log | cut -c1-260 | tail -60
