"""在 RobotSlice 工程里跑（10-31 战斗界面战机式告警）：
- SourceAssets/sfx_hud/*.wav（tools/make_hud_sfx.py 合成）→ /Game/Audio/HUD/S_hud_*（UMechHudAlerts 按路径加载；preview/ 子目录的试听不导入）
- SourceAssets/sfx_hud/voice/<female|mech|corp>/*.wav（voice/make_hud_voice.py：Windows 离线语音 + 后期）→ /Game/Audio/HUD/Voice/S_vo_<风格>_<句>
  （raw/ 是没加效果的原始合成，不导入）
都不循环：循环告警、重复播报都是代码按节奏一下一下触发的（离得越近越快、换告警时掐掉上一下）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_hud_sfx.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SFX = 'D:/RobotSlice/SourceAssets/sfx_hud'
VOICE = f'{SFX}/voice'


def task(filename, dest, name):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


jobs = []  # (源文件, 目录, 资源名)
for f in sorted(os.listdir(SFX)):
    if f.endswith('.wav'):
        jobs.append((f'{SFX}/{f}', '/Game/Audio/HUD', 'S_' + os.path.splitext(f)[0]))
for style in ('female', 'mech', 'corp'):
    d = f'{VOICE}/{style}'
    if not os.path.isdir(d):
        continue
    for f in sorted(os.listdir(d)):
        if f.endswith('.wav'):
            jobs.append((f'{d}/{f}', '/Game/Audio/HUD/Voice', f'S_vo_{style}_' + os.path.splitext(f)[0]))

tools.import_asset_tasks([task(src, dest, name) for src, dest, name in jobs])
ok = 0
for src, dest, name in jobs:
    p = f'{dest}/{name}'
    if eas.does_asset_exist(p):
        ok += 1
        w = eas.load_asset(p)
        try:
            w.set_editor_property('looping', False)
        except Exception:
            pass
        eas.save_loaded_asset(w)
        unreal.log(f'[hudsfx] {p} 有（{w.get_editor_property("duration"):.2f} 秒）')
    else:
        unreal.log_warning(f'[hudsfx] {p} 没导进去')
unreal.log(f'[hudsfx] 完成 {ok}/{len(jobs)}')
