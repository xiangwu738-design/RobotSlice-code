#!/bin/bash
# 开场解说运镜测试（10-31）：播一遍开场运镜，逐秒截图到 Saved/Screenshots/Intro/<键>/，日志 Saved/Logs/Intro_<键>.log（[intro] 台词时间轴），播完开打后自动退出。
# 用法：tools/run_intro_test.sh <键> [台词套 a|b|c] [额外参数...]
#   键：street / nature / valley / core / techpark / city（Content/Intro/MechIntro.txt 里的 map 段）
# 要显卡（离屏渲染）：用户在玩 / 开着 CS2 时别跑；跑之前先拿 Saved/ue_busy.txt 锁。最多跑 7 分钟（没生成开场 actor 时游戏不会自己退出）。
KEY="${1:-street}"
VAR="${2:-a}"
shift; shift
case "$KEY" in
  street)   URL="/Game/Maps/L_Street?Match" ;;
  nature)   URL="/Game/Maps/L_Nature?Match" ;;
  valley)   URL="/Game/Maps/L_Volcano?Core" ;;
  core)     URL="/Game/Maps/L_Range?Core" ;;
  techpark) URL="/Game/Maps/L_TechPark?Match" ;;
  city)     URL="/Game/Maps/L_City?Match" ;;
  *) echo "不认识的键 $KEY"; exit 1 ;;
esac
mkdir -p "D:/RobotSlice/Saved/Screenshots/Intro/$KEY"
export MSYS_NO_PATHCONV=1
timeout 420 "D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "D:/RobotSlice/RobotSlice.uproject" "$URL" \
  -game -MechIntroTest=$KEY -MechIntroVariant=$VAR -MechNoLobby -MechNoDraft -RenderOffscreen -unattended -nosplash -nosound \
  -ForceRes -ResX=1600 -ResY=900 -windowed -log=Intro_$KEY.log "$@" > /dev/null 2>&1
grep -a "\[intro\]" "D:/RobotSlice/Saved/Logs/Intro_$KEY.log" | cut -c31-260
