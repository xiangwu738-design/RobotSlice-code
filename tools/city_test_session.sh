#!/bin/bash
# 城区图一轮验证（在 city_ue_run.sh 里跑，拿着 UE 锁）：编译 → 贴图预算 → 成图机位 → 3v3 死斗自动对战 → 帧率
export MSYS_NO_PATHCONV=1
cd /d/RobotSlice
if [ "${CITY_SKIP_BUILD:-0}" != "1" ]; then
  "/d/EPIC/UE_5.8/Engine/Build/BatchFiles/Build.bat" RobotSliceEditor Win64 Development -Project="D:\RobotSlice\RobotSlice.uproject" -WaitMutex > Saved/Logs/city_compile.log 2>&1
  echo "[session] 编译退出码 $?"; grep -a -E "error|Result:|Total execution" Saved/Logs/city_compile.log | tail -5
fi
for step in ${CITY_STEPS:-tex shots battle perf}; do
  echo "[session] ---- $step $(date +%H:%M:%S)"
  case $step in
    build) /d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe D:/RobotSlice/RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_build_city.py"              -AllowCommandletRendering -RenderOffscreen -unattended -nosplash > Saved/Logs/city_build.log 2>&1
           echo "[session] 搭图退出码 $?"; grep -a "\[city\]" Saved/Logs/RobotSlice.log | tail -3 | cut -c30-200 ;;
    battle) bash tools/run_city_test.sh battle DM -MechDMTime=${CITY_DM_TIME:-200} -MechHour=11 -MechWeather=Overcast ;;
    elim) bash tools/run_city_test.sh battle Elim -MechWinRounds=1 -MechHour=11 -MechWeather=Overcast ;;
    mat) /d/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe D:/RobotSlice/RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_import_city.py mat" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash > Saved/Logs/city_mat.log 2>&1
         echo "[session] 材质退出码 $?"; grep -a "\[city\]" Saved/Logs/RobotSlice.log | tail -2 | cut -c30-200 ;;
    perf) bash tools/run_city_test.sh perf -MechHour=11 -MechWeather=Overcast ;;
    intro) bash tools/run_intro_test.sh city a -MechHour=11 -MechWeather=Overcast ;;
    shots) bash tools/run_city_test.sh shots -MechHour=11 -MechWeather=Overcast ;;
    *) bash tools/run_city_test.sh $step ;;
  esac
done
echo "[session] 结束 $(date +%H:%M:%S)"
