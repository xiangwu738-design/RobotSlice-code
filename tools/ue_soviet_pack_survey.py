"""苏联板楼图：看冬季包 / 天气包在 5.8 里有什么、能不能读。只读，不存盘。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_soviet_pack_survey.py" -nullrhi -unattended -nosplash
输出 Inbox/_k62/pack_survey.json（资产清单）+ pack_maps.json（五张示例图 + 天气示例图的 actor 和氛围参数）"""
import collections
import json
import sys

import unreal

OUT = 'D:/RobotSlice/Inbox/_k62'
eas = unreal.EditorAssetLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
reg.wait_for_completion() if hasattr(reg, 'wait_for_completion') else None


def log(m):
    unreal.log(f'[survey] {m}')


res = {}
for root in ('/Game/ANGRY_MESH', '/Game/VFXWeatherPack'):
    paths = eas.list_assets(root, recursive=True, include_folder=False)
    log(f'{root}: {len(paths)} 个资产')
    by = collections.defaultdict(list)
    for p in paths:
        ad = eas.find_asset_data(p)
        cls = str(ad.asset_class_path.asset_name)
        e = {'path': str(ad.package_name)}
        if cls == 'Texture2D':
            e['dims'] = str(ad.get_tag_value('Dimensions') or '')
            e['fmt'] = str(ad.get_tag_value('Format') or '')
        if cls == 'StaticMesh':
            e['tris'] = str(ad.get_tag_value('Triangles') or '')
            e['size'] = str(ad.get_tag_value('ApproxSize') or '')
            e['nanite'] = str(ad.get_tag_value('NaniteEnabled') or '')
        by[cls].append(e)
    res[root] = by
    log({k: len(v) for k, v in by.items()})
json.dump(res, open(f'{OUT}/pack_survey.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
log('清单写完')

# 粒子：能不能加载、类
for p in eas.list_assets('/Game/VFXWeatherPack/Particles') + eas.list_assets('/Game/ANGRY_MESH/NaturePack_WinterEnvironment/ParticleSystems'):
    a = eas.load_asset(p)
    log(f'粒子 {p} -> {a.get_class().get_name() if a else "读不出"}')

MAPS = sys.argv[1:] or ['Winter_Demo_All_Assets', 'Winter_Demo_01_Storm']
PATHS = {'NatureEffectsMap': '/Game/VFXWeatherPack/NatureEffectsMap'}
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out = {}
PROPS = {
    'DirectionalLight': ['intensity', 'light_color', 'temperature', 'use_temperature', 'light_source_angle', 'cast_shadows', 'indirect_lighting_intensity', 'volumetric_scattering_intensity'],
    'SkyLight': ['intensity', 'light_color', 'source_type', 'cubemap', 'real_time_capture', 'lower_hemisphere_color'],
    'ExponentialHeightFog': ['fog_density', 'fog_height_falloff', 'fog_inscattering_luminance', 'fog_inscattering_color', 'directional_inscattering_luminance', 'start_distance', 'fog_max_opacity', 'fog_cutoff_distance', 'volumetric_fog', 'volumetric_fog_albedo', 'volumetric_fog_extinction_scale', 'second_fog_data'],
    'SkyAtmosphere': [],
}
for name in MAPS:
    p = PATHS.get(name, f'/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Maps/{name}')
    log(f'加载 {p}')
    w = unreal.EditorLoadingAndSavingUtils.load_map(p)
    actors = sub.get_all_level_actors()
    cnt = collections.Counter(a.get_class().get_name() for a in actors)
    d = {'count': cnt.most_common(), 'detail': []}
    log(f'{name}: {len(actors)} 个 actor {cnt.most_common(20)}')
    for a in actors:
        cls = a.get_class().get_name()
        e = {'class': cls, 'label': a.get_actor_label(), 'loc': str(a.get_actor_location()), 'rot': str(a.get_actor_rotation()), 'scale': str(a.get_actor_scale3d())}
        if cls == 'StaticMeshActor':
            sm = a.static_mesh_component.static_mesh
            e['mesh'] = sm.get_path_name() if sm else None
            if name != 'Winter_Demo_All_Assets':
                continue      # 大图只看氛围相关
        elif 'Landscape' in cls or 'Foliage' in cls and name != 'Winter_Demo_All_Assets':
            for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                pass
        for comp in a.get_components_by_class(unreal.ActorComponent):
            ccls = comp.get_class().get_name()
            for k, plist in PROPS.items():
                if k in ccls:
                    for pp in plist:
                        try:
                            e[pp] = str(comp.get_editor_property(pp))
                        except Exception as ex:
                            pass
            if 'ParticleSystemComponent' in ccls:
                try:
                    t = comp.get_editor_property('template')
                    e['particle'] = t.get_path_name() if t else None
                except Exception:
                    pass
            if isinstance(comp, unreal.StaticMeshComponent) and 'mesh' not in e:
                sm = comp.static_mesh
                e['mesh'] = sm.get_path_name() if sm else None
                try:
                    e['mats'] = [m.get_path_name() if m else None for m in comp.get_materials()]
                except Exception:
                    pass
        if cls == 'PostProcessVolume':
            s = a.get_editor_property('settings')
            on = {}
            for pname in ('auto_exposure_method', 'auto_exposure_bias', 'auto_exposure_min_brightness', 'auto_exposure_max_brightness', 'color_grading_lut', 'color_grading_intensity', 'bloom_intensity', 'vignette_intensity', 'white_temp', 'color_saturation', 'color_contrast', 'color_gain', 'film_slope', 'ambient_occlusion_intensity', 'dynamic_global_illumination_method', 'reflection_method'):
                try:
                    if s.get_editor_property('override_' + pname):
                        v = s.get_editor_property(pname)
                        on[pname] = v.get_path_name() if hasattr(v, 'get_path_name') else str(v)
                except Exception as ex:
                    pass
            e['pp'] = on
            e['unbound'] = a.get_editor_property('unbound')
        d['detail'].append(e)
    # 蓝图 actor 的类路径
    d['classes'] = sorted({a.get_class().get_path_name() for a in actors})
    out[name] = d
json.dump(out, open(f'{OUT}/pack_maps.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
log('完成')
