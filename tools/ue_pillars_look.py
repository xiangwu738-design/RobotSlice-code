"""只改石柱山水的整体色调（读 tools/pillars_look.json）：后期体积 PP_Pillars 的调色、水面材质实例 MI_PL_Water 的三档颜色。不重搭地图。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_pillars_look.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
（本地测试流程里 kind=py 跑它）
"""
import json

import unreal

DST_MAP = '/Game/Maps/L_Pillars'
WATER_MI = '/Game/Env/Pillars/Materials/MI_PL_Water'
TUNE = json.load(open('D:/RobotSlice/tools/pillars_look.json', encoding='utf-8'))
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[pillarslook] ' + ' '.join(str(x) for x in a))

mi = eas.load_asset(WATER_MI)
if mi:
    for k, v in TUNE.get('water', {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
        log('水面', k, v)
    eas.save_loaded_asset(mi)
else:
    log('找不到水面材质实例', WATER_MI)

unreal.EditorLoadingAndSavingUtils.load_map(DST_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
done = 0
for a in sub.get_all_level_actors():
    if a.get_class().get_name() == 'PostProcessVolume' and a.get_actor_label() == 'PP_Pillars':
        pp = a.get_editor_property('settings')
        for key, value in TUNE['post'].items():
            if isinstance(value, list):
                value = unreal.Vector4(*value)
            try:
                pp.set_editor_property('override_' + key, True)
                pp.set_editor_property(key, value)
            except Exception as e:
                log('后期设不了', key, e)
        a.set_editor_property('settings', pp)
        done += 1
log('后期体积改了', done, '个')
unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log('已存')
