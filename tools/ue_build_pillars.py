"""石柱山水 /Game/Maps/L_Pillars：F25「冲破穹顶」的石柱和地形搬进游戏（放大 S 倍），偏空战的图。
用户 11-02：「石柱山水我是打算加入的……这就是一张偏空战的地图。一旦被击落就掉到水里，掉进水就会损毁。」规则在 C++（MechMapRules.h），这张图里只放标记：
- WaterKill：水面高度（Z = 0，全图）。MechArena：场地中心和半宽。TeamSpawn0/1、DMSpawn：出生点。
天光用工程的灯光底图（L_LightingBase：LightingDirector + 环境装置，时间天气在 AMechGameMode::StartPlay）。
网格都是 Blender 世界坐标，摆在原点、放大 S 倍、往下挪到水面 Z = 0；B() 把原作坐标换成 UE 厘米。
可重复跑：图已存在就删掉带 Pillars 标签的东西再摆；不存在就从底图新建。先跑 tools/ue_pillars_import.py、tools/pillars_spots.py。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_pillars.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
"""
import json
import math
import random

import unreal

SRC_MAP = '/Game/Maps/L_LightingBase'
DST = '/Game/Maps/L_Pillars'
DEST = '/Game/Env/Pillars'
EP = '/Game/Environments/AndreyPestryakov/Environment_Pack_01'
TAG = 'Pillars'
S = 6.0                 # 放大倍数（要和 tools/pillars_spots.py、ue_pillars_shots.py 一致）
META = json.load(open('D:/RobotSlice/Inbox/_f25/export/pillars.json', encoding='utf-8'))
SPOTS = json.load(open('D:/RobotSlice/Inbox/_f25/export/spots.json', encoding='utf-8'))
WZ = META['water_z']
ZOFF = -WZ * 100.0 * S  # 所有网格往下挪这么多，水面在 Z = 0
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[pillars] ' + ' '.join(str(x) for x in a))
_cache = {}
rng = random.Random(20261102)


def asset(path):
    if path not in _cache:
        _cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
        if _cache[path] is None:
            unreal.log_warning(f'[pillars] 缺资源 {path}')
    return _cache[path]


def B(x, y, z):
    """原作坐标（单位）→ UE 厘米（放大 S 倍，水面 Z = 0）"""
    return unreal.Vector(x * 100.0 * S, -y * 100.0 * S, (z - WZ) * 100.0 * S)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        log('设不了', obj.get_class().get_name(), name, e)


def tagged(a, folder, label=None, extra=()):
    a.tags = [TAG] + list(extra)
    a.set_folder_path(f'Pillars/{folder}')
    if label:
        a.set_actor_label(label)
    return a


def mesh_actor(mesh, loc, rot=None, scale=(1, 1, 1), folder='Misc', label=None, mat=None, collide=False, shadow=True, lumen=True, far=False):
    a = tagged(sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot or unreal.Rotator()), folder, label)
    a.set_actor_scale3d(unreal.Vector(*scale))
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if mat:
        for i in range(max(1, len(mesh.get_editor_property('static_materials')))):
            c.set_material(i, mat)
    if collide:
        c.set_collision_profile_name('BlockAll')
    else:
        c.set_collision_profile_name('NoCollision')
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_character_step_up_on', unreal.CanBeCharacterBase.ECB_NO)
    if not shadow:
        c.set_editor_property('cast_shadow', False)
    if not lumen:
        setp(c, 'affect_distance_field_lighting', False)
        setp(c, 'affect_dynamic_indirect_lighting', False)
        setp(c, 'visible_in_ray_tracing', False)
    if far:
        setp(c, 'cast_contact_shadow', False)
    return a


# ---------------------------------------------------------------- 关卡
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if eas.does_asset_exist(DST):
    unreal.EditorLoadingAndSavingUtils.load_map(DST)
    n = 0
    for a in sub.get_all_level_actors():
        if TAG in [str(t) for t in a.tags]:
            sub.destroy_actor(a)
            n += 1
    log('重建：删掉旧的', n, '个')
else:
    unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
    for a in sub.get_all_level_actors():
        cls = a.get_class().get_name()
        if not any(k in cls for k in ('LightingDirector', 'LightingEnvironmentRig', 'WorldSettings')) and 'Brush' not in cls:
            sub.destroy_actor(a)
    log('从底图新建', DST)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))

# ---------------------------------------------------------------- 石柱、地形（碰撞用网格本身：Complex as simple，取 Nanite 回退网格）
Z0 = unreal.Vector(0, 0, ZOFF)
SKIP = {'SM_PL_Rock_073_001'}  # 原作里叠在别的石柱顶上，单独看是悬空的
EAST_SINK = 2.0                # 原作单位
n_rock = 0
for name in sorted(META['meshes']):
    if 'Bush' in name:
        continue
    m = asset(f'{DEST}/Meshes/{name}')
    if not m:
        continue
    if name in SKIP:
        continue
    # 东边那几根（原作放在一块展示平面上，底是敞口的）：整根往下沉一点，敞口藏到水下
    zz = Z0
    if META['meshes'][name]['min'][0] > 27.0:
        zz = unreal.Vector(0, 0, ZOFF - EAST_SINK * 100.0 * S)
    mesh_actor(m, zz, scale=(S, S, S), folder='Terrain' if 'Terrain' in name else 'Rocks', label=name.replace('SM_PL_', ''), collide=True)
    n_rock += 1
log('摆了石柱和地形', n_rock)

# 远处的石林（不碰撞、场地外）：同样的石柱再摆一圈，往外一层比一层远、被雾吃掉，飞起来看不到世界的尽头
ROCKS = ['SM_PL_Rock_073_003', 'SM_PL_Rock_073_005', 'SM_PL_Rock_075_008', 'SM_PL_Rock_075_003', 'SM_PL_Rock_069_006', 'SM_PL_Rock_069_002', 'SM_PL_Rock_069_007']  # 只挑立得住的（底部敞口、横着的那些放大后像悬空）
CENTER = (10.0, 10.0)
for i in range(46):
    name = rng.choice(ROCKS)
    mm = META['meshes'][name]
    cx = (mm['min'][0] + mm['max'][0]) / 2
    cy = (mm['min'][1] + mm['max'][1]) / 2
    ang = rng.uniform(0, math.tau)
    dist = rng.uniform(150.0, 520.0)  # 原作单位：放大后 0.9–3.1 公里
    px = CENTER[0] + math.cos(ang) * dist
    py = CENTER[1] + math.sin(ang) * dist
    sc = S * rng.uniform(1.0, 1.9)
    yaw = rng.uniform(0, 360)
    # 网格是世界坐标，绕原点转再挪：先算这根的中心转完在哪
    r = math.radians(yaw)
    wx = (cx * math.cos(r) + cy * math.sin(r)) * 100.0 * sc      # UE 里 y 取反、yaw 正方向相反，合起来就是这样
    wy = (cx * math.sin(r) * -1.0 + cy * math.cos(r)) * -100.0 * sc
    # 目标位置 - 转完后的中心 = actor 位置
    tx, ty = px * 100.0 * S, -py * 100.0 * S
    # 有的石柱原作里是叠在别的石柱上的（底不在水里）：按网格最低点往下沉，底一定在水面以下
    loc = unreal.Vector(tx - (cx * math.cos(r) * 100.0 * sc + cy * math.sin(r) * 100.0 * sc), ty - (cx * math.sin(r) * 100.0 * sc - cy * math.cos(r) * 100.0 * sc), -(mm['min'][2] + 0.25 * (mm['max'][2] - mm['min'][2])) * 100.0 * sc)
    mesh_actor(asset(f'{DEST}/Meshes/{name}'), loc, unreal.Rotator(yaw=yaw), (sc, sc, sc), 'FarRocks', f'Far_{i}', shadow=False, lumen=False, far=True)

# ---------------------------------------------------------------- 岛上的植物（环境包现成的，L_Nature 也在用；坐标是 tools/pillars_spots.py 按坡度和高度挑的）
TD = f'{EP}/Trees_and_Bushes/Deciduous/Meshes'
PG = f'{EP}/Plants_and_Grass'
PLANT_SETS = {
    'tree': ([f'{TD}/SM_TreeSmall{i}' for i in (1, 2, 3)] + [f'{TD}/SM_TreeMiddle{i}' for i in (1, 2, 3)], (0.9, 1.5), True, 0.0),
    'bush': ([f'{TD}/SM_Bush{i}' for i in (1, 2, 3, 4)], (1.0, 1.9), True, 45000.0),
    'fern': ([f'{PG}/Ferns/Meshes/SM_Ferns_{i:02d}' for i in (1, 3, 5, 7, 9)] + [f'{PG}/Plants/Meshes/SM_Plants_MiddleGreen_{i}' for i in (1, 3, 5)], (1.2, 2.2), False, 16000.0),
    'grass': ([f'{PG}/Grass/Meshes/SM_Grass_Big_{i}' for i in (1, 2, 3, 4)] + [f'{PG}/Grass/Meshes/SM_Grass_Middle_{i}' for i in (1, 2)], (1.4, 2.4), False, 12000.0),
}
groups = {}
for x, y, z, kind in SPOTS.get('plants', []):
    meshes, sc_range, _, _ = PLANT_SETS[kind]
    m_ = rng.choice(meshes)
    sc = rng.uniform(*sc_range)
    groups.setdefault((kind, m_), []).append(unreal.Transform(B(x, y, z) - unreal.Vector(0, 0, 12.0), unreal.Rotator(yaw=rng.uniform(0, 360)), unreal.Vector(sc, sc, sc)))
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
for (kind, path), tf in sorted(groups.items()):
    me = asset(path)
    if not me:
        continue
    a = sub.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    root = sds.k2_gather_subobject_data_for_instance(a)[0]
    h, fail = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent, blueprint_context=None))
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_static_mesh(me)
    comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    comp.set_collision_profile_name('NoCollision')
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    _, _, cast, cull = PLANT_SETS[kind]
    comp.set_editor_property('cast_shadow', cast)
    if cull > 0:
        comp.set_editor_property('instance_end_cull_distance', int(cull))
        comp.set_editor_property('instance_start_cull_distance', int(cull * 0.8))
    if not cast:
        setp(comp, 'affect_distance_field_lighting', False)
        setp(comp, 'affect_dynamic_indirect_lighting', False)
    comp.add_instances(tf, False, True)
    tagged(a, 'Plants', f'{kind}_{path.rsplit("/", 1)[1]}')
log('植物', {k[0]: 0 for k in groups} and {kk: sum(len(v) for (k2, _), v in groups.items() if k2 == kk) for kk in PLANT_SETS})

# ---------------------------------------------------------------- 水面（不透明受光，见 ue_pillars_import.py）
plane = asset('/Engine/BasicShapes/Plane')
# 坑：一整块放大 8 万倍（80 公里）的平面受光是坏的（暗、满屏噪点，换引擎默认材质也一样，单层水是一片白），放大 6000 倍（6 公里）就正常。所以铺 7×7 块，共 42 公里
for ix in range(-3, 4):
    for iy in range(-3, 4):
        mesh_actor(plane, unreal.Vector(ix * 600000.0, iy * 600000.0, 0), scale=(6000, 6000, 1), folder='Water', label=f'Water_{ix}_{iy}', mat=asset(f'{DEST}/Materials/MI_PL_Water'), shadow=False, lumen=False)

# ---------------------------------------------------------------- 标记
tagged(sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 0)), 'Marks', 'WaterKill', ['WaterKill', 'KillDepth=60'])

# 场地：主石柱群 + 东边那几根高的（原作单位 x -52..72, y -38..58），四周和头顶是只挡机体的隐形墙
AX0, AX1, AY0, AY1 = -56.0, 76.0, -42.0, 62.0
TOP_M = 260.0
acx, acy = (AX0 + AX1) / 2, (AY0 + AY1) / 2
hx, hy = (AX1 - AX0) / 2 * 100.0 * S, (AY1 - AY0) / 2 * 100.0 * S
tagged(sub.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(acx * 100.0 * S, -acy * 100.0 * S, 0)), 'Marks', 'MechArena', ['MechArena', f'HalfX={hx:.0f}', f'HalfY={hy:.0f}'])
cube = asset('/Engine/BasicShapes/Cube')
ctr = unreal.Vector(acx * 100.0 * S, -acy * 100.0 * S, 0)
WALL_T = 400.0
for name, cx_, cy_, cz_, sx_, sy_, sz_ in (('Bound_E', hx + WALL_T / 2, 0, TOP_M * 50, WALL_T, hy * 2 + WALL_T, TOP_M * 100 + 4000), ('Bound_W', -hx - WALL_T / 2, 0, TOP_M * 50, WALL_T, hy * 2 + WALL_T, TOP_M * 100 + 4000),
                                           ('Bound_N', 0, hy + WALL_T / 2, TOP_M * 50, hx * 2 + WALL_T, WALL_T, TOP_M * 100 + 4000), ('Bound_S', 0, -hy - WALL_T / 2, TOP_M * 50, hx * 2 + WALL_T, WALL_T, TOP_M * 100 + 4000),
                                           ('Bound_Top', 0, 0, TOP_M * 100 + WALL_T / 2, hx * 2 + WALL_T, hy * 2 + WALL_T, WALL_T)):
    a = mesh_actor(cube, unreal.Vector(ctr.x + cx_, ctr.y + cy_, cz_), scale=(sx_ / 100.0, sy_ / 100.0, sz_ / 100.0), folder='Bounds', label=name, collide=True, shadow=False, lumen=False)
    a.set_actor_hidden_in_game(True)
    comp = a.static_mesh_component
    comp.set_editor_property('visible', False)
    comp.set_collision_profile_name('Custom')  # 先 Custom 再改（见 README 城区一节的坑）：对象类型 Vehicle、只挡 Pawn
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    a.tags = [TAG, 'ArenaWall']


def look_yaw(ax, ay, bx, by):
    """A 看向 B 的 UE 朝向（原作坐标）"""
    return math.degrees(math.atan2(-(by - ay), bx - ax))


# 两队在主石柱群南北两头的岛上（spots.json 里最大的两块），朝中间
team = SPOTS['team']
A_, B_ = team[0], team[1]
for i, (me_, other) in enumerate(((A_, B_), (B_, A_))):
    z = me_['z'] + 0.2 / S
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, B(me_['x'], me_['y'], z), unreal.Rotator(yaw=look_yaw(me_['x'], me_['y'], other['x'], other['y']))),
           'Spawns', f'TeamSpawn{i}', [f'TeamSpawn{i}'])
for i, sp in enumerate(SPOTS['dm']):
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, B(sp['x'], sp['y'], sp['z'] + 0.5 / S)), 'Spawns', f'DMSpawn_{i}', ['DMSpawn'])
log('出生点', team, '重生点', len(SPOTS['dm']))
ps = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
start = B(A_['x'], A_['y'], A_['z'] + 3.0 / S)
if ps:
    ps[0].set_actor_location(start, False, False)
    ps[0].set_actor_rotation(unreal.Rotator(yaw=look_yaw(A_['x'], A_['y'], B_['x'], B_['y'])), False)
    for extra in ps[1:]:
        sub.destroy_actor(extra)
else:
    tagged(sub.spawn_actor_from_class(unreal.PlayerStart, start, unreal.Rotator(yaw=look_yaw(A_['x'], A_['y'], B_['x'], B_['y']))), 'Spawns', 'PlayerStart')

cam = tagged(sub.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, 0, 5000)), 'Misc', 'ShotCam', ['ShotCam'])
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)

# ---------------------------------------------------------------- 后期：水面的 Lumen 屏幕追踪关掉（水面是一大张平面，屏幕追踪会打中自己，反射发黑发花、屏幕边上一圈亮），只用距离场追踪；色调往参考图的翠绿靠一点
ppv = tagged(sub.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0)), 'Light', 'PP_Pillars')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 50.0)
pp = ppv.get_editor_property('settings')
for k, v in (('lumen_reflections_screen_traces', False), ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 1.08)), ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.06)),
             ('color_gain_shadows', unreal.Vector4(0.96, 1.0, 1.03, 1.0)), ('vignette_intensity', 0.15), ('bloom_intensity', 0.25)):
    try:
        pp.set_editor_property('override_' + k, True)
        pp.set_editor_property(k, v)
    except Exception as e:
        log('后期设不了', k, e)
ppv.set_editor_property('settings', pp)

# ---------------------------------------------------------------- 灯光导演的逐地图调参（TuningSettings 存在地图里）
for d in [a for a in sub.get_all_level_actors() if 'LightingDirector' in a.get_class().get_name()]:
    t = d.get_editor_property('tuning_settings')
    for k, v in (('override_clouds', True), ('cloud_coverage', 0.55), ('cloud_density', 0.45), ('cloud_bottom_km', 2.2), ('cloud_depth_km', 2.0), ('cloud_shape_scale', 1.2), ('cloud_shadow_strength', 0.3),
                 ('override_weather', True), ('fog_density', 0.018), ('fog_height_falloff', 0.2), ('rain_intensity', 0.0), ('wetness', 0.0), ('wind_strength', 0.2), ('wind_direction', 45.0), ('lightning_rate', 0.0),
                 ('sun_multiplier', 0.85), ('sky_multiplier', 1.2)):
        try:
            t.set_editor_property(k, v)
        except Exception as e:
            log('调参失败', k, e)
    d.set_editor_property('tuning_settings', t)
    log('灯光导演调参完成')

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已保存', DST, '场地半宽（米）', hx / 100, hy / 100)
