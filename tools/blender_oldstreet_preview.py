"""老旧街道：Workbench 看原包（只读，不存盘）。隐藏人物、遮光棚、灯光组和大地板，出俯视正交和几张透视。
blender -b Inbox/_oldstreet/老旧街道.blend -P tools/blender_oldstreet_preview.py -- 输出目录
"""
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
HIDE_COLLS = {'WGTS_rig', 'WGTS_rig.001', '人物层', '人物层.001', '场景灯光组', '灯光集合1', '灯光集合2', 'PoleParts'}
HIDE_OBJS = {'地面.001', '建筑.002'}
for o in sc.objects:
    if set(c.name for c in o.users_collection) & HIDE_COLLS or o.name in HIDE_OBJS:
        o.hide_render = True
        o.hide_viewport = True
sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading
sh.light = 'STUDIO'
sh.color_type = 'TEXTURE'
sh.show_shadows = False
sh.show_cavity = False
sc.render.film_transparent = False
sc.world = sc.world or bpy.data.worlds.new('w')


def shoot(name, loc, rot=None, target=None, lens=26, ortho=None, res=(1280, 720)):
    cam = bpy.data.cameras.new(name)
    ob = bpy.data.objects.new(name, cam)
    sc.collection.objects.link(ob)
    ob.location = loc
    if target is not None:
        d = Vector(target) - Vector(loc)
        ob.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    else:
        ob.rotation_euler = Euler(rot)
    cam.lens = lens
    cam.clip_end = 3000
    if ortho:
        cam.type = 'ORTHO'
        cam.ortho_scale = ortho
    sc.camera = ob
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.filepath = os.path.join(OUT, name + '.png')
    bpy.ops.render.render(write_still=True)
    print('[oldstreet] 拍', name)


# 俯视：范围 x -70..70，y -30..100
shoot('top', (0, 35, 200), rot=(0, 0, 0), ortho=140, res=(1400, 1400))
shoot('street_n', (0, -6, 2), target=(0, 60, 1.5), lens=24)
shoot('street_n2', (0, 25, 2), target=(0, 60, 1.5), lens=24)
shoot('street_s', (0, 58, 2), target=(0, 0, 1.5), lens=24)
shoot('cross_e', (0, -11, 2), target=(60, -11, 1.5), lens=24)
shoot('cross_w', (0, -11, 2), target=(-60, -11, 1.5), lens=24)
shoot('high', (-18, -25, 25), target=(2, 25, 0), lens=24)
shoot('high2', (18, 80, 25), target=(0, 20, 0), lens=24)
