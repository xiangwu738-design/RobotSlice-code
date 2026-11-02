"""月球基地 L_Moon 布局表（纯 Python，改地图就改这里）。py -3 -I tools/moon_layout.py → Inbox/_k65/layout.json
坐标都是 UE 俯视：X 向东（右），Y 向南（下），米；原点 = 大穹顶中心 = 高程图上 (9.964, 33.212) 公里处（见 moon_terrain_bake.py）。
东西两队沿 X 轴对称（南北两半故意不一样，两队到南北的距离一样，公平；也方便喊位置：北边机库区、南边储罐区）。
每项：组名（meshes.json 里 SM_K65_<组名>_Solid/Deco/Glass，P_ 开头是单个零件）、包围盒中心放到 (x, y)、朝向 yaw（度，俯视顺时针）、缩放 s、pad=True 时地形按占地压平。
"""
import json
import math

SRC = 'D:/RobotSlice/Inbox/_k65/export/meshes.json'
OUT = 'D:/RobotSlice/Inbox/_k65/layout.json'
M = {m['name'].replace('SM_K65_', ''): m for m in json.load(open(SRC, encoding='utf-8'))['meshes']}
S = 1.2      # 建筑整体放大（K65 按真人尺寸，门 2.5 米；机体 2.5 米）
Items = []


def bbox(group):
    los, his = [], []
    for cls in ('Solid', 'Deco', 'Glass'):
        m = M.get(f'{group}_{cls}')
        if m and cls == 'Solid':
            los.append(m['min'])
            his.append(m['max'])
    if not los:
        for cls in ('Deco', 'Glass'):
            m = M.get(f'{group}_{cls}')
            if m:
                los.append(m['min'])
                his.append(m['max'])
    lo = [min(v[i] for v in los) for i in range(3)]
    hi = [max(v[i] for v in his) for i in range(3)]
    # Blender → UE：y 取反
    return (lo[0], -hi[1], lo[2]), (hi[0], -lo[1], hi[2])


def put(group, x, y, yaw=0.0, s=S, pad=False, note=''):
    lo, hi = bbox(group)
    cx, cy = (lo[0] + hi[0]) / 2 * s, (lo[1] + hi[1]) / 2 * s
    r = math.radians(yaw)
    wx = cx * math.cos(r) - cy * math.sin(r)
    wy = cx * math.sin(r) + cy * math.cos(r)
    w, d = (hi[0] - lo[0]) * s, (hi[1] - lo[1]) * s
    Items.append({'group': group, 'x': x - wx, 'y': y - wy, 'cx': x, 'cy': y, 'yaw': yaw, 's': s, 'pad': pad, 'w': w, 'd': d, 'top': hi[2] * s, 'note': note,
                  'classes': [c for c in ('Solid', 'Deco', 'Glass') if f'{group}_{c}' in M]})


def line(group, x0, y0, x1, y1, n, yaw=None, s=S):
    """一串零件（走廊）：从 (x0,y0) 到 (x1,y1) 均匀摆 n 个"""
    if yaw is None:
        yaw = math.degrees(math.atan2(y1 - y0, x1 - x0))
    for i in range(n):
        t = (i + 0.5) / n
        put(group, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, yaw, s)


def both(group, x, y, yaw=0.0, s=S, pad=False, note='', flip=180.0):
    """东西对称放一对：西边按给的坐标，东边 x 取反、转 flip 度"""
    put(group, -x, y, yaw, s, pad, note)
    put(group, x, y, yaw + flip, s, pad, note)


# ---------------------------------------------------------------- 中央
put('XLG_C', 0, 0, 0, 1.0, True, '大穹顶：外墙 62 米半径、墙顶 9 米、里面地面 5 米；玻璃没有碰撞（当气密膜），能直接飞进去')

# ---------------------------------------------------------------- 两队后方（出生区背后的墙）
put('MD_C', -212, 0, 90, S, True, '西队机库')
put('MD_F', 214, 0, -90, S, True, '东队球顶厂房')
# 出生区前面的挡板：从穹顶西 / 东门看不到出生点
put('SM_G', -112, 0, 90, S, True, '西队前挡')
put('SM_G', 112, 0, 90, S, True, '东队前挡')
line('P_Corridor_C', -80, 0, -98, 0, 1)      # 穹顶西门 → 前挡
line('P_Corridor_C', 80, 0, 98, 0, 1)

# ---------------------------------------------------------------- 北半边：机库区（高，屋顶能走）
put('MD_G', -124, -132, 0, S, True, '西北机库')
put('MD_G', 124, -132, 180, S, True, '东北机库')
put('LG_H', 0, -218, 0, 1.0, True, '北边大平台')
line('P_Corridor_A', 0, -80, 0, -165, 5, 90)   # 穹顶北门 → 平台：把北广场分成东西两半（矮墙，能跳过去）
# 北广场掩体（走廊两边）
both('SM_C', 38, -116, 0, 2.0)
both('P_Building_F', 34, -150, 0, 1.8)
both('P_BlastCover_A', 58, -96, 25, 1.6)
both('P_Tank_C', 18, -100, 0, 1.3)
both('P_Crate_A', 52, -136, 15, 2.5)

# ---------------------------------------------------------------- 南半边：储罐区（矮，开阔，一座拉索塔）
put('MD_A', -120, 122, 0, S, True, '西南小穹顶')
put('MD_B', 120, 126, 0, S, True, '东南六角厅')
put('MD_E', 0, 150, 0, S, True, '南边拉索塔')
line('P_Corridor_C', 0, 80, 0, 112, 2, 90)    # 穹顶南门 → 塔
both('P_Tank_B', 46, 104, 0, 2.2)
both('P_Tank_B', 66, 88, 0, 1.6)
both('P_Tank_C', 56, 124, 0, 1.6)
both('P_Tank_C', 22, 96, 0, 1.6)
both('P_Pipes_A', 62, 150, 90, 1.4)
put('LG_D', 25, 262, 0, 1.0, True, '南边发射架')

# ---------------------------------------------------------------- 东西两翼（出生区到南北两条路之间）
both('P_Building_G', 150, -60, 0, 1.8)
both('P_Corridor_E', 150, 60, 0, 1.8)
both('P_CoolingSystem_A', 94, -74, 40, 1.6)
both('P_TowerElement_A', 86, -40, 0, 1.2)
both('P_Pathway_B', 90, 64, 90, 1.6)
both('P_Antenna_A', 212, 84, 0, 1.0)
both('P_Building_E', 200, -80, 0, 1.6)
both('SM_H', 252, -105, 0, 1.5)
both('SM_I', 250, 56, 0, 1.5)
# 出生区矮掩体
both('P_Crate_A', 150, 22, 0, 2.5)
both('P_Crate_A', 146, -26, 40, 2.5)
both('P_BlastCover_A', 138, 34, 90, 1.5)
both('P_BlastCover_A', 138, -34, 90, 1.5)

# ---------------------------------------------------------------- 点缀（都不挡路：太阳能阵列、路灯、细天线）
# 太阳能阵列：基地外圈，西南一片、东北一片，一排排（真实月球基地最该有的东西）
for i in range(4):
    for j in range(4):
        put('SM_E', -335 + i * 21, 125 + j * 19, 0, 1.5)
        put('SM_E', 300 + i * 21, -150 + j * 19, 0, 1.5)
# 路灯：沿南北两条路和穹顶门口，亮着的灯头在阴影里很显眼
for x, y in ((60, -82), (100, -66), (135, -44), (30, -90), (60, 82), (100, 66), (135, 44), (30, 90)):
    both('P_Lamp_B', x, y, 0, 1.6)
for x, y in ((185, 30), (185, -30)):
    both('P_Lamp_A', x, y, 0, 1.8)
# 细天线、雷达：屋顶线以上的细节
both('P_Antenna_H', 262, 20, 0, 1.3)
both('P_Antenna_C', 70, -178, 0, 1.6)
put('P_Antenna_A', -40, 225, 200, 1.0)
# 外圈零散小站（在去环形坑的路上有东西可看、可躲）
put('SM_B', -330, -140, 30, 1.5)
put('SM_A', 330, 40, 110, 1.5)
put('SM_C', -60, -330, 15, 1.6)
put('SM_I', 180, 300, 200, 1.5)
put('SM_K', -250, 250, 60, 1.5)
put('P_Tank_B', -258, 262, 0, 1.6)

# ---------------------------------------------------------------- 场外地标
put('XLG_A', 300, -300, 0, 1.0, True, '东北高塔（189 米）')

# ---------------------------------------------------------------- 标记
# 两队出生中心（TeamSpawn：朝向 = 出生朝向，一队的几台沿左右排开，间隔 7 米）
team = [{'name': 'TeamSpawn0', 'x': -165.0, 'y': 0.0, 'yaw': 0.0}, {'name': 'TeamSpawn1', 'x': 165.0, 'y': 0.0, 'yaw': 180.0}]
# DMSpawn：铺满，≥ 15 米间隔；(x, y, 朝向, 基准高度：省略 = 按地形；数字 = 那个高度以上 1.5 米)
dm = [(-165, -22, 0), (165, 22, 180), (-60, -125, 60), (60, -125, 120), (-178, -72, 20), (178, -72, 160), (-180, 96, -20), (180, 96, 200),
      (-52, 118, -60), (52, 118, 240), (10, -290, 90), (-30, 200, -90), (-32, 34, 0, 5.2), (32, -34, 180, 5.2), (-275, -90, 20), (275, 90, 200)]

ARENA = {'cx': 0.0, 'cy': 25.0, 'hx': 470.0, 'hy': 395.0, 'top': 300.0}   # 活动范围：X ±470，Y -370~420；再往外是自然山坡 + 隐形墙
KEEP = [(x, y, r) for x, y, r in ((-395, -275, 70), (61, -296, 45), (20, 390, 40), (-262, -52, 35))]    # 这些环形坑别被压平、别撒大石头

Items = [i for i in Items if i]
json.dump({'items': Items, 'team': team, 'dm': dm, 'arena': ARENA, 'keep': KEEP}, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('布局', len(Items), '项 →', OUT)
for it in Items:
    if it['pad']:
        print(f"  {it['group']:8s} 中心 ({it['cx']:6.1f},{it['cy']:6.1f}) 占地 {it['w']:.0f}×{it['d']:.0f} 顶 {it['top']:.0f} 米 yaw {it['yaw']:.0f}  {it['note']}")
