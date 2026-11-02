"""K059 城市街区死斗图的布局（纯 Python，不依赖 Blender / UE）：写 Inbox/_k059/plan.json + 平面草图 plan_2d.png。
坐标用 Blender 的：米，X 向东、Y 向北、Z 向上；UE 里换成 (x, -y, z) 厘米、yaw 取反（ue_build_city.py 里换）。
素材：K059 城市街道（KB3D City Streets：街边小品组 Vig<n>、单件道具 Prop<名>）+ K005（KB3D Every City 楼房 C<n>，
原包是缩小的模型，一层约 1.35 米，导出时放大 2.4 倍到一层约 3.2 米）+ 程序方块（路面、人行道、在建框架楼、天桥、高架、转播塔）。
「正面」约定：K059 小品组带人行道的，马路一侧在本地 -Y；K005 联排小楼门口台阶也在 -Y。yaw=0 即正面朝南。
py -3 -I tools/k059_city_plan.py
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'Inbox', '_k059')
S005 = 2.4                      # K005 楼房放大倍数
AX, AY = 105.0, 90.0           # 活动范围半宽（东西 210 米、南北 180 米）
CEIL = 46.0                    # 头顶隐形板高度（米）
SIDEWALK = 0.15                # 人行道 / 街区地面高出路面

surv5 = json.load(open(os.path.join(OUT, 'survey_K005.json'), encoding='utf-8'))
SIZE5 = {int(c['name'].split()[1]): [v * S005 for v in c['size']] for c in surv5['collections']
         if c['name'].startswith('Collection ') and len(c['name'].split()) == 2 and c['min']}
surv9 = json.load(open(os.path.join(OUT, 'survey.json'), encoding='utf-8'))

items = []


def box(name, mat, x0, x1, y0, y1, z0, z1, col=True, yaw=0.0):
    """轴对齐方块（min/max，米）；yaw 绕中心转"""
    items.append({'kind': 'box', 'name': name, 'mat': mat, 'c': [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2],
                  's': [x1 - x0, y1 - y0, z1 - z0], 'yaw': yaw, 'col': col})


def bldg(cid, x, y, yaw=0.0, z=SIDEWALK, tag=''):
    """K005 楼：(x, y) = 底面中心"""
    items.append({'kind': 'bldg', 'id': cid, 'x': x, 'y': y, 'z': z, 'yaw': yaw, 'size': SIZE5[cid], 'tag': tag})


def bldg_box(cid, x0, y0, yaw=0.0, z=SIDEWALK):
    """按西南角放 K005 楼（只用于 yaw 0 / 90 / 180 / 270）"""
    sx, sy = SIZE5[cid][:2]
    if int(round(yaw)) % 180:
        sx, sy = sy, sx
    bldg(cid, x0 + sx / 2, y0 + sy / 2, yaw, z)
    return x0 + sx, y0 + sy


def vig(n, x, y, yaw=0.0, z=0.0):
    """K059 小品组（保留原包原点：带人行道的组原点在人行道中间；人行道板导出时去掉，顶面 0.15 由地面方块给）"""
    items.append({'kind': 'vig', 'id': n, 'x': x, 'y': y, 'z': z, 'yaw': yaw})


def prop(name, x, y, yaw=0.0, z=SIDEWALK, scale=1.0):
    """K059 单件道具（底面中心）或别的现成网格（name 带 / 的是 UE 资源路径）"""
    items.append({'kind': 'prop', 'id': name, 'x': x, 'y': y, 'z': z, 'yaw': yaw, 'scale': scale})


def spawn(tag, x, y, yaw, z=SIDEWALK):
    items.append({'kind': 'spawn', 'tag': tag, 'x': x, 'y': y, 'z': z, 'yaw': yaw})


def sign(img, x, y, z, w, h, yaw, frame=True):
    """广告牌 / 屏幕：img 是 SourceAssets/city_signs 里的图名；面朝本地 -Y（yaw 同上）"""
    items.append({'kind': 'sign', 'img': img, 'x': x, 'y': y, 'z': z, 'w': w, 'h': h, 'yaw': yaw, 'frame': frame})


# ================================================================ 地面
# 整片路面（沥青），往外铺到 260 米，远处高楼站在上面
box('Ground', 'asphalt', -260, 260, -260, 260, -1.0, 0.0)
# 再往外铺到 3 公里（不然 520 米外就是大气的「地面」，地平线一圈蓝色），低一点免得和近处重叠闪烁
for k, (x0, x1, y0, y1) in enumerate(((-3000, 3000, 260, 3000), (-3000, 3000, -3000, -260), (-3000, -260, -260, 260), (260, 3000, -260, 260))):
    box(f'Ground_Far{k}', 'concrete_dark', x0, x1, y0, y1, -1.0, -0.02)
# 街：东西大道（路面 Y ±10，两侧 6 米人行道）、西街 X -52..-38、东街 X 38..52（上面是高架）
AVE = (-10.0, 10.0)
WST = (-52.0, -38.0)
EST = (38.0, 52.0)
SW_W = 6.0
# 街区（含人行道）：按街的边缘切，往外延伸到 150 米
BX = [(-150.0, WST[0]), (WST[1], EST[0]), (EST[1], 150.0)]
BY = [(-150.0, AVE[0]), (AVE[1], 150.0)]
INNER = {  # 每个街区内部地面材质
    ('W', 'N'): 'concrete', ('M', 'N'): 'gravel', ('E', 'N'): 'concrete',
    ('W', 'S'): 'concrete', ('M', 'S'): 'plaza', ('E', 'S'): 'concrete',
}
for ix, (x0, x1) in enumerate(BX):
    for iy, (y0, y1) in enumerate(BY):
        key = ('WME'[ix], 'SN'[iy])
        # 人行道条：挨着街的边
        sw = []
        if ix > 0:
            sw.append((x0, x0 + SW_W, y0, y1))
        if ix < 2:
            sw.append((x1 - SW_W, x1, y0, y1))
        if iy == 0:
            sw.append((x0, x1, y1 - SW_W, y1))
        else:
            sw.append((x0, x1, y0, y0 + SW_W))
        for k, (a, b, c, d) in enumerate(sw):
            box(f'Sidewalk_{key[0]}{key[1]}{k}', 'sidewalk', a, b, c, d, -0.3, SIDEWALK)
        ix0 = x0 + (SW_W if ix > 0 else 0)
        ix1 = x1 - (SW_W if ix < 2 else 0)
        iy0 = y0 + (SW_W if iy == 1 else 0)
        iy1 = y1 - (SW_W if iy == 0 else 0)
        # 人行道条在角上互相重叠一点不要紧（同材质同高度）；内部地面
        box(f'Lot_{key[0]}{key[1]}', INNER[key], ix0, ix1, iy0, iy1, -0.3, SIDEWALK - 0.005)

# 路面标线（白色，不带碰撞）
def dashes(name, x0, x1, y, along='x', w=0.15, dash=3.0, gap=6.0):
    t = x0
    k = 0
    while t < x1:
        e = min(t + dash, x1)
        if along == 'x':
            box(f'{name}_{k}', 'paint', t, e, y - w / 2, y + w / 2, 0.0, 0.012, col=False)
        else:
            box(f'{name}_{k}', 'paint', y - w / 2, y + w / 2, t, e, 0.0, 0.012, col=False)
        t += dash + gap
        k += 1


for seg in ((-200, WST[0] - 4), (WST[1] + 4, EST[0] - 4), (EST[1] + 4, 200)):
    box(f'AveCenterA_{seg[0]}', 'paint_y', seg[0], seg[1], -0.25, -0.1, 0.0, 0.012, col=False)
    box(f'AveCenterB_{seg[0]}', 'paint_y', seg[0], seg[1], 0.1, 0.25, 0.0, 0.012, col=False)
    dashes(f'AveLaneN_{seg[0]}', seg[0], seg[1], 5.0)
    dashes(f'AveLaneS_{seg[0]}', seg[0], seg[1], -5.0)
for name, (a, b) in (('W', WST), ('E', EST)):
    c = (a + b) / 2
    for seg in ((-200, AVE[0] - 4), (AVE[1] + 4, 200)):
        dashes(f'St{name}Center_{seg[0]}', seg[0], seg[1], c, along='y')
# 斑马线：大道过两条街的路口两侧、街过大道两侧
def zebra(name, cx, cy, length, along):
    for k in range(int(length / 1.2)):
        t = -length / 2 + k * 1.2 + 0.3
        if along == 'x':   # 条纹沿 Y 方向排开，人沿 X 走
            box(f'{name}_{k}', 'paint', cx - 1.5, cx + 1.5, cy + t, cy + t + 0.6, 0.0, 0.012, col=False)
        else:
            box(f'{name}_{k}', 'paint', cx + t, cx + t + 0.6, cy - 1.5, cy + 1.5, 0.0, 0.012, col=False)


for nm, (a, b) in (('W', WST), ('E', EST)):
    zebra(f'ZebraAve{nm}0', a - 2.0, 0.0, 19.0, 'x')
    zebra(f'ZebraAve{nm}1', b + 2.0, 0.0, 19.0, 'x')
    zebra(f'ZebraSt{nm}0', (a + b) / 2, AVE[1] + 2.0, 13.0, 'y')
    zebra(f'ZebraSt{nm}1', (a + b) / 2, AVE[0] - 2.0, 13.0, 'y')

# ================================================================ 西北：联排小楼（12–17 米屋顶连成一片）+ 后巷 + 两栋 20 米楼 + 北边老旅馆
x = -114.0
for cid in (21, 20, 19, 25, 24, 22, 23, 19):
    sx, sy, sz = SIZE5[cid]
    bldg(cid, x + sx / 2, 22.0 + sy / 2, 0.0)            # 门口台阶朝南（大道）
    x += sx
ROW_END = x
# 联排东头到人行道之间留 6 米的巷口
box('Alley_NW_mark', 'concrete_dark', ROW_END, -64.0, 22.0, 41.0, -0.3, SIDEWALK + 0.005)
bldg_box(6, -110.0, 50.0)       # 20 米（一半在场外）
bldg_box(1, -107.0, 76.0)       # 39 米老旅馆，南墙就是场地北边
bldg_box(5, -64.0 - SIZE5[5][1], 52.0, 90.0)  # 消防站 20 米：门朝东（西街）
bldg_box(23, -82.0 - SIZE5[23][1], 52.0, 90.0)  # 消防站西边一栋小楼
# 后巷杂物
vig(20, -96.0, 44.5, 180.0, SIDEWALK)   # 垃圾箱、木箱
vig(43, -72.0, 44.0, 90.0, SIDEWALK)
prop('Dumpster_A', -80.0, 46.5, 0.0)
prop('Dumpster_B', -104.0, 73.0, 0.0)

# ================================================================ 西南：大道边两栋 + 停车场（盖布车）+ 南边低楼
bldg_box(34, -104.0, -22.0 - SIZE5[34][1])      # 29.5 米，屋顶看得到大道
bldg_box(9, -64.0 - SIZE5[9][1], -46.0, 90.0)    # 22 米窄楼，转 90 度贴西街
bldg_box(7, -86.0, -90.0 - 6.0)                  # 南墙外沿
bldg_box(6, -110.0, -100.0)
# 停车场：盖布车两排
for k, (cx, cy, yw) in enumerate(((-98, -48, 90), (-92, -48, 95), (-86, -48, 88), (-98, -60, 90), (-90, -61, 82),
                                  (-80, -60, 92), (-74, -56, 0), (-100, -72, 90))):
    prop('/Game/Env/PolyHaven/covered_car/covered_car_2k/covered_car', cx, cy, yw)
vig(6, -78.0, -74.0, 0.0, SIDEWALK)     # 工地办公集装箱 + 围栏
vig(18, -96.0, -27.0, 0.0)              # 大道边：电线杆、垃圾箱

# ================================================================ 中北：工地 + 两层在建框架楼（7 / 14 米）
FX0, FX1, FY0, FY1 = -22.0, 14.0, 34.0, 66.0
cols_x = [FX0, FX0 + 12, FX0 + 24, FX1]
cols_y = [FY0, FY0 + 10.67, FY0 + 21.33, FY1]
for i, cx in enumerate(cols_x):
    for j, cy in enumerate(cols_y):
        box(f'Frame_Col_{i}{j}', 'concrete', cx - 0.6, cx + 0.6, cy - 0.6, cy + 0.6, SIDEWALK, 14.0 if cx < 6 else 7.0)
# 一层楼板（顶 7 米），中间开个 8×8 的洞（上下层通）
box('Frame_L1_a', 'concrete', FX0 - 0.6, FX1 + 0.6, FY0 - 0.6, FY0 + 12.0, 6.4, 7.0)
box('Frame_L1_b', 'concrete', FX0 - 0.6, FX1 + 0.6, FY0 + 20.0, FY1 + 0.6, 6.4, 7.0)
box('Frame_L1_c', 'concrete', FX0 - 0.6, -8.0, FY0 + 12.0, FY0 + 20.0, 6.4, 7.0)
box('Frame_L1_d', 'concrete', 0.0, FX1 + 0.6, FY0 + 12.0, FY0 + 20.0, 6.4, 7.0)
# 二层楼板（顶 14 米）只盖西半边，东半边露出一层屋面
box('Frame_L2', 'concrete', FX0 - 0.6, 2.6, FY0 - 0.6, FY1 + 0.6, 13.4, 14.0)
# 矮墙 / 剪力墙：一层北边一段、二层西边护栏
box('Frame_Wall_N', 'concrete_dark', FX0 - 0.6, -6.0, FY1 - 0.2, FY1 + 0.4, SIDEWALK, 6.4)
box('Frame_Wall_core', 'concrete_dark', -4.0, -2.0, FY0 + 6.0, FY0 + 14.0, SIDEWALK, 6.4)
box('Frame_Parapet_L2W', 'concrete', FX0 - 0.6, FX0 + 0.2, FY0 - 0.6, FY1 + 0.6, 14.0, 15.0)
box('Frame_Parapet_L1E', 'concrete', FX1 - 0.2, FX1 + 0.6, FY0 + 8, FY1 - 8, 7.0, 8.0)
# 集装箱垛（2.6 米一层）
def container(name, cx, cy, yaw, z=SIDEWALK, mat='container'):
    L, W, H = 12.2, 2.45, 2.6
    items.append({'kind': 'box', 'name': name, 'mat': mat, 'c': [cx, cy, z + H / 2], 's': [L, W, H], 'yaw': yaw, 'col': True})


container('Cont_NM_1', 20.0, 76.0, 90)
container('Cont_NM_2', 23.0, 76.0, 90)
container('Cont_NM_3', 21.5, 76.0, 90, SIDEWALK + 2.6, 'container_b')
container('Cont_NM_4', -14.0, 26.5, 0)
container('Cont_NM_5', 8.0, 74.0, 15)
vig(8, -10.0, 79.0, 0.0, SIDEWALK)      # 施工坑 + 水泥搅拌
vig(4, 18.0, 28.5, 180.0, SIDEWALK)     # 工地集装箱办公室 + 水马
vig(14, 22.0, 52.0, 90.0, SIDEWALK)     # 脚手架 + 砖垛
vig(5, -26.0 + 2.0, 45.0, 90.0, SIDEWALK)
prop('Scaffolding_A', 15.5, 40.0, 0.0)
prop('Scaffolding_B', 15.5, 42.4, 0.0)
prop('CableRoll_A', 5.0, 30.0, 0.0)
prop('ConcreteMixer_', -20.0, 70.0, 30.0)
prop('Palete_A', 10.0, 70.0, 10.0)
prop('Palete_A', 11.5, 69.0, 40.0)
prop('Trailer_B', -24.0, 84.0, 90.0)
# 工地围挡：沿人行道内边，留两个口
for k, (x0, x1) in enumerate(((-26.0, -16.0), (-6.0, 6.0), (14.0, 26.0))):
    nseg = int((x1 - x0) / 3.0)
    for s in range(nseg):
        prop('Fence_A', x0 + 1.5 + s * 3.0, 22.6, 0.0)

# ================================================================ 中南：广场（地铁站亭 7 米屋顶、报亭、公交站、花坛）
box('Pavilion', 'steel', -18.0, 2.0, -32.0, -24.0, SIDEWALK, 7.0)
box('Pavilion_Roof', 'concrete', -18.6, 2.6, -32.6, -23.4, 7.0, 7.4)
box('Pavilion_Glass', 'glass', -17.6, 1.6, -23.95, -23.85, 0.5, 6.6, col=False)
for k, (x0, y0, x1, y1) in enumerate(((-26, -50, -14, -46), (8, -50, 20, -46), (-26, -66, -18, -60), (12, -66, 20, -60),
                                      (-6, -58, 2, -54))):
    box(f'Planter_{k}', 'concrete', x0, x1, y0, y1, SIDEWALK, 1.3)
for k, (tx, ty) in enumerate(((-22, -48), (-17, -48), (11, -48), (16, -48), (-22, -63), (16, -63), (-2, -56))):
    prop('Tree_A', tx, ty, k * 37.0, 1.3)
box('Vent_1', 'concrete_dark', -14.0, -8.0, -70.0, -66.0, SIDEWALK, 4.2)   # 地铁通风井（4 米高掩体）
box('Vent_1_Grille', 'steel_dark', -13.6, -8.4, -69.6, -66.4, 4.2, 4.5)
box('Vent_2', 'concrete_dark', 22.0, 26.0, -58.0, -52.0, SIDEWALK, 4.2)
box('Vent_2_Grille', 'steel_dark', 22.4, 25.6, -57.6, -52.4, 4.2, 4.5)
vig(34, 14.0, -36.0, 0.0, SIDEWALK)     # 椭圆报亭
vig(38, -24.0, -38.0, 90.0, SIDEWALK)   # 报刊亭
vig(17, 0.0, -40.0, 0.0)                # 信息亭 + 长椅 + 灌木（带人行道原点）
vig(25, 22.0, -19.0, 180.0)             # 公交站（北边人行道，面朝大道）
vig(10, -12.0, 19.0, 0.0)               # 大道北侧公交站
bldg(18, 0.0, -73.0 - SIZE5[18][1] / 2, 0.0)   # 南边 70 米阶梯塔，北墙在 -73
bldg(17, -36.0 - 8.0, -110.0, 0.0)
bldg(13, 28.0, -100.0, 0.0)

# ================================================================ 东北：老旅馆 35 米大屋顶（远端高点）
bldg_box(3, 68.0, 28.0)
# 旅馆 35 米大屋顶太空（狙击天堂）→ 摆几个楼梯间 / 机房 / 水箱座当掩体
RT = SIDEWALK + SIZE5[3][2]
for k, (cx, cy, sx, sy, h) in enumerate(((78, 40, 6, 4, 3.5), (92, 50, 4, 7, 4.0), (104, 64, 6, 5, 3.0), (82, 66, 5, 5, 4.5),
                                         (104, 36, 4, 4, 3.0))):
    box(f'Roof_C3_{k}', 'concrete_dark', cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, RT - 0.3, RT + h)
container('Cont_NE_1', 74.0, 82.0, 0)
container('Cont_NE_2', 74.0, 79.0, 0, SIDEWALK, 'container_b')
container('Cont_NE_3', 74.0, 80.5, 0, SIDEWALK + 2.6)
vig(33, 98.0, 83.0, 180.0, SIDEWALK)
bldg_box(8, 88.0, 76.0, 90.0)

# ================================================================ 东南：65 米写字楼（爬不上去）+ 装卸场
bldg_box(2, 66.0, -68.0)
for k, (cx, cy, yw) in enumerate(((70, -80, 0), (86, -82, 0), (100, -76, 90))):
    container(f'Cont_SE_{k}', cx, cy, yw)
container('Cont_SE_top', 86.0, -82.0, 0, SIDEWALK + 2.6, 'container_b')
vig(3, 98.0, -24.0, 0.0, SIDEWALK)      # 警用拖车 + 路障
prop('Trailer_C', 62.5, -84.0, 90.0)

# ================================================================ 大道：中间转播塔（挡住两头出生点的对视）+ 车和水马
TX0, TX1 = 6.0, 18.0
for k, (cx, cy) in enumerate(((TX0, -3.5), (TX0, 3.5), (TX1, -3.5), (TX1, 3.5))):
    box(f'Tower_Leg_{k}', 'steel', cx - 0.5, cx + 0.5, cy - 0.5, cy + 0.5, 0.0, 12.0)
box('Tower_Deck', 'steel_dark', TX0 - 0.8, TX1 + 0.8, -4.3, 4.3, 11.4, 12.0)
box('Tower_Base', 'concrete', TX0 - 1.5, TX1 + 1.5, -5.0, 5.0, 0.0, 1.0)
TXC = (TX0 + TX1) / 2
box('Tower_ScreenBack', 'steel_dark', TXC - 0.4, TXC + 0.4, -3.6, 3.6, 4.0, 11.0)
sign('odds_board', TXC - 0.45, 0.0, 4.4, 7.0, 4.2, -90.0, frame=False)     # 朝西（蓝队出生点方向）
sign('odds_board', TXC + 0.45, 0.0, 4.4, 7.0, 4.2, 90.0, frame=False)      # 朝东
box('Tower_Rail_N', 'steel', TX0 - 0.8, TX1 + 0.8, 4.2, 4.3, 12.0, 13.0, col=True)
box('Tower_Rail_S', 'steel', TX0 - 0.8, TX1 + 0.8, -4.3, -4.2, 12.0, 13.0, col=True)
# 盖布车 / 水马：碎掉大道的长视线
for k, (cx, cy, yw) in enumerate(((-72, 5.5, 8), (-60, -4.0, -12), (-28, 6.0, 175), (-20, -6, 4), (30, 4.5, 180),
                                  (64, -5.5, 10), (78, 6.0, 172))):
    prop('/Game/Env/PolyHaven/covered_car/covered_car_2k/covered_car', cx, cy, yw, 0.0)
for k, (cx, cy, yw) in enumerate(((-84, -2, 90), (-84, 1.2, 90), (-34, 0, 0), (84, -1.5, 90), (84, 1.7, 90))):
    prop('Barier_B', cx, cy, yw, 0.0)
container('Cont_Ave_W', -66.0, -2.5, 8, 0.0)
for k, (cx, cy, yw) in enumerate(((-48.0, 52.0, 95), (-42.0, 30.0, 80), (-47.0, -36.0, 100), (-43.0, -60.0, 85))):
    prop('/Game/Env/PolyHaven/covered_car/covered_car_2k/covered_car', cx, cy, yw, 0.0)
container('Cont_StW', -43.0, 64.0, 88, 0.0, 'container_b')
container('Cont_StW2', -47.0, -52.0, 92, 0.0)
container('Cont_Ave_E', 70.0, 3.0, -6, 0.0, 'container_b')

# ================================================================ 天桥：工地一层楼板（7 米）跨过大道到广场站亭屋顶
BX0, BX1 = -14.0, -6.0
box('Bridge_Deck', 'steel_dark', BX0, BX1, -24.0, FY0 + 2.0, 6.3, 7.0)
box('Bridge_Floor', 'concrete', BX0 + 0.1, BX1 - 0.1, -24.0, FY0 + 2.0, 7.0, 7.08)
box('Bridge_Rail_W', 'steel', BX0, BX0 + 0.15, -23.0, FY0, 7.0, 8.2)
box('Bridge_Rail_E', 'steel', BX1 - 0.15, BX1, -23.0, FY0, 7.0, 8.2)
for k, cy in enumerate((-13.0, 13.0)):
    for cx in (BX0 + 0.8, BX1 - 0.8):
        box(f'Bridge_Col_{k}_{int(cx)}', 'steel', cx - 0.4, cx + 0.4, cy - 0.4, cy + 0.4, 0.0, 6.3)

# ================================================================ 高架：东街上空，桥面顶 11 米，宽 12 米，延伸到场外
VX0, VX1, VZ = 39.0, 51.0, 11.0
box('Viaduct_Deck', 'concrete', VX0, VX1, -200.0, 200.0, VZ - 1.8, VZ)
box('Viaduct_Par_W', 'concrete', VX0, VX0 + 0.4, -200.0, 200.0, VZ, VZ + 1.1)
box('Viaduct_Par_E', 'concrete', VX1 - 0.4, VX1, -200.0, 200.0, VZ, VZ + 1.1)
box('Viaduct_Rail_W', 'steel', VX0 + 3.8, VX0 + 4.0, -200.0, 200.0, VZ, VZ + 0.25, col=False)
box('Viaduct_Rail_E', 'steel', VX1 - 4.0, VX1 - 3.8, -200.0, 200.0, VZ, VZ + 0.25, col=False)
for k, py in enumerate((-180, -132, -84, -60, -36, -16, 16, 36, 60, 84, 132, 180)):
    box(f'Viaduct_Pier_{k}', 'concrete', 43.6, 46.4, py - 1.4, py + 1.4, 0.0, VZ - 1.8)
    box(f'Viaduct_Cap_{k}', 'concrete', VX0 + 1.0, VX1 - 1.0, py - 1.2, py + 1.2, VZ - 2.6, VZ - 1.8)
# 高架上的两节停运列车（掩体，3.6 米高，可上车顶）
box('Train_1', 'train', VX0 + 4.5, VX0 + 8.0, -42.0, -2.0, VZ, VZ + 3.6)
box('Train_2', 'train', VX1 - 8.0, VX1 - 4.5, 24.0, 64.0, VZ, VZ + 3.6)

# ================================================================ 街边小品：沿人行道
# 大道北侧人行道（Y 10..16），正面朝南 → yaw 0
for k, (n, cx) in enumerate(((21, -96.0), (27, -76.0), (23, 46.0 - 20), (30, 64.0), (22, 86.0))):
    vig(n, cx, 13.0, 0.0)
# 大道南侧人行道（Y -16..-10），正面朝北 → yaw 180
for k, (n, cx) in enumerate(((24, -92.0), (19, -74.0), (31, -24.0), (26, 34.0 - 10), (28, 66.0), (32, 92.0))):
    vig(n, cx, -13.0, 180.0)
# 西街两侧（X -58..-52 / -38..-32）
for k, (n, cy) in enumerate(((29, 60.0), (15, 30.0), (11, -30.0), (12, -64.0))):
    vig(n, -55.0, cy, 90.0)             # 西侧人行道，正面朝东
for k, (n, cy) in enumerate(((16 if False else 21, 40.0), (18, 70.0), (27, -40.0), (23, -70.0))):
    vig(n, -35.0, cy, -90.0)            # 东侧人行道，正面朝西
# 东街两侧（高架下）
for k, (n, cy) in enumerate(((22, 46.0), (25, -46.0), (20, 74.0))):
    vig(n, 35.0, cy, 90.0)
for k, (n, cy) in enumerate(((24, 30.0), (19, -30.0), (26, -66.0))):
    vig(n, 55.0, cy, -90.0)
# 红绿灯、路灯
for k, (cx, cy, yw) in enumerate(((-59.0, 17.0, 0), (-31.0, -17.0, 180), (31.0, 17.0, 0), (59.0, -17.0, 180))):
    prop('TrafficLights_', cx, cy, yw)
for k in range(-4, 5):
    if abs(k * 24.0 - 12) > 8:
        prop('Lamp_D', k * 24.0 - 12, 15.2, 180.0)
        prop('Lamp_D', k * 24.0 + 0, -15.2, 0.0)

# ================================================================ 边界：街口路障（场外一侧）+ 告示
for k, (cx, cy, yw) in enumerate(((-108.0, -4.0, 90), (-108.0, 4.0, 90), (108.0, -4.0, -90), (108.0, 4.0, -90),
                                  (-45.0, 93.0, 0), (45.0, 93.0, 0), (-45.0, -93.0, 180), (45.0, -93.0, 180))):
    vig(7, cx, cy, yw, 0.0)             # 警用岗亭 + 道闸 + ROAD CLOSED
for k, (cx, cy, yw) in enumerate(((-106.0, -8.0, 90), (106.0, 8.0, -90), (-49.0, 91.0, 0), (49.0, -91.0, 180))):
    vig(37, cx, cy, yw, 0.0)            # 警用拖车（灯塔）
sign('evac_notice', -90.0, -21.9, 2.4, 4.0, 2.4, 180.0)   # 西南楼北墙（对着大道）：疏散通告
sign('evac_notice', 66.0, -25.9, 2.4, 4.0, 2.4, 180.0)    # 东南写字楼北墙
sign('event_ad', -87.0, 36.0, 15.0, 14.0, 7.0, 0.0)       # 联排屋顶：赛事广告（朝南，大道上看得到）
sign('sponsor_ad', 89.0, 27.9, 22.0, 18.0, 9.0, 0.0)      # 东北旅馆南墙：赞助商

# ================================================================ 远景高楼（场外一圈）
for cid, x, y, yaw in ((26, -150, 150, 0), (27, -60, 175, 0), (31, 30, 185, 0), (33, 120, 160, 90), (30, 185, 70, 0),
                       (36, 175, -40, 0), (28, 150, -150, 90), (35, 60, -175, 0), (29, -40, -180, 0), (32, -140, -150, 0),
                       (14, -185, -60, 0), (15, -180, 40, 0), (16, -125, 115, 0), (11, 115, 115, 0), (17, 120, -115, 0),
                       (10, 0, 150, 0)):
    bldg(cid, x, y, yaw, 0.0, 'far')

# 再外一圈：300–900 米的楼群（同几栋楼转着摆，远看是连成片的城市），没有碰撞
import random
rng = random.Random(59)
FAR_IDS = [26, 27, 28, 29, 30, 31, 32, 33, 35, 36, 14, 15, 16, 13, 11, 17, 2, 18, 10]
for ring, (r0, r1, n) in enumerate(((290, 420, 26), (450, 650, 30), (700, 950, 30))):
    for k in range(n):
        ang = (k + rng.random() * 0.6) / n * 2 * math.pi
        r = rng.uniform(r0, r1)
        bldg(rng.choice(FAR_IDS), r * math.cos(ang), r * math.sin(ang), rng.choice((0, 90, 180, 270)) + rng.uniform(-8, 8), 0.0, 'far')

# ================================================================ 出生点
# 团队淘汰：两队各一组（大道两头），GameMode 按 Team0 / Team1 标签取中心和朝向
for i, dy in enumerate((-6.0, -2.0, 2.0, 6.0)):
    spawn('Team0', -95.0, dy, 0.0, 0.0)
    spawn('Team1', 95.0, dy, 180.0, 0.0)
# 死斗：12 个分散点（DM），朝场地中心
for x, y in ((-92, 46), (-70, -64), (-45, 80), (-45, -80), (2, 86), (0, -68), (45, 80), (45, -80), (95, 18), (-95, -16),
             (84, 82), (78, -73)):
    spawn('DM', float(x), float(y), math.degrees(math.atan2(-y, -x)), SIDEWALK)   # 出生点 yaw = 面朝方向（逆时针角），朝场地中心

# 自检：出生点 4 米内不能有楼 / 带碰撞的高于 0.5 米的方块
def inside(px, py, cx, cy, sx, sy, yaw, pad):
    c, s_ = math.cos(math.radians(-yaw)), math.sin(math.radians(-yaw))
    dx, dy = px - cx, py - cy
    lx, ly = dx * c - dy * s_, dx * s_ + dy * c
    return abs(lx) < sx / 2 + pad and abs(ly) < sy / 2 + pad


for sp in (i for i in items if i['kind'] == 'spawn'):
    for it in items:
        if it['kind'] == 'bldg' and inside(sp['x'], sp['y'], it['x'], it['y'], it['size'][0], it['size'][1], it['yaw'], 2.0):
            print('[plan] 出生点在楼里', sp['tag'], sp['x'], sp['y'], 'C%d' % it['id'])
        if it['kind'] == 'box' and it['col'] and it['c'][2] + it['s'][2] / 2 > 0.5 and it['c'][2] - it['s'][2] / 2 < 3.0 and                 inside(sp['x'], sp['y'], it['c'][0], it['c'][1], it['s'][0], it['s'][1], it['yaw'], 2.0):
            print('[plan] 出生点挨着方块', sp['tag'], sp['x'], sp['y'], it['name'])

plan = {'arena': {'center': [0.0, 0.0, 0.0], 'half': [AX, AY], 'ceiling': CEIL, 'sidewalk': SIDEWALK, 'k005_scale': S005},
        'items': items}
json.dump(plan, open(os.path.join(OUT, 'plan.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
from collections import Counter
print('[plan]', Counter(i['kind'] for i in items), '→', os.path.join(OUT, 'plan.json'))

# ---------------------------------------------------------------- 平面草图（每米 4 像素，北在上）
try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit
PX, R = 4, 160
img = Image.new('RGB', (2 * R * PX, 2 * R * PX), (40, 42, 46))
dr = ImageDraw.Draw(img)


def P(x, y):
    return ((x + R) * PX, (R - y) * PX)


def rect(cx, cy, sx, sy, yaw, fill, outline=None):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    pts = []
    for dx, dy in ((-sx / 2, -sy / 2), (sx / 2, -sy / 2), (sx / 2, sy / 2), (-sx / 2, sy / 2)):
        pts.append(P(cx + dx * c - dy * s, cy + dx * s + dy * c))
    dr.polygon(pts, fill=fill, outline=outline)


COL = {'asphalt': (58, 60, 64), 'sidewalk': (120, 120, 118), 'concrete': (100, 100, 104), 'gravel': (110, 100, 86),
       'plaza': (130, 128, 122), 'paint': (230, 230, 230), 'paint_y': (220, 190, 60), 'concrete_dark': (80, 80, 84),
       'container': (60, 110, 160), 'container_b': (170, 90, 50), 'steel': (150, 160, 170), 'steel_dark': (70, 76, 84),
       'glass': (90, 140, 170), 'train': (190, 200, 210)}
for it in sorted((i for i in items if i['kind'] == 'box'), key=lambda i: i['c'][2] + i['s'][2] / 2):
    rect(it['c'][0], it['c'][1], it['s'][0], it['s'][1], it['yaw'], COL.get(it['mat'], (200, 0, 200)))
for it in items:
    if it['kind'] == 'bldg':
        sx, sy, sz = it['size']
        g = int(min(255, 60 + sz * 2.5))
        rect(it['x'], it['y'], sx, sy, it['yaw'], (g, g - 10, g - 30) if it['tag'] != 'far' else (70, 70, 80), (0, 0, 0))
        dr.text(P(it['x'] - 3, it['y'] + 2), f"C{it['id']}\n{sz:.0f}m", fill=(255, 255, 255))
    elif it['kind'] in ('vig', 'prop'):
        dr.ellipse([P(it['x'] - 1.2, it['y'] + 1.2), P(it['x'] + 1.2, it['y'] - 1.2)], fill=(230, 120, 40))
    elif it['kind'] == 'spawn':
        col = {'Team0': (60, 160, 255), 'Team1': (255, 70, 60), 'DM': (255, 255, 80)}[it['tag']]
        dr.ellipse([P(it['x'] - 2, it['y'] + 2), P(it['x'] + 2, it['y'] - 2)], fill=col)
    elif it['kind'] == 'sign':
        rect(it['x'], it['y'], it['w'], 0.6, it['yaw'], (255, 0, 255))
dr.rectangle([P(-AX, AY), P(AX, -AY)], outline=(255, 60, 60), width=3)
img.save(os.path.join(OUT, 'plan_2d.png'))
print('[plan] 草图', os.path.join(OUT, 'plan_2d.png'))
