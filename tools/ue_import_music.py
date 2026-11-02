"""在 RobotSlice 工程里跑（11-01 背景音乐）：
SourceAssets/music/mus_*.wav（从 D:\\Music 选的曲子，music_convert 转成 48k 立体声、统一到 -16 LUFS）→ /Game/Audio/Music/S_mus_*
（UMechMusic 按路径加载；选曲表见 docs/音乐推荐_20261101.md）。都不设循环：UMechMusic 自己在快放完时交叉淡入重新开始。可以重复跑（覆盖）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_music.py" -unattended -nosplash
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'D:/RobotSlice/SourceAssets/music'
DEST = '/Game/Audio/Music'

jobs = []
for f in sorted(os.listdir(SRC)):
    if f.startswith('mus_') and f.endswith('.wav'):
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
        unreal.log_warning(f'[music] {p} 没导进去')
        continue
    ok += 1
    w = eas.load_asset(p)
    w.set_editor_property('looping', False)
    # 音乐听得出压缩痕迹，质量比音效默认的高一点
    try:
        w.set_editor_property('compression_quality', 70)
    except Exception as e:
        unreal.log_warning(f'[music] {p} 设压缩质量失败：{e}')
    eas.save_loaded_asset(w)
    unreal.log(f'[music] {p} 有（{w.get_editor_property("duration"):.1f} 秒）')
unreal.log(f'[music] 完成 {ok}/{len(jobs)}')
