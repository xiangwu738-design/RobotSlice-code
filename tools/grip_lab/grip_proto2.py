"""握姿原型 v2：标准握法（柄穿过手心空腔）。刀：右手按 BladeDir 摆、双手刀左手同样；枪：右手照握把、长枪左手托护木。
blender -b <SKM_Robot_v3.blend> -P grip_proto2.py -- <输出目录> [案例名 ...]"""
import bpy
import sys
import math
import os
from mathutils import Vector, Matrix

exec(open(os.path.join(os.path.dirname(__file__), 'grip_core.py'), encoding='utf-8').read())

args = sys.argv[sys.argv.index('--') + 1:]
OUT = args[0]
ONLY = args[1:]
os.makedirs(OUT, exist_ok=True)
ROOT = 'D:/RobotSlice'

cam = setup()
for case in CASES:
    name = case['name']
    if ONLY and not any(name.startswith(o) for o in ONLY):
        continue
    s = case['side']
    w = load_weapon(f"{ROOT}/SourceAssets/{case['fbx']}.fbx")
    H, k, n, d = hand_frame_rest(s)
    Mw, shape = place_canonical(s, case)
    w.matrix_world = Mw
    mats, log = solve(s, shape, trigger=case.get('trigger', False))
    apply_pose(mats)
    print(f'[proto2] {name} {log}')
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.6, location=LAST_TARGET)
    mk = bpy.context.active_object
    tgt = H + d * 6.5 + n * 2.5
    shoot(cam, tgt, -n * 0.35 + k * 0.7 + Vector((0, 0, 0.35)), 60, f'{OUT}/{name}_a.png')
    shoot(cam, tgt, n * 0.9 - k * 0.3 + d * 0.15, 60, f'{OUT}/{name}_b.png')
    shoot(cam, tgt, -k * 1.0 + n * 0.25, 60, f'{OUT}/{name}_c.png')
    bpy.data.objects.remove(w, do_unlink=True)
    bpy.data.objects.remove(mk, do_unlink=True)
