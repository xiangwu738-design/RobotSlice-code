"""苏联板楼图 L_Soviet 的布局表（改地图就改这里）：纯 Python 算坐标 → Inbox/_k62/plan.json + 地面遮罩 T_SV_GroundMask.png + 俯视布局图 + 自检。
py -3 -I tools/soviet_plan.py
坐标：Blender 的（米，x 向东，y 向北）；UE 里 (x, -y, z)，yaw 取反。场地中心对称（(x,y) → (-x,-y)、朝向 +180°），两队公平。
第一版定位：枪械主场。两条东西向长街（300 米）拉视线，三排楼夹着院子，南北向两条横街和楼间缝隙绕行；中间院子低矮、掩体密，近身也能打。
"""
import json
import math
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HALF_X, HALF_Y, CEIL = 150.0, 110.0, 62.0
HEIGHTS = 'D:/RobotSlice/Inbox/_k62/export/heights'
OUT = 'D:/RobotSlice/Inbox/_k62'
META = json.load(open('D:/RobotSlice/Inbox/_k62/export/buildings.json', encoding='utf-8'))['buildings']
PROPS = json.load(open('D:/RobotSlice/Inbox/_soviet_props/export/meshes.json', encoding='utf-8'))['props']
items = []


def bldg(i, x, y, yaw=0.0, sym=True, far=False, note=''):
    items.append({'kind': 'bldg', 'id': i, 'x': x, 'y': y, 'yaw': yaw, 'far': far, 'note': note})
    if sym:
        items.append({'kind': 'bldg', 'id': i, 'x': -x, 'y': -y, 'yaw': yaw + 180.0, 'far': far, 'note': note})


def prop(name, x, y, yaw=0.0, sym=True, collide=None, z=0.0, scale=1.0):
    """小品：SM_SVS_<name>（Sketchfab / Poly Haven）。collide=None 按尺寸判断（水平 ≥1 米且高 ≥0.9 米才挡机体）"""
    items.append({'kind': 'prop', 'id': name, 'x': x, 'y': y, 'z': z, 'yaw': yaw, 'collide': collide, 'scale': scale})
    if sym:
        items.append({'kind': 'prop', 'id': name, 'x': -x, 'y': -y, 'z': z, 'yaw': yaw + 180.0, 'collide': collide, 'scale': scale})


def wmesh(path, x, y, yaw=0.0, scale=1.0, sym=True, collide=False, z=0.0):
    """冬季包的网格（树、灌木、围栏、石头）"""
    items.append({'kind': 'winter', 'id': path, 'x': x, 'y': y, 'z': z, 'yaw': yaw, 'scale': scale, 'collide': collide})
    if sym:
        items.append({'kind': 'winter', 'id': path, 'x': -x, 'y': -y, 'z': z, 'yaw': yaw + 180.0, 'scale': scale, 'collide': collide})


# ---------------------------------------------------------------- 街道框架
# 长街：y = ±50（宽 24，路面 ±42..±58），全长 300 米；横街：x = ±50（宽 16）
PROSPEKT_Y, PROSPEKT_HALF = 50.0, 12.0
CROSS_X, CROSS_HALF = 50.0, 8.0
roads = [  # (x0, x1, y0, y1, 值) 遮罩矩形。值 0.56 左右 = 压实的雪、噪声决定哪里露沥青；1 = 全沥青（车辙）
    (-240, 240, 42, 58, 0.61), (-240, 240, -58, -42, 0.61),          # 长街路面（一直铺到场外，视线尽头还是路）
    (43, 57, -240, 240, 0.61), (-57, -43, -240, 240, 0.61),          # 横街
    (-240, 240, 99, 107, 0.48), (-240, 240, -107, -99, 0.48),        # 后巷：只露一点
    (-150, -112, -7, 7, 0.47), (112, 150, -7, 7, 0.47),              # 出生广场
    (-150, -128, -38, 38, 0.44), (128, 150, -38, 38, 0.44),
    (-43, 43, -7, 7, 0.46),                                           # 中院小路
    (-7, 7, -38, 38, 0.46),
]
# 车辙：弯弯曲曲的折线（不是全程笔直），淡一点；中间有断开
ruts = []
_r = random.Random(77)


def rut_line(fixed, along_x, off, amp, gap):
    pts = []
    t = -240.0
    ph1, ph2 = _r.uniform(0, 6.28), _r.uniform(0, 6.28)
    while t <= 240.0:
        w = fixed + off + amp * math.sin(t / 37.0 + ph1) + 0.45 * amp * math.sin(t / 11.0 + ph2)
        pts.append((t, w) if along_x else (w, t))
        t += 3.0
    ruts.append((pts, gap))


for c in (50.0, -50.0):
    # 一条路两道车，车辙不等间距（别像铁轨）
    for off, amp, gap in ((-5.0, 0.7, 40), (-3.1, 0.7, 40), (2.6, 0.9, 55), (4.6, 0.9, 55)):
        rut_line(c, True, off, amp, gap)
    for off, amp, gap in ((-3.4, 0.5, 45), (-1.5, 0.5, 45), (2.2, 0.6, 35), (4.0, 0.6, 35)):
        rut_line(c, False, off, amp, gap)

# ---------------------------------------------------------------- 北排（y 64..97）+ 中心对称的南排：中高楼，屋顶 13~47 米能上去
# 楼之间留 13 米以上的缝，连后巷和长街
bldg('MD_E', -128, 79, 0, note='北排西')
bldg('MD_I', -80, 76, 0, note='北排西二')
bldg('MD_B', 0, 82, 0, note='北排中：两座塔之间能穿过去（空中连桥）')
bldg('MD_J', 78, 82, 0, note='北排东二')
bldg('MD_E', 127, 79, 0, note='北排东（11-02 用户：住宅区不该有工业烟囱 → 带烟囱的 MD_C 换成和北排西一样的 MD_E）')
# ---------------------------------------------------------------- 中间三个大院（y -38..38）
# 西 / 东大院：出生广场。靠横街一侧竖一栋 MD_F（40×17×26，中间有个腾空的门洞），挡住两队出生点的对视
bldg('MD_F', -69, 0, 90, note='西大院东墙：门洞在 5~10 米高')
bldg('MD_D', -107, 22.5, 0, note='西大院北楼')
bldg('SM_G', -108, -23, 0, note='西大院南楼')
# 中院：四栋矮楼 + 中间一栋小楼，院子里是掩体，近身战场
bldg('SM_D', -22, 30, 0, note='中院北西')
bldg('SM_J', 26, 28, 0, note='中院北东')
bldg('SM_F', 0, 0, 0, sym=False, note='中院中心小楼（中心对称，只放一栋）')

# ---------------------------------------------------------------- 掩体：车、车库、公交站（长街上每 40~60 米一处能躲的）
GAR = 'Garages'
# 长街：北街路边，南街是对称的。车库排在路肩（比机体高）
prop(GAR, -96, 60.5, 90)
prop(GAR, 20, 41.0, 90)
prop(GAR, 96, 60.5, 90)
for x, c, yaw in ((-134, 'Zhiguli_Red', 90), (-128.5, 'Zhiguli_Gray', 90), (-123, 'Zhiguli_Blue', 90)):
    prop(c, x, 56.5, yaw)
for x, c, yaw in ((-70, 'CoveredCar', 90), (-62.5, 'Zhiguli_Green', 90)):
    prop(c, x, 43.5, yaw)
for x, c, yaw in ((-8, 'Zhiguli_Gray', 87), (-14.5, 'Zhiguli_Red', 93)):
    prop(c, x, 56.5, yaw)
for x, c, yaw in ((62, 'Niva', 90), (69, 'Zhiguli_Blue', 90)):
    prop(c, x, 43.5, yaw)
for x, c, yaw in ((128, 'CoveredCar', 90), (134, 'Zhiguli_Green', 92)):
    prop(c, x, 56.5, yaw)
# 横街上两辆斜停的车
prop('Zhiguli_Blue', 47, 20, 15)
prop('Zhiguli_Red', 53, -12, 170)
prop('CoveredCar', 54.5, 76, 5)
# 公交站（站台在长街北边 / 南边人行道）
prop('BusStop2', -40, 62, 270)
prop('BusStop1', 34, 62, 90)
prop('BusStop2', 88, 38.6, 90)
prop('BusStop1', -104, 38.8, 270)
# 路灯：沿长街每 36 米一根（不挡机体）。用 K62 自带的 Lamp_A 放大 1.4 倍（8.3 米高，双臂）；Sketchfab 路灯包放大到 9 米时灯头有 5 米长，比例不对
def kprop(name, x, y, yaw=0.0, sym=True, scale=1.0, collide=False):
    items.append({'kind': 'kprop', 'id': name, 'x': x, 'y': y, 'yaw': yaw, 'scale': scale, 'collide': collide})
    if sym:
        items.append({'kind': 'kprop', 'id': name, 'x': -x, 'y': -y, 'yaw': yaw + 180.0, 'scale': scale, 'collide': collide})


for i, x in enumerate(range(-144, 145, 36)):
    kprop('Lamp_A', x, 40.5, 0, scale=1.4)
    kprop('Lamp_A', x + 18, 59.5, 0, scale=1.4)
for y in (-20, 18):
    kprop('Lamp_A', 42.0, y, 90, scale=1.4)
    kprop('Lamp_A', 58.0, y + 12, 90, scale=1.4)
# 路灯的暖光（没有阴影的点光源，只照一小片雪地）
for i, x in enumerate(range(-144, 145, 36)):
    for lx, ly in ((x, 42.0), (x + 18, 58.0)):
        items.append({'kind': 'light', 'x': lx, 'y': ly, 'z': 7.5})
        items.append({'kind': 'light', 'x': -lx, 'y': -ly, 'z': 7.5})
# 电箱、燃气箱、垃圾箱贴着楼边，路边不空
for n, x, y, yaw in (('Dumpster_A', -92, 36, 0), ('Dumpster_A', -8, 22, 20), ('ElectricBox_A', -60.5, 22, 90), ('GasBox_A', -128, 64, 0), ('Dumpster_A', 42, 65, 180), ('ElectricBox_A', 8, 39, 0),
                     ('BasketballHoop_A', -112, 3, 90), ('Barrel_A', -96, 64.5, 0), ('Cage_A', 62, 66, 0)):
    kprop(n, x, y, yaw)

# 西大院（出生）：两排车库挡一下，免得一出生就被屋顶的枪手看全
prop(GAR, -134, 19, 90)
prop(GAR, -134, -19, 90)
prop(GAR, -94, 2, 0)
prop('Niva', -86, -12, 40)
# 中院：车库、游乐场、长椅、花坛
prop(GAR, -24, 9, 90)
prop(GAR, 25, -9, 90, sym=False)
prop(GAR, -25, -9, 90, sym=False)
prop('Rocket', -28, -2, 20, collide=False)
prop('Swings', 28, 3, 0, sym=False, collide=False)
prop('Slide', 20, 13, 90, sym=False, collide=False)
prop('Seesaw', -14, -13, 40, sym=False, collide=False)
prop('FlowerBed', -33, -18, 0, sym=False)
prop('FlowerBed', 33, 18, 0, sym=False)
prop('Bench', 15, 7, 0, collide=False)
prop('Bench', -20, 18, 90, collide=False)
prop('TrashCan', 13, 10, 0, collide=False)
prop('TrashCan', -17, 19, 0, collide=False)
prop('Hydrant', -41, 36, 0, collide=False)
prop('Hydrant', 41, 37, 0, collide=False)
# 倒地枯树
prop('DeadTrunk2', -117, 8, 35, collide=False)
prop('DeadTrunk', -100, -5, 80, collide=False)

# ---------------------------------------------------------------- 树（冬季包；不挡机体）
T = '/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Meshes/Trees/'
F = '/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Meshes/Props/'
random.seed(62)
# 长街中间没有隔离带，树种在路肩上（y≈39、61 之外一点）和楼前
for x, y, n, s in ((-146, 62, 'SM_Tree_A_02', 1.0), (-92, 37, 'SM_Tree_A_03', 1.0), (-40, 39, 'SM_Tree_A_04', 1.0), (36, 37.5, 'SM_Tree_A_02', 1.0),
                   (114, 37, 'SM_Tree_A_04', 1.0), (-82, 61.5, 'SM_Tree_A_05', 1.2), (-14, 62, 'SM_Tree_A_06', 1.1), (92, 62, 'SM_Tree_A_05', 1.2),
                   # 院子
                   (-138, 8, 'SM_Tree_C_03', 0.8), (-122, -8, 'SM_Tree_C_04', 0.7), (-82, -22, 'SM_Tree_C_06', 1.0), (-84, 24, 'SM_Tree_C_03', 0.7),
                   (-30, 19, 'SM_Tree_C_07', 0.7), (35, 8, 'SM_Tree_C_06', 1.0), (-8, 22, 'SM_Tree_C_06', 1.0),
                   # 后巷 / 楼缝
                   (-96, 92, 'SM_Tree_A_03', 1.0), (36, 92, 'SM_Tree_A_02', 1.0), (-37, 72, 'SM_Tree_C_04', 0.7), (92, 86, 'SM_Tree_B_06', 1.0),
                   (-148, 104, 'SM_Tree_B_03', 0.9), (0, 106, 'SM_Tree_B_06', 1.0)):
    wmesh(T + n, x, y, yaw=random.uniform(0, 360), scale=s)
for x, y, n, s in ((-48, 64, 'SM_Bush_A_03', 0.7), (-140, -30, 'SM_Bush_A_01', 1.0), (-14, 36, 'SM_Bush_A_01', 1.0), (86, 32, 'SM_Bush_A_02', 0.5), (-90, -32, 'SM_Bush_A_03', 0.6),
                   (-62, 96, 'SM_Bush_A_02', 0.5)):
    wmesh(T + n, x, y, yaw=random.uniform(0, 360), scale=s)
# 枯枝、木板（不挡）
D_ = [('SM_Debris_01', -112, 12), ('SM_Debris_03', -30, 44), ('SM_Debris_Small_03', 12, 30), ('SM_Debris_02', -128, 36), ('SM_Debris_01', -60, 92), ('SM_Debris_Small_01', -92, -28),
      ('SM_Plank_Group_B_01', -130, 12), ('SM_Plank_Group_C_02', -92, 20), ('SM_Stump_01', -84, 30), ('SM_Root_02', -28, 16)]
for n, x, y in D_:
    wmesh(F + n, x, y, yaw=random.uniform(0, 360))
# 院子和楼前的灌木丛（枯灌木顶上带雪，贴着楼脚摆，地面不至于空）
for x, y in ((-122, 41), (-99, 37.5), (-80, 39), (-60, 37), (-38, 21), (-5, 40), (8, 37.5), (40, 40), (62, 38), (78, 39), (92, 37), (-142, 63), (-104, 64), (-62, 63), (-31, 63), (31, 64), (62, 64), (98, 63),
             (-58, 22), (-58, -8), (-78, 5), (-100, 7), (-124, -10), (-142, -28), (-15, -24), (33, -23), (-34, -30), (-88, 100), (-40, 100), (30, 97), (94, 97), (-148, 86)):
    wmesh(T + random.choice(('SM_Bush_A_01', 'SM_Bush_A_01', 'SM_Bush_A_03', 'SM_Bush_A_02')), x, y, yaw=random.uniform(0, 360), scale=random.uniform(0.55, 0.9))
# 中院南北侧木栅栏（第一版只做视觉，不挡）
for i in range(5):
    wmesh(F + f'SM_Fence_B_0{1 + i % 5}', -41 + i * 3.05, 21.5, yaw=0, scale=1.0)
    wmesh(F + f'SM_Fence_C_0{1 + i % 4}', 33 + i * 2.0, -21, yaw=0, scale=1.0)


# ---------------------------------------------------------------- 远景楼：无碰撞。场外接着把街摆下去，再摆几圈
def ring_ok(x, y):
    """别挡长街和横街的视线尽头"""
    if abs(abs(y) - PROSPEKT_Y) < 16 or abs(abs(x) - CROSS_X) < 12:
        return False
    return True


LG = ['LG_A', 'LG_B', 'LG_C', 'LG_D', 'LG_F', 'LG_G', 'LG_I', 'LG_J']  # 11-02 去掉 LG_H（大烟囱）
# ① 紧贴边界：场地四周一圈（高楼当墙）
N_RING = [('LG_I', -88, 136, 0), ('LG_B', 16, 141, 0), ('LG_J', 106, 150, 90), ('LG_G', -142, 133, 90), ('LG_A', 180, 128, 180),
          ('LG_G', -185, 25, 90), ('LG_F', -190, -18, 90)]
for i, x, y, yaw in N_RING:
    bldg(i, x, y, yaw, far=True)
bldg('LG_C', -175, 80, 0, far=True)
# ② 长街延伸段：路两边继续排楼（场外 170~520 米），视线尽头是雾里的楼
rng = random.Random(622)
seq = ['MD_E', 'MD_J', 'MD_H', 'LG_B', 'MD_E', 'MD_A', 'MD_D', 'LG_G', 'MD_I', 'LG_I', 'MD_B', 'LG_I']  # 11-02 去掉带烟囱的 MD_C、LG_H
xc = 200.0
k = 0
while xc < 640:
    n = seq[k % len(seq)]
    k += 1
    w = META[n]['max'][0] - META[n]['min'][0]
    d = META[n]['max'][1] - META[n]['min'][1]
    bldg(n, xc, PROSPEKT_Y + PROSPEKT_HALF + 2 + d / 2, rng.choice((0, 180)), far=True)
    m = seq[(k + 5) % len(seq)]
    d2 = META[m]['max'][1] - META[m]['min'][1]
    bldg(m, xc + 12, PROSPEKT_Y - PROSPEKT_HALF - 2 - d2 / 2, rng.choice((0, 180)), far=True)
    xc += w + rng.uniform(12, 26)
# ③ 外面两圈（300~1000 米）：大楼转着摆
for radius, count, offs in ((330, 16, 0.0), (520, 22, 0.13), (780, 26, 0.31), (1050, 26, 0.05)):
    for i in range(count):
        a = 2 * math.pi * (i + offs) / count
        x, y = radius * math.cos(a) * 1.15, radius * math.sin(a) * 0.95
        x += rng.uniform(-25, 25)
        y += rng.uniform(-25, 25)
        if not ring_ok(x, y):
            continue
        # 11-02 用户：楼和街没有关系 → 外圈也按街道方向摆（0 / 90 / 180 / 270 度），不再随便转
        items.append({'kind': 'bldg', 'id': rng.choice(LG), 'x': x, 'y': y, 'yaw': rng.choice((0.0, 90.0, 180.0, 270.0)), 'far': True, 'very_far': radius > 500, 'note': ''})

# ---------------------------------------------------------------- 出生点
TEAM = {'Team0': (-135.0, 0.0, 0.0), 'Team1': (135.0, 0.0, 180.0)}
DM = [(-135, 30), (-120, 50), (-96, 102), (-40, 103), (-46, 28), (-50, -70), (-8, 44), (-30, 3), (-143, 92),
      (-82, 15), (-52, 72), (20, 103)]
DM = DM + [(-x, -y) for x, y in DM]
START = (-135.0, 0.0, 0.0)   # ?Range 用的第一个 PlayerStart


def wmat(x, y, yaw):
    return x, y, math.radians(yaw)


# ---------------------------------------------------------------- 栅格自检：1 米格，算场内的屋顶高度和「挡不挡」
H = 300
NX, NY = int(2 * H), int(2 * H)
RES = 1.0
top = np.zeros((NY, NX), np.float32)
block = np.zeros((NY, NX), bool)
_cache = {}


def load_b(i):
    if i not in _cache:
        _cache[i] = np.load(f'{HEIGHTS}/{i}_b.npz')
    return _cache[i]


def to_grid(x, y):
    return int((x + H) / RES), int((y + H) / RES)


def stamp(i, x, y, yaw):
    d = load_b(i)
    t = d['top']
    b = d['b1'] | d['b2']
    c = float(d['cell'])
    x0, y0 = float(d['x0']), float(d['y0'])
    ca, sa = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    ys, xs = np.nonzero(t > 0.2)
    lx = x0 + (xs + 0.5) * c
    ly = y0 + (ys + 0.5) * c
    gx = ((x + ca * lx - sa * ly + H) / RES).astype(int)
    gy = ((y + sa * lx + ca * ly + H) / RES).astype(int)
    ok = (gx >= 0) & (gx < NX) & (gy >= 0) & (gy < NY)
    np.maximum.at(top, (gy[ok], gx[ok]), t[ys[ok], xs[ok]])
    bb = b[ys[ok], xs[ok]]
    block[gy[ok][bb], gx[ok][bb]] = True


for it in items:
    if it['kind'] == 'bldg' and (abs(it['x']) < 290 and abs(it['y']) < 290):
        stamp(it['id'], it['x'], it['y'], it['yaw'])

# 屋顶实心当挡（空壳内部算不可进）：从场外把不挡的格子泛洪，到不了的算楼里
reach = np.zeros_like(block)
stack = [to_grid(-HALF_X + 1, 50)[::-1]]
gxmin, gymin = to_grid(-HALF_X, -HALF_Y)
gxmax, gymax = to_grid(HALF_X, HALF_Y)
while stack:
    j, i = stack.pop()
    if j < gymin or j >= gymax or i < gxmin or i >= gxmax or reach[j, i] or block[j, i]:
        continue
    reach[j, i] = True
    stack += [(j + 1, i), (j - 1, i), (j, i + 1), (j, i - 1)]

# 自检：出生点
issues = []


def clear_dist(x, y):
    gx, gy = to_grid(x, y)
    r = 25
    sub_b = block[gy - r:gy + r + 1, gx - r:gx + r + 1]
    ys, xs = np.nonzero(sub_b)
    if len(xs) == 0:
        return 99.0
    return float(np.min(np.hypot(xs - r, ys - r)))


def los(a, b, z=2.0):
    """两点视线（z 高度）是否被屋顶高于 z 的格子挡住，1 米一步"""
    n = int(math.hypot(b[0] - a[0], b[1] - a[1]))
    for k in range(1, n):
        t = k / n
        gx, gy = to_grid(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
        if top[gy, gx] > z + 0.5:
            return False
    return True


for nm, (x, y, _) in TEAM.items():
    print('[plan]', nm, '离最近的墙', round(clear_dist(x, y), 1), '米')
    for off in (-10.5, -3.5, 3.5, 10.5):
        cd = clear_dist(x, y + off)
        if cd < 4:
            issues.append(f'{nm} 排位 {off} 离墙 {cd:.1f}')
print('[plan] 两队出生点互相看得见（2 米高）：', los(TEAM['Team0'], TEAM['Team1']), '；20 米高：', los(TEAM['Team0'], TEAM['Team1'], 20))
for i, (x, y) in enumerate(DM):
    cd = clear_dist(x, y)
    gx, gy = to_grid(x, y)
    if cd < 4 or not reach[gy, gx]:
        issues.append(f'DM{i} ({x},{y}) 离墙 {cd:.1f} 可达 {reach[gy, gx]}')
for i in range(len(DM)):
    for k in range(i + 1, len(DM)):
        if math.hypot(DM[i][0] - DM[k][0], DM[i][1] - DM[k][1]) < 15:
            issues.append(f'DM{i} DM{k} 相距不足 15 米')
# 全场可达比例、视线统计
inside = np.zeros_like(block)
inside[gymin:gymax, gxmin:gxmax] = True
print('[plan] 场内面积', int(inside.sum()), '可达（地面）', int((reach & inside).sum()), f'{(reach & inside).sum() / inside.sum():.0%}')
# 通道宽度：可达格到墙的距离分布（只看 prospekts 外的窄缝）
from collections import Counter
print('[plan] 问题：', issues or '无')

# ---------------------------------------------------------------- 地面遮罩
MASK_HALF = 240.0
N = 2048
mask = Image.new('L', (N, N), 0)
dm = ImageDraw.Draw(mask)
for x0, x1, y0, y1, v in sorted(roads, key=lambda r: r[4]):
    px = lambda x: (x + MASK_HALF) / (2 * MASK_HALF) * N
    py = lambda y: (MASK_HALF - y) / (2 * MASK_HALF) * N
    dm.rectangle([px(x0), py(y1), px(x1), py(y0)], fill=int(v * 255))
mask = mask.filter(ImageFilter.GaussianBlur(5))
rut = Image.new('L', (N, N), 0)
dr_ = ImageDraw.Draw(rut)
for pts, gap in ruts:
    seg = []
    for k, (x, y) in enumerate(pts):
        # 每隔一段断开，雪把车辙盖住了
        if (k * 3) % (gap * 3) < gap * 3 * 0.72:
            seg.append(((x + MASK_HALF) / (2 * MASK_HALF) * N, (MASK_HALF - y) / (2 * MASK_HALF) * N))
        else:
            if len(seg) > 1:
                dr_.line(seg, fill=190, width=3)
            seg = []
    if len(seg) > 1:
        dr_.line(seg, fill=190, width=3)
rut = rut.filter(ImageFilter.GaussianBlur(1.1))
# 车辙在路上才有
road_only = np.asarray(mask, dtype=np.float32) / 255.0
rut_a = np.asarray(rut, dtype=np.float32) / 255.0
r_ch = np.maximum(road_only, rut_a * (road_only > 0.3))
# 露草通道：低频随机斑块（不进路面），中心对称
rs = np.random.RandomState(62)
g_ch = rs.rand(N // 32, N // 32).astype(np.float32)
g_ch = np.asarray(Image.fromarray((g_ch * 255).astype(np.uint8)).resize((N, N), Image.BICUBIC), dtype=np.float32) / 255.0
g_ch = 0.5 * (g_ch + g_ch[::-1, ::-1])
g_ch = np.clip((g_ch - 0.42) * 3.2, 0, 1) * np.clip(1.0 - road_only * 2.2, 0, 1)
Image.merge('RGB', (Image.fromarray((r_ch * 255).astype(np.uint8)), Image.fromarray((g_ch * 255).astype(np.uint8)), Image.new('L', (N, N), 0))).save(f'{OUT}/T_SV_GroundMask.png')

# ---------------------------------------------------------------- plan.json
json.dump({'items': items, 'arena': {'half': [HALF_X, HALF_Y], 'ceiling': CEIL, 'center': [0, 0, 1.0]},
           'team': TEAM, 'dm': DM, 'start': START, 'mask_half': MASK_HALF}, open(f'{OUT}/plan.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('[plan] 物件', Counter(i['kind'] for i in items), '场内楼', sum(1 for i in items if i['kind'] == 'bldg' and not i['far']), '远景楼', sum(1 for i in items if i['kind'] == 'bldg' and i['far']))

# ---------------------------------------------------------------- 俯视布局图（标注）：4 像素 = 1 米
SCALE = 4
PAD = 40
W = int((HALF_X * 2 + 2 * PAD) * SCALE)
Hh = int((HALF_Y * 2 + 2 * PAD) * SCALE)
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 26)
fontb = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 34)
fonts = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 20)


def P(x, y):
    return (x + HALF_X + PAD) * SCALE, (HALF_Y + PAD - y) * SCALE


g0 = int((-HALF_X - PAD + H) / RES)
g1 = int((HALF_X + PAD + H) / RES)
h0 = int((-HALF_Y - PAD + H) / RES)
h1 = int((HALF_Y + PAD + H) / RES)
sub = top[h0:h1, g0:g1][::-1]
rgb = np.zeros(sub.shape + (3,), np.uint8)
rgb[...] = (235, 238, 243)        # 雪地
mm = np.asarray(mask.resize((int(MASK_HALF * 2), int(MASK_HALF * 2))))
for j in range(sub.shape[0]):
    for i in range(sub.shape[1]):
        wx = i + (-HALF_X - PAD)
        wy = (HALF_Y + PAD) - j
        mj, mi = int(MASK_HALF - wy), int(wx + MASK_HALF)
        if 0 <= mj < mm.shape[0] and 0 <= mi < mm.shape[1]:
            v = mm[mj, mi]
            if v > 150:
                rgb[j, i] = (150, 155, 165) if v > 220 else (200, 204, 212)
h = sub
bld = h > 0.2
# 高度色带：矮 = 浅砖，高 = 深紫
lv = np.clip(h / 60.0, 0, 1)
rgb[bld, 0] = (225 - 150 * lv[bld]).astype(np.uint8)
rgb[bld, 1] = (180 - 150 * lv[bld]).astype(np.uint8)
rgb[bld, 2] = (140 - 40 * lv[bld]).astype(np.uint8)
img = Image.fromarray(rgb).resize((W, Hh), Image.NEAREST)
d = ImageDraw.Draw(img, 'RGBA')
d.rectangle([P(-HALF_X, HALF_Y), P(HALF_X, -HALF_Y)], outline=(220, 30, 30, 255), width=5)
for it in items:
    if it['kind'] == 'bldg' and not it['far']:
        tp = load_b(it['id'])
        d.text(P(it['x'] - 9, it['y'] + 3), f"{it['id']}\n{float(tp['maxz']):.0f}米", fill=(255, 255, 255, 255), font=fonts, stroke_width=2, stroke_fill=(0, 0, 0, 255))
    elif it['kind'] == 'prop':
        col = (40, 90, 200, 255) if it['id'].startswith(('Zhiguli', 'Niva', 'CoveredCar')) else (120, 70, 40, 255) if it['id'] == 'Garages' else (0, 150, 150, 255) if it['id'].startswith('BusStop') else None
        if col:
            m = PROPS['SM_SVS_' + it['id']]
            w2, l2 = (m['max'][0] - m['min'][0]) / 2, (m['max'][1] - m['min'][1]) / 2
            a = math.radians(it['yaw'])
            cs = [(sx * w2, sy * l2) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            d.polygon([P(it['x'] + math.cos(a) * cx - math.sin(a) * cy, it['y'] + math.sin(a) * cx + math.cos(a) * cy) for cx, cy in cs], fill=col)
    elif it['kind'] == 'winter' and '/Trees/' in it['id']:
        cx, cy = P(it['x'], it['y'])
        d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=(30, 120, 70, 230))
for k, (x, y) in enumerate(DM):
    cx, cy = P(x, y)
    d.ellipse([cx - 17, cy - 17, cx + 17, cy + 17], fill=(255, 215, 0, 255), outline=(0, 0, 0, 255), width=2)
    d.text((cx - 11, cy - 14), str(k + 1), fill=(0, 0, 0, 255), font=fonts)
for nm, (x, y, yaw) in TEAM.items():
    cx, cy = P(x, y)
    col = (50, 110, 255, 255) if nm == 'Team0' else (240, 60, 60, 255)
    d.ellipse([cx - 26, cy - 26, cx + 26, cy + 26], fill=col, outline=(255, 255, 255, 255), width=4)
    d.text((cx - 60, cy + 30), '蓝队出生' if nm == 'Team0' else '红队出生', fill=col, font=fontb, stroke_width=3, stroke_fill=(255, 255, 255, 255))


def arrow(pts, col, label, lx, ly):
    for a, b in zip(pts, pts[1:]):
        d.line([P(*a), P(*b)], fill=col, width=7)
    a, b = pts[-2], pts[-1]
    ang = math.atan2(-(b[1] - a[1]), b[0] - a[0])
    bx, by = P(*b)
    d.polygon([(bx, by), (bx - 28 * math.cos(ang - 0.4), by - 28 * math.sin(ang - 0.4)), (bx - 28 * math.cos(ang + 0.4), by - 28 * math.sin(ang + 0.4))], fill=col)
    d.text(P(lx, ly), label, fill=col, font=font, stroke_width=3, stroke_fill=(255, 255, 255, 255))


# 长街（橙）、绕行（绿）
LONG = (240, 120, 0, 210)
ROUTE = (0, 160, 70, 220)
arrow([(-148, 50), (148, 50)], LONG, '北长街：300 米直视线，车库 / 车 / 公交站当掩体', -60, 51)
arrow([(148, -50), (-148, -50)], LONG, '南长街（中心对称）', -30, -51)
arrow([(50, -108), (50, 108)], LONG, '横街 220 米', 52, 100)
arrow([(-50, 108), (-50, -108)], LONG, '横街', -66, -100)
ROUTES = [([(-135, 0), (-101, -2), (-82, -3), (-81, 30), (-58, 30), (-42, 15), (-14, 16)], '院子绕行', -92, 18),
          ([(-135, -8), (-128, -30), (-128, -48), (-100, -60), (-100, -105), (-60, -105), (30, -105), (50, -105), (50, -85)], '后巷绕后（长街打不到）', -96, -99),
          ([(-70, 30), (-58, 44), (-37, 62), (-37, 100)], '楼缝到后巷', -36, 88)]
for pts, label, lx, ly in ROUTES:
    arrow(pts, ROUTE, label, lx, ly)
    for a0, b0 in zip(pts, pts[1:]):
        if not los(a0, b0, 2.0):
            print('[plan] 路线被挡', label, a0, b0)
print('[plan] 视线（2 米高）：北长街全长', los((-148, 50), (148, 50)), '南长街', los((-148, -50), (148, -50)), '东横街', los((50, -108), (50, 108)), '西横街', los((-50, -108), (-50, 108)), '中院东西', los((-40, 0), (40, 0)), '后巷', los((-148, 104), (148, 104)))
d.text((10, 4), f'苏联板楼 L_Soviet 布局（第一版）  场地 300×220 米，限高 {CEIL:.0f} 米，中心对称；红框 = 隐形边界', fill=(10, 10, 10, 255), font=font)
d.text((10, 36), '颜色越深楼越高；黄点 = 死斗重生点；蓝块 = 车、棕块 = 车库、青块 = 公交站；绿点 = 树；橙线 = 长街视线；绿线 = 绕行路线', fill=(10, 10, 10, 255), font=fonts)
img.save(f'{OUT}/布局.png')
print('[plan] 写完', f'{OUT}/布局.png', img.size)
