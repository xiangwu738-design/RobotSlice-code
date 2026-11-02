"""只读：盘点 /Game/Maps/L_Nature 里的东西（出生点、地形、水面、废墟、植被、声音、后期），不保存。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="tools/ue_nature_survey.py" -AllowCommandletRendering -RenderOffscreen
"""
import collections

import unreal

MAP = '/Game/Maps/L_Nature'
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
actors = sub.get_all_level_actors()
P = lambda *a: unreal.log('[survey] ' + ' '.join(str(x) for x in a))
m = lambda v: f'({v.x / 100:.0f},{v.y / 100:.0f},{v.z / 100:.0f})'

cls = collections.Counter(a.get_class().get_name() for a in actors)
P('actor 总数', len(actors), dict(cls.most_common(40)))
for a in actors:
    if isinstance(a, unreal.PlayerStart):
        P('出生点', a.get_name(), m(a.get_actor_location()), 'yaw', round(a.get_actor_rotation().yaw), [str(t) for t in a.tags])
for a in actors:
    if isinstance(a, unreal.LandscapeProxy):
        o, e = a.get_actor_bounds(False)
        mat = a.get_editor_property('landscape_material')
        P('地形', a.get_name(), '中心', m(o), '半径', m(e), '材质', mat.get_path_name() if mat else None)
# 静态网格：按网格统计数量和平均位置；挑出大块的（水面、山、废墟）
by_mesh = collections.defaultdict(list)
for a in actors:
    if isinstance(a, unreal.StaticMeshActor):
        sm = a.static_mesh_component.static_mesh
        by_mesh[sm.get_path_name() if sm else 'None'].append(a)
for k, v in sorted(by_mesh.items(), key=lambda kv: -len(kv[1])):
    a = v[0]
    o, e = a.get_actor_bounds(False)
    mats = [x.get_path_name().split('/')[-1] for x in a.static_mesh_component.get_materials() if x][:3]
    P('网格', len(v), k.split('/')[-1], '例', a.get_name(), m(a.get_actor_location()), '半尺寸', m(e), '文件夹', a.get_folder_path(), mats, [str(t) for t in a.tags][:3])
for a in actors:
    n = a.get_name().lower() + ' ' + a.get_actor_label().lower()
    if any(k in n for k in ('water', 'lake', 'river', 'ocean', 'sea')):
        o, e = a.get_actor_bounds(False)
        P('水', a.get_class().get_name(), a.get_actor_label(), '中心', m(o), '半尺寸', m(e))
# 植被
for a in actors:
    if isinstance(a, unreal.InstancedFoliageActor):
        comps = a.get_components_by_class(unreal.InstancedStaticMeshComponent)
        tot = 0
        for c in comps:
            n = c.get_instance_count()
            tot += n
            sm = c.static_mesh
            P('植被', n, sm.get_name() if sm else None, 'cull', c.get_editor_property('instance_end_cull_distance'))
        P('植被合计', tot, '组件', len(comps))
for a in actors:
    k = a.get_class().get_name()
    if k in ('AmbientSound', 'PostProcessVolume', 'ExponentialHeightFog', 'SkyAtmosphere', 'DirectionalLight', 'SkyLight', 'VolumetricCloud',
             'DecalActor', 'LevelSequenceActor', 'BP_Sky_Sphere_C', 'Note', 'BlockingVolume', 'TriggerVolume'):
        P(k, a.get_actor_label(), m(a.get_actor_location()), a.get_folder_path())
for a in actors:
    if 'Nature' in [str(t) for t in a.tags]:
        P('废墟', a.get_actor_label(), a.static_mesh_component.static_mesh.get_name(), m(a.get_actor_location()), 'scale', round(a.get_actor_scale3d().x, 2))
# 废墟材质：母材质、参数、谁在用
ar = unreal.AssetRegistryHelpers.get_asset_registry()
opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
seen = set()
for sm_path in sorted(k for k in by_mesh if '/Env/Ruins/' in k)[:3]:
    sm = unreal.load_asset(sm_path)
    for i, mat in enumerate(sm.get_editor_property('static_materials')):
        mi = mat.material_interface
        if not mi or mi.get_path_name() in seen:
            continue
        seen.add(mi.get_path_name())
        par = mi.get_editor_property('parent') if isinstance(mi, unreal.MaterialInstance) else None
        tex = [(p.parameter_info.name, p.parameter_value.get_name() if p.parameter_value else None) for p in mi.get_editor_property('texture_parameter_values')] if par else []
        sca = [(p.parameter_info.name, round(p.parameter_value, 3)) for p in mi.get_editor_property('scalar_parameter_values')] if par else []
        P('废墟材质', sm.get_name(), i, mat.material_slot_name, mi.get_path_name(), '母', par.get_path_name() if par else None, tex, sca)
for pkg in ('/Game/Env/Ruins/Materials/MI_concreteBlocks', '/Game/Env/Materials/M_KitSG', '/Game/Env/Ruins/Meshes/SM_Bldg_LG_A_grp'):
    refs = ar.get_referencers(pkg, opt)
    P('引用', pkg, [str(r) for r in refs][:20])
P('完成')
