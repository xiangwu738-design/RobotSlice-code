"""按 k051_layout.py 的布局表在 Blender 里把 K051 模块摆出来，用 Workbench 拍预览（不占多少显卡）。
- top.png：正交俯视，画面范围由 --view 半宽决定（布局图标注按同一换算）
- obl_*.png：四个斜俯视；eye_*.png：机体眼睛高度（地面 3 米 / 桥面 10.5 米）的视平线
- 出生点放 2.4 米高的彩色柱（蓝 0 队、红 1 队、绿 死斗），活动范围画一圈红色细框
blender -b K051.blend -P tools/k051_preview.py -- layout.json 输出目录 [--only top]
"""
import json
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

argv = sys.argv[sys.argv.index('--') + 1:]
LAYOUT, OUT = argv[0], argv[1]
ONLY = argv[argv.index('--only') + 1].split(',') if '--only' in argv else None
os.makedirs(OUT, exist_ok=True)
L = json.load(open(LAYOUT, encoding='utf-8'))
sc = bpy.context.scene
root = sc.collection.children[0]

# ---------------------------------------------------------------- 模块 → 物体
pieces = {}
for c in root.children:
    obs = [o for o in c.all_objects if o.type == 'MESH']
    if c.name == 'Export':
        for o in obs:
            pieces[o.name.replace('KB3D_HTS_', '').replace('_Main', '')] = [o]
    elif obs:
        parts = sorted(o.name.replace('KB3D_HTS_', '').split('_') for o in obs)[0]
        pieces['_'.join(parts[:2])] = obs
# 原来那一堆叠在原点的模块不渲染
for lc in bpy.context.view_layer.layer_collection.children:
    lc.exclude = True

made = {}
# 远景高楼（k051_backdrop_export.py 从 K005 导出的 backdrop.fbx）：有用到才导入
if any(it['piece'].startswith('TPB_') for it in L['items']):
    bd = os.path.join(os.path.dirname(LAYOUT), 'backdrop', 'backdrop.fbx')
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=bd)
    for o in set(bpy.data.objects) - before:
        if o.type == 'MESH' and o.name.startswith('SM_TPB_'):
            o.matrix_world = o.matrix_world.Identity(4) if False else o.matrix_world
            pieces[o.name.replace('SM_', '')] = [o]
            for c in list(o.users_collection):
                c.objects.unlink(o)
    for m in bpy.data.materials:
        if m.name.startswith('TPB_'):
            m.diffuse_color = {'TPB_Panel': (0.8, 0.81, 0.83, 1), 'TPB_Glass': (0.05, 0.12, 0.15, 1), 'TPB_Dark': (0.12, 0.12, 0.13, 1)}[m.name[:9] if m.name[:9] in ('TPB_Panel', 'TPB_Glass') else 'TPB_Dark']


def piece_coll(it):
    key = it['piece'] + '|' + ','.join(it.get('skip', [])) + '|' + ','.join(it.get('only', []))
    if key in made:
        return made[key]
    obs = pieces[it['piece']]
    if it.get('skip'):
        obs = [o for o in obs if not any(o.name.endswith('_' + s) for s in it['skip'])]
    if it.get('only'):
        obs = [o for o in obs if any(o.name.endswith('_' + s) for s in it['only'])]
    c = bpy.data.collections.new('P_' + key.replace('|', '_'))
    for o in obs:
        c.objects.link(o)
    made[key] = c
    return c


inst = bpy.data.collections.new('Layout')
sc.collection.children.link(inst)
for it in L['items']:
    if it['piece'].startswith('BRAND_'):   # 标识薄板预览里不摆
        continue
    e = bpy.data.objects.new(it['piece'], None)
    e.instance_type = 'COLLECTION'
    e.instance_collection = piece_coll(it)
    e.location = it['loc']
    e.rotation_euler = Euler((0, 0, math.radians(it['yaw'])))
    e.scale = it['scale']
    inst.objects.link(e)


def mat(name, rgb):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*rgb, 1)
    return m


def box(name, center, size, m):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    o.data.materials.append(m)
    o.color = m.diffuse_color
    for c in o.users_collection:
        c.objects.unlink(o)
    inst.objects.link(o)
    return o


bx, by = L['bound']
box('Ground', (0, 0, -0.25), (bx * 2 + 120, by * 2 + 120, 0.5), mat('G', (0.36, 0.37, 0.39)))
box('Plaza', (0, 0, -0.02), (bx * 2, by * 2, 0.06), mat('P', (0.5, 0.52, 0.55)))
red = mat('Bound', (0.9, 0.1, 0.1))
for sx, sy, w, h in ((0, by, bx * 2, 0.6), (0, -by, bx * 2, 0.6), (bx, 0, 0.6, by * 2), (-bx, 0, 0.6, by * 2)):
    box('Wall', (sx, sy, 0.3), (w, h, 0.6), red)
cols = {'team0': mat('T0', (0.1, 0.35, 1.0)), 'team1': mat('T1', (1.0, 0.15, 0.1)), 'dm': mat('DM', (0.1, 0.9, 0.3))}
for k, lst in L['spawns'].items():
    for x, y, z, yaw in lst:
        bpy.ops.mesh.primitive_cylinder_add(radius=0.9, depth=2.4, location=(x, y, z - 1.6 + 1.2))
        o = bpy.context.active_object
        o.data.materials.append(cols[k])
        o.color = cols[k].diffuse_color
        for c in o.users_collection:
            c.objects.unlink(o)
        inst.objects.link(o)

# ---------------------------------------------------------------- Workbench
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_shadows = True
sh.show_cavity = True
sh.cavity_type = 'BOTH'
sc.display.light_direction = (0.45, -0.35, 0.82)
sc.display.shadow_shift = 0.05
try:
    sh.background_type = 'VIEWPORT'
    sh.background_color = (0.62, 0.68, 0.75)
except Exception:
    pass
cd = bpy.data.cameras.new('Cam')
cam = bpy.data.objects.new('Cam', cd)
sc.collection.objects.link(cam)
sc.camera = cam
cd.clip_start, cd.clip_end = 0.3, 4000


def shot(name, loc, target=None, rot=None, ortho=None, lens=24, res=(1920, 1080)):
    if ONLY and name not in ONLY and not any(name.startswith(o) for o in ONLY):
        return
    cam.location = loc
    cam.rotation_euler = rot if rot is not None else (Vector(target) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    if ortho:
        cd.type = 'ORTHO'
        cd.ortho_scale = ortho
    else:
        cd.type = 'PERSP'
        cd.lens = lens
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.filepath = f'{OUT}/{name}.png'
    bpy.ops.render.render(write_still=True)
    print('[preview]', name)


VX = bx + 18                         # 俯视图半宽（米），布局图标注用同一个数
VY = VX * 0.75
shot('top', (0, 0, 300), rot=(0, 0, 0), ortho=VX * 2, res=(2400, 1800))
for k, (dx, dy) in enumerate(((1, -1), (-1, -1), (-1, 1), (1, 1))):
    shot(f'obl_{k}', (dx * 150, dy * 125, 110), target=(0, 0, 0), lens=26)
eyes = [('eye_base_w', (-80, 0, 10.5), (40, 0, 6)), ('eye_base_e', (80, 0, 10.5), (-40, 0, 6)),
        ('eye_center', (0, -8, 3), (0, 60, 8)), ('eye_deck_n', (-38, 49, 10.5), (40, 49, 9)),
        ('eye_ne_corner', (100, 66, 3), (40, 20, 6)), ('eye_sw_corner', (-100, -66, 3), (-30, -20, 6)),
        ('eye_south', (60, -72, 3), (-40, -60, 12)), ('eye_under', (-30, 49, 3), (30, 49, 3))]
for name, p, t in eyes:
    shot(name, p, target=t, lens=18)
json.dump({'view_half_x': VX, 'view_half_y': VY, 'res': [2400, 1800]}, open(f'{OUT}/top_view.json', 'w'))
