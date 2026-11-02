"""在 L_Nature 上做场景细化（可重复跑：先删掉上次加的，再加）：
1. 废墟残楼：原来所有材质槽都是引擎默认的灰格子材质（导入时没挂上），在关卡里给每栋楼按槽名挂 /Game/Env/Nature/Materials/MI_RN_*
   （风化母材质 M_RuinNature）。只是关卡里的覆盖材质，网格资产本身不改。
2. 环境音：两层全图 2D 底噪（白天林鸟 + 轻风）+ 若干位置声（树丛鸟叫 / 树叶、草地虫鸣、湖边水声、往废墟方向的风和乌鸦），
   位置按植被实例和湖的样条自动找，集中在对战区（出生点和它前方 90 米之间）周围 250 米内。
加的东西都带标签 NatureDetail、放在 Nature/Detail 文件夹。撤销：tools/ue_nature_detail.py undo（删掉这些 actor、去掉废墟覆盖材质）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="tools/ue_nature_detail.py [undo|nosound|nomat|nograss|boulder]" -AllowCommandletRendering -RenderOffscreen
"""
import math
import random
import sys

import unreal

MAP = '/Game/Maps/L_Nature'
TAG = 'NatureDetail'
RUIN_TAG = 'Nature'
MAT = '/Game/Env/Nature/Materials'
AUD = '/Game/Audio/Nature'
args = sys.argv[1:]
UNDO = 'undo' in args
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[nature-detail] ' + ' '.join(str(x) for x in a))
rng = random.Random(29)

unreal.EditorLoadingAndSavingUtils.load_map(MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = sub.get_all_level_actors()
removed = 0
for a in actors:
    if TAG in [str(t) for t in a.tags]:
        sub.destroy_actor(a)
        removed += 1
log('删掉上次加的', removed)

# ---------------------------------------------------------------- 废墟覆盖材质
ruins = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor) and RUIN_TAG in [str(t) for t in a.tags]]
if 'nomat' not in args:
    n = miss = 0
    missing = set()
    for a in ruins:
        c = a.static_mesh_component
        slots = c.static_mesh.get_editor_property('static_materials')
        if UNDO:
            c.set_editor_property('override_materials', [])
            continue
        for i, s in enumerate(slots):
            name = str(s.material_slot_name)
            path = f'{MAT}/MI_RN_{name}'
            if eas.does_asset_exist(path):
                c.set_material(i, eas.load_asset(path))
                n += 1
            else:
                missing.add(name)
                miss += 1
    log('废墟', len(ruins), '栋，挂材质', n, '个槽，缺', miss, sorted(missing))

# ---------------------------------------------------------------- 环境音
starts = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
spawn = starts[0].get_actor_location()
yaw = starts[0].get_actor_rotation().yaw
red = spawn + unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0) * 9000
center = (spawn + red) * 0.5
R = 25000.0       # 湖岸 / 废墟方向的声音：对战区中心 250 米内
R_NEAR = 16000.0  # 树丛鸟叫：160 米内（对战区本身 ±75 米，机体跑得快，边上听得到就行）
R_OPEN = 12000.0  # 草地虫鸣：120 米内


def ground(x, y):
    r = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 200000), unreal.Vector(x, y, -200000),
                                               unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(r, tuple):
        r = r[1] if r[0] else None
    if r is None:
        return None
    t = r.to_tuple()
    return t[4].z if t[0] else None


placed = []


def ambient(name, sound, loc, vol, att=None, label=None):
    s = eas.load_asset(f'{AUD}/S_Nat_{sound}')
    a = sub.spawn_actor_from_class(unreal.AmbientSound, loc, unreal.Rotator(0, 0, 0))
    a.set_actor_label(label or f'Amb_{name}')
    a.tags = [TAG]
    a.set_folder_path('Nature/Detail/Audio')
    ac = a.get_editor_property('audio_component')
    ac.set_editor_property('sound', s)
    ac.set_editor_property('volume_multiplier', vol)
    if att:
        ac.set_editor_property('attenuation_settings', eas.load_asset(f'{AUD}/{att}'))
        ac.set_editor_property('allow_spatialization', True)
    else:
        ac.set_editor_property('allow_spatialization', False)   # 全图底噪：不定位、不衰减
        ac.set_editor_property('override_attenuation', False)
    placed.append((name, sound, round(loc.x / 100), round(loc.y / 100), vol, att))
    return a


if not UNDO and 'nosound' not in args:
    # 全图 2D 底噪（放在出生点，只是个位置）
    ambient('bed_day', 'bed_day', spawn + unreal.Vector(0, 0, 300), 0.30)
    ambient('bed_wind', 'bed_wind', spawn + unreal.Vector(0, 0, 300), 0.20)

    # 植被：树按 30 米格子统计，找对战区附近树最密的格子（鸟、树叶），没有树但有草的格子（草地虫鸣）
    foliage = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.InstancedFoliageActor)]
    cell = 3000.0
    trees, grass = {}, {}
    for fa in foliage:
        for c in fa.get_components_by_class(unreal.InstancedStaticMeshComponent):
            mesh = c.static_mesh.get_name() if c.static_mesh else ''
            kind = 'tree' if 'Tree' in mesh else 'grass' if 'Grass_Middle' in mesh else None
            if not kind:
                continue
            cnt = c.get_instance_count()
            step = 1 if kind == 'tree' else 25
            for i in range(0, cnt, step):
                t = c.get_instance_transform(i, True)
                p = t.translation
                if (p.x - center.x) ** 2 + (p.y - center.y) ** 2 > (R_NEAR if kind == 'tree' else R_OPEN) ** 2:
                    continue
                k = (int(p.x // cell), int(p.y // cell))
                d = trees if kind == 'tree' else grass
                d[k] = d.get(k, 0) + 1
    log('对战区附近：有树的格子', len(trees), '有草的格子', len(grass))

    def cell_pos(k, h=600):
        x, y = (k[0] + 0.5) * cell, (k[1] + 0.5) * cell
        z = ground(x, y)
        return unreal.Vector(x, y, (z if z is not None else spawn.z) + h)

    def pick(cells, n, min_dist):
        out = []
        for k, _ in sorted(cells.items(), key=lambda kv: -kv[1]):
            p = ((k[0] + 0.5) * cell, (k[1] + 0.5) * cell)
            if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 > min_dist ** 2 for q in out):
                out.append(p)
            if len(out) >= n:
                break
        return [(int(p[0] // cell), int(p[1] // cell)) for p in out]

    tree_cells = pick(trees, 6, 4500)
    birds = ['birds_a', 'birds_b', 'birds_c']
    for i, k in enumerate(tree_cells):
        # 树最密的几处：树叶沙沙（中等范围）+ 一种鸟叫（小范围，挂高一点像在树上）
        ambient(f'leaves_{i}', 'spot_leaves', cell_pos(k, 500), 0.55, 'SA_Nat_Mid')
        ambient(f'birds_{i}', 'spot_' + birds[i % 3], cell_pos(k, 1400) + unreal.Vector(rng.uniform(-800, 800), rng.uniform(-800, 800), 0), 0.6, 'SA_Nat_Small')
    for i, k in enumerate(tree_cells[:2]):
        ambient(f'woodpecker_{i}', 'spot_woodpecker', cell_pos(k, 900) + unreal.Vector(1200, -900, 0), 0.4, 'SA_Nat_Small')
    for i, k in enumerate(tree_cells[2:4]):
        ambient(f'creak_{i}', 'spot_creak', cell_pos(k, 400) + unreal.Vector(-700, 600, 0), 0.35, 'SA_Nat_Small')
    open_cells = {k: v for k, v in grass.items() if trees.get(k, 0) == 0}
    for i, k in enumerate(pick(open_cells, 3, 6000)):
        ambient(f'meadow_{i}', 'spot_meadow' if i % 2 == 0 else 'spot_meadow2', cell_pos(k, 150), 0.45, 'SA_Nat_Mid')

    # 湖：沿湖岸样条每隔一段放水声（两种循环长度不同，交替放）
    lakes = [a for a in sub.get_all_level_actors() if a.get_class().get_name() == 'WaterBodyLake']
    for lake in lakes:
        spl = lake.get_component_by_class(unreal.SplineComponent)
        n = spl.get_number_of_spline_points() if spl else 0
        length = spl.get_spline_length() if spl else 0
        o, e = lake.get_actor_bounds(False)
        log('湖', lake.get_actor_label(), '样条点', n, '周长', round(length / 100), '米')
        if n:
            k = max(4, int(length / 6000))
            for i in range(k):
                p = spl.get_location_at_distance_along_spline(length * i / k, unreal.SplineCoordinateSpace.WORLD)
                if (p.x - center.x) ** 2 + (p.y - center.y) ** 2 > (R * 0.8) ** 2:   # 只放离对战区 200 米内的湖岸
                    continue
                z = ground(p.x, p.y)
                ambient(f'lake_{i}', 'spot_lake' if i % 2 == 0 else 'spot_lake_waves',
                        unreal.Vector(p.x, p.y, max(z or p.z, o.z + e.z - 300) + 100), 0.5 if i % 2 == 0 else 0.4, 'SA_Nat_Mid')

    # 废墟方向：离对战区中心 150 米、朝最近的几片废墟放风声和乌鸦（远处才听得到，提示「那边是废墟城市」）
    dirs = sorted(ruins, key=lambda a: (a.get_actor_location() - center).length())
    used = []
    for a in dirs:
        p = a.get_actor_location()
        ang = math.atan2(p.y - center.y, p.x - center.x)
        if any(abs((ang - u + math.pi) % (2 * math.pi) - math.pi) < math.radians(50) for u in used):
            continue
        used.append(ang)
        x, y = center.x + math.cos(ang) * 15000, center.y + math.sin(ang) * 15000
        z = ground(x, y) or spawn.z
        ambient(f'ruinwind_{len(used)}', 'spot_ruin_wind', unreal.Vector(x, y, z + 800), 0.45, 'SA_Nat_Large')
        if len(used) <= 2:
            ambient(f'crows_{len(used)}', 'spot_crows', unreal.Vector(x, y, z + 2500), 0.5, 'SA_Nat_Mid')
        if len(used) >= 4:
            break
    for row in placed:
        log('声音', row)
    log('声音合计', len(placed), '对战区中心', [round(v / 100) for v in (center.x, center.y, center.z)])

# ---------------------------------------------------------------- 草和花的消失距离：示例图按人的尺度（中草 10 米、小草 7 米就消失），
# 机体镜头离地 3–4 米、在机体身后 8 米，几步外的草甸就成了光秃的贴图。按机体尺度拉远（参数 nograss 不改；undo 恢复原值）
CULL = {   # 网格名前缀: (原值, 新值) 厘米
    'SM_Grass_Middle': (1000, 2400), 'SM_Grass_Big': (1500, 3500), 'SM_Grass_Small': (700, 1200),
    'SM_Flower_': (800, 1800), 'SM_Plants_Middle': (1500, 2800), 'SM_Plants_Phragmites': (1200, 3000),
}
ORIG = 'D:/RobotSlice/Saved/nature_detail_cull_orig.json'   # 第一次改之前记下原值（起止距离），undo 按它恢复
if 'nograss' not in args:
    import json
    import os
    orig = json.load(open(ORIG, encoding='utf-8')) if os.path.exists(ORIG) else {}
    first = not orig and not UNDO
    changed = {}
    for fa in [a for a in sub.get_all_level_actors() if isinstance(a, unreal.InstancedFoliageActor)]:
        for c in fa.get_components_by_class(unreal.InstancedStaticMeshComponent):
            mesh_name = c.static_mesh.get_name() if c.static_mesh else ''
            for prefix, (old, new) in CULL.items():
                if not mesh_name.startswith(prefix):
                    continue
                if first:
                    orig[mesh_name] = [c.get_editor_property('instance_start_cull_distance'), c.get_editor_property('instance_end_cull_distance')]
                if UNDO:
                    st, en = orig.get(mesh_name, [0, old])
                else:
                    st, en = int(new * 0.8), new   # 末尾 20% 渐隐（材质支持的话）
                c.set_editor_property('instance_start_cull_distance', st)
                c.set_editor_property('instance_end_cull_distance', en)
                changed[mesh_name] = (st, en)
    if first and orig:
        json.dump(orig, open(ORIG, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    log('草花消失距离（起, 止）', changed)

# ---------------------------------------------------------------- Bob Ross 场景的摄影测量巨石：湖岸对战区外侧摆一小组（试过：贴图图集接缝发黑、远看发糊，
# 不如环境包的石头，默认不摆；参数 boulder 才摆）
BOULDER = '/Game/Env/Nature/Props/SM_Nat_ScanBoulder'
if not UNDO and 'boulder' in args and eas.does_asset_exist(BOULDER):
    mesh = eas.load_asset(BOULDER)
    lakes = [a for a in sub.get_all_level_actors() if a.get_class().get_name() == 'WaterBodyLake']
    spl = lakes[0].get_component_by_class(unreal.SplineComponent) if lakes else None
    lc = lakes[0].get_actor_location() if lakes else center
    # 别摆在树荫底下（扫描石头的贴图偏亮，在树荫里像一块灰盒子）：岸边 15 米内有树就跳过
    tree_pos = []
    for fa in [x for x in sub.get_all_level_actors() if isinstance(x, unreal.InstancedFoliageActor)]:
        for c in fa.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if c.static_mesh and 'Tree' in c.static_mesh.get_name():
                for j in range(c.get_instance_count()):
                    tp = c.get_instance_transform(j, True).translation
                    if math.hypot(tp.x - lc.x, tp.y - lc.y) < 30000:
                        tree_pos.append((tp.x, tp.y))
    spots = []
    if spl:
        L = spl.get_spline_length()
        for i in range(160):
            p = spl.get_location_at_distance_along_spline(L * i / 160, unreal.SplineCoordinateSpace.WORLD)
            in_arena = abs(p.x - center.x) < 7800 and abs(p.y - center.y) < 7800
            d = math.hypot(p.x - center.x, p.y - center.y)
            if in_arena or d > 17000 or d < 9000:
                continue
            out0 = unreal.Vector(p.x - lc.x, p.y - lc.y, 0)
            out0 = out0 * (1.0 / max(1.0, out0.length()))
            bx, by = p.x + out0.x * 900, p.y + out0.y * 900
            if any(math.hypot(bx - tx, by - ty) < 1500 for tx, ty in tree_pos):
                continue
            if all(math.hypot(p.x - q.x, p.y - q.y) > 2600 for q in spots):
                spots.append(p)
            if len(spots) >= 3:
                break
    for i, p in enumerate(spots):
        out = unreal.Vector(p.x - lc.x, p.y - lc.y, 0)
        out = out * (1.0 / max(1.0, out.length()))
        x, y = p.x + out.x * 900, p.y + out.y * 900      # 岸线往陆地退 9 米
        z = ground(x, y)
        if z is None:
            continue
        s = rng.uniform(1.6, 2.2)
        a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, z - 257 * s * 0.22),
                                       unreal.Rotator(pitch=rng.uniform(-4, 4), yaw=rng.uniform(0, 360), roll=rng.uniform(-4, 4)))
        a.set_actor_label(f'BobRoss_Boulder_{i}')
        a.tags = [TAG]
        a.set_folder_path('Nature/Detail/BobRoss')
        c = a.static_mesh_component
        c.set_static_mesh(mesh)
        c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
        a.set_actor_scale3d(unreal.Vector(s, s, s))
        if eas.does_asset_exist('/Game/Env/Nature/Materials/MI_Nat_ScanBoulder'):
            c.set_material(0, eas.load_asset('/Game/Env/Nature/Materials/MI_Nat_ScanBoulder'))
        log('巨石', i, [round(v / 100) for v in (x, y, z)], '缩放', round(s, 2))

unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
log('已保存', MAP, '（撤销）' if UNDO else '')
