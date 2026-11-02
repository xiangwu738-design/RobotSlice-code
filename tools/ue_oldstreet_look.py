"""只改老旧街道的光照调参和后期（读 tools/oldstreet_look.json），不重搭。调色时用。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_oldstreet_look.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
"""
import json

import unreal

DST_MAP = '/Game/Maps/L_OldStreet'
TUNE = json.load(open('D:/RobotSlice/tools/oldstreet_look.json', encoding='utf-8'))
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[oldstreet] ' + ' '.join(str(x) for x in a))
unreal.EditorLoadingAndSavingUtils.load_map(DST_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if 'LightingDirector' in cls:
        t = a.get_editor_property('tuning_settings')
        for k, v in TUNE['tuning'].items():
            t.set_editor_property(k, v)
        a.set_editor_property('tuning_settings', t)
        log('光照导演已更新')
    elif cls == 'PostProcessVolume' and a.get_actor_label() == 'PP_OldFilm':
        pp = a.get_editor_property('settings')
        for key, value in TUNE['post'].items():
            if isinstance(value, list):
                value = unreal.Vector4(*value)
            pp.set_editor_property('override_' + key, True)
            pp.set_editor_property(key, value)
        a.set_editor_property('settings', pp)
        log('后期已更新')
    elif 'LightingEnvironmentRig' in cls:
        atm = a.get_editor_property('atmosphere')
        for k, v in TUNE.get('atmosphere', {}).items():
            if isinstance(v, list):
                v = unreal.LinearColor(*v)
            atm.set_editor_property(k, v)
        log('天空大气已更新')
unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log('已存')
