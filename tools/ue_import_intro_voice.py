"""开场解说配音导入（10-31）：SourceAssets/voice_intro/final/<编号>.wav → /Game/Audio/Intro/S_intro_<编号>（AMechMatchIntro 按路径加载）。
wav 由 SourceAssets/voice_intro/tools/analyze.py lines 生成（ChatTTS 合成 + 体育场后期），不循环。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_intro_voice.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'D:/RobotSlice/SourceAssets/voice_intro/final'
DEST = '/Game/Audio/Intro'


def task(filename, name):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = DEST
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


jobs = [(f'{SRC}/{f}', 'S_intro_' + os.path.splitext(f)[0]) for f in sorted(os.listdir(SRC)) if f.endswith('.wav')]
tools.import_asset_tasks([task(src, name) for src, name in jobs])
ok = 0
for src, name in jobs:
    p = f'{DEST}/{name}'
    if eas.does_asset_exist(p):
        ok += 1
        w = eas.load_asset(p)
        try:
            w.set_editor_property('looping', False)
        except Exception:
            pass
        eas.save_loaded_asset(w)
        unreal.log(f'[introvoice] {p} 有（{w.get_editor_property("duration"):.2f} 秒）')
    else:
        unreal.log_warning(f'[introvoice] {p} 没导进去')
unreal.log(f'[introvoice] 完成 {ok}/{len(jobs)}')
