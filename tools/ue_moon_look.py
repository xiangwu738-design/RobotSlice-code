"""只改月壤材质实例 MI_Moon_Ground 的参数（读 tools/moon_look.json），不重导贴图、不重搭地图。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_moon_look.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
"""
import json

import unreal

MI = '/Game/Env/Moon/Materials/MI_Moon_Ground'
TUNE = json.load(open('D:/RobotSlice/tools/moon_look.json', encoding='utf-8'))
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
mi = eas.load_asset(MI)
if not mi:
    raise RuntimeError('找不到 ' + MI)
for k, v in TUNE.get('scalars', {}).items():
    mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    unreal.log_warning(f'[moonlook] {k} = {v}')
eas.save_loaded_asset(mi)
unreal.log_warning('[moonlook] 已存 ' + MI)
