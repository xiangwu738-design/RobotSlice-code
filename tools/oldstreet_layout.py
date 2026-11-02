"""老旧街道（L_OldStreet）布局表：纯 Python，Blender 导出和 UE 搭图都读它算出来的 Inbox/_oldstreet/layout.json。改地图就改这里。
py -3 -I tools/oldstreet_layout.py          写 layout.json、打印统计，并出一张平面草图 docs/老旧街道_20261102/平面布局.png（要 PIL）

坐标约定：
- 「原包坐标」(x, y, z)：老旧街道.blend 的世界坐标（米）。街沿 +y，横路沿 x，路面 z = ZR。
- 「地图坐标」(X, Y, Z)：UE 世界坐标（米，写进地图时乘 100）。X = S·(y−YC)，Y = S·(x−XC)，Z = S·(z−ZR)。
  街沿 UE +X（循环方向），右手边是 UE +Y，文字不镜像；原点 = 路口中心路面。
- 原包朝向 θ（从上往下看逆时针）对应 UE yaw = −θ。
循环：沿 UE X，周期 PERIOD = 100 米（原包 76.923 米），接缝面（下一阶段代码做穿越的面）X = ±50 米。
"""
import json
import math
import os
import random

S = 1.3
PERIOD = 100.0            # 地图米
P0 = PERIOD / S           # 原包米
ZR = -0.372               # 原包路面高度（地面.008）→ 地图 Z = 0
ROAD_X = (-0.953, 3.13)   # 南北向路面的两边 = 两条路缘石的中线（路缘石 立方体.115 / 立方体.112 量的）
ARM_Y = (-7.906, -4.011)  # 东横路路面的两边（立方体.112 / 立方体.001 横段的中线）
YC = (ARM_Y[0] + ARM_Y[1]) / 2   # 路口中心 → 地图 X = 0
XC = 0.5                  # 两排楼正面之间的中线 → 地图 Y = 0
W0, W1 = YC - P0 / 2, YC + P0 / 2   # 原包 y 窗口：地面、路缘、护桩都按这个窗口切
FRONT_W, FRONT_E = -5.5, 6.5        # 两排楼的正面（原包里约 -4 / +5，往外各挪 1.5 米：街宽 9→12 米，放大后 15.6 米，机体 15 米/秒 + 闪避 26 米/秒要有地方躲）
SIDEWALK_Z = ZR + 0.045   # 人行道比路面高 4.5 厘米（放大后约 6 厘米），边上有路缘石盖着
FAR_X = 400.0             # 地面往两侧铺多远（原包米）

ARENA_HALF_Y = 40.0       # 两侧隐形墙：地图 Y = ±40 米
CEILING = 40.0            # 头顶隐形板：地图 Z = 40 米
FULL_COPIES = 1           # 全精度带碰撞：k = -1, 0, +1（共 3 份）
LOW_COPIES = 4            # 低模：两头各 4 份（k = ±2..±5）。5.5 个周期 = 550 米外是街景贴片
END_WALL_X = 149.0        # 这一阶段还没有循环代码：全精度层两头放只挡机体的墙（X = ±149 米），下一阶段穿接缝后碰不到它

# 五种楼（原包里朝向都是 0 的那一个当本地坐标）。size = 原包 x / y 尺寸，h 高度，base = 底面比路面低多少（原包米，照片底部的路面 / 台阶埋进地里）
TYPES = {
    'A': {'src': '建筑', 'size': (9.59, 11.215), 'h': 13.915, 'base': -0.15, 'good': '-x -y'},       # 商平维修店，-x / -y 两面都是正面
    'B': {'src': '旧房屋2', 'size': (4.625, 7.62), 'h': 13.084, 'base': -0.05, 'good': '四面'},       # 商平维修店窄版
    'D': {'src': '立方体.116', 'size': (11.15, 11.524), 'h': 11.15, 'base': -0.6, 'good': '-y（沈荡钟表店），+x（灰泥 + 竖招牌）还行'},
    'E': {'src': '立方体.094', 'size': (10.262, 8.577), 'h': 8.178, 'base': -1.2, 'good': '+x（国营照相馆），±y 灰泥墙'},
    'F': {'src': '立方体', 'size': (16.931, 19.447), 'h': 5.532, 'base': -0.17, 'good': '-x -y（36524 便利店），+x +y 是拉伸的招牌（难看）'},
}


def size_of(t, th):
    sx, sy = TYPES[t]['size']
    return (sy, sx) if int(round(th)) % 180 == 90 else (sx, sy)


def wrap(y):
    return W0 + (y - W0) % P0


def ue(x, y, z):
    return S * (y - YC), S * (x - XC), S * (z - ZR)


def row_x(front, t, th):
    """正面贴着 front 的楼的中心 x：东排往 +x 长，西排往 -x 长"""
    w = size_of(t, th)[0]
    return front + w / 2 if front > 0 else front - w / 2


def at_y(y0, t, th):
    return y0 + size_of(t, th)[1] / 2


# ---- 全精度层的楼（带碰撞）：(类型, 中心 x, 中心 y, θ)。街面连成一排，只留东边两条小巷，横路两侧也摆满
STREET = []


def E(t, th, y0):
    STREET.append((t, row_x(FRONT_E, t, th), at_y(y0, t, th), th))


def W(t, th, y0):
    STREET.append((t, row_x(FRONT_W, t, th), at_y(y0, t, th), th))


E('A', 0, -0.33)      # 东排：商平维修店（原包位置），南面朝横路也是正面
E('E', 180, 15.45)    # 国营照相馆
E('B', 0, 24.7)       # 商平维修店窄版（原包这里空着，补楼）
E('F', -90, 35.0)     # 便利店：转 -90°，-y 正面朝街、-x 正面朝北边的横路（难看的 +x / +y 朝背面和小巷）
W('D', 90, -5.62)     # 西排：沈荡钟表店（原包 立方体.117 的位置），路口往里看最显眼的一栋
W('B', 0, 5.85)
W('E', 0, 13.8)       # 国营照相馆
W('D', 0, 22.7)       # 同一种楼换一面朝街：灰泥 + 沈荡钟表竖招牌
W('F', 180, 34.6)     # 便利店（-x 正面朝街）
W('A', 180, 54.3)     # 商平维修店：-x 朝街，-y 朝北边的广场
# 东横路北侧（南面朝路）
STREET.append(('D', 16.3 + 5.575, 0.325 + 5.762, 0))
STREET.append(('A', 27.7 + 4.795, -0.2 + 5.6075, 0))
# 东横路南侧（北面朝路）；便利店 E('F') 的北面已经朝着路
STREET.append(('E', 26.2 + 4.2885, -25.0 - 5.131, 90))
# 西边广场北侧（南面朝路）
STREET.append(('E', -17.3 - 4.2885, -0.55 + 5.131, -90))
STREET.append(('D', -26.1 - 5.575, -0.55 + 5.762, 0))
# 西边广场南侧（北面朝路）
STREET.append(('B', -15.3 - 2.3125, -11.41 - 3.81, 0))
STREET.append(('D', -20.2 - 5.575, -11.41 - 5.762, 180))

# ---- 树：同一棵（原包七叶树）。西人行道一排等距 3 棵（原包 4 棵一段 76 米，放大后树冠直径 34 米，整条街盖死、街上一片黑），东南广场 1 棵
TREES = [(-2.55, -9.46), (-2.4, 16.2), (-2.4, 41.8), (10.47, -9.46)]
TRUNK_R = 0.95            # 树干碰撞半径（原包米）

# ---- 护桩（蓝白 U 形）：(来源, 个数, 第一个的 y 中心, x 偏移)，间距 = P0/个数，正好整除周期
BOLLARDS = [('平面.006', 11, -15.4, 0.0), ('平面.007', 10, -1.02, 0.0), ('平面.005', 11, -15.25, 5.63)]
BOLLARD_SKIP_ARM = (-9.0, -3.2)   # 东边那两排在横路口上的跳过（x > 0）
BOLLARD_SKIP_TREE = 2.2           # 西边那排离树干太近的跳过

# ---- 地面贴图平铺：沿街方向必须整除周期，接缝才对得上
ROAD_V_TILES = 18         # Road003 一张约 4.27 米（原包 4.23）
PAVE_TILE = P0 / 36       # PavingStones 一张 2.137 米（原包 3.9×1.46 米拉伸了）

# ---- 贴图和材质（贴图由 tools/oldstreet_textures.py 从 Inbox/_oldstreet/textures 转成 2 的幂 + ARM）
MATS = {
    'OS_OldHouse': {'D': 'T_OS_OldHouse_D', 'gamma': 1.2, 'rough': 0.75},
    'OS_Clock': {'D': 'T_OS_Clock_D', 'gamma': 1.2, 'rough': 0.8},
    'OS_Photo': {'D': 'T_OS_Photo_D', 'gamma': 1.0, 'tint': (1.25, 1.25, 1.25), 'rough': 0.8},
    'OS_Mart': {'D': 'T_OS_Mart_D', 'gamma': 1.2, 'rough': 0.55},
    'OS_MartGlow': {'D': 'T_OS_Mart_D', 'gamma': 1.0, 'rough': 0.5, 'glow': 0.12},
    'OS_MartSign': {'D': 'T_OS_Mart_D', 'gamma': 1.0, 'rough': 0.5, 'glow': 0.3},
    'OS_Road': {'D': 'T_OS_Road_D', 'N': 'T_OS_Road_N', 'ARM': 'T_OS_Road_ARM', 'rough': 1.0, 'gamma': 0.8, 'tint': (1.05, 1.02, 0.97)},
    'OS_Paving': {'D': 'T_OS_Paving_D', 'N': 'T_OS_Paving_N', 'ARM': 'T_OS_Paving_ARM', 'rough': 1.0, 'gamma': 1.0, 'tint': (0.95, 0.93, 0.88)},
    'OS_Curb': {'D': 'T_OS_Curb_D', 'gamma': 1.0, 'tint': (0.75, 0.75, 0.75), 'rough': 0.85},
    'OS_BollardBlue': {'color': (0.07, 0.11, 0.33), 'rough': 0.5, 'metal': 0.45},
    'OS_BollardWhite': {'color': (0.8, 0.8, 0.8), 'rough': 0.5},
    'OS_Rail': {'color': (0.72, 0.72, 0.7), 'rough': 0.55, 'metal': 0.3},
    'OS_PoleBasic': {'color': (0.21, 0.21, 0.21), 'rough': 0.45, 'metal': 0.6},
    'OS_PoleWhite': {'color': (0.52, 0.52, 0.52), 'rough': 0.8, 'metal': 0.3},
    'OS_PoleConcrete': {'color': (0.45, 0.44, 0.42), 'rough': 0.9},
    'OS_Wire': {'color': (0.02, 0.02, 0.02), 'rough': 0.7},
    'OS_Dark': {'color': (0.03, 0.03, 0.03), 'rough': 0.6},
    'OS_SignalBody': {'color': (0.06, 0.06, 0.06), 'rough': 0.35, 'metal': 0.8},
    'OS_SignalRed': {'color': (0.22, 0.01, 0.01), 'rough': 0.35},
    'OS_SignalAmber': {'color': (0.3, 0.12, 0.01), 'rough': 0.35},
    'OS_SignalGreen': {'color': (0.01, 0.8, 0.05), 'rough': 0.4, 'glow': 0.9},
    'OS_Ped': {'D': 'T_OS_Ped_D', 'gamma': 1.0, 'rough': 0.5},
    'OS_PedGlow': {'D': 'T_OS_Ped_D', 'gamma': 1.5, 'rough': 0.5, 'glow': 0.6},
    'OS_Sign': {'D': 'T_OS_Sign_D', 'gamma': 1.0, 'rough': 0.5, 'mask': (0.659, 0.867)},   # 原包：亮度 0.659–0.867 做透明度（白底抠掉）
    'OS_Stall': {'D': 'T_OS_Stall_D', 'gamma': 1.5, 'rough': 0.7},
    'OS_StallSide': {'D': 'T_OS_StallSide_D', 'gamma': 1.2, 'rough': 0.7},
    'OS_Box': {'D': 'T_OS_Box_D', 'N': 'T_OS_Box_N', 'gamma': 1.0, 'rough': 0.7},
    'OS_Wood': {'D': 'T_OS_Wood_D', 'gamma': 1.2, 'rough': 0.55, 'tint': (0.6, 0.6, 0.6)},
    'OS_StoolPink': {'color': (0.83, 0.38, 0.32), 'rough': 0.4},
    'OS_CupMetal': {'color': (0.27, 0.27, 0.27), 'rough': 0.3, 'metal': 1.0},
    'OS_Chopstick': {'color': (0.48, 0.12, 0.01), 'rough': 0.5},
    'OS_Bark': {'D': 'T_OS_Bark_D', 'N': 'T_OS_Bark_N', 'gamma': 1.0, 'rough': 0.85, 'tint': (0.9, 0.85, 0.8)},
    'OS_Leaf': {'leaf': True, 'tint': (0.95, 0.9, 0.5), 'tint2': (1.2, 0.95, 0.42), 'sss': (0.95, 0.8, 0.28), 'rough': 0.65},
    'OS_FarGround': {'color': (0.42, 0.35, 0.25), 'rough': 0.95},
}
# 原包材质 → 槽位（Blender 导出用）。原包有不少同图不同名的
SLOT_OF = {
    '材质.008': 'OS_OldHouse', '材质.012': 'OS_OldHouse', '材质.013': 'OS_OldHouse', '材质.027': 'OS_Clock', '材质.003': 'OS_Photo',
    '材质.010': 'OS_Mart', '材质.009': 'OS_MartGlow', '材质.001': 'OS_MartSign', '材质.016': 'OS_Curb', '材质.031': 'OS_BollardBlue', '材质.032': 'OS_BollardWhite',
    '.Pole.Basic': 'OS_PoleBasic', '.Pole.White': 'OS_PoleWhite', '.Pole.Concrete': 'OS_PoleConcrete', '.Pole.Lamp': 'OS_PoleWhite', '.Pole.Wire': 'OS_Wire', '材质.026': 'OS_Dark',
    '材质.025': 'OS_Dark', '标志': 'OS_Sign', 'black': 'OS_SignalBody', 'red lights': 'OS_SignalRed', 'amber light': 'OS_SignalAmber', 'green light': 'OS_SignalGreen',
    'Material.031': 'OS_SignalBody', 'Material.032': 'OS_SignalBody', '材质.022': 'OS_Ped', '材质.023': 'OS_PedGlow', '材质.024': 'OS_PedGlow',
    'tripo_mat_f2cab702': 'OS_Box', 'F8~DQP[T5{QW7D[@4~E$(55': 'OS_Stall', '材质.028': 'OS_StallSide', '材质.019': 'OS_Wood', '材质.021': 'OS_StoolPink', '材质.017': 'OS_CupMetal', '材质.018': 'OS_Chopstick',
    '材质.014': 'OS_Rail', 'bq_Bark_Aesculus-hippocastanum': 'OS_Bark', 'bq_Leaf_Aesculus-hippocastanum': 'OS_Leaf',
}


def town():
    """远景楼（不带碰撞）：隐形墙以外的街区，横路两头也一直有楼。随机种子固定，改了也能复现。朝横路的那排把好看的面朝路"""
    rnd = random.Random(20261102)
    out = []
    good_south = {'A': 0, 'B': 0, 'D': 0, 'E': -90, 'F': 0}     # 好看的面朝 -y 的朝向
    good_north = {'A': 180, 'B': 0, 'D': 180, 'E': 90, 'F': 180}   # 朝 +y
    pool = 'AABDDEEBF'
    step_x, step_y = 14.5, 13.5
    # 东侧：y 从横路北边 0.3 到下一个周期横路南边 -25+P0。西侧：y 从 -0.55 到 -11.41+P0
    for side, x0, y0, y1 in ((1, 33.0, 0.3, -25.0 + P0), (-1, -32.0, -0.55, -11.41 + P0)):
        rows = max(1, int((y1 - y0) // step_y))
        dy = (y1 - y0) / rows
        for c in range(18):
            xc = x0 + side * (c + 0.5) * step_x
            for r in range(rows):
                t = rnd.choice(pool)
                if r == 0:
                    th = good_south[t]
                elif r == rows - 1:
                    th = good_north[t]
                else:
                    th = rnd.choice((0, 90, 180, -90))
                sx, sy = size_of(t, th)
                # 贴着横路的那排正面对齐路边，别的随机错开一点
                if r == 0:
                    yy = y0 + sy / 2
                elif r == rows - 1:
                    yy = y1 - sy / 2
                else:
                    yy = y0 + (r + 0.5) * dy + rnd.uniform(-0.8, 0.8)
                out.append((t, xc + rnd.uniform(-0.8, 0.8), yy, th))
    return out


def inst_row(t, x, y, th):
    X, Y, Z = ue(x, wrap(y), ZR + TYPES[t]['base'])
    return {'type': t, 'X': round(X, 3), 'Y': round(Y, 3), 'Z': round(Z, 3), 'yaw': -th}


def build():
    data = {
        'S': S, 'PERIOD': PERIOD, 'P0': P0, 'ZR': ZR, 'XC': XC, 'YC': YC, 'W0': W0, 'W1': W1, 'ROAD_X': ROAD_X, 'ARM_Y': ARM_Y,
        'FRONT_W': FRONT_W, 'FRONT_E': FRONT_E, 'SIDEWALK_Z': SIDEWALK_Z, 'FAR_X': FAR_X, 'ARENA_HALF_Y': ARENA_HALF_Y, 'CEILING': CEILING,
        'FULL_COPIES': FULL_COPIES, 'LOW_COPIES': LOW_COPIES, 'END_WALL_X': END_WALL_X,
        'types': TYPES, 'mats': MATS, 'slot_of': SLOT_OF, 'trees_src': TREES, 'bollards': BOLLARDS, 'bollard_skip_arm': BOLLARD_SKIP_ARM, 'bollard_skip_tree': BOLLARD_SKIP_TREE,
        'road_v_tiles': ROAD_V_TILES, 'pave_tile': PAVE_TILE, 'trunk_r': TRUNK_R,
        'street': [inst_row(*b) for b in STREET],
        'town': [inst_row(*b) for b in town()],
    }
    data['trees'] = []
    for x, y in TREES:
        X, Y, Z = ue(x, wrap(y), -0.3)
        data['trees'].append({'X': round(X, 3), 'Y': round(Y, 3), 'Z': round(Z, 3)})
    # 出生点（米）：下一阶段循环后两队隔 50 米（往哪边走都是 50 米）
    data['team_spawn'] = [{'X': -25.0, 'Y': 0.0, 'yaw': 0.0}, {'X': 25.0, 'Y': 0.0, 'yaw': 180.0}]
    # 街景贴片（两头最远处，X = ±552 米）：图是在 X = ∓540 米、高 3.2 米顺街截的（FOV 20.5°，不带雾），所以大小正好接得上：552 米深处的街宽在图上和贴片处真实街宽一样。
    # 只有街口那一条缝和屋顶上方一点点看得见。V 从上到下：z = 59 → 0，z = -53 → 1；z 30 以上淡出（30 米以上是真天空）
    data['backdrop'] = {'w': 200.0, 'h': 112.5, 'zc': 3.2, 'fade_top': 0.26, 'fade_bottom': 0.37, 'bright': 0.85}
    data['dm_spawn'] = [(0.0, 0.0), (-2.0, 26.0), (-2.0, -22.0), (42.0, 1.0), (19.0, 2.0), (-21.0, -2.0), (27.0, 34.0), (-22.0, 34.0), (27.0, -33.0), (-30.0, -33.0)]
    return data


def plan_png(data, out):
    from PIL import Image, ImageDraw, ImageFont
    px = 6     # 每米像素
    W_, H_ = int(320 * px), int(180 * px)
    im = Image.new('RGB', (W_, H_), (235, 232, 225))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 22)

    def P(X, Y):
        return (W_ / 2 + X * px, H_ / 2 - Y * px)   # X 向右（沿街），Y 向上

    def rect(X0, Y0, X1, Y1, fill=None, outline=None):
        a, b = P(X0, Y1), P(X1, Y0)
        d.rectangle([a, b], fill=fill, outline=outline, width=2)

    for k in range(-1, 2):
        rx0, ry0, rx1, ry1 = (k * PERIOD - 50, S * (ROAD_X[0] - XC), k * PERIOD + 50, S * (ROAD_X[1] - XC))
        rect(rx0, ry0, rx1, ry1, fill=(120, 120, 120))
        rect(k * PERIOD + S * (ARM_Y[0] - YC), S * (ROAD_X[1] - XC), k * PERIOD + S * (ARM_Y[1] - YC), 90, fill=(120, 120, 120))
    colors = {'A': (190, 140, 90), 'B': (160, 110, 70), 'D': (120, 150, 170), 'E': (200, 180, 130), 'F': (90, 170, 110)}
    for k in range(-1, 2):
        for layer, alpha in (('town', 0.5), ('street', 1.0)):
            for b in data[layer]:
                t = b['type']
                sx, sy = size_of(t, -b['yaw'])
                bx, by = S * sy / 2, S * sx / 2
                c = colors[t] if alpha == 1.0 else tuple(int(v * 0.5 + 200 * 0.5) for v in colors[t])
                rect(b['X'] + k * PERIOD - bx, b['Y'] - by, b['X'] + k * PERIOD + bx, b['Y'] + by, fill=c, outline=(40, 40, 40))
        for t in data['trees']:
            a = P(t['X'] + k * PERIOD, t['Y'])
            r = S * 13 * px
            d.ellipse([a[0] - r, a[1] - r, a[0] + r, a[1] + r], outline=(40, 120, 40), width=1)
            d.ellipse([a[0] - 6, a[1] - 6, a[0] + 6, a[1] + 6], fill=(30, 90, 30))
    for x_ in (-50, 50):
        d.line([P(x_, -90), P(x_, 90)], fill=(220, 30, 30), width=3)
        d.text(P(x_ + 1, 85), f'接缝 X={x_}', fill=(220, 30, 30), font=f)
    d.line([P(-160, 40), P(160, 40)], fill=(30, 30, 220), width=3)
    d.line([P(-160, -40), P(160, -40)], fill=(30, 30, 220), width=3)
    d.text(P(-158, 39), '隐形墙 Y=±40', fill=(30, 30, 220), font=f)
    for i, s_ in enumerate(data['team_spawn']):
        a = P(s_['X'], s_['Y'])
        d.ellipse([a[0] - 10, a[1] - 10, a[0] + 10, a[1] + 10], fill=(220, 40, 40) if i else (40, 80, 220))
    for x_, y_ in data['dm_spawn']:
        a = P(x_, y_)
        d.rectangle([a[0] - 6, a[1] - 6, a[0] + 6, a[1] + 6], fill=(240, 200, 20))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im.save(out)


if __name__ == '__main__':
    data = build()
    p = 'D:/RobotSlice/Inbox/_oldstreet/layout.json'
    json.dump(data, open(p, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    print('写好', p, '街面楼', len(data['street']), '远景楼（每周期）', len(data['town']), '窗口', round(W0, 3), round(W1, 3), 'YC', YC)
    plan_png(data, 'D:/RobotSlice/docs/老旧街道_20261102/平面布局.png')
