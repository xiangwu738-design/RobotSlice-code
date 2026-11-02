"""把 valley_arena_plan.py 算好的夺芯布置写进 /Game/Maps/L_Volcano（山谷图）：
- 标签点：CoreFoundry、CoreSpawn + Half0/1、CoreStation + Half0/1 + A/B、CoreNode + Id=N + Link=M（MechCoreArena 自然地形布局读这些）；
  CoreArena 点补 HalfX= / HalfY=（活动范围，厘米）
- 树和石头：actor「CoreVegetation」（标签 CoreObstacles），每种网格两个 HISM：活动范围里的带碰撞（树用网格自带的树干胶囊、
  石头用自带凸包），外面的只看不碰
所有加的东西带 ValleyArena 标签，重跑先删掉旧的。高度在地形上重新打射线量。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_valley_arena.py D:/RobotSlice_MapLab/_survey/v04_plan.json" -nullrhi -unattended
"""
import json
import math
import sys

import unreal

PLAN = json.load(open([a for a in sys.argv[1:] if not a.startswith('-')][0], encoding='utf-8'))
MAP = '/Game/Maps/L_Volcano'
VEG_PATH = '/Game/Env/Valley'
P = lambda *a: unreal.log('[arena] ' + ' '.join(str(x) for x in a))

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
old = [a for a in sub.get_all_level_actors() if 'ValleyArena' in [str(t) for t in a.tags]]
for a in old:
    sub.destroy_actor(a)
P('删掉旧布置', len(old))
land = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]


def ground(x_m, y_m):
    """只打地形（树石都还没放 / 已删），返回厘米"""
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x_m * 100, y_m * 100, 500000), unreal.Vector(x_m * 100, y_m * 100, -500000),
                                                 unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    if hit is None or not hit.to_tuple()[0]:
        return None
    return hit.to_tuple()[4].z


def point(label, x, y, tags, z_off=0.0):
    z = ground(x, y)
    tp = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x * 100, y * 100, (z or 0.0) + z_off))
    tp.set_actor_label(label)
    tp.tags = [unreal.Name(t) for t in tags + ['ValleyArena']]
    return tp, z


# ---- 场地标签点 ----
arena = [a for a in sub.get_all_level_actors() if 'CoreArena' in [str(t) for t in a.tags]][0]
hx, hy = PLAN['half_extent_m']
arena.tags = [t for t in arena.tags if not str(t).startswith(('HalfX=', 'HalfY='))] + [unreal.Name(f'HalfX={hx * 100:.0f}'), unreal.Name(f'HalfY={hy * 100:.0f}')]
f = PLAN['foundry']
_, fz = point('CoreFoundry', f[0], f[1], ['CoreFoundry'])
for h, s in enumerate(PLAN['spawns']):
    point(f'CoreSpawn{h}', s[0], s[1], ['CoreSpawn', f'Half{h}'])
for h, pair in PLAN['stations'].items():
    for k, s in enumerate(pair):
        point(f'CoreStation{h}{"AB"[k]}', s[0], s[1], ['CoreStation', f'Half{h}', 'AB'[k]])
links = {}
for x, y in PLAN['edges']:
    links.setdefault(x, []).append(y)
    links.setdefault(y, []).append(x)
for i, n in enumerate(PLAN['nodes']):
    point(f'CoreNode{i:03d}', n[0], n[1], ['CoreNode', f'Id={i}'] + [f'Link={j}' for j in sorted(links.get(i, []))])
P('标签点：铸造台地面', None if fz is None else round(fz / 100, 2), '路点', len(PLAN['nodes']), '路段', len(PLAN['edges']))

# ---- 树和石头 ----
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)


def add_component(actor, cls, name):
    root = sds.k2_gather_subobject_data_for_instance(actor)[0]
    handle, reason = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root, new_class=cls, blueprint_context=None))
    if not reason.is_empty():
        raise RuntimeError(f'加组件失败 {name}: {reason}')
    sds.rename_subobject(handle, unreal.Text(name))
    return unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))


veg = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0))
veg.set_actor_label('CoreVegetation')
veg.tags = [unreal.Name('CoreObstacles'), unreal.Name('ValleyArena')]
veg.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
params = {}
try:
    for rows in json.load(open('D:/RobotSlice_MapLab/_survey/valley_grass_params.json', encoding='utf-8')).values():
        for r in rows:
            if r.get('name'):
                params[r['name']] = r
except Exception as e:
    P('读品种参数失败', e)


def xform(t, scale=None, z_add=0.0):
    rot = unreal.Rotator(roll=t.get('roll', 0.0), pitch=t.get('pitch', 0.0), yaw=t['yaw'])
    sc = scale or t['scale']
    return unreal.Transform(location=unreal.Vector(t['a'] * 100, t['b'] * 100, t['z'] * 100 + z_add), rotation=rot,
                            scale=unreal.Vector(sc[0], sc[1], sc[2]))


groups = {}
for t in PLAN['trees'] + PLAN['rocks']:
    # 活动范围里的带碰撞（包里的树自带树干胶囊碰撞、石头自带凸包碰撞），外面的只看不碰，省物理
    groups.setdefault((t['path'], bool(t.get('collide'))), []).append(t)
for (path, collide), items in sorted(groups.items()):
    mesh = unreal.load_asset(path.split('.')[0])
    if not mesh:
        P('找不到网格', path)
        continue
    name = mesh.get_name()
    comp = add_component(veg, unreal.HierarchicalInstancedStaticMeshComponent, ('Veg_' if collide else 'VegFar_') + name)
    comp.set_static_mesh(mesh)
    comp.set_mobility(unreal.ComponentMobility.STATIC)
    pr = params.get(name, {})
    cull = pr.get('cull') or [0, 50000]
    comp.set_editor_property('instance_start_cull_distance', int(max(0, cull[1] * 0.8)))
    comp.set_editor_property('instance_end_cull_distance', int(cull[1]))
    comp.set_editor_property('affect_distance_field_lighting', False)
    comp.set_collision_profile_name('BlockAll' if collide else 'NoCollision')
    comp.add_instances([xform(t) for t in items], False, True)
    P('植被', name, len(items), '个', '碰撞' if collide else '无碰撞', '消失距离', cull[1] / 100, '米')

ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
P('保存', MAP, ok)
