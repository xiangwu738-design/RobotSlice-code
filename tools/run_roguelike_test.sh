#!/bin/bash
# 11-01 保卫战肉鸽切片的测试（Source/RobotSlice/MechRunTest.cpp）。用法：
#   tools/run_roguelike_test.sh logic   逐张卡放一遍动作，只看日志（-nullrhi，L_Range，不用显卡，不动画质设置）
#   tools/run_roguelike_test.sh shots   同一套测试带截图（L_Defense，离屏渲染，截图在 Saved/Screenshots/Run；要显卡，用户在玩时别跑）
#   tools/run_roguelike_test.sh net     本机双开（-nullrhi，不用显卡）：房主 + 客户端，客户端升级、选卡、开枪、放重斩、放导弹，看效果（MechRunTest.cpp 的 -MechRunNetTest）
#   tools/run_roguelike_test.sh netshots 同上，离屏渲染，客户端截图在 Saved/Screenshots/RunNet（要显卡，用户在玩时别跑）
# 跑之前自己拿 Saved/ue_busy.txt 锁；-ForceRes 会改写用户的画质设置：shots、netshots 开头备份、结尾还原。
MODE="${1:-logic}"
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
GUS="D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini"
export MSYS_NO_PATHCONV=1
show() {
  grep -a -E "$2" "$1" | cut -c1-260 | sed 's/^\[[^]]*\]\[[^]]*\]//'
}
case "$MODE" in
logic)
  timeout 400 "$UE" "$PROJ" "/Game/Maps/L_Range?Defense" -game -MechRunTest -MechNoIntro -nullrhi -nosound -unattended -nosplash -log=RunTest.log > /dev/null 2>&1
  echo "exit $?"
  show D:/RobotSlice/Saved/Logs/RunTest.log "\[run\] (通过|没通过|汇总)|Fatal error|Assertion failed|Unhandled Exception"
  ;;
shots)
  cp -f "$GUS" "$GUS.runtest_bak"
  timeout 500 "$UE" "$PROJ" "/Game/Maps/L_Defense?Defense" -game -MechRunTest -MechNoIntro -RenderOffscreen -windowed -ForceRes -ResX=1920 -ResY=1080 -nosound -unattended -nosplash -log=RunTestShots.log > /dev/null 2>&1
  echo "exit $?"
  mv -f "$GUS.runtest_bak" "$GUS"
  show D:/RobotSlice/Saved/Logs/RunTestShots.log "\[run\] (没通过|汇总)|Fatal error|Assertion failed|Unhandled Exception"
  ;;
net|netshots)
  if [ "$MODE" = "netshots" ]; then
    cp -f "$GUS" "$GUS.runtest_bak"
    COMMON="-game -MechNoIntro -MechRunLoadout -RenderOffscreen -windowed -ForceRes -ResX=1280 -ResY=720 -nosound -unattended -nosplash"
  else
    # -MechRunLoadout：固定步枪 + 大剑 + 导弹。不加的话按用户存的配装，步枪和导弹的卡不生效
    COMMON="-game -MechNoIntro -MechRunLoadout -nullrhi -nosound -unattended -nosplash"
  fi
  timeout 300 "$UE" "$PROJ" "/Game/Maps/L_Range?Defense?listen" $COMMON -MechRunNetTest=Host -log=RunNetHost.log > /dev/null 2>&1 &
  HOST=$!
  sleep 25
  timeout 270 "$UE" "$PROJ" 127.0.0.1 $COMMON -MechRunNetTest=Client -log=RunNetClient.log > /dev/null 2>&1 &
  CLIENT=$!
  wait $HOST $CLIENT
  if [ "$MODE" = "netshots" ]; then
    mv -f "$GUS.runtest_bak" "$GUS"
  fi
  echo "== 房主 =="
  show D:/RobotSlice/Saved/Logs/RunNetHost.log "\[runnet\]|\[run\] 选卡|Fatal error|Assertion failed|Unhandled Exception"
  echo "== 客户端 =="
  show D:/RobotSlice/Saved/Logs/RunNetClient.log "\[runnet\]|Fatal error|Assertion failed|Unhandled Exception"
  ;;
esac
