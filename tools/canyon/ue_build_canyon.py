"""夺芯峡谷美术图 /Game/Maps/L_Canyon：从 L_LightingBase（天空灯光）另存，摆地形块（Nanite，无碰撞）+ 隐藏的碰撞块（50 厘米体素，贴合规格）
+ 远景 + 浅河（水面 + 带 Water 标签的隐藏碰撞盒）+ 掩体石头（灰盒同位置同尺寸）+ 散布（碎石、植被）+ 两个 PlayerStart。
不放游戏逻辑（回收站 / 核心 / 吊笼 / 落石闸 / 路点由代码在原点生成）。重复跑会从 L_LightingBase 重来。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/canyon/ue_build_canyon.py" -AllowCommandletRendering -RenderOffscreen -unattended"""
import math
import random

import unreal

SRC_MAP = '/Game/Maps/L_LightingBase'
DST_MAP = '/Game/Maps/L_Canyon'
C = '/Game/Env/Canyon'
ENV = '/Game/Environments/AndreyPestryakov/Environment_Pack_01'
eas = unreal.EditorAssetLibrary
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rng = random.Random(20261029)
log = lambda *a: unreal.log('[canyon] ' + ' '.join(str(x) for x in a))
stats = {}

unreal.EditorLoadingAndSavingUtils.load_map(SRC_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RobotSlice.MechGameMode'))
for a in sub.get_all_level_actors():
    if isinstance(a, unreal.PlayerStart):
        sub.destroy_actor(a)


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def spawn_sm(label, mesh, loc=(0, 0, 0), yaw=0.0, scale=(1, 1, 1), folder='Canyon', collision='NoCollision', visible=True,
             material=None, tags=None, comp_tags=None, shadow=True, pitch=0.0, roll=0.0):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, V(*loc), unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.tags = ['Canyon'] + (tags or [])
    c = a.static_mesh_component
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    c.set_static_mesh(mesh if not isinstance(mesh, str) else eas.load_asset(mesh))
    if material:
        for i in range(c.get_num_materials()):
            c.set_material(i, material)
    c.set_collision_profile_name(collision)
    if not visible:
        c.set_visibility(False)
        a.set_actor_hidden_in_game(True)
    c.set_cast_shadow(shadow)
    if comp_tags:
        c.set_editor_property('component_tags', comp_tags)
    a.set_actor_scale3d(V(*scale))
    stats[folder] = stats.get(folder, 0) + 1
    return a


# ---------------------------------------------------------------- 地形 / 碰撞 / 远景 / 水
for i in range(8):
    spawn_sm(f'Canyon_Tile{i}', f'{C}/Meshes/SM_Canyon_Tile{i}', folder='Canyon/Terrain')
    spawn_sm(f'Canyon_Col{i}', f'{C}/Meshes/SM_Canyon_Col{i}', folder='Canyon/Collision', collision='BlockAll', visible=False, shadow=False)
spawn_sm('Canyon_Far', f'{C}/Meshes/SM_Canyon_Far', folder='Canyon/Terrain', collision='BlockAll')
spawn_sm('Canyon_Water', f'{C}/Meshes/SM_Canyon_Water', folder='Canyon/Water', shadow=False)
spawn_sm('Canyon_WaterCol', f'{C}/Meshes/SM_Canyon_WaterCol', folder='Canyon/Water', collision='BlockAll', visible=False, shadow=False,
         comp_tags=['Water'])


def trace(x0, y0, z0, x1, y1, z1):
    r = unreal.SystemLibrary.line_trace_single(world, V(x0, y0, z0), V(x1, y1, z1), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
                                               unreal.DrawDebugTrace.NONE, True)
    if isinstance(r, tuple):
        r = r[1] if r[0] else None
    if r is None:
        return None
    t = r.to_tuple()
    return t[4] if t[0] else None


def ground(x, y, ztop=6000.0):
    p = trace(x, y, ztop, x, y, -500)
    return None if p is None else p.z


# 坐标方向自检（北崖顶 22 米 / 南崖顶 26 米，岩洞在北）
gn, gs = ground(0, 3300), ground(0, -3300)
gc = ground(0, 3200, 550)
log(f'方向自检：北 (0,3300) 顶 {gn}，南 (0,-3300) 顶 {gs}，岩洞内 (0,3200) 地面 {gc}')

# ---------------------------------------------------------------- 石头
PROPS = f'{C}/Props'
BIG = [eas.load_asset(f'{PROPS}/SM_StonesBig_{i}') for i in range(1, 6)]
MID = [eas.load_asset(f'{PROPS}/SM_StonesMiddle_{i:02d}') for i in range(1, 17)]
MID = [m for m in MID if m]


def bounds(mesh):
    b = mesh.get_bounding_box()
    return b.min, b.max


def place_stone(label, mesh, cx, cy, gz, size, yaw, folder, collision='BlockAll', sink=0.1, shadow=True):
    """size = (长, 宽, 高) 厘米：把石头的包围盒缩放到这个尺寸（绕 Z 转 yaw 前），底面埋进地面 sink 比例"""
    lo, hi = bounds(mesh)
    bx, by, bz = max(1.0, hi.x - lo.x), max(1.0, hi.y - lo.y), max(1.0, hi.z - lo.z)
    s = (size[0] / bx, size[1] / by, size[2] / bz)
    # 包围盒中心 → (cx, cy)，底 → gz - sink*高
    ox, oy = (lo.x + hi.x) * 0.5 * s[0], (lo.y + hi.y) * 0.5 * s[1]
    r = math.radians(yaw)
    dx = ox * math.cos(r) - oy * math.sin(r); dy = ox * math.sin(r) + oy * math.cos(r)
    z = gz - sink * size[2] - lo.z * s[2]
    return spawn_sm(label, mesh, (cx - dx, cy - dy, z), yaw, s, folder, collision=collision, shadow=shadow)


# 灰盒掩体（同位置同尺寸，换成真石头；有碰撞）
cover = 0
for S in (-1, 1):
    for (px, py) in [(2200, 300), (4800, -1500), (5200, 1550), (7000, 500), (9600, -1400)]:
        place_stone(f'Cover_Big_{S * px}_{py}', rng.choice(BIG), S * px, py, 0.0, (520, 520, 600), rng.choice([0, 90, 180, 270]) + rng.uniform(-8, 8),
                    'Canyon/Cover', sink=0.06); cover += 1
    for (px, py) in [(1500, -300), (3800, 600), (6400, -1500), (8000, -400), (10500, 900)]:
        place_stone(f'Cover_Small_{S * px}_{py}', rng.choice(BIG + MID[:6]), S * px, py, 0.0, (310, 310, 250), rng.choice([0, 90, 180, 270]) + rng.uniform(-8, 8),
                    'Canyon/Cover', sink=0.06); cover += 1
    for X in (4800, 6800, 9500):
        for side in (-1, 1):
            place_stone(f'Cover_Ledge_{S * X}_{side}', rng.choice(MID), S * X, side * 2380, 900.0, (300, 250, 175), rng.choice([0, 180]) + rng.uniform(-5, 5),
                        'Canyon/Cover', sink=0.08); cover += 1
    place_stone(f'Cover_Cave_{S * 2500}', rng.choice(MID), S * 2500, 3550, 0.0, (400, 250, 250), rng.choice([0, 180]), 'Canyon/Cover', sink=0.05); cover += 1
    place_stone(f'Cover_Cave_{S * 6000}', rng.choice(MID), S * 6000, 2850, 0.0, (400, 250, 250), rng.choice([0, 180]), 'Canyon/Cover', sink=0.05); cover += 1
    gz = ground(S * 5000, 3400) or 2200.0
    place_stone(f'Cover_TopN_{S * 5000}', rng.choice(BIG), S * 5000, 3400, gz, (520, 420, 320), rng.choice([0, 180]), 'Canyon/Cover', sink=0.08); cover += 1
    gz = ground(S * 6500, -3400) or 2600.0
    place_stone(f'Cover_TopS_{S * 6500}', rng.choice(BIG), S * 6500, -3400, gz, (520, 420, 320), rng.choice([0, 180]), 'Canyon/Cover', sink=0.08); cover += 1
log('掩体石头', cover)


# ---------------------------------------------------------------- 散布
def in_box(x, y, x0, x1, y0, y1):
    return x0 <= x <= x1 and y0 <= y <= y1


def keep_out_floor(x, y, r):
    """谷底散布要躲开的地方（r 是物件半径）"""
    ax = abs(x)
    if ax < 900 + r and abs(y) < 1900:
        return True                                   # 铸造台、石拱
    if 1150 - r < ax < 3850 + r and abs(y) > 1100 - r:
        return True                                   # 坡道（含坡脚）
    if y > 0 and (ax < 700 + r or 7450 - r < ax < 8550 + r):
        return True                                   # 北侧洞口
    if y < 0 and 6800 - r < ax < 8000 + r:
        return True                                   # 吊笼
    if math.hypot(ax - 8800, y - 1000) < 750 + r:
        return True                                   # 回收站 A
    if 11150 - r < ax < 12850 + r and abs(y) < 900 + r:
        return True                                   # 出生点
    return False


def wall_y(x, side, z, y_from, y_to):
    p = trace(x, y_from * side, z, x, y_to * side, z)
    return None if p is None else abs(p.y)


# 谷底墙脚碎石（有碰撞，往谷里最多伸 1.6 米）
n = 0
x = -12950.0
while x < 12950:
    x += rng.uniform(160, 520)
    for side in (-1, 1):
        if rng.random() < 0.25:
            continue
        sz = rng.uniform(70, 150)
        if keep_out_floor(x, side * 1700, sz * 0.6):
            continue
        wy = wall_y(x, side, 40.0, 1300, 2800)
        if wy is None or wy < 1780:
            continue
        cy = side * (wy - sz * 0.3)
        if abs(cy) - sz * 0.5 < 1620:
            cy = side * (1620 + sz * 0.5)
        place_stone(f'Talus_{n}', rng.choice(MID), x, cy, 0.0, (sz * rng.uniform(0.9, 1.4), sz, sz * rng.uniform(0.5, 0.85)), rng.uniform(0, 360),
                    'Canyon/Scatter/Talus', sink=0.15)
        n += 1
# 两头端墙下的乱石（出生点 15x15 米以外）
for S in (-1, 1):
    for k in range(10):
        y = rng.uniform(-1650, 1650)
        sz = rng.uniform(120, 260)
        wx = trace(S * 12000, y, 60, S * 13800, y, 60)
        if wx is None:
            continue
        cx = S * (abs(wx.x) - sz * 0.3)
        if abs(cx) - sz * 0.6 < 12850:
            continue
        place_stone(f'EndRubble_{S}_{k}', rng.choice(BIG + MID), cx, y, 0.0, (sz, sz * rng.uniform(0.8, 1.2), sz * 0.7), rng.uniform(0, 360),
                    'Canyon/Scatter/Talus', sink=0.15)
        n += 1
log('墙脚碎石', n)

# 栈道内侧墙脚碎石（贴着上层崖壁，不进 6 米走道）
n = 0
for side in (-1, 1):
    x = -11900.0
    while x < 11900:
        x += rng.uniform(250, 700)
        ax = abs(x)
        if ax < 800 or abs(ax - 3700) < 550 or (side < 0 and abs(ax - 8200) < 850) or (side < 0 and abs(ax - 7400) < 700):
            continue
        if any(abs(ax - X) < 450 for X in (4800, 6800, 9500)):
            continue
        sz = rng.uniform(55, 120)
        wy = wall_y(x, side, 980.0, 2100, 3400)
        if wy is None:
            continue
        cy = side * max(wy - sz * 0.35, 2420 + sz * 0.5)
        place_stone(f'LedgeRubble_{n}', rng.choice(MID), x, cy, 900.0, (sz * 1.2, sz, sz * 0.6), rng.uniform(0, 360), 'Canyon/Scatter/Ledge', sink=0.15)
        n += 1
log('栈道碎石', n)

# 岩洞墙脚小碎石（无碰撞，不挡路）
n = 0
for k in range(70):
    x = rng.uniform(-8900, 8900)
    if abs(abs(x) - 4500) < 400 or abs(x) < 650 or 7450 < abs(x) < 8550:
        continue
    y = rng.choice([2745, 3655]) + rng.uniform(-20, 20)
    sz = rng.uniform(30, 75)
    place_stone(f'CavePebble_{n}', rng.choice(MID), x, y, 0.0, (sz * 1.3, sz, sz * 0.6), rng.uniform(0, 360), 'Canyon/Scatter/Cave',
                collision='NoCollision', sink=0.2, shadow=False)
    n += 1
log('岩洞碎石', n)


# 植被
def load_all(folder, prefix):
    out = []
    for p in eas.list_assets(folder, recursive=False, include_folder=False):
        nm = p.split('/')[-1].split('.')[0]
        if nm.startswith(prefix) and 'FoliageType' not in nm:
            out.append(eas.load_asset(p))
    return out


DRY = load_all(f'{ENV}/Plants_and_Grass/Plants/Meshes', 'SM_Plants_BigDry')
GRASS = load_all(f'{ENV}/Plants_and_Grass/Grass/Meshes', 'SM_Grass_Small') + load_all(f'{ENV}/Plants_and_Grass/Grass/Meshes', 'SM_Grass_Middle')
SMALLP = load_all(f'{ENV}/Plants_and_Grass/Plants_Small/Meshes', 'SM_Plants_Small')
PINE_DRY = load_all(f'{ENV}/Trees_and_Bushes/Pines/Meshes', 'SM_PineDryV1Middle') + load_all(f'{ENV}/Trees_and_Bushes/Pines/Meshes', 'SM_PineDryV2Big')
PINE = load_all(f'{ENV}/Trees_and_Bushes/Pines/Meshes', 'SM_PineV1Middle') + load_all(f'{ENV}/Trees_and_Bushes/Pines/Meshes', 'SM_PineV2Big')
BUSH = load_all(f'{ENV}/Trees_and_Bushes/Deciduous/Meshes', 'SM_Bush')
log('植被素材', len(DRY), len(GRASS), len(SMALLP), len(PINE_DRY), len(PINE), len(BUSH))


def plant(label, mesh, x, y, gz, height, folder, collision='NoCollision'):
    lo, hi = bounds(mesh)
    s = height / max(1.0, hi.z - lo.z)
    s *= rng.uniform(0.85, 1.15)
    return spawn_sm(label, mesh, (x, y, gz - lo.z * s - 5), rng.uniform(0, 360), (s, s, s), folder, collision=collision)


n = 0
# 崖顶（内台地）：石头 + 枯灌木 + 少量松
for k in range(900):
    x = rng.uniform(-12700, 12700)
    side = rng.choice([-1, 1])
    y = side * rng.uniform(2700, 4450)
    if math.hypot(abs(x) - (5000 if side > 0 else 6500), abs(y) - 3400) < 450:
        continue
    gz = ground(x, y)
    if gz is None or gz < 1800:
        continue
    r = rng.random()
    if r < 0.30 and MID:
        sz = rng.uniform(80, 260)
        place_stone(f'TopRock_{n}', rng.choice(MID + BIG), x, y, gz, (sz * rng.uniform(1, 1.5), sz, sz * rng.uniform(0.4, 0.8)), rng.uniform(0, 360),
                    'Canyon/Scatter/Top', sink=0.2)
    elif r < 0.62 and DRY:
        plant(f'TopDry_{n}', rng.choice(DRY), x, y, gz, rng.uniform(70, 150), 'Canyon/Scatter/Top')
    elif r < 0.85 and GRASS:
        plant(f'TopGrass_{n}', rng.choice(GRASS), x, y, gz, rng.uniform(40, 80), 'Canyon/Scatter/Top')
    elif r < 0.93 and PINE:
        plant(f'TopPine_{n}', rng.choice(PINE), x, y, gz, rng.uniform(450, 800), 'Canyon/Scatter/Top')
    elif PINE_DRY:
        plant(f'TopPineDry_{n}', rng.choice(PINE_DRY), x, y, gz, rng.uniform(400, 700), 'Canyon/Scatter/Top')
    n += 1
    if n > 420:
        break
log('崖顶散布', n)

# 外圈高原：稀疏松树和石头（远看有东西）
n = 0
for k in range(700):
    x = rng.uniform(-30000, 30000); y = rng.choice([-1, 1]) * rng.uniform(5400, 26000)
    if rng.random() < 0.3:
        x = rng.choice([-1, 1]) * rng.uniform(13900, 30000); y = rng.uniform(-26000, 26000)
    gz = ground(x, y, 30000)
    if gz is None:
        continue
    if rng.random() < 0.55 and (PINE or PINE_DRY):
        plant(f'FarPine_{n}', rng.choice(PINE + PINE_DRY), x, y, gz, rng.uniform(450, 900), 'Canyon/Scatter/Far')
    else:
        sz = rng.uniform(150, 500)
        place_stone(f'FarRock_{n}', rng.choice(BIG), x, y, gz, (sz * 1.3, sz, sz * 0.6), rng.uniform(0, 360), 'Canyon/Scatter/Far',
                    collision='NoCollision', sink=0.25)
    n += 1
    if n > 260:
        break
log('外圈散布', n)

# 谷底：河岸和墙脚的枯草 / 小灌木（无碰撞），栈道外侧少量
n = 0
for k in range(500):
    x = rng.uniform(-12800, 12800)
    band = rng.random()
    if band < 0.45:
        y = rng.choice([-1, 1]) * rng.uniform(560, 900)
    elif band < 0.85:
        y = rng.choice([-1, 1]) * rng.uniform(1450, 1760)
    else:
        y = rng.choice([-1, 1]) * rng.uniform(2300, 2480)
    if abs(y) < 1800 and keep_out_floor(x, y, 120):
        continue
    if abs(y) > 2000 and (abs(x) < 800 or abs(abs(x) - 3700) < 500 or (y < 0 and abs(abs(x) - 8200) < 800)):
        continue
    gz = ground(x, y, 1500)
    if gz is None:
        continue
    m = rng.random()
    if m < 0.45 and GRASS:
        plant(f'FloorGrass_{n}', rng.choice(GRASS), x, y, gz, rng.uniform(35, 70), 'Canyon/Scatter/Floor')
    elif m < 0.8 and DRY:
        plant(f'FloorDry_{n}', rng.choice(DRY), x, y, gz, rng.uniform(60, 120), 'Canyon/Scatter/Floor')
    elif SMALLP:
        plant(f'FloorSmall_{n}', rng.choice(SMALLP), x, y, gz, rng.uniform(30, 60), 'Canyon/Scatter/Floor')
    n += 1
    if n > 260:
        break
log('谷底植被', n)

# ---------------------------------------------------------------- 出生点、截图机位
for (x, yaw, nm) in ((-12000, 0.0, 'West'), (12000, 180.0, 'East')):
    ps = sub.spawn_actor_from_class(unreal.PlayerStart, V(x, 0, 120), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    ps.set_actor_label(f'PlayerStart_{nm}')
    ps.set_folder_path('Canyon')
cam = sub.spawn_actor_from_class(unreal.CameraActor, V(0, -6000, 3000), unreal.Rotator(pitch=-20, yaw=90, roll=0))
cam.set_actor_label('ShotCam')
cam.set_folder_path('Canyon/Debug')
cam.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)

# ---------------------------------------------------------------- 碰撞对规格（UE 里实测，射线打碰撞）
checks = []
def chk(what, x, y, ztop, expect, tol):
    g = ground(x, y, ztop)
    checks.append((what, x, y, expect, g, g is not None and abs(g - expect) <= tol))
for S in (-1, 1):
    for X in (1000, 2700, 4200, 6000, 8800, 10500, 12000):
        chk('谷底中线附近', S * X, -700, 3000, 0.0, 20.0)
        chk('谷底北侧', S * X, 1100, 3000, 0.0, 20.0)
    for X in (4300, 5800, 7600, 9000, 11000):
        chk('南栈道', S * X, -2150, 2000, 900.0, 20.0)
        chk('北栈道', S * X, 2150, 2000, 900.0, 20.0)
    for X in (1600, 2500, 3400):
        chk('坡道', S * X, 1500, 2000, 900.0 * (X - 1300) / 2400, 20.0)
        chk('坡道', S * X, -1500, 2000, 900.0 * (X - 1300) / 2400, 20.0)
    for X in (1500, 3500, 5500, 7000, 8600):
        chk('岩洞', S * X, 3200, 550, 0.0, 20.0)
    chk('回收站A', S * 8800, 1000, 3000, 0.0, 10.0)
    chk('回收站B', S * 8200, -2150, 2000, 900.0, 10.0)
    chk('出生点', S * 12000, 0, 3000, 0.0, 10.0)
    chk('浅河(碰撞盒顶 3 厘米)', S * 3000, 0, 3000, 3.0, 5.0)
for Y in (-1500, -900, 900, 1500):
    chk('石拱桥面', 0, Y, 3000, 900.0, 20.0)
bad = [c for c in checks if not c[5]]
log(f'碰撞对规格：{len(checks)} 处，超差 {len(bad)}')
for c in bad:
    log('   超差', c)

unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log('分组数量', stats)
log('已保存', DST_MAP)
