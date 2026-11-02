"""在 RobotSlice 工程里跑（10-31 结算场景的配乐和音效）：
SourceAssets/sfx_results/results_*.wav（make_results_music.py 合成的配乐 / 底噪、make_results_sfx.py 加工的音效）→ /Game/Audio/Results/S_results_*
（AMechResultsStage::LoadAudio 按路径加载）。底噪 S_results_bed 设循环，其余不循环。可以重复跑（覆盖）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_results_audio.py" -unattended -nosplash
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'D:/RobotSlice/SourceAssets/sfx_results'
DEST = '/Game/Audio/Results'

jobs = []
for f in sorted(os.listdir(SRC)):
    if f.startswith('results_') and f.endswith('.wav'):
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
        unreal.log_warning(f'[resultsaudio] {p} 没导进去')
        continue
    ok += 1
    w = eas.load_asset(p)
    try:
        w.set_editor_property('looping', t.destination_name == 'S_results_bed')
    except Exception as e:
        unreal.log_warning(f'[resultsaudio] {p} 设循环失败：{e}')
    eas.save_loaded_asset(w)
    unreal.log(f'[resultsaudio] {p} 有（{w.get_editor_property("duration"):.2f} 秒，循环 {w.get_editor_property("looping")}）')
unreal.log(f'[resultsaudio] 完成 {ok}/{len(jobs)}')
