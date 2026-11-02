"""K051 高科技街道 → 死斗地图「相位电子园区」(L_TechPark) 的布局表。
只算数、不依赖 Blender / UE：py -3 tools/k051_layout.py [输出.json]（默认 Inbox/_k051/layout.json）

坐标：Blender 坐标、最终米（已乘 S），x 东、y 北、z 上；UE 里 Y 取反、乘 100。
模块原尺寸是给真人的（门 3.1 米、高架路宽 8 米），统一放大 S=1.5 给 2.4 米的机体用：高架路宽 12 米、桥面顶 7.5 米、桥下净空约 5.8 米。
地图左右镜像（x → -x）：西边蓝队（0 队）、东边红队（1 队），团队淘汰两边公平。

布局（俯视，单位米）：
  - 活动范围 x ±112、y ±80（224 × 160 米），四周和头顶 42 米隐形墙
  - 中央：高架环路（7.5 米）围出的广场，环路拐角中心 (±45.4, ±49.1)；南北两边中间各一个丁字路口，坡道朝里一直落到广场中心
    （两条坡道脚相对，中间 12 米是「中央交汇口」）；广场东西各一座观景塔 LG_D（顶 18 米），挡住两座基地之间的直线视野
  - 东西两头：圆形高台（Road_G，7.5 米，半径约 22 米）= 两队基地，由环路东西两边丁字路口伸出去连上；台上有安检亭 LG_K 做出生掩体
  - 北边带：大门 LG_A（顶 24 米，16 米高处有站台）+ 东西向悬挂单轨（21 米）
  - 南边带：单轨车站支架 LG_B + 停着的列车 Train_A（15–21 米，可以落在车顶）
  - 四个角：北角是大堂楼 LG_C（14 米屋顶）+ 「20」号塔 LG_H（26 米）；南角是带外楼梯的服务楼 LG_F + 玻璃廊 LG_I + 亭子
  - 环路下和广场里散放售货亭、花坛、全息屏做掩体
"""
import json
import math
import os
import sys

S = 1.5
DECK = 5.0 * S          # 高架桥面顶 7.5 米
LX, LY = 45.4, 49.1     # 环路拐角中心
BOUND = (112.0, 80.0)   # 活动范围半宽
CEIL = 42.0             # 头顶隐形墙

items = []


def put(piece, x, y, yaw=0.0, z=0.0, sx=1.0, sy=1.0, sz=1.0, tag='', skip=(), only=()):
    """skip / only：按物体名后缀（如 Tracks、Main）去掉或只留模块里的某几件"""
    items.append({'piece': piece, 'loc': [round(x, 3), round(y, 3), round(z, 3)], 'yaw': round(yaw, 3),
                  'scale': [round(S * sx, 4), round(S * sy, 4), round(S * sz, 4)], 'tag': tag,
                  'skip': list(skip), 'only': list(only)})


def mput(piece, x, y, yaw=0.0, **kw):
    """左右镜像各放一个（东边原样，西边 x 取反、朝向镜像）"""
    put(piece, x, y, yaw, **kw)
    put(piece, -x, y, 180.0 - yaw, **kw)


def rot(x, y, yaw):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return x * c - y * s, x * s + y * c


# ---------------------------------------------------------------- 高架环路（Road_A 直段可以沿长度方向拉伸）
# 拐角 Road_E：模块里两条腿 -Y / +X，拐点在 (-5.2, 5.12)×S
EC = (-5.2 * S, 5.12 * S)
for (cx, cy, yaw) in ((LX, LY, -90.0), (-LX, LY, 0.0), (LX, -LY, 180.0), (-LX, -LY, 90.0)):
    ox, oy = rot(*EC, yaw)
    put('Road_E', cx - ox, cy - oy, yaw, tag='deck')
LEG = 12.73 * S
# 北边、南边：中间丁字路口 Road_C（主线中心在模块 y=-2.04，支路伸到 y=7.96），支路朝里
for side in (1, -1):
    yaw = 180.0 if side > 0 else 0.0
    # 北边转 180°（支路朝 -Y 朝里），南边不转
    ox, oy = rot(0, -2.04 * S, yaw)
    put('Road_C', 0 - ox, side * LY - oy, yaw, tag='deck')
    fill = LX - LEG - 10 * S          # 丁字路口两边到拐角之间的直段长度
    for sx in (1, -1):
        put('Road_A', sx * (10 * S + fill / 2), side * LY, 0.0, sx=fill / (20 * S), tag='deck')
    # 支路尽头接 Road_B（平台 10.3 米 + 坡道），坡道朝广场中心
    arm_end = side * (LY - 10 * S)
    byaw = -90.0 if side > 0 else 90.0
    bx, by = rot(-7.82 * S, 0, byaw)
    put('Road_B', 0 - bx, arm_end - by, byaw, tag='deck')
# 东边、西边：中间丁字路口，支路朝外接基地圆台
for side in (1, -1):
    yaw = -90.0 if side > 0 else 90.0
    ox, oy = rot(0, -2.04 * S, yaw)
    put('Road_C', side * LX - ox, 0 - oy, yaw, tag='deck')
    fill = LY - LEG - 10 * S
    for sy in (1, -1):
        put('Road_A', side * LX, sy * (10 * S + fill / 2), 90.0, sx=fill / (20 * S), tag='deck')
    arm_end = side * (LX + 10 * S)
    # Road_G：接口在模块 x=-15.76（朝 -X），东边不转、西边转 180°
    gyaw = 0.0 if side > 0 else 180.0
    gx, gy = rot(-15.76 * S, 0.02 * S, gyaw)
    put('Road_G', arm_end - gx, 0 - gy, gyaw, tag='base')
G_CENTER = LX + 10 * S + 15.76 * S + 1.93 * S   # 圆台圆心 x（约 86.9）

# ---------------------------------------------------------------- 广场中间
mput('BldgLG_D', 21.0, 0.0, 90.0)                       # 观景塔，挡基地之间的直线
mput('BldgMD_H', 20.0, 27.0, 0.0)                       # 全息柱亭
mput('BldgMD_H', 20.0, -27.0, 0.0)
mput('BldgMD_B', 33.0, 14.0, 90.0)                      # 售货亭
mput('BldgMD_A', 33.0, -14.0, 0.0)
mput('Planter_F', 9.0, 14.0, 0.0)                       # 坡道两侧的大花坛（半身掩体）
mput('Planter_F', 9.0, -14.0, 0.0)
mput('Planter_B', 12.0, 3.0, 90.0)
mput('Planter_B', 12.0, -3.0, 90.0)
mput('BldgSM_M', 6.5, 30.0, 0.0)                        # 新闻屏柱
mput('BldgSM_F', 6.5, -30.0, 0.0)
mput('Lamp_A', 28.0, 38.0, 0.0)
mput('Lamp_A', 28.0, -38.0, 180.0)
mput('Planter_D', 37.0, 30.0, 0.0)
mput('Planter_D', 37.0, -30.0, 0.0)
put('HoloDeck_A', 0.0, 0.0, 0.0)                        # 中央交汇口地面的全息台（矮，不挡）

# ---------------------------------------------------------------- 基地圆台
mput('BldgLG_K', G_CENTER + 2.0, 0.0, 90.0, z=DECK)     # 安检亭：出生掩体
mput('Planter_B', G_CENTER - 12.0, 9.0, 90.0, z=DECK)
mput('Planter_B', G_CENTER - 12.0, -9.0, 90.0, z=DECK)
mput('BldgSM_D', G_CENTER + 13.0, 12.0, 200.0, z=DECK)  # 屏幕
mput('BldgSM_D', G_CENTER + 13.0, -12.0, 160.0, z=DECK)
# 圆台下面：支柱之间放几个箱亭当掩体
mput('BldgMD_E', G_CENTER - 4.0, 0.0, 90.0)

# ---------------------------------------------------------------- 北边带：大门 + 单轨（单线，21 米高，悬挂式）
RAIL = 2.93 * S                 # LG_B 支架两条轨距中心 ±4.4 米
YN = 72.0                       # 北线轨道 y
put('BldgLG_A', 0.0, YN - 4.3 * S, 0.0, skip=('Tracks',))      # 大门正面（电梯那面）朝南；自带的那段轨用下面连续的轨代替
for x in (-40.0, 40.0, -128.0, 128.0):
    put('BldgLG_B', x, YN - RAIL, 0.0, only=('Main',))             # 只要 X 形支架，北边那条臂托轨道
for k in range(-3, 3):
    put('TrainTracks_A', (k + 0.5) * 34.26 * S, YN, 0.0)

# ---------------------------------------------------------------- 南边带：车站 + 双线 + 停着的列车
YS = -66.0
put('BldgLG_B', 0.0, YS, 0.0, skip=('Tracks',))
for x in (-60.0, 60.0, -128.0, 128.0):
    put('BldgLG_B', x, YS, 0.0, only=('Main',))
for k in range(-3, 3):
    for dy in (-RAIL, RAIL):
        put('TrainTracks_A', (k + 0.5) * 34.26 * S, YS + dy, 0.0)
put('Train_A', 18.0, YS + RAIL, 0.0)
mput('BldgLG_E', 36.0, YS - 2.0, 180.0)                 # 候车棚

# ---------------------------------------------------------------- 四个角
# 北角：大堂楼 + 20 号塔 + 亭子
mput('BldgLG_C', 88.0, 54.0, 90.0)
mput('BldgLG_H', 64.0, 60.0, 180.0)
mput('BldgLG_G', 72.0, 32.0, 0.0)
mput('BldgMD_D', 98.0, 30.0, 90.0)
mput('Planter_F', 104.0, 72.0, 90.0)
mput('Planter_C', 56.0, 28.0, 0.0)
mput('BldgSM_K', 54.0, 44.0, 0.0)
# 南角：服务楼 + 玻璃廊 + 亭子
mput('BldgLG_F', 80.0, -56.0, 180.0)
mput('BldgLG_I', 60.0, -40.0, 90.0)
mput('BldgLG_G', 98.0, -34.0, 0.0)
mput('BldgMD_F', 100.0, -54.0, 0.0)
mput('BldgLG_J', 96.0, -72.0, 0.0)
mput('Planter_F', 74.0, -30.0, 0.0)
mput('BldgMD_I', 46.0, -68.0, 0.0)
# ---------------------------------------------------------------- 远景（K005 现代高楼，换成相位电子的白面板 + 冷青玻璃；墙外，不进活动范围）
def bd(num, x, y, yaw=0.0, k=1.6):
    items.append({'piece': f'TPB_C{num}', 'loc': [x, y, 0.0], 'yaw': yaw, 'scale': [k, k, k], 'tag': 'backdrop', 'skip': [], 'only': []})


bd(30, -20.0, 215.0, 0.0, 3.0)                 # 相位电子（PEC）总部：六棱玻璃塔放大到 157 米，从广场往北穿过大门看得见
bd(31, 150.0, 185.0, 20.0, 1.8)
bd(27, -160.0, 170.0, 10.0, 1.6)
bd(35, 75.0, 265.0, 0.0, 1.7)
bd(26, -95.0, 255.0, 30.0, 1.6)
bd(33, 70.0, -175.0, 0.0, 1.7)
bd(32, -75.0, -185.0, 90.0, 1.8)
bd(27, 175.0, -150.0, 35.0, 1.4)
bd(36, -180.0, -150.0, 0.0, 1.6)
bd(31, 250.0, 30.0, 90.0, 1.5)
bd(10, 205.0, -70.0, 90.0, 1.6)
bd(18, 185.0, 105.0, 0.0, 1.5)
bd(33, -250.0, -20.0, 90.0, 1.7)
bd(10, -205.0, 75.0, 90.0, 1.6)
bd(14, -190.0, -100.0, 0.0, 1.6)
# 紧贴墙外的一圈矮一点的楼，把场地围起来
bd(28, 70.0, 108.0, 0.0, 1.5)
bd(28, -70.0, 108.0, 0.0, 1.5)
bd(18, 140.0, 55.0, 90.0, 1.4)
bd(18, -140.0, -55.0, 90.0, 1.4)
bd(14, 140.0, -40.0, 0.0, 1.5)
bd(14, -140.0, 45.0, 0.0, 1.5)
bd(17, 132.0, 0.0, 0.0, 1.6)
bd(17, -132.0, 0.0, 0.0, 1.6)
bd(36, 30.0, -112.0, 90.0, 1.3)
bd(36, -35.0, -112.0, 90.0, 1.3)

# ---------------------------------------------------------------- 标识（占位）：相位电子 PHASE、主办方合意集团 CONSENT
# k051_brand_export.py 做的薄板网格：宽 1 米、原点底边中点、正面朝南（-Y）；这里的缩放 = 宽度（米），不乘 S
def brand(name, x, y, z, yaw, width):
    items.append({'piece': f'BRAND_{name}', 'loc': [x, y, z], 'yaw': yaw, 'scale': [width, width, width], 'tag': 'brand', 'skip': [], 'only': []})


brand('LogoPHASE', 0.0, 63.0, 17.0, 0.0, 16.0)                       # 大门横梁正面（横梁南面约在 y=63.7；朝南，从广场看得见）
brand('LogoPHASE', -20.0, 215.0 - 24.0, 128.0, 0.0, 34.0)            # 总部塔上部（朝南）
brand('LogoPHASE', G_CENTER + 2.0 - 5.2, 0.0, DECK + 7.2, -90.0, 7.0)  # 东基地安检亭西面（朝场地）
brand('LogoPHASE', -(G_CENTER + 2.0 - 5.2), 0.0, DECK + 7.2, 90.0, 7.0)
brand('SignCONSENT', -40.0, YN - 2 * RAIL, 15.5, 0.0, 10.0)          # 北单轨支架空着的南臂下面挂主办方牌（朝南）
brand('SignCONSENT', 40.0, YN - 2 * RAIL, 15.5, 0.0, 10.0)
brand('SignCONSENT', -24.0, YS + RAIL, 14.0, 180.0, 12.0)            # 南车站北线轨道下（列车停在 x>-9 那段，错开；朝北）

# ---------------------------------------------------------------- 出生点
spawns = {'team0': [], 'team1': [], 'dm': []}
for k, dy in enumerate((-9.0, 0.0, 9.0, 18.0)):
    spawns['team1'].append([G_CENTER - 8.0, dy if k < 3 else -18.0, DECK + 1.6, 180.0])
    spawns['team0'].append([-(G_CENTER - 8.0), dy if k < 3 else -18.0, DECK + 1.6, 0.0])
dm = [
    (G_CENTER - 10.0, 12.0, DECK, 180.0), (-(G_CENTER - 10.0), -12.0, DECK, 0.0),   # 两个基地台上
    (100.0, 52.0, 0.0, 225.0), (-100.0, 52.0, 0.0, -45.0),                          # 北角
    (96.0, -56.0, 0.0, 135.0), (-96.0, -56.0, 0.0, 45.0),                           # 南角
    (30.0, 70.0, 0.0, 270.0), (-30.0, 70.0, 0.0, 270.0),                            # 北边带（大门两侧）
    (40.0, -74.0, 0.0, 90.0), (-40.0, -74.0, 0.0, 90.0),                            # 南边带
    (24.0, LY, DECK, 180.0), (-24.0, -LY, DECK, 0.0),                               # 环路上
]
for x, y, z, yaw in dm:
    spawns['dm'].append([x, y, z + 1.6, yaw])

# ---------------------------------------------------------------- 标注（布局图用）
notes = {
    'high': [
        ('环路 7.5', 0.0, LY), ('基地台 7.5', G_CENTER, 0.0), ('基地台 7.5', -G_CENTER, 0.0),
        ('观景塔 18', 21.0, 0.0), ('观景塔 18', -21.0, 0.0), ('大门站台 16 / 顶 24', 0.0, YN - 4.3 * S),
        ('20 号塔 26', 64.0, 60.0), ('20 号塔 26', -64.0, 60.0), ('大堂楼 14', 88.0, 54.0), ('大堂楼 14', -88.0, 54.0),
        ('列车 15–21', 18.0, YS + RAIL), ('服务楼 9', 80.0, -56.0), ('服务楼 9', -80.0, -56.0),
    ],
    'routes': [
        ('高架环路', [(-LX, LY), (LX, LY), (LX, -LY), (-LX, -LY), (-LX, LY)]),
        ('中路（地面）', [(-G_CENTER, 0), (-40, 0), (-21, 8), (0, 0), (21, -8), (40, 0), (G_CENTER, 0)]),
        ('北侧地面', [(-100, 40), (-60, 62), (0, 58), (60, 62), (100, 40)]),
        ('南侧地面', [(-100, -40), (-60, -62), (0, -58), (60, -62), (100, -40)]),
        ('坡道', [(0, LY - 15), (0, 6)]), ('坡道', [(0, -LY + 15), (0, -6)]),
    ],
}

out = {'scale': S, 'deck': DECK, 'bound': BOUND, 'ceiling': CEIL, 'items': items, 'spawns': spawns, 'notes': notes}
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'Inbox', '_k051', 'layout.json')
json.dump(out, open(path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'[layout] {len(items)} 件，出生点 0 队 {len(spawns["team0"])} / 1 队 {len(spawns["team1"])} / 死斗 {len(spawns["dm"])} → {os.path.abspath(path)}')
