"""按 Inbox/_k62/plan.json（tools/soviet_plan.py）搭 /Game/Maps/L_Soviet：冬天的苏联住宅区。整张重建。不用 LightingDirector（C++ 里按地图名设天气，这张图不改 C++），
阴天的光、天空球（薄雾漫射天 HDRI）、雾、后期、下雪粒子都在地图里。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_build_soviet.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
坐标：plan 是 Blender 的（米，y 向北）→ UE (x, -y, z) × 100，yaw 取反。
"""
import json
import math
import os

import unreal

PLAN = json.load(open('D:/RobotSlice/Inbox/_k62/plan.json', encoding='utf-8'))
PROPS = json.load(open('D:/RobotSlice/Inbox/_soviet_props/export/meshes.json', encoding='utf-8'))['props']
DST = '/Game/Maps/L_Soviet'
DEST = '/Game/Env/Soviet'
TAG = 'Soviet'
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
log = lambda *a: unreal.log('[soviet] ' + ' '.join(str(x) for x in a))
_cache = {}

# 氛围参数（第一版，看截图调）
SUN = dict(intensity=1.1, angle=25.0, pitch=-52.0, yaw=205.0)
SKY_INT = 1.0
SKY_TINT = float(os.environ.get('SV_SKY_TINT', '1.0'))
FOG = dict(density=0.014, falloff=0.06, lum=(0.60, 0.65, 0.72))
EV = float(os.environ.get('SV_EV', '0.4'))
LAMP_CD = float(os.environ.get('SV_LAMP', '70'))


def asset(path):
    if path not in _cache:
        _cache[path] = eas.load_asset(path) if eas.does_asset_exist(path) else None
        if _cache[path] is None:
            unreal.log_warning(f'[soviet] 缺资源 {path}')
    return _cache[path]


def V(x, y, z):
    return unreal.Vector(x * 100.0, -y * 100.0, z * 100.0)


def R(yaw):
    return unreal.Rotator(roll=0.0, pitch=0.0, yaw=-yaw)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        log('设不了', obj.get_class().get_name(), name, e)


def tagged(a, folder, label=None, extra=()):
    a.tags = [TAG] + list(extra)
    a.set_folder_path(f'Soviet/{folder}')
    if label:
        a.set_actor_label(label)
    return a


def spawn_mesh(mesh, loc, yaw, scale=1.0, folder='Misc', label=None, collide=True, shadow=True, gi=True, mat=None):
    a = tagged(sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, R(yaw)), folder, label)
    if scale != 1.0:
        a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
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
    if not gi:
        setp(c, 'affect_distance_field_lighting', False)
        setp(c, 'affect_dynamic_indirect_lighting', False)
    return a


# ---------------------------------------------------------------- 关卡：每次新建
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

# ---------------------------------------------------------------- 地面：一整块（平的，机体不会被路牙绊）；材质按世界坐标 + 遮罩分路面和雪地
cube = asset('/Engine/BasicShapes/Cube')
gr = spawn_mesh(cube, V(0, 0, -10), 0, 1.0, 'Ground', 'Ground', True, False, True, asset(f'{DEST}/Materials/MI_SV_Ground'))
gr.set_actor_scale3d(unreal.Vector(16000, 16000, 20))

# ---------------------------------------------------------------- 楼和小品
counts = {}
for it in PLAN['items']:
    k = it['kind']
    counts[k] = counts.get(k, 0) + 1
    if k == 'bldg':
        far = it.get('far')
        vf = it.get('very_far')
        base = it['id']
        loc = V(it['x'], it['y'], 0)
        solid = asset(f'{DEST}/Buildings/SM_SV_{base}_Solid')
        if not solid:
            continue
        lab = f'{"Far" if far else "Bldg"}_{base}'
        folder = 'Far' if far else 'Buildings'
        # 场内：Solid 有碰撞 + Deco 无碰撞。远景：都不碰撞；很远的只放 Solid（Deco 占七成面数，看不出来）、不投影
        spawn_mesh(solid, loc, it['yaw'], 1.0, folder, lab, not far, not vf, not vf)
        if not vf:
            deco = asset(f'{DEST}/Buildings/SM_SV_{base}_Deco')
            if deco:
                spawn_mesh(deco, loc, it['yaw'], 1.0, folder, lab + '_Deco', False, not far, False)
    elif k == 'prop':
        name = 'SM_SVS_' + it['id']
        mesh = asset(f'{DEST}/Props/SVS_{it["id"]}')      # FBX 只有一个网格时 UE 按文件名命名
        if not mesh:
            continue
        m = PROPS[name]
        size = [m['max'][i] - m['min'][i] for i in range(3)]
        collide = it.get('collide')
        if collide is None:   # 城区图同样的规则：水平不到 1 米、高不到 0.9 米的不挡机体
            collide = max(size[0], size[1]) >= 1.0 and size[2] >= 0.9
        spawn_mesh(mesh, V(it['x'], it['y'], it.get('z', 0.0)), it['yaw'], it.get('scale', 1.0), 'Props', it['id'], collide)
    elif k == 'kprop':      # K62 自带的单件（路灯等）
        mesh = asset(f'{DEST}/Props/SM_SVP_{it["id"]}')
        if mesh:
            spawn_mesh(mesh, V(it['x'], it['y'], it.get('z', 0.0)), it['yaw'], it.get('scale', 1.0), 'Props', it['id'], bool(it.get('collide')))
    elif k == 'light':
        pl = tagged(sub.spawn_actor_from_class(unreal.PointLight, V(it['x'], it['y'], it['z']), R(0)), 'Lamps', 'LampLight')
        lc_ = pl.point_light_component
        lc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
        for key, val in (('intensity_units', unreal.LightUnits.CANDELAS), ('intensity', LAMP_CD), ('attenuation_radius', 1100.0), ('light_color', unreal.Color(r=255, g=178, b=112, a=255)),
                         ('cast_shadows', False), ('indirect_lighting_intensity', 0.0), ('volumetric_scattering_intensity', 0.0), ('source_radius', 30.0)):
            setp(lc_, key, val)
    elif k == 'winter':
        mesh = asset(it['id'])
        if mesh:
            spawn_mesh(mesh, V(it['x'], it['y'], it.get('z', 0.0)), it['yaw'], it.get('scale', 1.0), 'Winter', it['id'].rsplit('/', 1)[-1], bool(it.get('collide')))

# ---------------------------------------------------------------- 边界：四面隐形墙 + 天花板（只挡机体，不挡子弹 / 镜头 / AI 视线）
A = PLAN['arena']
hx, hy, ceil = A['half'][0], A['half'][1], A['ceiling']
walls = [('Wall_N', (0, hy + 1, ceil / 2), (2 * hx + 4, 2, ceil + 10)), ('Wall_S', (0, -hy - 1, ceil / 2), (2 * hx + 4, 2, ceil + 10)),
         ('Wall_E', (hx + 1, 0, ceil / 2), (2, 2 * hy + 4, ceil + 10)), ('Wall_W', (-hx - 1, 0, ceil / 2), (2, 2 * hy + 4, ceil + 10)),
         ('Ceiling', (0, 0, ceil + 1), (2 * hx + 4, 2 * hy + 4, 2))]
for name, c, s in walls:
    a = spawn_mesh(cube, V(*c), 0, 1.0, 'Bounds', name, True, False, False)
    a.set_actor_scale3d(unreal.Vector(*s))
    a.set_actor_hidden_in_game(True)
    comp = a.static_mesh_component
    comp.set_editor_property('visible', False)
    comp.set_collision_profile_name('Custom')    # 先 Custom 再改（城区图的坑：不然存盘读回来按 BlockAll）
    comp.set_collision_object_type(unreal.CollisionChannel.ECC_VEHICLE)
    comp.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN, unreal.CollisionResponseType.ECR_BLOCK)
    a.tags = [TAG, 'ArenaWall']
tagged(sub.spawn_actor_from_class(unreal.TargetPoint, V(*A['center']), R(0)), 'Bounds', 'MechArena', ['MechArena', f'HalfX={hx * 100:.0f}', f'HalfY={hy * 100:.0f}'])

# ---------------------------------------------------------------- 出生点：一个 PlayerStart（?Range 用它）+ TargetPoint 标记（MechMaps.h 的约定）
sx, sy, syaw = PLAN['start']
tagged(sub.spawn_actor_from_class(unreal.PlayerStart, V(sx, sy, 2.0), R(syaw)), 'Spawns', 'PlayerStart')
for tag, (x, y, yaw) in PLAN['team'].items():
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, V(x, y, 0.2), R(yaw)), 'Spawns', 'TeamSpawn' + tag[-1], ['TeamSpawn' + tag[-1]])
for i, (x, y) in enumerate(PLAN['dm']):
    tagged(sub.spawn_actor_from_class(unreal.TargetPoint, V(x, y, 0.2), R(0)), 'Spawns', f'DM_{i + 1:02d}', ['DMSpawn'])

# ---------------------------------------------------------------- 阴天：弱太阳（只给一点方向感）+ 天光采集 HDRI 天空球 + 雾
sphere = asset('/Engine/BasicShapes/Sphere')
sky_mi = asset(f'{DEST}/Materials/MI_SV_Sky')
unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(sky_mi, 'SkyTint', unreal.LinearColor(SKY_TINT, SKY_TINT, SKY_TINT, 1))
eas.save_loaded_asset(sky_mi)
dome = spawn_mesh(sphere, V(0, 0, 0), 0, 1.0, 'Sky', 'SkyDome', False, False, False, sky_mi)
dome.set_actor_scale3d(unreal.Vector(120000, 120000, 120000))
sun = tagged(sub.spawn_actor_from_class(unreal.DirectionalLight, V(0, 0, 80), unreal.Rotator(roll=0, pitch=SUN['pitch'], yaw=SUN['yaw'])), 'Light', 'Sun')
lc = sun.light_component
lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
for k, v in (('intensity', SUN['intensity']), ('temperature', 7800.0), ('use_temperature', True), ('light_source_angle', SUN['angle']), ('cast_shadows', True)):
    setp(lc, k, v)
sky = tagged(sub.spawn_actor_from_class(unreal.SkyLight, V(0, 0, 90)), 'Light', 'SkyLight')
sc_ = sky.light_component
sc_.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
setp(sc_, 'source_type', unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
setp(sc_, 'sky_distance_threshold', 150000.0)
setp(sc_, 'real_time_capture', False)
setp(sc_, 'intensity', SKY_INT)
setp(sc_, 'lower_hemisphere_is_black', False)
setp(sc_, 'lower_hemisphere_color', unreal.LinearColor(0.42, 0.45, 0.5, 1))     # 雪地反光：下半球不黑
fog = tagged(sub.spawn_actor_from_class(unreal.ExponentialHeightFog, V(0, 0, 0)), 'Light', 'HeightFog')
fc = fog.component
for k, v in (('fog_density', FOG['density']), ('fog_height_falloff', FOG['falloff']), ('fog_max_opacity', 1.0), ('start_distance', 4000.0),
             ('fog_inscattering_luminance', unreal.LinearColor(*FOG['lum'], 1)), ('enable_volumetric_fog', False)):
    setp(fc, k, v)
ppv = tagged(sub.spawn_actor_from_class(unreal.PostProcessVolume, V(0, 0, 0)), 'Light', 'PP_Soviet')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
for k, v in (('auto_exposure_method', unreal.AutoExposureMethod.AEM_HISTOGRAM), ('auto_exposure_min_brightness', EV), ('auto_exposure_max_brightness', EV),
             ('auto_exposure_bias', 0.0), ('auto_exposure_apply_physical_camera_exposure', False),
             ('bloom_intensity', 0.25), ('vignette_intensity', 0.3), ('film_grain_intensity', 0.05),
             ('color_saturation', unreal.Vector4(0.95, 0.97, 1.0, 0.82)), ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.08)),
             ('color_gain_shadows', unreal.Vector4(0.92, 0.97, 1.06, 1.0)), ('color_offset_shadows', unreal.Vector4(0.0, 0.002, 0.008, 0.0))):
    setp(pp, 'override_' + k, True)
    setp(pp, k, v)
ppv.set_editor_property('settings', pp)

# ---------------------------------------------------------------- 下雪：跟着相机的雪花盒子（tools/soviet_snowbox.py + M_SV_Snow），近一层、远一层。
# 包围盒放大到几百公里（顶点在着色器里挪到相机旁边，按原位置算的包围盒会被视锥剔除）
snow = asset(f'{DEST}/Props/SM_SV_SnowBox')
for lab, mi, sc_val in (('SnowNear', 'MI_SV_SnowNear', 1.0), ('SnowFar', 'MI_SV_SnowFar', 3.4)):
    if not snow:
        break
    a = spawn_mesh(snow, V(0, 0, 0), 0, sc_val, 'Snow', lab, False, False, False, asset(f'{DEST}/Materials/{mi}'))
    c = a.static_mesh_component
    setp(c, 'bounds_scale', 200.0)
    setp(c, 'mobility', unreal.ComponentMobility.MOVABLE)
    setp(c, 'cast_shadow', False)
    setp(c, 'receives_decals', False)
    setp(c, 'evaluate_world_position_offset', True)

# 截图机位
cam = tagged(sub.spawn_actor_from_class(unreal.CameraActor, V(0, 0, 20)), 'Misc', 'ShotCam', ['ShotCam'])
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log('已存', DST, counts)
