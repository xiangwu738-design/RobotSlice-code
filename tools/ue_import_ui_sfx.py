"""在 RobotSlice 工程里跑（11-01 菜单按钮音效）：
SourceAssets/sfx_ui/ui_*.wav（make_ui_sfx.py 合成）→ /Game/Audio/UI/S_ui_*（MechUIKit.cpp 的 MechKitPriv::UISound 按路径加载）。
都不循环。可以重复跑（覆盖）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_ui_sfx.py" -unattended -nosplash
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'D:/RobotSlice/SourceAssets/sfx_ui'
DEST = '/Game/Audio/UI'

jobs = []
for f in sorted(os.listdir(SRC)):
    if f.startswith('ui_') and f.endswith('.wav'):
        t = unreal.AssetImportTask()
        t.filename = f'{SRC}/{f}'
        t.destination_path = DEST
        t.destination_name = 'S_' + os.path.splitext(f)[0]
        t.automated = True
        t.replace_existing = True
        t.save = True
        jobs.append(t)
tools.import_asset_tasks(jobs)
ok = 0
for t in jobs:
    p = f'{DEST}/{t.destination_name}'
    if not eas.does_asset_exist(p):
        unreal.log_warning(f'[uisfx] {p} 没导进去')
        continue
    ok += 1
    w = eas.load_asset(p)
    try:
        w.set_editor_property('looping', False)
    except Exception as e:
        unreal.log_warning(f'[uisfx] {p} 设循环失败：{e}')
    eas.save_loaded_asset(w)
    unreal.log(f'[uisfx] {p} 有（{w.get_editor_property("duration"):.3f} 秒）')
unreal.log(f'[uisfx] 完成 {ok}/{len(jobs)}')
