"""按 plan.json 在 Blender 里把整张城市图摆出来，出 Workbench 预览（俯视 + 视平线）；可选另存 .blend 方便手动看。
blender -b "Inbox/K059 _城市街道.blend" -P tools/blender_k059_preview.py -- 输出目录 [--save 场景.blend] [--only top|eye]
只读原包：K005 的楼按需从原包追加进来，原 .blend 不保存。"""
import json
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

args = sys.argv[sys.argv.index('--') + 1:]
OUT = os.path.abspath(args[0])
SAVE = args[args.index('--save') + 1] if '--save' in args else None
ONLY = args[args.index('--only') + 1] if '--only' in args else ''
ROOT = 'D:/RobotSlice'
K005 = f'{ROOT}/Inbox/K005——摩天大楼建筑物城市街道楼房.blend'
plan = json.load(open(f'{ROOT}/Inbox/_k059/plan.json', encoding='utf-8'))
os.makedirs(OUT, exist_ok=True)
try:
    bpy.context.preferences.system.gl_texture_limit = 'CLAMP_512'
except Exception:
    pass

src_scene = bpy.data.scenes['KB3D_CityStreets-Native']
view = bpy.data.scenes.new('city_preview')
if bpy.context.window:
    bpy.context.window.scene = view
root = bpy.data.collections.new('CityPreview')
view.collection.children.link(root)

# ---- K059：小品组去掉人行道板；单件道具一件一个集合（原点挪到底面中心）
exp = {c.name: c for c in bpy.data.collections if c.name.startswith('Export ')}
trash = bpy.data.collections.new('removed_sidewalks')
for k in range(2, 54):
    c = exp.get(f'Export {k}')
    for o in list(c.objects):
        if 'SideWalk' in o.name:
            c.objects.unlink(o)
            trash.objects.link(o)
props = {}
for o in exp['Export 1'].all_objects:
    if o.type != 'MESH':
        continue
    short = o.name.replace('KB3D_CTS_', '').replace('_Main', '')
    pc = bpy.data.collections.new('P_' + short)
    pc.objects.link(o)
    ws = [o.matrix_world @ Vector(b) for b in o.bound_box]
    pc.instance_offset = (sum(w.x for w in ws) / 8, sum(w.y for w in ws) / 8, min(w.z for w in ws))
    props[short] = pc


def find_prop(name):
    if name in props:
        return props[name]
    hits = [v for k, v in props.items() if k.startswith(name)]
    return hits[0] if hits else None


# ---- K005：按需追加
need = sorted({i['id'] for i in plan['items'] if i['kind'] == 'bldg'})
with bpy.data.libraries.load(K005, link=False) as (src, dst):
    dst.collections = [f'Collection {n}' for n in need if f'Collection {n}' in src.collections]
b005 = {}
for c in dst.collections:
    if c:
        b005[int(c.name.split()[1].split('.')[0])] = c
print('[preview] K005 楼', sorted(b005))

# ---- 方块材质（Workbench 用视口颜色）
COL = {'asphalt': (0.11, 0.115, 0.125), 'sidewalk': (0.42, 0.42, 0.41), 'concrete': (0.5, 0.5, 0.52), 'gravel': (0.36, 0.32, 0.26),
       'plaza': (0.55, 0.54, 0.5), 'paint': (0.9, 0.9, 0.9), 'paint_y': (0.85, 0.7, 0.2), 'concrete_dark': (0.3, 0.3, 0.32),
       'container': (0.15, 0.32, 0.5), 'container_b': (0.55, 0.25, 0.12), 'steel': (0.6, 0.63, 0.66), 'steel_dark': (0.2, 0.22, 0.25),
       'glass': (0.3, 0.45, 0.55), 'train': (0.75, 0.78, 0.8), 'car': (0.25, 0.27, 0.3), 'Team0': (0.1, 0.4, 1), 'Team1': (1, 0.15, 0.1),
       'DM': (1, 0.9, 0.1), 'sign': (0.9, 0.2, 0.8)}
mats = {}


def mat(key):
    if key not in mats:
        m = bpy.data.materials.new('pv_' + key)
        m.diffuse_color = (*COL.get(key, (1, 0, 1)), 1)
        mats[key] = m
    return mats[key]


cube_me = None


def add_box(name, c, s, yaw, key):
    global cube_me
    if cube_me is None:
        bpy.ops.mesh.primitive_cube_add(size=1)
        tmp = bpy.context.active_object
        cube_me = tmp.data
        bpy.data.objects.remove(tmp)
    me = cube_me.copy()
    me.materials.append(mat(key))
    o = bpy.data.objects.new(name, me)
    o.location = c
    o.scale = s
    o.rotation_euler = (0, 0, math.radians(yaw))
    root.objects.link(o)
    return o


def add_inst(name, coll, loc, yaw, scale=1.0):
    o = bpy.data.objects.new(name, None)
    o.instance_type = 'COLLECTION'
    o.instance_collection = coll
    o.location = loc
    o.rotation_euler = (0, 0, math.radians(yaw))
    o.scale = (scale,) * 3
    root.objects.link(o)
    return o


S = plan['arena']['k005_scale']
missing = []
for n, it in enumerate(plan['items']):
    k = it['kind']
    if k == 'box':
        add_box(it['name'], it['c'], it['s'], it['yaw'], it['mat'])
    elif k == 'bldg':
        c = b005.get(it['id'])
        if c:
            add_inst(f'B{it["id"]}_{n}', c, (it['x'], it['y'], it['z']), it['yaw'], S)
        else:
            missing.append(f'C{it["id"]}')
    elif k == 'vig':
        add_inst(f'V{it["id"]}_{n}', exp[f'Export {it["id"]}'], (it['x'], it['y'], it['z']), it['yaw'])
    elif k == 'prop':
        if '/' in it['id']:   # UE 里现成的盖布车：这里用方块代替
            add_box(f'car_{n}', (it['x'], it['y'], it['z'] + 0.75), (4.6, 1.9, 1.5), it['yaw'] + 90, 'car')
            continue
        pc = find_prop(it['id'])
        if pc:
            add_inst(f'P_{n}', pc, (it['x'], it['y'], it['z']), it['yaw'], it.get('scale', 1.0))
        else:
            missing.append(it['id'])
    elif k == 'spawn':
        o = add_box(f'spawn_{n}', (it['x'], it['y'], it['z'] + 1.25), (1.6, 1.6, 2.5), it['yaw'], it['tag'])
    elif k == 'sign':
        yaw = math.radians(it['yaw'])
        nrm = Vector((math.sin(yaw), -math.cos(yaw), 0))
        add_box(f'sign_{n}', Vector((it['x'], it['y'], it['z'] + it['h'] / 2)) + nrm * 0.05, (it['w'], 0.1, it['h']), it['yaw'], 'sign')
print('[preview] 缺的', missing, flush=True)
import time
T0 = time.time()
# 场地边界：细红线框
A = plan['arena']
for nm, c, s in (('wN', (0, A['half'][1], 0.3), (2 * A['half'][0], 0.4, 0.6)), ('wS', (0, -A['half'][1], 0.3), (2 * A['half'][0], 0.4, 0.6)),
                 ('wE', (A['half'][0], 0, 0.3), (0.4, 2 * A['half'][1], 0.6)), ('wW', (-A['half'][0], 0, 0.3), (0.4, 2 * A['half'][1], 0.6))):
    add_box(nm, c, s, 0, 'Team1')

# ---- 渲染设置
view.render.engine = 'BLENDER_WORKBENCH'
sh = view.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_cavity = True
sh.cavity_type = 'WORLD'
sh.show_shadows = '--shadows' in args
sh.shadow_intensity = 0.45
view.display.light_direction = (0.45, -0.35, 0.82)
sh.background_type = 'VIEWPORT'
sh.background_color = (0.62, 0.66, 0.72)
view.render.image_settings.file_format = 'PNG'
cam = bpy.data.objects.new('pv_cam', bpy.data.cameras.new('pv_cam'))
view.collection.objects.link(cam)
view.camera = cam
cam.data.clip_start, cam.data.clip_end = 0.1, 3000


def shot(name, loc, look, lens=24.0, ortho=None, res=(1920, 1080)):
    if ortho:
        cam.data.type = 'ORTHO'
        cam.data.ortho_scale = ortho
    else:
        cam.data.type = 'PERSP'
        cam.data.lens = lens
    cam.location = loc
    cam.rotation_euler = (Vector(look) - Vector(loc)).to_track_quat('-Z', 'Y').to_euler()
    view.render.resolution_x, view.render.resolution_y = res
    view.render.filepath = os.path.join(OUT, name + '.png')
    bpy.ops.render.render(write_still=True, scene=view.name)
    print('[preview] 出图', name, f'{time.time() - T0:.0f}s', flush=True)


if ONLY in ('', 'top'):
    cam.rotation_mode = 'XYZ'
    cam.data.type = 'ORTHO'
    cam.data.ortho_scale = 2 * A['half'][0] + 30
    cam.location = (0, 0, 400)
    cam.rotation_euler = (0, 0, 0)
    view.render.resolution_x, view.render.resolution_y = 1800, int(1800 * (2 * A['half'][1] + 30) / (2 * A['half'][0] + 30))
    view.render.filepath = os.path.join(OUT, 'top.png')
    bpy.ops.render.render(write_still=True, scene=view.name)
    print('[preview] 出图 top', cam.data.ortho_scale, f'{time.time() - T0:.0f}s', flush=True)
if ONLY in ('', 'eye'):
    E = 3.2   # 机体镜头高度（2.5 米机体，镜头在肩后上方）
    shot('eye_W_spawn', (-95, 0, E + 1), (0, 0, 4))
    shot('eye_E_spawn', (95, 2, E + 1), (0, 0, 4))
    shot('eye_plaza', (10, -70, E), (-10, 0, 6))
    shot('eye_site', (-25, 85, E + 2), (0, 40, 5))
    shot('eye_viaduct', (45, -80, 11 + E), (45, 40, 12))
    shot('eye_stW', (-45, -85, E), (-45, 60, 6))
    shot('high_overview', (-140, -150, 120), (0, 0, 0), lens=28)
    shot('roof_NW', (-90, 30, 14 + E), (20, -10, 8))

if SAVE:
    for s in list(bpy.data.scenes):
        if s != view:
            pass
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(SAVE), copy=True)
    print('[preview] 存了', SAVE)
