"""原版峡谷图 /Game/Maps/L_MegaCanyon：科幻峡谷包 Scene_03 原样搬进游戏（两面墙夹一条沟、两边甲板、远景大平面、卡通云、大行星），能飞着逛。
用户 11-01：「这个草坪和渲染效果不太满意。能否先将原地图拉近游戏」。尺度和原包一致（1 米 = 1 米）：沟口约 16 米宽、楼柱群挂下来 176 米、
沟底 -599 米，甲板顶面约 +10 米，远景平面铺到前后 7 公里。网格都是 Blender 世界坐标，摆在原点（UE 按 (x, -y, z) 读）。
可重复跑：图已存在就删掉带 MegaCanyon 标签的东西再摆；不存在就新建空关卡。先跑 tools/ue_megacanyon_import.py。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_megacanyon.py" -AllowCommandletRendering -RenderOffscreen -unattended

下面的坐标都按 Blender 写（米），B() 换成 UE 厘米。
- 原包相机：(-0.48, 18.13, 10.54) 朝 -Y 往下 10.5°，27 毫米镜头；渲染图里左边那面墙（+X 那一半）被太阳照亮，右边在阴影里。
- 碰撞（全是隐形方块，物体类型照常、挡一切）：两边各一整块「甲板 + 沟壁」实心块（沟口 x = +7.59 / -9.13，和远景平面的缝一样，
  楼柱群那一段正好在楼柱正面的 30% 分位附近）；甲板单元那一片顶面垫到 9.9 米（单元顶 10.1、平面顶 8.4）；沟底 -599 米。
- 活动范围：x ±305、y -7300~7200、头顶 1500 米（四周和头顶是只挡机体的隐形墙），沟底能下去。
"""
import json
import math

import unreal

DST = '/Game/Maps/L_MegaCanyon'
DEST = '/Game/Env/MegaCanyon'
TAG = 'MegaCanyon'
META = json.load(open('D:/RobotSlice/Inbox/_megacanyon/export/megacanyon.json', encoding='utf-8'))
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log('[megacanyon] ' + ' '.join(str(x) for x in a))
_cache = {}

SLOT = (-9.13, 7.59)            # 远景平面中间那条缝（沟口）的两边
DECK_TOP, PLANE_TOP, FLOOR = 9.9, 8.37, -599.0
LEN = (-7374.8, 7259.45)        # 远景平面沿 Y 的范围
WALLS_Y = (-402.0, 13.4)        # 楼柱群（Cube.056）沿 Y 的范围
TILES_Y = (-317.4, 13.5)        # 甲板单元（阵列）沿 Y 的范围


def asset(path):
    if path not in _cache:
        _cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
        if _cache[path] is None:
            unreal.log_warning(f'[megacanyon] 缺资源 {path}')
    return _cache[path]


def B(x, y, z):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        log('设不了', obj.get_class().get_name(), name, e)


def tagged(a, folder, label=None, extra=()):
    a.tags = [TAG] + list(extra)
    a.set_folder_path(f'MegaCanyon/{folder}')
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
        c.set_collision_profile_name('NoCollision')     # 只设 CollisionEnabled 不够，引擎方块 / 球自带的简单碰撞还在（见 README 梦核旷野一节的坑）
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


def box(name, x0, x1, y0, y1, z0, z1, folder, mat=None, collide=True, visible=True, shadow=True):
    """Blender 坐标下的轴对齐方块（米）"""
    cube = asset('/Engine/BasicShapes/Cube')
    c = B((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    return mesh_actor(cube, c, None, (abs(x1 - x0), abs(y1 - y0), abs(z1 - z0)), folder, name, mat, collide, shadow, hidden=not visible)


def ism_actor(label, mesh, mat, transforms, folder):
    a = sub.spawn_actor_from_class(unreal.Actor, unreal.Vector(0, 0, 0))
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root = sds.k2_gather_subobject_data_for_instance(a)[0]
    h, fail = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root, new_class=unreal.InstancedStaticMeshComponent,
                                                                 blueprint_context=None))
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
    comp.set_static_mesh(mesh)
    comp.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    comp.set_material(0, mat)
    comp.set_collision_profile_name('NoCollision')
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    comp.add_instances(transforms, False, True)
    tagged(a, folder, label)
    log('实例化', label, comp.get_instance_count(), '份')
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
    assert les.new_level(DST), '新建关卡失败'
    log('新建', DST)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))
MI = {k: asset(f'{DEST}/Materials/{k}') for k in ('MI_MC_Wall', 'MI_MC_Deck', 'MI_MC_Plane', 'MI_MC_Backing', 'MI_MC_Sky', 'MI_MC_Planet',
                                                  'MI_MC_Moon', 'MI_MC_Cloud')}
M = lambda n: asset(f'{DEST}/Meshes/{n}')

# ---------------------------------------------------------------- 原包的网格（世界坐标，摆在原点）
mesh_actor(M('SM_MC_Walls'), B(0, 0, 0), folder='Canyon', label='Walls', mat=MI['MI_MC_Wall'])
mesh_actor(M('SM_MC_Plane'), B(0, 0, 0), folder='Canyon', label='FarPlane', mat=MI['MI_MC_Plane'])
mesh_actor(M('SM_MC_Bits'), B(0, 0, 0), folder='Canyon', label='Bits', mat=MI['MI_MC_Wall'])
mesh_actor(M('SM_MC_Clouds'), B(0, 0, 0), folder='Sky', label='Clouds', mat=MI['MI_MC_Cloud'], shadow=False)
mesh_actor(M('SM_MC_Planet'), B(0, 0, 0), folder='Sky', label='Planet', mat=MI['MI_MC_Planet'], shadow=False, lumen=False)
mesh_actor(M('SM_MC_Moon'), B(0, 0, 0), folder='Sky', label='Moon', mat=MI['MI_MC_Moon'], shadow=False, lumen=False)

# 甲板：一块单元按原包阵列（13 × 7）摆，再沿 X=0 镜像一份（缩放 X = -1）
dk = META['deck']
sa, sb = dk['step_a'], dk['step_b']
tf = []
for side in (1, -1):
    for j in range(dk['count_b']):
        for i in range(dk['count_a']):
            dx, dy = i * sa[0] + j * sb[0], i * sa[1] + j * sb[1]
            tf.append(unreal.Transform(B(dx * side, dy, 0), unreal.Rotator(), unreal.Vector(side, 1, 1)))
ism_actor('DeckTiles', M('SM_MC_DeckTile'), MI['MI_MC_Deck'], tf, 'Canyon')

# 原包远景平面中间只有一条缝、没有沟壁（Cycles 里靠体积雾糊过去）；楼柱群那 415 米以外补两面沟壁，楼柱群背后补一面衬墙，沟底补一块地
for nm, (y0, y1) in (('Far', (LEN[0], WALLS_Y[0])), ('Back', (WALLS_Y[1], LEN[1]))):
    box(f'CanyonWallR_{nm}', SLOT[1], SLOT[1] + 1.0, y0, y1, FLOOR, PLANE_TOP - 0.05, 'Canyon', MI['MI_MC_Backing'], collide=False)
    box(f'CanyonWallL_{nm}', SLOT[0] - 1.0, SLOT[0], y0, y1, FLOOR, PLANE_TOP - 0.05, 'Canyon', MI['MI_MC_Backing'], collide=False)
wm = META['meshes']['SM_MC_Walls']
box('BackingR', wm['max'][0], wm['max'][0] + 2.0, WALLS_Y[0], WALLS_Y[1], FLOOR, PLANE_TOP - 0.05, 'Canyon', MI['MI_MC_Backing'], collide=False)
box('BackingL', wm['min'][0] - 2.0, wm['min'][0], WALLS_Y[0], WALLS_Y[1], FLOOR, PLANE_TOP - 0.05, 'Canyon', MI['MI_MC_Backing'], collide=False)
box('CanyonFloor', wm['min'][0] - 2.0, wm['max'][0] + 2.0, LEN[0], LEN[1], FLOOR - 10.0, FLOOR, 'Canyon', MI['MI_MC_Backing'], collide=True)

# ---------------------------------------------------------------- 碰撞（隐形）
box('SolidR', SLOT[1], 309.66, LEN[0], LEN[1], FLOOR, PLANE_TOP, 'Collision', collide=True, visible=False)
box('SolidL', -309.66, SLOT[0], LEN[0], LEN[1], FLOOR, PLANE_TOP, 'Collision', collide=True, visible=False)
box('TileTopR', SLOT[1], 77.5, TILES_Y[0], TILES_Y[1], PLANE_TOP, DECK_TOP, 'Collision', collide=True, visible=False)
box('TileTopL', -77.5, SLOT[0], TILES_Y[0], TILES_Y[1], PLANE_TOP, DECK_TOP, 'Collision', collide=True, visible=False)
A = {'x': 305.0, 'y': (-7300.0, 7200.0), 'top': 1500.0}
for name, (x0, x1, y0, y1, z0, z1) in (('Bound_E', (A['x'], A['x'] + 2, *A['y'], FLOOR, A['top'])), ('Bound_W', (-A['x'] - 2, -A['x'], *A['y'], FLOOR, A['top'])),
                                        ('Bound_N', (-A['x'], A['x'], A['y'][1], A['y'][1] + 2, FLOOR, A['top'])),
                                        ('Bound_S', (-A['x'], A['x'], A['y'][0] - 2, A['y'][0], FLOOR, A['top'])),
                                        ('Bound_Top', (-A['x'], A['x'], *A['y'], A['top'], A['top'] + 2))):
    a = box(name, x0, x1, y0, y1, z0, z1, 'Bounds', collide=True, visible=False)
    comp = a.static_mesh_component
    comp.set_collision_profile_name('Custom')        # 先 Custom 再改（见 README 城区一节的坑）：对象类型 Vehicle、只挡 Pawn
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    a.tags = [TAG, 'ArenaWall']

# ---------------------------------------------------------------- 天空、光、雾、后期（照原图：亮面冷白、阴影深蓝灰、沟里和地平线发白）
sphere = asset('/Engine/BasicShapes/Sphere')
mesh_actor(sphere, B(0, 0, 0), scale=(120000, 120000, 120000), folder='Sky', label='SkyDome', mat=MI['MI_MC_Sky'], shadow=False, lumen=False)
sd = META['sun']['dir']                    # 原包太阳光线方向（Blender），UE 里 y 取反
dx, dy, dz = sd[0], -sd[1], sd[2]
yaw, pitch = math.degrees(math.atan2(dy, dx)), math.degrees(math.asin(max(-1.0, min(1.0, dz))))
sun = tagged(sub.spawn_actor_from_class(unreal.DirectionalLight, B(0, 0, 50), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw)), 'Light', 'Sun')
lc = sun.light_component
lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
for k, v in (('intensity', 9.0), ('temperature', 6200.0), ('use_temperature', True), ('light_source_angle', 0.5), ('cast_shadows', True)):
    setp(lc, k, v)
log('太阳 yaw', round(yaw, 1), 'pitch', round(pitch, 1))
sky = tagged(sub.spawn_actor_from_class(unreal.SkyLight, B(0, 0, 60)), 'Light', 'SkyLight')
sc_ = sky.light_component
sc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
# 天光照天空球本身（采集场景）：原包世界背景是均匀的深石板蓝，没有太阳光斑；借 HDR 立方体贴图的话，里面贴地平线的太阳（亮度 46）
# 会变成第二个低角度太阳，把本该在阴影里的那面墙照白
setp(sc_, 'source_type', unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
setp(sc_, 'sky_distance_threshold', 100000.0)
setp(sc_, 'real_time_capture', False)
setp(sc_, 'intensity', 1.6)
setp(sc_, 'light_color', unreal.Color(r=235, g=240, b=255, a=255))   # 原包世界背景是深石板蓝：阴影是蓝灰（第一版 140/176/255 太饱和，阴影成了藏青）
setp(sc_, 'lower_hemisphere_is_black', False)
setp(sc_, 'lower_hemisphere_color', unreal.LinearColor(0.05, 0.07, 0.12, 1))
fog = tagged(sub.spawn_actor_from_class(unreal.ExponentialHeightFog, B(0, 0, 0)), 'Light', 'HeightFog')
fc = fog.component
# 原包沟里和甲板上方各有几个体积雾盒子（密度 0.003–0.005、淡蓝），沟越深越白、地平线发白：用高度雾，往下指数变浓
for k, v in (('fog_density', 0.012), ('fog_height_falloff', 0.05), ('fog_max_opacity', 1.0), ('start_distance', 3000.0),
             ('fog_cutoff_distance', 2500000.0), ('fog_inscattering_luminance', unreal.LinearColor(0.62, 0.75, 1.05, 1)),
             ('directional_inscattering_luminance', unreal.LinearColor(1.2, 1.15, 1.05, 1)), ('directional_inscattering_exponent', 10.0),
             ('directional_inscattering_start_distance', 5000.0), ('enable_volumetric_fog', False)):
    setp(fc, k, v)
ppv = tagged(sub.spawn_actor_from_class(unreal.PostProcessVolume, B(0, 0, 0)), 'Light', 'PP_MegaCanyon')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
EV = 1.3      # 第一版 0.9：亮面全白、看不出面板图
for k, v in (('auto_exposure_method', unreal.AutoExposureMethod.AEM_HISTOGRAM), ('auto_exposure_min_brightness', EV),
             ('auto_exposure_max_brightness', EV), ('auto_exposure_bias', 0.0), ('auto_exposure_apply_physical_camera_exposure', False),
             ('bloom_intensity', 0.4), ('vignette_intensity', 0.2), ('indirect_lighting_intensity', 0.5),
             ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 1.05)), ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.08)),
             ('color_gain_highlights', unreal.Vector4(0.98, 1.0, 1.03, 1.0)),
             ('color_gain_shadows', unreal.Vector4(0.95, 0.98, 1.05, 1.0)), ('color_offset_shadows', unreal.Vector4(0.0, 0.002, 0.006, 0.0))):
    setp(pp, 'override_' + k, True)
    setp(pp, k, v)
ppv.set_editor_property('settings', pp)

# ---------------------------------------------------------------- 出生点、场地标记、截图机位
start = B(-20.0, 0.0, DECK_TOP + 2.0)       # 沟左边（-X）的甲板上、离沟口 11 米，朝沟里看（UE +Y = 原包 -Y，和原图相机同方向），对面那面墙是被照亮的
ps = [a for a in sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
if ps:
    ps[0].set_actor_location(start, False, False)
    ps[0].set_actor_rotation(unreal.Rotator(yaw=90.0), False)
    for extra in ps[1:]:
        sub.destroy_actor(extra)
else:
    tagged(sub.spawn_actor_from_class(unreal.PlayerStart, start, unreal.Rotator(yaw=90.0)), 'Spawns', 'PlayerStart')
for name, loc, yaw_ in (('TeamSpawn0', B(-20.0, 0.0, DECK_TOP + 0.3), 90.0), ('TeamSpawn1', B(20.0, -300.0, DECK_TOP + 0.3), -90.0)):
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, loc, unreal.Rotator(yaw=yaw_)), 'Spawns', name, [name])
# 场地标记：中心放在沟底上面（掉出判定是中心以下 30 米，沟底 -599 米要算在场内）
tagged(sub.spawn_actor_from_class(unreal.TargetPoint, B(0, (A['y'][0] + A['y'][1]) / 2, FLOOR + 10)), 'Bounds', 'MechArena',
       ['MechArena', f'HalfX={A["x"] * 100:.0f}', f'HalfY={(A["y"][1] - A["y"][0]) / 2 * 100:.0f}'])
cam = tagged(sub.spawn_actor_from_class(unreal.CameraActor, B(0, 0, 20)), 'Misc', 'ShotCam', ['ShotCam'])
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已保存', DST)
