"""L_Volcano：沿夺芯峡谷的崖面摆火山包的 5 块扫描石头（Nanite、无碰撞），盖住地形垂直面的拉伸贴图、打散笔直的崖边。
石头只摆在灰盒碰撞体后面（包围盒前沿 ≥ 崖面、顶 ≤ 可走的面），不会伸进能走的地方。可重复跑（先删 VolcanoRocks 文件夹里的）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_volcano_rocks.py" -AllowCommandletRendering -RenderOffscreen -unattended
"""
import math
import random

import unreal

MAP = '/Game/Maps/L_Volcano'
FOLDER = 'VolcanoRocks'
P = lambda *a: unreal.log('[rocks] ' + ' '.join(str(x) for x in a))
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
old = [a for a in eas.get_all_level_actors() if str(a.get_folder_path()) == FOLDER]
for a in old:
    eas.destroy_actor(a)
P('删旧石头', len(old))

meshes = []
for i in range(1, 6):
    sm = unreal.load_asset(f'/Game/Env/Volcano/Assets/Rock_{i}/Model/Rock_{i}')
    b = sm.get_bounding_box()
    meshes.append((sm, (b.min.x, b.min.y, b.min.z), (b.max.x, b.max.y, b.max.z)))
rng = random.Random(11)


def aabb(m, yaw_k, s):
    """yaw = k·90° 时的包围盒（厘米，相对原点）"""
    (x0, y0, z0), (x1, y1, z1) = m[1], m[2]
    if yaw_k % 2:
        x0, y0, x1, y1 = -y1, x0, -y0, x1   # 转 90°：x' = -y, y' = x
    if yaw_k in (2, 3):
        x0, y0, x1, y1 = -x1, -y1, -x0, -y0
    return x0 * s, y0 * s, z0 * s, x1 * s, y1 * s, z1 * s


placed = 0


def put(m, k, s, ox, oy, oz):
    global placed
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(ox, oy, oz), unreal.Rotator(roll=0, pitch=0, yaw=k * 90))
    a.set_actor_scale3d(unreal.Vector(s, s, s))
    c = a.static_mesh_component
    c.set_static_mesh(m[0])
    c.set_collision_profile_name('NoCollision')
    c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
    a.set_folder_path(FOLDER)
    placed += 1


def wall(x0, x1, side, y_front, z0, z_top, depth, skip=(), scale=(1.0, 2.5), axis='y'):
    """沿 X 从 x0 到 x1（米）摆一排：side=+1 往 +Y 是崖里面；前沿贴 y_front，底在 z0，顶不超过 z_top。axis='x' 时换成端墙（沿 Y 摆，side 是 X 方向）"""
    x = x0
    while x < x1:
        if any(a <= x <= b for a, b in skip):
            x += 2.0
            continue
        m = rng.choice(meshes)
        k = rng.randrange(4)
        s = rng.uniform(*scale)
        bx0, by0, bz0, bx1, by1, bz1 = aabb(m, k, s)
        # 高度不够就缩
        h = (bz1 - bz0) / 100
        if h > (z_top - z0):
            s *= (z_top - z0) / h * 0.98
            bx0, by0, bz0, bx1, by1, bz1 = aabb(m, k, s)
        w = (bx1 - bx0) / 100 if axis == 'y' else (by1 - by0) / 100
        d = (by1 - by0) / 100 if axis == 'y' else (bx1 - bx0) / 100
        if d > depth * 1.6:   # 太厚的换一块
            x += 1.0
            continue
        hit = [b for a, b in skip if x < b and x + w > a]
        if hit:   # 会盖住洞口：跳到洞口后面
            x = max(hit) + 0.5
            continue
        sink = rng.uniform(0.0, 0.5)   # 前沿贴着碰撞面往里 0~0.5 米（地形台阶面往里斜了 2.5~3 米，石头填这个楔形）
        zbase = z0 * 100 - bz0 - rng.uniform(0, 0.8) * 100
        if axis == 'y':
            cx = x * 100 + w * 50 - (bx0 + bx1) / 2
            cy = (y_front + sink) * 100 - by0 if side > 0 else (y_front - sink) * 100 - by1
            put(m, k, s, cx, cy, zbase)
        else:
            cy = x * 100 + w * 50 - (by0 + by1) / 2
            cx = (y_front + sink) * 100 - bx0 if side > 0 else (y_front - sink) * 100 - bx1
            put(m, k, s, cx, cy, zbase)
        x += w * rng.uniform(0.55, 0.85)


DOORS = [(-85, -75), (-6, 6), (75, 85)]
for side in (1, -1):
    # ① 谷底到栈道的崖面（|y| = 18，0–9 米）；北侧让开三个岩洞口
    wall(-120, 118, side, 18.3 * side, 0.0, 8.8, 4.5 if side > 0 else 6.0, skip=DOORS if side > 0 else (), scale=(0.9, 1.6))   # 北侧别伸进岩洞（y 27 起）
    # ② 栈道到崖顶（|y| = 25，9 米到 22 / 26 米）
    wall(-120, 118, side, 25.3 * side, 7.5, (21.8 if side > 0 else 25.8), 18.0, scale=(1.4, 2.6))
    # ③ 外沿（|y| = 46 往外，灰盒外面，打散笔直的崖顶边）
    wall(-150, 148, side, 46.5 * side, (19.0 if side > 0 else 23.0), 60.0, 30.0, scale=(1.8, 3.2))
for side in (1, -1):
    # ④ 两头端墙（x = ±130，谷底宽 36 米）
    wall(-18, 16, side, 130.3 * side, 0.0, 40.0, 20.0, scale=(1.6, 2.8), axis='x')
P('摆了', placed, '块')
P('存地图', unreal.EditorLoadingAndSavingUtils.save_current_level())
