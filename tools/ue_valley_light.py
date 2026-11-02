"""山谷图（L_Volcano）的光照：包里原来是傍晚低太阳，出生点前的大坡整片在阴影里、天光也弱，前景几乎全黑。
改成下午两三点的太阳（仰角约 45°、斜着照过场地），天光实时采集并加强，后期自动曝光稍微提亮（包里的曝光范围按 10 勒克斯太阳配好了，不动）。先打印原值再改。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_valley_light.py [dump]" -nullrhi -unattended
参数 dump：只打印不改。
"""
import sys

import unreal

DUMP = 'dump' in sys.argv[1:]
MAP = '/Game/Maps/L_Volcano'
SUN_PITCH = -46.0     # 仰角 46°
SUN_YAW = 125.0       # 场地沿 +X：太阳从左后方斜照，坡面有明暗
P = lambda *a: unreal.log('[light] ' + ' '.join(str(x) for x in a))

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = sub.get_all_level_actors()


def props(obj, names):
    out = {}
    for n in names:
        try:
            v = obj.get_editor_property(n)
            out[n] = round(v, 3) if isinstance(v, float) else str(v)[:60]
        except Exception as e:
            out[n] = '?'
    return out


for a in actors:
    if isinstance(a, unreal.DirectionalLight):
        c = a.light_component
        P('太阳', a.get_actor_rotation(), props(c, ['intensity', 'light_color', 'temperature', 'use_temperature', 'atmosphere_sun_light',
                                                 'cast_shadows', 'dynamic_shadow_distance_movable_light', 'light_source_angle']))
        if not DUMP:
            a.set_actor_rotation(unreal.Rotator(roll=0, pitch=SUN_PITCH, yaw=SUN_YAW), False)
    elif isinstance(a, unreal.SkyLight):
        c = a.light_component
        P('天光', props(c, ['intensity', 'real_time_capture', 'source_type', 'lower_hemisphere_is_black', 'lower_hemisphere_color', 'mobility']))
        if not DUMP:
            c.set_editor_property('real_time_capture', True)
            c.set_editor_property('intensity', max(1.5, c.get_editor_property('intensity')))
            c.set_editor_property('lower_hemisphere_is_black', False)
            c.set_editor_property('lower_hemisphere_color', unreal.LinearColor(0.12, 0.13, 0.11, 1.0))
    elif isinstance(a, unreal.PostProcessVolume):
        s = a.settings
        P('后期', 'unbound', a.unbound, props(s, ['auto_exposure_method', 'auto_exposure_min_brightness', 'auto_exposure_max_brightness',
                                                 'auto_exposure_bias', 'override_auto_exposure_min_brightness', 'override_auto_exposure_max_brightness',
                                                 'override_auto_exposure_bias', 'bloom_intensity', 'override_bloom_intensity', 'vignette_intensity']))
        if not DUMP:
            # 包里原来就是按 10 勒克斯的太阳配的自动曝光（0.75~5 EV），范围不动，只稍微提亮
            s.set_editor_property('override_auto_exposure_bias', True)
            s.set_editor_property('auto_exposure_bias', 1.3)
            a.set_editor_property('settings', s)
    elif isinstance(a, unreal.ExponentialHeightFog):
        P('雾', a.get_actor_location(), props(a.component, ['fog_density', 'fog_height_falloff', 'fog_inscattering_luminance', 'volumetric_fog']))
if not DUMP:
    P('保存', unreal.EditorLoadingAndSavingUtils.save_map(world, MAP))
