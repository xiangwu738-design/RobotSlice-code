"""在 RobotSlice 工程里跑：用 Poly Haven（CC0）素材搭一条赛博朋克夜街当测试场景，另存为 /Game/Maps/L_Alley。
天空、大气、天气沿用 AdvancedLightingSystem：从 V29 地图里只留灯光总控、环境装置和出生点，其余全删。
重复运行会整张重建（从 V29 重新开始），不会叠加。
"""
import math
import random

import unreal

SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_Alley'
PH = '/Game/Env/PolyHaven'
TAG = 'Alley'
eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rng = random.Random(2077)

# ---------------------------------------------------------------- 场景尺寸（米）
STREET_X = (-60.0, 60.0)   # 主街东西向
STREET_HALF = 9.0          # 街宽 18 米（含人行道），北侧公寓、南侧厂房
SIDEWALK = 3.0
ALLEY_X = 12.0             # 北侧在这里断开一条 6 米宽的小巷
YARD_X = 42.0              # 东头集装箱堆场从这里开始（南侧厂房到此为止）


def lin(hexstr):
    h = hexstr.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


def V(x, y, z):
    return unreal.Vector(x * 100.0, y * 100.0, z * 100.0)


def rot_xy(x, y, yaw):
    r = math.radians(yaw)
    return x * math.cos(r) - y * math.sin(r), x * math.sin(r) + y * math.cos(r)


# ---------------------------------------------------------------- 资源
def kit(name):
    """一套 Poly Haven 模型里的所有网格：名字 → (网格, 包围盒最小点, 最大点)（米）"""
    out = {}
    for path in eas.list_assets(f'{PH}/{name}', recursive=True, include_folder=False):
        a = eas.load_asset(path)
        if isinstance(a, unreal.StaticMesh):
            b = a.get_bounding_box()
            out[a.get_name()] = (a, b.min / 100.0, b.max / 100.0)
    return out


def instance(name, color, rough=0.5, emissive=None, strength=0.0, folder='/Game/Env/Materials'):
    path = f'{folder}/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', eas.load_asset('/Game/Robot/Materials/M_RobotMaster'))
    mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', unreal.LinearColor(*lin(color), 1))
    mel.set_material_instance_scalar_parameter_value(mi, 'Roughness', rough)
    if emissive:
        mel.set_material_instance_vector_parameter_value(mi, 'Emissive', unreal.LinearColor(*lin(emissive), 1))
        mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', strength)
    eas.save_loaded_asset(mi)
    return mi


CUBE = eas.load_asset('/Engine/BasicShapes/Cube')
M = {k: eas.load_asset(f'/Game/Env/Materials/{k}') for k in
     ('MI_Asphalt', 'MI_AsphaltDirty', 'MI_ConcreteFloor', 'MI_ConcreteWall', 'MI_ConcreteWall2', 'MI_Corrugated',
      'MI_Container', 'MI_FactoryWall', 'MI_MetalPlate', 'MI_Brick')}

count = {'piece': 0, 'block': 0, 'light': 0}


def spawn_mesh(mesh, loc, yaw=0.0, scale=(1, 1, 1), mat=None, folder='Alley', collide=True, shadow=True, label=None):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(pitch=0.0, yaw=yaw, roll=0.0))
    a.tags = [TAG]
    a.set_folder_path(folder)
    if label:
        a.set_actor_label(label)
    c = a.static_mesh_component
    c.set_static_mesh(mesh)
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    if mat:
        for i in range(max(1, c.get_num_materials())):
            c.set_material(i, mat)
    if not collide:
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    if not shadow:
        c.set_editor_property('cast_shadow', False)
    a.set_actor_scale3d(unreal.Vector(*scale))
    return a


def block(cx, cy, z0, sx, sy, sz, mat, yaw=0.0, folder='Alley/Blocks', collide=True):
    """世界坐标对齐材质的实心方块：中心 (cx, cy)，底面高度 z0，尺寸 sx × sy × sz（米）"""
    count['block'] += 1
    return spawn_mesh(CUBE, V(cx, cy, z0 + sz / 2), yaw, (sx, sy, sz), mat, folder, collide)


def place_piece(kitd, name, ox, oy, oz, yaw, anchor, folder):
    """把目录里的零件放到建筑坐标里：anchor 是零件在目录里的偏移（米），(ox, oy, oz) 是在建筑局部坐标里的位置"""
    if name not in kitd:
        return None
    mesh = kitd[name][0]
    ax, ay, az = anchor
    lx, ly = rot_xy(ox - ax, oy - ay, yaw)
    count['piece'] += 1
    return spawn_mesh(mesh, V(BX + lx, BY + ly, oz - az), yaw, folder=folder)


def anchor_of(kitd, name):
    _, lo, hi = kitd[name]
    return ((lo.x + hi.x) / 2, float(round((lo.y + hi.y) / 2)), lo.z)


# ---------------------------------------------------------------- 楼：按开间 × 层数拼立面，背后补实心墙体
def facade(kitd, x0, y0, yaw, bays, floors, depth, ground, upper, top, tint_mat, label):
    """x0, y0：立面起点（米，世界）；yaw：0 表示立面朝 -Y（北侧楼朝街）。ground/upper/top 是可选墙板名列表。"""
    global BX, BY
    BX, BY = x0, y0
    folder = f'Alley/Buildings/{label}'
    width = 3.0 * bays
    x = 1.5
    i = 0
    while i < bays:
        for f in range(floors):
            pool = ground if f == 0 else (top if f == floors - 1 and top else upper)
            name = rng.choice(pool)
            if name not in kitd:
                continue
            anchor = anchor_of(kitd, name)
            z = 3.0 * f
            place_piece(kitd, name, x, 0.0, z, yaw, anchor, folder)
            insert = name[len('wall_'):]
            if insert in kitd:
                place_piece(kitd, insert, x, 0.0, z, yaw, anchor, folder)
            if f == 0:
                dado = 'dado_' + insert if 'dado_' + insert in kitd else 'dado_standard_standard_01'
                if dado in kitd:
                    place_piece(kitd, dado, x, 0.0, 0.0, yaw, anchor_of(kitd, dado), folder)
            else:
                for cn in ('cornice_standard_standard_01', 'cornice01_standard_standard_01'):
                    if cn in kitd:
                        a = anchor_of(kitd, cn)
                        place_piece(kitd, cn, x, 0.0, z - 0.1, yaw, a, folder)
                        break
        crown = 'crown_standard_standard_01'
        if crown in kitd:
            place_piece(kitd, crown, x, 0.0, 3.0 * floors, yaw, anchor_of(kitd, crown), folder)
        x += 3.0
        i += 1
    # 背后的实心墙体（立面只是一层面片）
    cx, cy = rot_xy(width / 2, depth / 2 + 0.03, yaw)
    block(x0 + cx, y0 + cy, -0.2, width, depth, 3.0 * floors + 0.9, tint_mat, yaw, folder)
    return width


# ---------------------------------------------------------------- 开始：从 V29 只留灯光和出生点
unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
keep_classes = ('LightingDirector', 'LightingEnvironmentRig', 'PlayerStart', 'WorldSettings')
for a in sub.get_all_level_actors():
    cls = a.get_class().get_name()
    if not any(k in cls for k in keep_classes) and 'Brush' not in cls:
        sub.destroy_actor(a)
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))
for a in sub.get_all_level_actors():
    if isinstance(a, unreal.PlayerStart):
        a.set_actor_location(V(-52, 0, 1.6), False, False)
        a.set_actor_rotation(unreal.Rotator(pitch=0, yaw=0, roll=0), False)

apt = kit('modular_urban_apartments_facade')
fac = kit('modular_factory_facade')
unreal.log(f'[alley] 公寓件 {len(apt)}，厂房件 {len(fac)}')

# ---------------------------------------------------------------- 地面：路面、人行道、外围
L = STREET_X[1] - STREET_X[0]
block(0, 0, -0.5, L + 40, (STREET_HALF - SIDEWALK) * 2, 0.5, M['MI_Asphalt'])
for side in (1, -1):
    block(0, side * (STREET_HALF - SIDEWALK / 2), -0.5, L + 40, SIDEWALK, 0.65, M['MI_ConcreteFloor'])
block(0, 0, -1.0, 400, 400, 0.48, M['MI_AsphaltDirty'])  # 远处垫底

# ---------------------------------------------------------------- 北侧：公寓楼（朝南），在 ALLEY_X 处断开一条小巷
APT_GROUND = [n for n in apt if n.startswith('wall_door_') or n == 'wall_standard_standard_01' or n.startswith('wall_window_centered_large_01')]
APT_UPPER = [n for n in apt if n.startswith('wall_window_') and not n.endswith('_03') and 'angled' not in n]
APT_TOP = [n for n in apt if n.startswith('wall_window_') and n.endswith('_03')]
x = STREET_X[0] - 6
segments = [(x, ALLEY_X - 3)]
segments.append((ALLEY_X + 3, STREET_X[1] + 6))
for si, (xa, xb) in enumerate(segments):
    xx = xa
    bi = 0
    while xx < xb - 5:
        bays = min(rng.randint(3, 6), int((xb - xx) // 3))
        floors = rng.randint(3, 6)
        facade(apt, xx, STREET_HALF, 0.0, bays, floors, 14.0, APT_GROUND, APT_UPPER, APT_TOP,
               rng.choice([M['MI_ConcreteWall'], M['MI_Brick']]), f'Apt_{si}_{bi}')
        xx += 3.0 * bays
        bi += 1

# ---------------------------------------------------------------- 南侧：厂房（朝北），到堆场为止
FAC_GROUND = [n for n in fac if n.startswith(('wall_door_recessed', 'wall_door_centered', 'wall_door_garage_door_01'))] + ['wall_standard_standard_01']
FAC_UPPER = [n for n in fac if n.startswith('wall_window_') and 'door' not in n]
xx = STREET_X[0] - 6
bi = 0
while xx < YARD_X - 6:
    bays = min(rng.randint(4, 8), int((YARD_X - xx) // 3))
    floors = rng.randint(2, 4)
    # yaw 180：立面朝 +Y；起点要放在这一段的东端
    facade(fac, xx + 3.0 * bays, -STREET_HALF, 180.0, bays, floors, 16.0, FAC_GROUND, FAC_UPPER, None,
           rng.choice([M['MI_FactoryWall'], M['MI_ConcreteWall2']]), f'Fac_{bi}')
    xx += 3.0 * bays
    bi += 1

# ---------------------------------------------------------------- 小巷尽头的墙、东头堆场的围墙
block(ALLEY_X, STREET_HALF + 26, -0.2, 6.5, 1.0, 12, M['MI_ConcreteWall2'])
block(STREET_X[1] + 14, -6, -0.2, 1.0, 40, 8, M['MI_Corrugated'])
block(YARD_X + 15, -27, -0.2, 34, 1.0, 8, M['MI_Corrugated'])
block(YARD_X - 0.5, -18, -0.2, 1.0, 18, 8, M['MI_Corrugated'])
block(YARD_X + 15, -15, -0.5, 34, 24, 0.5, M['MI_ConcreteFloor'])

# 集装箱（掩体）：两三层堆起来，用集装箱侧板材质
for i, (cx, cy, yaw, stack) in enumerate([(48, -6, 0, 2), (54, -12, 90, 1), (50, -20, 10, 3), (58, -3, 90, 2),
                                          (44, -14, 90, 1), (60, -20, 0, 1)]):
    for k in range(stack):
        block(cx + (k % 2) * 0.4, cy, 2.6 * k, 6.06, 2.44, 2.59, M['MI_Container'], yaw + (k % 2) * 4, 'Alley/Yard')

# ---------------------------------------------------------------- 横跨街道的钢天桥（7 米高）
BR_X = -18.0
block(BR_X, 0, 7.0, 4.0, STREET_HALF * 2 + 2, 0.35, M['MI_MetalPlate'], 0, 'Alley/Bridge')
for side in (1, -1):
    block(BR_X + side * 1.95, 0, 7.35, 0.08, STREET_HALF * 2 + 2, 1.1, M['MI_MetalPlate'], 0, 'Alley/Bridge')
    block(BR_X, side * 6.0, 0, 0.5, 0.5, 7.0, M['MI_MetalPlate'], 0, 'Alley/Bridge')
# 天桥两端的楼梯平台（方便跳上去）
for k in range(4):
    block(BR_X - 3.5 - k * 2.2, -STREET_HALF + 1.2, 0, 2.2, 2.0, 1.75 * (k + 1), M['MI_MetalPlate'], 0, 'Alley/Bridge')

# ---------------------------------------------------------------- 街面道具
def prop(kitname, x, y, yaw=0.0, name=None):
    kd = kit(kitname)
    meshes = [kd[name]] if name else list(kd.values())
    for mesh, lo, hi in meshes:
        spawn_mesh(mesh, V(x, y, 0.15), yaw, folder='Alley/Props')


for i in range(9):
    x = STREET_X[0] + 8 + i * 13
    prop('street_lamp_01', x, STREET_HALF - 0.6 if i % 2 else -STREET_HALF + 0.6, 0 if i % 2 else 180)
for x, y, yaw in [(-40, -3, 15), (-36, -2, -5), (5, 2.5, 80), (8, 3, 95), (28, -2, 30), (30, -3.5, 10)]:
    prop(rng.choice(['concrete_road_barrier', 'concrete_road_barrier_02']), x, y, yaw)
for x, y in [(-46, 7.2), (-20, -7.4), (2, 7.4), (24, -7.3), (36, 7.2)]:
    prop('metal_trash_can', x, y, rng.uniform(0, 360))
for x, y in [(-30, -7.0), (-29, -6.2), (16, 7.0), (47, -9)]:
    prop(rng.choice(['Barrel_01', 'barrel_03']), x, y, rng.uniform(0, 360))
for x, y in [(-11, 6.8), (40, -4)]:
    prop('barrel_stove', x, y)
prop('covered_car', -2, -5.5, 8)
prop('covered_car', 22, 5.8, 185)
for x, y in [(-44, 7.6), (-6, -7.6), (19, 7.6)]:
    prop(rng.choice(['utility_box_01', 'utility_box_02']), x, y, 0 if y > 0 else 180)
for x in (-35, -8, 25):
    prop('water_manhole_cover', x, 0.5)

# 外挂空调：挂在北侧楼面二三楼
ac = kit('exterior_aircon_unit')
for i in range(14):
    x = STREET_X[0] + 4 + i * 8.3 + rng.uniform(-1, 1)
    z = rng.choice([3.6, 6.6, 9.6])
    for mesh, lo, hi in ac.values():
        spawn_mesh(mesh, V(x, STREET_HALF - 0.45, z), 180, folder='Alley/Props')

# ---------------------------------------------------------------- 霓虹招牌 + 彩色面光源
import os

SIGN_SRC = 'D:/RobotSlice/SourceAssets/signs'
sign_tasks = []
for f in sorted(os.listdir(SIGN_SRC)):
    if f.endswith('.fbx') and not eas.does_asset_exist(f'/Game/Env/Signs/{f[:-4]}'):
        t = unreal.AssetImportTask()
        t.filename = f'{SIGN_SRC}/{f}'
        t.destination_path = '/Game/Env/Signs'
        t.automated = True
        t.replace_existing = True
        t.save = True
        sign_tasks.append(t)
if sign_tasks:
    tools.import_asset_tasks(sign_tasks)

SIGN_COLORS = {'sign_ramen': '#ff2a6a', 'sign_repair': '#2ae8ff', 'sign_clinic': '#3cf0ff', 'sign_hotel': '#ffb020',
               'sign_pawn': '#ff3355', 'sign_energy': '#2affb0', 'sign_arcade': '#b04bff', 'sign_24h': '#ffb020',
               'sign_open': '#ff2a8a', 'sign_hx': '#ff7a1a'}
board = instance('MI_Sign_Board', '#0c0d10', 0.4, folder='/Game/Env/Signs')
signs = {}
for name, color in SIGN_COLORS.items():
    mesh = eas.load_asset(f'/Game/Env/Signs/{name}')
    if mesh:
        neon = instance(f'MI_Neon_{name[5:]}', '#000000', 0.5, color, 60.0, folder='/Game/Env/Signs')
        mats = list(mesh.get_editor_property('static_materials'))
        for sm in mats:
            sm.set_editor_property('material_interface', neon if 'Neon' in str(sm.get_editor_property('material_slot_name')) else board)
        mesh.set_editor_property('static_materials', mats)
        eas.save_loaded_asset(mesh)
        signs[name] = (mesh, color)

placements = [('sign_ramen', -48, 1, 5.5), ('sign_repair', -40, 1, 3.4), ('sign_clinic', -30, 1, 6.0),
              ('sign_hotel', -14, 1, 8.0), ('sign_pawn', 2, 1, 5.0), ('sign_arcade', 20, 1, 6.5),
              ('sign_energy', -36, -1, 4.0), ('sign_24h', -10, -1, 3.2), ('sign_open', 8, -1, 3.0),
              ('sign_hx', 30, -1, 5.0)]
for name, x, side, z in placements:
    if name not in signs:
        continue
    mesh, color = signs[name]
    y = STREET_HALF - 0.6 if side > 0 else -STREET_HALF + 0.6
    # 竖排的挑出来（垂直于墙），横排的贴墙
    vertical = name in ('sign_ramen', 'sign_clinic', 'sign_hotel', 'sign_pawn', 'sign_arcade')
    if vertical:
        y = STREET_HALF - 1.1 if side > 0 else -STREET_HALF + 1.1
        yaw = 90
    else:
        yaw = 0 if side > 0 else 180
    spawn_mesh(mesh, V(x, y, z), yaw, folder='Alley/Signs', shadow=False)
    light = sub.spawn_actor_from_class(unreal.RectLight, V(x, y - side * 1.2, z + 0.8),
                                       unreal.Rotator(pitch=-20, yaw=-90 if side > 0 else 90, roll=0))
    light.tags = [TAG]
    light.set_folder_path('Alley/Lights')
    lc = light.rect_light_component
    lc.set_light_color(unreal.LinearColor(*lin(color), 1))
    lc.set_intensity(110.0)
    lc.set_editor_property('source_width', 160.0)
    lc.set_editor_property('source_height', 80.0)
    lc.set_attenuation_radius(1600.0)
    lc.set_editor_property('cast_shadows', False)
    count['light'] += 1

# 路灯的光：冷白，打在湿路面上
for i in range(9):
    x = STREET_X[0] + 8 + i * 13
    y = STREET_HALF - 2.2 if i % 2 else -STREET_HALF + 2.2
    light = sub.spawn_actor_from_class(unreal.SpotLight, V(x, y, 3.7), unreal.Rotator(pitch=-90, yaw=0, roll=0))
    light.tags = [TAG]
    light.set_folder_path('Alley/Lights')
    lc = light.spot_light_component
    # 冷白和钠灯暖黄交替，亮度压低，避免地面一片过曝的白斑
    lc.set_light_color(unreal.LinearColor(0.7, 0.82, 1.0, 1) if i % 3 else unreal.LinearColor(1.0, 0.62, 0.32, 1))
    lc.set_intensity(1200.0)
    lc.set_attenuation_radius(1500.0)
    lc.set_editor_property('inner_cone_angle', 20.0)
    lc.set_editor_property('outer_cone_angle', 55.0)
    count['light'] += 1

# ---------------------------------------------------------------- 头顶电线：横拉在街道上空，亚洲街巷最有辨识度的细节
CABLE_SRC = 'D:/RobotSlice/SourceAssets/cables'
cable_tasks = []
for f in sorted(os.listdir(CABLE_SRC)):
    if f.endswith('.fbx') and not eas.does_asset_exist(f'/Game/Env/Cables/{f[:-4]}'):
        t = unreal.AssetImportTask()
        t.filename = f'{CABLE_SRC}/{f}'
        t.destination_path = '/Game/Env/Cables'
        t.automated = True
        t.replace_existing = True
        t.save = True
        cable_tasks.append(t)
if cable_tasks:
    tools.import_asset_tasks(cable_tasks)
cable_mat = instance('MI_Cable', '#0b0b0c', 0.45, folder='/Game/Env/Cables')
cables = [eas.load_asset(f'/Game/Env/Cables/{n}') for n in
          ('cable_single_thin', 'cable_single_thick', 'cable_pair', 'cable_bundle', 'cable_bundle_heavy')]
cables = [c for c in cables if c]
span_y = (STREET_HALF - 0.3) * 2
for i in range(30):
    x = rng.uniform(STREET_X[0] + 3, YARD_X - 2)
    dx = rng.uniform(-5, 5)
    z = rng.uniform(5.5, 12.5)
    length = math.hypot(span_y, dx)
    yaw = math.degrees(math.atan2(span_y, dx))
    spawn_mesh(rng.choice(cables), V(x, -STREET_HALF + 0.3, z), yaw, (length / 18.0, 1, 1), cable_mat,
               'Alley/Cables', collide=False, shadow=True)

# ---------------------------------------------------------------- 一层店面：画了涂鸦的卷帘门窗
shutters = kit('rollershutter_door')
shutters.update(kit('rollershutter_window_01'))
for i in range(16):
    name = rng.choice([n for n in shutters if 'graffiti' in n] + list(shutters))
    mesh, lo, hi = shutters[name]
    cx = (lo.x + hi.x) / 2
    side = 1 if i % 2 else -1
    x = rng.uniform(STREET_X[0] + 2, (ALLEY_X - 4) if (side > 0 and i < 8) else YARD_X - 3)
    y = STREET_HALF - 0.31 if side > 0 else -STREET_HALF + 0.31
    yaw = 0 if side > 0 else 180
    ox, oy = rot_xy(-cx, 0, yaw)
    spawn_mesh(mesh, V(x + ox, y + oy, 0.15), yaw, folder='Alley/Shopfronts')

# ---------------------------------------------------------------- 更多街面杂物：轮胎堆、油桶、垃圾桶
for k in range(10):
    x = rng.uniform(STREET_X[0] + 2, YARD_X)
    y = (STREET_HALF - 0.9) * rng.choice([1, -1])
    choice = rng.random()
    if choice < 0.35:
        for t in range(rng.randint(2, 4)):
            spawn_mesh(kit('old_tyre')['old_tyre_2k'][0], V(x + rng.uniform(-0.1, 0.1), y, 0.15 + 0.3 + t * 0.17),
                       rng.uniform(0, 360), folder='Alley/Props')
    elif choice < 0.7:
        prop(rng.choice(['Barrel_01', 'barrel_03']), x, y, rng.uniform(0, 360))
    else:
        prop('metal_trash_can', x, y, rng.uniform(0, 360))

# ---------------------------------------------------------------- 电影感后期：青色暗部、暖品红高光、辉光、色散、颗粒、暗角
ppv = sub.spawn_actor_from_class(unreal.PostProcessVolume, V(0, 0, 0), unreal.Rotator(pitch=0, yaw=0, roll=0))
ppv.tags = [TAG]
ppv.set_actor_label('PP_CinematicGrade')
ppv.set_folder_path('Alley/Lights')
ppv.set_editor_property('unbound', True)
ppv.set_editor_property('priority', 10.0)
pp = ppv.get_editor_property('settings')
for key, value in (('bloom_intensity', 0.85), ('scene_fringe_intensity', 0.35), ('vignette_intensity', 0.45),
                   ('film_grain_intensity', 0.22), ('lens_flare_intensity', 0.25),
                   ('color_saturation', unreal.Vector4(1.0, 1.0, 1.0, 1.08)),
                   ('color_contrast', unreal.Vector4(1.0, 1.0, 1.0, 1.12)),
                   ('color_gain_shadows', unreal.Vector4(0.86, 0.97, 1.1, 1.0)),
                   ('color_gain_highlights', unreal.Vector4(1.08, 0.98, 1.0, 1.0)),
                   ('color_offset_shadows', unreal.Vector4(0.0, 0.004, 0.012, 0.0))):
    pp.set_editor_property('override_' + key, True)
    pp.set_editor_property(key, value)
ppv.set_editor_property('settings', pp)

# ---------------------------------------------------------------- 远景高楼：一栋一个方块，窗户由 M_Skyline 按世界坐标生成
sky_mat = eas.load_asset('/Game/Env/Materials/M_Skyline')
beacon = instance('MI_Beacon', '#000000', 0.5, '#ff1a10', 25.0)
screens = [instance('MI_Screen_M', '#000000', 0.4, '#e0207a', 4.0), instance('MI_Screen_C', '#000000', 0.4, '#18b8e0', 4.0),
           instance('MI_Screen_A', '#000000', 0.4, '#f09a20', 3.5)]
for i in range(70):
    ang = (i + rng.random() * 0.7) / 70 * 2 * math.pi
    dist = rng.uniform(150, 420)
    cx, cy = math.cos(ang) * dist, math.sin(ang) * dist * 0.85
    if abs(cy) < 40 and abs(cx) < 85:
        continue
    w, d, h = rng.uniform(20, 50), rng.uniform(20, 50), rng.uniform(60, 240)
    yaw = rng.uniform(0, 90)
    block(cx, cy, -1, w, d, h, sky_mat, yaw, 'Alley/Skyline', collide=False)
    block(cx, cy, h - 1, 0.6, 0.6, 0.6, beacon, 0, 'Alley/Skyline', collide=False)  # 楼顶航空障碍灯
    if rng.random() < 0.22:
        # 朝街区那一面挂一块大屏
        face = math.degrees(math.atan2(-cy, -cx))
        sx, sy = math.cos(math.radians(face)) * (min(w, d) / 2 + 0.5), math.sin(math.radians(face)) * (min(w, d) / 2 + 0.5)
        block(cx + sx, cy + sy, rng.uniform(0.35, 0.7) * h, 0.5, rng.uniform(10, 20), rng.uniform(6, 12),
              rng.choice(screens), face, 'Alley/Skyline', collide=False)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
unreal.log(f'[alley] 已另存 {DST_MAP}：建筑零件 {count["piece"]}、方块 {count["block"]}、灯 {count["light"]}')
