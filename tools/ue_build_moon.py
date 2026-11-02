"""月球基地 /Game/Maps/L_Moon：按 Inbox/_moon_nasa/work/layout_final.json 摆基地、岩石、远景、天空、光、标记。Landscape 是 ue_moon_landscape.py 导好的，这里不动。
可重复跑：删掉带 Moon 标签的东西再摆。没有 C++，低重力只用 WorldSettings 的全局重力覆盖（环境变量 MOON_G 可改，默认见 GRAVITY）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_moon.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
坐标：布局表里是 UE 米（X 东、Y 南），网格是 K65 原点，按 (x, y) 摆、yaw 俯视顺时针。
"""
import json
import math
import os
import random

import unreal

DST = '/Game/Maps/L_Moon'
DEST = '/Game/Env/Moon'
TAG = 'Moon'
W = 'D:/RobotSlice/Inbox/_moon_nasa/work'
L = json.load(open(f'{W}/layout_final.json', encoding='utf-8'))
GRAVITY = float(os.environ.get('MOON_G', '-327.0'))    # 980 / 3（真实月球 1/6 = -163）
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[moon] ' + ' '.join(str(x) for x in a))
_cache = {}


def asset(path):
    if path not in _cache:
        _cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
        if _cache[path] is None:
            unreal.log_warning(f'[moon] 缺资源 {path}')
    return _cache[path]


def U(x, y, z=0.0):
    return unreal.Vector(x * 100.0, y * 100.0, z * 100.0)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        log('设不了', obj.get_class().get_name(), name, e)


def tagged(a, folder, label=None, extra=()):
    a.tags = [TAG] + list(extra)
    a.set_folder_path(f'Moon/{folder}')
    if label:
        a.set_actor_label(label)
    return a


def mesh_actor(mesh, loc, rot=None, scale=(1, 1, 1), folder='Misc', label=None, mat=None, collide=False, shadow=True, lumen=True, hidden=False):
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
    if not lumen or hidden:
        setp(c, 'affect_distance_field_lighting', False)
        setp(c, 'affect_dynamic_indirect_lighting', False)
        setp(c, 'visible_in_ray_tracing', False)
    if hidden:
        a.set_actor_hidden_in_game(True)
        c.set_editor_property('visible', False)
        c.set_editor_property('cast_shadow', False)
    return a


# ---------------------------------------------------------------- 关卡
assert eas.does_asset_exist(DST), '先跑 ue_moon_landscape.py'
unreal.EditorLoadingAndSavingUtils.load_map(DST)
n = 0
for a in sub.get_all_level_actors():
    if TAG in [str(t) for t in a.tags]:
        sub.destroy_actor(a)
        n += 1
log('重建：删掉旧的', n, '个')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ws = world.get_world_settings()
ws.set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))
ws.set_editor_property('global_gravity_set', True)
ws.set_editor_property('global_gravity_z', GRAVITY)
log('全局重力', GRAVITY)
# 地形在 -600 米以下还有东西（远景低谷），KillZ 放低
ws.set_editor_property('kill_z', -100000.0)
land = next((a for a in sub.get_all_level_actors() if isinstance(a, unreal.Landscape)), None)
assert land, '图里没有 Landscape'
land.set_editor_property('landscape_material', asset(f'{DEST}/Materials/MI_Moon_Ground'))
# 地形不投影：Landscape 画影子时用的 LOD 比屏幕上的粗，粗网格高出来的地方在自己身上落下一块块黑斑（远处、高处看像云影）。
# 太阳够高，真实地形（坡都小于太阳高度）本来也几乎没有投影，起伏靠朝向明暗就够了；建筑和石头的影子照常落在地上
for k_, v_ in (('cast_shadow', False), ('cast_dynamic_shadow', False), ('cast_static_shadow', False), ('cast_far_shadow', False)):
    setp(land, k_, v_)
for c_ in land.get_components_by_class(unreal.LandscapeComponent):
    c_.set_editor_property('cast_shadow', False)

# ---------------------------------------------------------------- 基地
M = lambda name: asset(f'{DEST}/Meshes/{name}')
placed = 0
for it in L['items']:
    rot = unreal.Rotator(yaw=it['yaw'])
    scl = (it['s'], it['s'], it['s'])
    base = f"SM_K65_{it['group']}"
    label = f"{it['group']}@{it['cx']:.0f},{it['cy']:.0f}"
    loc = U(it['x'], it['y'], it['z'])
    for cls in it['classes']:
        me = M(f'{base}_{cls}')
        if not me:
            continue
        # 大穹顶的玻璃没有碰撞（当气密膜，机体直接穿；门都是封死的）；别的玻璃顶当实心
        collide = cls == 'Solid' or (cls == 'Glass' and it['group'] != 'XLG_C')
        mesh_actor(me, loc, rot, scl, f"Base/{it['group']}", f'{label}_{cls}', collide=collide, shadow=cls != 'Glass', lumen=cls == 'Solid')
        placed += 1
log('基地网格', placed)

# ---------------------------------------------------------------- 远景（真实高程图，整张 2470×1507 × 16 米；不碰撞、不进 Lumen）
far = M('SM_Moon_FarTerrain')
if far:
    mesh_actor(far, U(0, 0, 0), folder='Terrain', label='FarTerrain', mat=asset(f'{DEST}/Materials/MI_Moon_FarGround'), shadow=False, lumen=False)

fill_ = M('SM_Moon_FarFill')
if fill_:
    mesh_actor(fill_, U(0, 0, 0), folder='Terrain', label='FarFill', mat=asset(f'{DEST}/Materials/MI_Moon_FillGround'), shadow=False, lumen=False)

# ---------------------------------------------------------------- 岩石（实例化）：按 种类 + 有无碰撞 分组，给的 s 是目标直径（米）
sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
groups = {}
sizes = {}
for k in ('boulder_01', 'namaqualand_boulder_02', 'namaqualand_boulder_03'):
    m = asset(f'{DEST}/Rocks/SM_Rock_{k}')
    if m:
        b = m.get_bounding_box()
        sizes[k] = max(b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z)
        log('岩石原始尺寸', k, round(sizes[k]))
for r in L['rocks']:
    if r['k'] not in sizes:
        continue
    s = r['s'] * 100.0 / sizes[r['k']]
    sq = s * r.get('sq', 1.0)
    tf = unreal.Transform(U(r['x'], r['y'], r['z'] - r['s'] * 0.18), unreal.Rotator(roll=r['roll'], pitch=r['pitch'], yaw=r['yaw']), unreal.Vector(s, s, s * 0.8))
    groups.setdefault((r['k'], r['c']), []).append(tf)
for (k, collide), tfs in sorted(groups.items()):
    a = sub.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    root = sds.k2_gather_subobject_data_for_instance(a)[0]
    h, fail = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.HierarchicalInstancedStaticMeshComponent, blueprint_context=None))
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_static_mesh(asset(f'{DEST}/Rocks/SM_Rock_{k}'))
    comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if collide:
        comp.set_collision_profile_name('BlockAll')
    else:
        comp.set_collision_profile_name('NoCollision')
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    comp.add_instances(tfs, False, True)
    if not collide:      # 碎石：太远的不画
        comp.set_editor_property('instance_end_cull_distance', 60000)
        comp.set_editor_property('instance_start_cull_distance', 50000)
    tagged(a, 'Rocks', f"Rocks_{k}_{'solid' if collide else 'deco'}")
    log('岩石', k, '碰撞' if collide else '无碰撞', len(tfs))

# ---------------------------------------------------------------- 天空：星空大球、地球
SUN_AZ = 215.0     # 太阳方位（从北顺时针）：西南偏南，光线朝东北偏北照。沿南北方向多一些，两队都是侧光，谁也不用迎着太阳打
SUN_EL = float(os.environ.get('MOON_SUN_EL', '40.0'))
sa, se = math.radians(SUN_AZ), math.radians(SUN_EL)
# 指向太阳的方向（UE：X 东，Y 南）
sdx, sdy, sdz = math.cos(se) * math.sin(sa), -math.cos(se) * math.cos(sa), math.sin(se)
sphere = asset('/Engine/BasicShapes/Sphere')
sky_m = asset(f'{DEST}/Materials/M_Moon_Sky')
mesh_actor(sphere, U(0, 0, 0), scale=(200000, 200000, 200000), folder='Sky', label='StarDome', mat=sky_m, shadow=False, lumen=False)   # 半径 100 公里
EARTH_AZ, EARTH_EL, EARTH_DIST, EARTH_DEG = 28.0, 26.0, 90000.0, 6.5      # 东北偏北、仰角 26°，放大到 6.5°（真实 1.9°）：南北向打架时抬头能看到
ea, ee = math.radians(EARTH_AZ), math.radians(EARTH_EL)
ex, ey, ez = math.cos(ee) * math.sin(ea), -math.cos(ee) * math.cos(ea), math.sin(ee)
rad_m = EARTH_DIST * math.tan(math.radians(EARTH_DEG / 2))
scale = rad_m * 100.0 / 50.0
earth_m = asset(f'{DEST}/Materials/M_Moon_Earth')
earth = mesh_actor(sphere, U(ex * EARTH_DIST, ey * EARTH_DIST, ez * EARTH_DIST), unreal.Rotator(roll=-14.0, pitch=0.0, yaw=138.0), (scale, scale, scale), 'Sky', 'Earth', earth_m, shadow=False, lumen=False)
log('地球半径(米)', round(rad_m), '方向', round(ex, 2), round(ey, 2), round(ez, 2), '太阳方向', round(sdx, 2), round(sdy, 2), round(sdz, 2))

# ---------------------------------------------------------------- 光：硬朗的太阳（光源角度小 = 影子边硬），没有雾、没有云、没有大气。暗部靠 Lumen 地面反弹 + 一点地光
yaw = math.degrees(math.atan2(-sdy, -sdx))
pitch = math.degrees(math.asin(-sdz))
sun = tagged(sub.spawn_actor_from_class(unreal.DirectionalLight, U(0, 0, 500), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw)), 'Light', 'Sun')
lc = sun.light_component
lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
for k, v in (('intensity', float(os.environ.get('MOON_SUN', '9.5'))), ('temperature', 5900.0), ('use_temperature', True), ('light_source_angle', float(os.environ.get('MOON_SUN_ANGLE', '0.4'))), ('cast_shadows', True)):
    setp(lc, k, v)
# 地光（地球 + 被照亮的月面反弹的近似）：不投影的弱方向光，从太阳对面低角度来，暗面不至于死黑、能看清机体（真空里阴影其实是纯黑，纯黑没法打）
fill = tagged(sub.spawn_actor_from_class(unreal.DirectionalLight, U(0, 0, 510), unreal.Rotator(roll=0, pitch=-18.0, yaw=math.degrees(math.atan2(sdy, sdx)))), 'Light', 'Earthshine')
fc_ = fill.light_component
fc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
for k, v in (('intensity', float(os.environ.get('MOON_FILL', '0.6'))), ('cast_shadows', False), ('light_color', unreal.Color(r=205, g=218, b=255, a=255)), ('atmosphere_sun_light', False), ('forward_shading_priority', 0)):
    setp(fc_, k, v)
# 顶光（不投影）：把影子里朝上的地面抬一点，看得清地形和躲在影子里的机体
top = tagged(sub.spawn_actor_from_class(unreal.DirectionalLight, U(0, 0, 515), unreal.Rotator(roll=0, pitch=-85.0, yaw=0.0)), 'Light', 'TopFill')
tc_ = top.light_component
tc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
for k, v in (('intensity', float(os.environ.get('MOON_TOP', '0.7'))), ('cast_shadows', False), ('light_color', unreal.Color(r=215, g=222, b=240, a=255)), ('atmosphere_sun_light', False), ('forward_shading_priority', 0)):
    setp(tc_, k, v)
sky = tagged(sub.spawn_actor_from_class(unreal.SkyLight, U(0, 0, 520)), 'Light', 'SkyLight')
sc_ = sky.light_component
sc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
setp(sc_, 'source_type', unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
setp(sc_, 'sky_distance_threshold', 1000000.0)     # 只采星空球（再近的东西不算天空）
setp(sc_, 'real_time_capture', False)
setp(sc_, 'intensity', float(os.environ.get('MOON_SKY', '1.0')))
# 下半球给一点中灰：相当于被太阳照亮的月面反上来的光（机体、建筑的下半部分不至于全黑）
setp(sc_, 'lower_hemisphere_is_black', True)
setp(sc_, 'lower_hemisphere_color', unreal.LinearColor(0.09, 0.09, 0.095, 1))
ppv = tagged(sub.spawn_actor_from_class(unreal.PostProcessVolume, U(0, 0, 0)), 'Light', 'PP_Moon')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
EV = float(os.environ.get('MOON_EV', '1.6'))
for k, v in (('auto_exposure_method', unreal.AutoExposureMethod.AEM_HISTOGRAM), ('auto_exposure_min_brightness', EV), ('auto_exposure_max_brightness', EV), ('auto_exposure_bias', 0.0),
             ('auto_exposure_apply_physical_camera_exposure', False), ('bloom_intensity', 0.35), ('vignette_intensity', 0.15), ('indirect_lighting_intensity', 1.0),
             ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 1.0)), ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.06)), ('local_exposure_highlight_contrast_scale', 1.0)):
    setp(pp, 'override_' + k, True)
    setp(pp, k, v)
ppv.set_editor_property('settings', pp)

# ---------------------------------------------------------------- 隐形墙（对象类型 Vehicle、只挡 Pawn），和 MechArena 一致；墙线都埋在坡里或坡后
cube = asset('/Engine/BasicShapes/Cube')
A = L['arena']
x0, x1, y0, y1 = A['cx'] - A['hx'], A['cx'] + A['hx'], A['cy'] - A['hy'], A['cy'] + A['hy']
zb, zt = -60.0, A['top']
T = 4.0
for name, (cx, cy, cz, sx, sy, sz) in (('Bound_W', (x0 - T / 2, A['cy'], (zb + zt) / 2, T, y1 - y0 + 2 * T, zt - zb)), ('Bound_E', (x1 + T / 2, A['cy'], (zb + zt) / 2, T, y1 - y0 + 2 * T, zt - zb)),
                                         ('Bound_N', (A['cx'], y0 - T / 2, (zb + zt) / 2, x1 - x0, T, zt - zb)), ('Bound_S', (A['cx'], y1 + T / 2, (zb + zt) / 2, x1 - x0, T, zt - zb)),
                                         ('Bound_Top', (A['cx'], A['cy'], zt + T / 2, x1 - x0 + 2 * T, y1 - y0 + 2 * T, T))):
    a = mesh_actor(cube, U(cx, cy, cz), None, (sx, sy, sz), 'Bounds', name, collide=True, hidden=True)
    comp = a.static_mesh_component
    comp.set_collision_profile_name('Custom')
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    a.tags = [TAG, 'ArenaWall']

# ---------------------------------------------------------------- 标记
tagged(sub.spawn_actor_from_class(unreal.TargetPoint, U(A['cx'], A['cy'], A['z'])), 'Marks', 'MechArena', ['MechArena', f"HalfX={A['hx'] * 100:.0f}", f"HalfY={A['hy'] * 100:.0f}"])
for t in L['team']:
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, U(t['x'], t['y'], t['z']), unreal.Rotator(yaw=t['yaw'])), 'Marks', t['name'], [t['name']])
for i, d in enumerate(L['dm']):
    tagged(sub.spawn_actor_from_class(unreal.PlayerStart, U(d['x'], d['y'], d['z'] + 1.0), unreal.Rotator(yaw=d['yaw'])), 'Marks', f'DMSpawn_{i + 1}', ['DMSpawn'])
# ?Range 模式出生在第一个 PlayerStart（DM 第 1 个，西队出生区）；木桩点放在出生点东北的空地上（默认的「正前方 10/25/50 米」会刷进前挡）
for i, (x, y) in enumerate(((-128, -48), (-108, -62), (-88, -92))):
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, U(x, y, 8.0)), 'Marks', f'Dummy_{i + 1}', ['Dummy'])
cam = tagged(sub.spawn_actor_from_class(unreal.CameraActor, U(0, 0, 60)), 'Misc', 'ShotCam', ['ShotCam'])
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已保存', DST, 'actor 数', len(sub.get_all_level_actors()))
