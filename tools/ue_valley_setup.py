"""山谷夺芯图（用户 10-30 选的 8K Valley Landscape Pack 4 号地形 V04-1 区域）搬进主工程：地形 actor 原样（原分辨率、原材质图层、草系统植被），
不改地形数据，只把整块地形旋转 + 平移，让场地 A→B 沿 +X、场地中心在原点（夺芯代码 / 小地图 / AI 都按场地沿 X 轴写的）。
前置：草稿工程 D:/RobotSlice_MapLab 里在编辑器另存过的 /Game/ValleyLab/L_Valley_4（4.27 老地形不另存，-game 里不长树和草），
      连同 /Game/8KValleyLandscapePack 的 Assets、Terrains 拷进本工程 Content。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_valley_setup.py [步骤…]" -nullrhi -unattended
步骤：move params strip map（不写 = 全做）。params 导出树石品种参数，strip 把树和石头从草类型里拿掉（改由 ue_valley_arena.py 摆成带碰撞的实例）。必须 -nullrhi：带显卡保存时地形要重新合并 1024 个组件的权重图，D3D12 会爆（Too many residency sets）。
"""
import math
import sys

import unreal

args = [a for a in sys.argv[1:] if not a.startswith('-')]
STEPS = set(args) if args else {'move', 'params', 'strip', 'map'}
SRC = '/Game/8KValleyLandscapePack'
DST = '/Game/Env/Valley'
SRC_MAP = '/Game/ValleyLab/L_Valley_4'
MAP = '/Game/Maps/L_Volcano'   # 主菜单的地图表写死在 MechGameMode.cpp（别的会话在改），所以沿用这个路径
# V04-1：原地形里的场地中心（米）和 A→B 方向（度，原地形坐标，X 右 Y 下）
CENTER = (6799.908, 4888.188)
THETA = 105.0
EAL = unreal.EditorAssetLibrary
P = lambda *a: unreal.log('[valley] ' + ' '.join(str(x) for x in a))

if 'move' in STEPS and EAL.does_directory_exist(SRC):
    ok = EAL.rename_directory(SRC, DST)
    P('搬目录', SRC, '→', DST, ok)
    EAL.save_directory(DST, only_if_is_dirty=True, recursive=True)

TREE_ROCK = ('Tree', 'Rock')
GRASS_TYPES = [f'{DST}/Terrains/Grass_Type/LGT_{n}' for n in ('Forest', 'Grass', 'Mud', 'Cliff', 'Soil')]


def pval(v, name):
    x = v.get_editor_property(name)
    for k in ('default', 'default_value'):
        try:
            return x.get_editor_property(k)
        except Exception:
            pass
    return x


if 'params' in STEPS:
    # 树和石头的品种参数 + 网格尺寸 / 碰撞，给离线摆放（valley_arena_plan.py）用
    import json
    out = {}
    for p in GRASS_TYPES:
        gt = unreal.load_asset(p)
        if not gt:
            continue
        rows = []
        for v in gt.get_editor_property('grass_varieties'):
            m = v.get_editor_property('grass_mesh')
            row = {'mesh': m.get_path_name() if m else None, 'name': m.get_name() if m else None, 'density': pval(v, 'grass_density'),
                   'cull': [pval(v, 'start_cull_distance'), pval(v, 'end_cull_distance')]}
            for k in ('use_grid', 'placement_jitter', 'random_rotation', 'align_to_surface', 'use_landscape_lightmap', 'cast_dynamic_shadow',
                      'min_lod', 'weight_attenuates_max_scale', 'max_scale_weight_attenuation'):
                try:
                    row[k] = v.get_editor_property(k)
                except Exception:
                    pass
            try:
                row['scaling'] = str(v.get_editor_property('scaling')).split('.')[-1]
                for k in ('scale_x', 'scale_y', 'scale_z'):
                    iv = v.get_editor_property(k)
                    row[k] = [iv.get_editor_property('min'), iv.get_editor_property('max')]
            except Exception as e:
                row['scale_err'] = str(e)
            if m:
                b = m.get_bounds()
                row['bounds_cm'] = [[b.origin.x - b.box_extent.x, b.origin.y - b.box_extent.y, b.origin.z - b.box_extent.z],
                                    [b.origin.x + b.box_extent.x, b.origin.y + b.box_extent.y, b.origin.z + b.box_extent.z]]
                bs = m.get_editor_property('body_setup')
                if bs:
                    g = bs.get_editor_property('agg_geom')
                    row['collision'] = {k: len(g.get_editor_property(k)) for k in ('sphere_elems', 'box_elems', 'sphyl_elems', 'convex_elems')}
                    row['trace_flag'] = str(bs.get_editor_property('collision_trace_flag')).split('.')[-1]
                row['tris'] = m.get_num_triangles(0) if hasattr(m, 'get_num_triangles') else None
                row['lods'] = m.get_num_lods()
                row['nanite'] = m.get_editor_property('nanite_settings').get_editor_property('enabled')
            rows.append(row)
        out[p.split('/')[-1]] = rows
    fn = 'D:/RobotSlice_MapLab/_survey/valley_grass_params.json'
    json.dump(out, open(fn, 'w', encoding='utf-8'), ensure_ascii=False, indent=1, default=str)
    P('品种参数写到', fn)

if 'strip' in STEPS:
    # 树和石头不再由地形草系统生成（草系统没有碰撞、位置也读不出来），改由 ue_valley_arena.py 在场地周围摆成带碰撞的实例；草和灌木照旧
    for p in GRASS_TYPES:
        gt = unreal.load_asset(p)
        if not gt:
            continue
        vs = list(gt.get_editor_property('grass_varieties'))
        keep = [v for v in vs if not (v.get_editor_property('grass_mesh') and any(k in v.get_editor_property('grass_mesh').get_name() for k in TREE_ROCK))]
        if len(keep) != len(vs):
            gt.set_editor_property('grass_varieties', keep)
            EAL.save_loaded_asset(gt, False)
            P('草类型去掉树石', p.split('/')[-1], len(vs), '→', len(keep))

if 'map' in STEPS:
    unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = sub.get_all_level_actors()
    lands = [a for a in actors if isinstance(a, unreal.LandscapeProxy)]
    P('地形', [l.get_actor_label() for l in lands], '其他', sorted(a.get_class().get_name() for a in actors if a not in lands))
    land = lands[0]
    cx, cy = CENTER[0] * 100.0, CENTER[1] * 100.0
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(cx, cy, 1000000), unreal.Vector(cx, cy, -1000000),
                                                 unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    cz = hit.to_tuple()[4].z if hit is not None and hit.to_tuple()[0] else 0.0
    P('场地中心原坐标（米）', CENTER, '地面', round(cz / 100, 2))
    # 新变换：世界' = R·(世界 − c)，R 绕 Z 转 −THETA 度 → 地形新位置 L1 = R·(L0 − c)
    yaw = -THETA
    r = math.radians(yaw)
    l0 = land.get_actor_location()
    dx, dy, dz = l0.x - cx, l0.y - cy, l0.z - cz
    l1 = unreal.Vector(dx * math.cos(r) - dy * math.sin(r), dx * math.sin(r) + dy * math.cos(r), dz)
    rot0 = land.get_actor_rotation()
    land.set_actor_location_and_rotation(l1, unreal.Rotator(roll=rot0.roll, pitch=rot0.pitch, yaw=rot0.yaw + yaw), False, True)
    P('地形', '位置', [round(v / 100, 1) for v in (l0.x, l0.y, l0.z)], '→', [round(v / 100, 1) for v in (l1.x, l1.y, l1.z)], '转', yaw)
    # 核对：中心应在原点、地面高 0；A 端（原 c − u·400）应在 (−400, 0)
    for name, (x, y) in (('中心', (0, 0)), ('A 端', (-40000, 0)), ('B 端', (40000, 0))):
        h = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 1000000), unreal.Vector(x, y, -1000000),
                                                   unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(h, tuple):
            h = h[1] if h[0] else None
        P('核对', name, '地面', round(h.to_tuple()[4].z / 100, 2) if h is not None and h.to_tuple()[0] else None)
    # 掉出世界：地形最低处远在场地下面（原地形高差 768 米）
    ws = world.get_world_settings()
    ws.set_editor_property('kill_z', -150000.0)
    # 场地中心点（GameMode 找带 CoreArena 标签的点，在这里搭夺芯场地；CoreTerrain = 自然地形布局，不搭灰盒）
    tp = sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0))
    tp.set_actor_label('CoreArena')
    tp.tags = [unreal.Name('CoreArena'), unreal.Name('CoreTerrain')]
    cam = sub.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 50000))
    cam.set_actor_label('ShotCam')
    cam.tags = [unreal.Name('ShotCam')]
    cam.get_editor_property('root_component').set_mobility(unreal.ComponentMobility.MOVABLE)
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    P('另存', MAP, ok)
