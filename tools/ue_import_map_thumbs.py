"""在 RobotSlice 工程里跑（11-02 房间页地图缩略图）：
SourceAssets/ui_maps/T_Map_<地图>.png（tools/room_map_thumbs.py 裁好的 640×360）→ /Game/UI/Maps/T_Map_<地图>（MechDraftUI 按路径加载，Config/DefaultGame.ini 的 DirectoriesToAlwaysCook 里有这个目录）。
界面贴图：不压缩成法线那些，不要 mip、不流送。可以重复跑（覆盖）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_map_thumbs.py" -unattended -nosplash
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'D:/RobotSlice/SourceAssets/ui_maps'
DEST = '/Game/UI/Maps'

jobs = []
for f in sorted(os.listdir(SRC)):
    if f.startswith('T_Map_') and f.endswith('.png'):
        t = unreal.AssetImportTask()
        t.filename = f'{SRC}/{f}'
        t.destination_path = DEST
        t.destination_name = os.path.splitext(f)[0]
        t.automated = True
        t.replace_existing = True
        t.save = True
        jobs.append(t)
tools.import_asset_tasks(jobs)
for t in jobs:
    p = f'{DEST}/{t.destination_name}'
    if not eas.does_asset_exist(p):
        unreal.log_warning(f'[mapthumb] {p} 没导进去')
        continue
    tex = eas.load_asset(p)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('never_stream', True)
    tex.set_editor_property('srgb', True)
    eas.save_loaded_asset(tex)
    unreal.log(f'[mapthumb] {p} 有：{tex.blueprint_get_size_x()}×{tex.blueprint_get_size_y()}')
