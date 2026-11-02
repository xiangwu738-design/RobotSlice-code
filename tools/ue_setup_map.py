"""在 RobotSlice 工程里跑：把 V29 校园地图整理成测试占位地图并另存（原地图不动）。
- 游戏模式改成 MechGameMode
- 外圈生成一圈远处的楼群，楼身加霓虹灯带、发光广告牌（赛博朋克方向的背景）
- 一部分路灯改成品红 / 青色
重复运行会先删掉上次生成的背景。
"""
import math
import random

import unreal

SRC = '/Game/CampusV29/Maps/L_XiuzhouCampus_V29'
DST = '/Game/Maps/L_CampusTest'
MAT_DIR = '/Game/Robot/Materials'
TAG = 'GeneratedBackground'

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eas = unreal.EditorAssetLibrary
actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def lin(hexstr):
    h = hexstr.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def instance(name, color, rough=0.6, emissive=None, strength=0.0):
    path = f'{MAT_DIR}/{name}'
    mi = unreal.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', unreal.load_asset(f'{MAT_DIR}/M_RobotMaster'))
    mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', unreal.LinearColor(*lin(color), 1))
    mel.set_material_instance_scalar_parameter_value(mi, 'Roughness', rough)
    if emissive:
        mel.set_material_instance_vector_parameter_value(mi, 'Emissive', unreal.LinearColor(*lin(emissive), 1))
        mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', strength)
    eas.save_loaded_asset(mi)
    return mi


unreal.EditorLoadingAndSavingUtils.load_map(SRC)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

# 去掉上次生成的背景，以及旧工程里（插件没带过来的）FPV 航线、可撞碎玻璃
for a in actors_sub.get_all_level_actors():
    cls = a.get_class().get_path_name()
    if a.actor_has_tag(TAG) or 'CampusShuttle' in cls:
        actors_sub.destroy_actor(a)

# 游戏模式
ws = world.get_world_settings()
ws.set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))

# 校园范围：所有静态网格的包围盒
lo = unreal.Vector(1e12, 1e12, 1e12)
hi = unreal.Vector(-1e12, -1e12, -1e12)
for a in actors_sub.get_all_level_actors():
    if isinstance(a, unreal.StaticMeshActor):
        origin, ext = a.get_actor_bounds(False)
        if max(ext.x, ext.y) > 50000:  # 跳过 2.6 公里的大地面之类，只按建筑算校园范围
            continue
        lo = unreal.Vector(min(lo.x, origin.x - ext.x), min(lo.y, origin.y - ext.y), min(lo.z, origin.z - ext.z))
        hi = unreal.Vector(max(hi.x, origin.x + ext.x), max(hi.y, origin.y + ext.y), max(hi.z, origin.z + ext.z))
center = unreal.Vector((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z)
half = max(hi.x - lo.x, hi.y - lo.y) / 2
unreal.log(f'[map] 校园范围 {lo} ~ {hi}，半径约 {half / 100:.0f} 米')

cube = unreal.load_asset('/Engine/BasicShapes/Cube')
plane = unreal.load_asset('/Engine/BasicShapes/Plane')
# 楼身带一点极弱的自发光，夜里能从天空里分出轮廓
m_building = instance('MI_City_Building', '#1b1f25', 0.55, '#1c2638', 0.35)
m_building2 = instance('MI_City_BuildingLight', '#2a3038', 0.5, '#26304a', 0.45)
m_window = instance('MI_City_Windows', '#0a0c10', 0.4, '#8fb0d8', 1.6)
neons = [instance('MI_Neon_Magenta', '#000000', 0.5, '#ff2a8a', 45.0),
         instance('MI_Neon_Cyan', '#000000', 0.5, '#2ae8ff', 40.0),
         instance('MI_Neon_Amber', '#000000', 0.5, '#ffb020', 35.0)]
m_board = [instance('MI_Board_Magenta', '#000000', 0.5, '#d0237a', 6.0),
           instance('MI_Board_Cyan', '#000000', 0.5, '#1fb8d0', 6.0)]


def block(name, loc, size, mat, rot_yaw=0.0, collide=True):
    a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(0, 0, rot_yaw))
    a.set_actor_label(name)
    a.tags = [TAG]
    a.set_folder_path('Background')
    smc = a.static_mesh_component
    smc.set_static_mesh(cube)
    smc.set_material(0, mat)
    smc.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if not collide:
        smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        smc.set_editor_property('cast_shadow', False)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100, size[1] / 100, size[2] / 100))
    return a


rng = random.Random(7)
n_buildings = n_neon = 0
for ring, (r0, r1, hmin, hmax, count) in enumerate(((1.25, 1.6, 2500, 6000, 70), (1.7, 2.6, 5000, 16000, 90))):
    for i in range(count):
        ang = (i + rng.random() * 0.8) / count * 2 * math.pi
        dist = half * rng.uniform(r0, r1)
        x = center.x + math.cos(ang) * dist
        y = center.y + math.sin(ang) * dist
        w, d = rng.uniform(1800, 5500), rng.uniform(1800, 5500)
        h = rng.uniform(hmin, hmax)
        yaw = math.degrees(ang) + rng.uniform(-8, 8)
        mat = m_building if rng.random() < 0.7 else m_building2
        block(f'City_{ring}_{i}', unreal.Vector(x, y, center.z + h / 2), (w, d, h), mat, yaw)
        n_buildings += 1
        # 朝向校园那一面：几道窗户亮带 + 偶尔一条竖向霓虹
        face = unreal.Vector(-math.cos(ang), -math.sin(ang), 0)
        fx, fy = x + face.x * (w / 2 + 30), y + face.y * (w / 2 + 30)
        for k in range(rng.randint(2, 6)):
            z = center.z + rng.uniform(0.1, 0.9) * h
            block(f'Win_{ring}_{i}_{k}', unreal.Vector(fx, fy, z), (60, d * rng.uniform(0.5, 0.9), rng.uniform(80, 220)),
                  m_window, yaw, collide=False)
        if rng.random() < 0.55:
            nz = center.z + rng.uniform(0.15, 0.6) * h
            block(f'Neon_{ring}_{i}', unreal.Vector(fx + face.x * 20, fy + face.y * 20, nz),
                  (40, rng.uniform(60, 140), rng.uniform(600, 2200)), rng.choice(neons), yaw, collide=False)
            n_neon += 1
        if ring == 0 and rng.random() < 0.3:
            bz = center.z + rng.uniform(0.3, 0.7) * h
            block(f'Board_{ring}_{i}', unreal.Vector(fx + face.x * 25, fy + face.y * 25, bz),
                  (30, rng.uniform(1200, 2400), rng.uniform(600, 1200)), rng.choice(m_board), yaw, collide=False)

# 地图自带 2.6 公里的大地面，楼群就站在上面，不另铺地面

# 路灯：一半改成品红 / 青色
lamps = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.PointLight)]
for i, lamp in enumerate(lamps):
    comp = lamp.point_light_component
    if i % 3 == 0:
        comp.set_light_color(unreal.LinearColor(1.0, 0.16, 0.54, 1))
    elif i % 3 == 1:
        comp.set_light_color(unreal.LinearColor(0.16, 0.9, 1.0, 1))

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
unreal.log(f'[map] 已另存 {DST}：楼 {n_buildings} 栋，霓虹 {n_neon} 条，路灯 {len(lamps)} 盏重新上色')
