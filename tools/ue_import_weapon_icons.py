"""把 SourceAssets/ui_weapon_icons/T_WI_*.png 导入成 /Game/UI/WeaponIcons/T_WI_*（MechHudFeed.cpp 按路径加载；这个目录在 DefaultGame.ini 的 DirectoriesToAlwaysCook 里）。流程见 tools/make_weapon_icons.sh。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_weapon_icons.py" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import os
import sys

import unreal

sys.path.insert(0, 'D:/RobotSlice/tools')
import weapon_icon_table as table

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
files = sorted(f for f in os.listdir(table.OUT_DIR) if f.startswith('T_WI_') and f.endswith('.png'))
tasks = []
for f in files:
    t = unreal.AssetImportTask()
    t.filename = f'{table.OUT_DIR}/{f}'
    t.destination_path = table.DEST
    t.destination_name = os.path.splitext(f)[0]
    t.automated = True
    t.replace_existing = True
    t.save = False
    tasks.append(t)
tools.import_asset_tasks(tasks)
ok = 0
for f in files:
    p = f'{table.DEST}/{os.path.splitext(f)[0]}'
    tex = eas.load_asset(p)
    if not tex:
        unreal.log_warning(f'[weaponicon] {p} 没导进去')
        continue
    # 不压缩（只有白色 + 透明，贴图很小，压缩反而边缘脏）；要 Mip：1080p 下显示只有贴图高度的四分之一，没有 Mip 缩小时闪；不流送，HUD 第一帧就要清楚
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('srgb', True)
    eas.save_loaded_asset(tex)
    ok += 1
    unreal.log(f'[weaponicon] {p} 有（{tex.blueprint_get_size_x()}×{tex.blueprint_get_size_y()}）')
unreal.log(f'[weaponicon] 导入完成 {ok}/{len(files)}')
