"""击毁播报的武器剪影（11-01，用户：「关于击杀播报，请你在武器显示部分使用武器真正的黑白底图」）：哪件武器用哪个模型、怎么摆。
tools/ue_export_weapon_meshes.py（UE 里跑）和 tools/make_weapon_icons.py（py -3 跑）共用这张表；流程见 tools/make_weapon_icons.sh。

模型路径不写死：枪和刀照 Source/RobotSlice/MechWeapons.cpp 表里的顺序，肩部照 MechItemStudio.cpp 的 Shoulders[]（AMechItemStudio::MeshFor 用的那张）。
所以换了模型、补了缺的肩部装备模型，只要代码表更新了，重跑 make_weapon_icons.sh 就跟着变。名字顺序要和 MechFeedIcon（MechFeed.h）、MechHudFeed.cpp 一致。
"""
import os
import re

ROOT = 'D:/RobotSlice'
SRC = f'{ROOT}/Source/RobotSlice'
OUT_DIR = f'{ROOT}/SourceAssets/ui_weapon_icons'      # 剪影 PNG（T_WI_*.png 导入进工程）
MESH_DIR = f'{ROOT}/Saved/WeaponIconMeshes'           # 中间产物：从工程导出的 OBJ
DEST = '/Game/UI/WeaponIcons'

GUNS = ['Rifle', 'SMG', 'Shotgun', 'Marksman', 'Sniper', 'DualPistol']                         # EMechShooter 的顺序
BLADES = ['Katana', 'QuickBlade', 'OneHand', 'GreatSword', 'TwinBlades', 'Blunt', 'Dagger']    # EMechMeleeWeapon 的顺序
SHOULDERS = ['Missiles', 'Artillery', 'Shield', 'Grapple', 'Net', 'Smoke', 'Emp', 'Burst', 'Launch', 'Shockwave', 'Mine']  # EMechShoulder 的顺序


# 11-01 模型代理补的四件（机身借的旧模型换成自己的）。ICON_OVERRIDE：图标不用无人机机身、用更好认的那个模型
# （导弹画导弹本身：导弹巢的轮廓只是一块圆角方块；地雷画雷本身，不画布雷器）
SHOULDER_MESH = {
    'Burst': '/Game/Weapons/Meshes/SM_BurstPack',
    'Launch': '/Game/Weapons/Meshes/SM_LaunchSpring',
    'Shockwave': '/Game/Weapons/Meshes/SM_ShockRing',
    'Mine': '/Game/Weapons/Meshes/SM_Mine',
    'Missiles': '/Game/Weapons/Meshes/SM_Missile',
}
ICON_OVERRIDE = {'Mine', 'Missiles'}


def items():
    """[(名字, 模型路径)]：代码表里还没有模型的肩部装备不在里面（HUD 退回几何小图标）"""
    with open(f'{SRC}/MechWeapons.cpp', encoding='utf-8') as f:
        weapons = re.findall(r'TEXT\("(/Game/Weapons/(?:\w+/)*SM_\w+)"\)', f.read())
    if len(weapons) != len(GUNS) + len(BLADES):
        raise RuntimeError(f'MechWeapons.cpp 里找到 {len(weapons)} 个武器模型，应该是 {len(GUNS) + len(BLADES)} 个：{weapons}')
    with open(f'{SRC}/MechItemStudio.cpp', encoding='utf-8') as f:
        m = re.search(r'Shoulders\[\]\s*=\s*\{(.*?)\};', f.read(), re.S)
    shoulders = re.findall(r'TEXT\("([^"]+)"\)', m.group(1)) if m else []
    result = list(zip(GUNS + BLADES, weapons)) + list(zip(SHOULDERS, shoulders))
    # 代码表（Shoulders[]）里还没写上的，用已经做好的模型（SHOULDER_MESH）；模型不存在时导出那一步只报一行、HUD 退回几何小图标
    have = {name for name, _ in result}
    result += [(name, path) for name, path in SHOULDER_MESH.items() if name not in have]
    return [(name, SHOULDER_MESH.get(name, path) if name in ICON_OVERRIDE else path) for name, path in result]


# 每件怎么摆（看过 _contact.png 定的）：默认最长的轴横放、第二长的竖放（看最宽的那一面）。
#   flipx 左右翻（枪口 / 刀尖朝右：击毁者在左、被击毁的在右），flipy 上下翻（握把朝下）；现有模型都不用翻（新模型看 _contact.png）；
#   axes=(横放的轴, 竖放的轴)：不按长短轴，指定 OBJ 的轴号；
#   yscale：竖直方向放大（太扁的东西）；
#   pair：一对武器画两把（双持手枪一前一后、双刀交叉成 X）。
POSE = {
    'DualPistol': {'pair': 'offset'},
    'TwinBlades': {'pair': 'cross', 'angle': 10},
    'Mine': {'axes': (0, 1), 'yscale': 2.0},  # 从上看只是一个圆：从侧面看扁圆盘 + 顶上的灯，竖直放大才看得清
}
