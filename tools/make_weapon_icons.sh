#!/bin/bash
# 重新出击毁播报的武器剪影（11-01）：换了武器模型、补了缺的肩部装备模型（AMechItemStudio::MeshFor 的 Shoulders[] 多了一项）之后跑这个。
#   1 UE 里把模型导出成 OBJ（tools/ue_export_weapon_meshes.py，到 Saved/WeaponIconMeshes）
#   2 py -3 画成纯白剪影 + 透明底（tools/make_weapon_icons.py，到 SourceAssets/ui_weapon_icons，顺带 _contact.png 总览）
#   3 UE 里导入成 /Game/UI/WeaponIcons/T_WI_*（tools/ue_import_weapon_icons.py）
# 摆放（枪口朝右、握把朝下）在 tools/weapon_icon_table.py 的 POSE，新模型看一眼 _contact.png 要不要翻。HUD 按路径加载，不用改代码、不用重新编译；
# 打包要重新 cook（/Game/UI/WeaponIcons 在 DefaultGame.ini 的 DirectoriesToAlwaysCook 里）。
# 要占用工程：先拿 Saved/ue_busy.txt 锁，编辑器没开着才行。参数 draw 只重跑第 2 步（调 POSE 用，不用开 UE）。
set -e
UE="D:/EPIC/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
PROJ="D:/RobotSlice/RobotSlice.uproject"
export MSYS_NO_PATHCONV=1
ARGS="-AllowCommandletRendering -RenderOffscreen -unattended -nosplash"
if [ "$1" != "draw" ] && [ "$1" != "import" ]; then
  "$UE" "$PROJ" -run=pythonscript -script="D:/RobotSlice/tools/ue_export_weapon_meshes.py" $ARGS -log=WeaponIconExport.log > /dev/null 2>&1 || true
  grep -a "\[weaponicon\]\|Fatal" D:/RobotSlice/Saved/Logs/WeaponIconExport.log | cut -c30-200
fi
if [ "$1" != "import" ]; then
  py -3 D:/RobotSlice/tools/make_weapon_icons.py
fi
if [ "$1" != "draw" ]; then
  "$UE" "$PROJ" -run=pythonscript -script="D:/RobotSlice/tools/ue_import_weapon_icons.py" $ARGS -log=WeaponIconImport.log > /dev/null 2>&1 || true
  grep -a "\[weaponicon\]\|Fatal" D:/RobotSlice/Saved/Logs/WeaponIconImport.log | cut -c30-200
fi
