"""K62：从导出的 FBX 算每栋楼 Solid 在机体高度上的「挡不挡」图（0.5 米一格）：横向短射线打墙。楼是空壳，竖直射线看不出墙，所以用这个。
blender -b --factory-startup -P tools/k62_blocked.py -- Inbox/_k62/export
写 heights/<组>_b.npz：top（屋顶高）、b1（1.2 米高有墙）、b2（2.4 米高有墙）、b3（6 米高有墙）。
"""
import glob
import os
import sys

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
DIRS = [Vector((1, 0, 0)), Vector((-1, 0, 0)), Vector((0, 1, 0)), Vector((0, -1, 0)), Vector((0.707, 0.707, 0)), Vector((-0.707, -0.707, 0)), Vector((0.707, -0.707, 0)), Vector((-0.707, 0.707, 0))]
cell = 0.5
for f in sorted(glob.glob(f'{OUT}/fbx/SV_*.fbx')):
    name = os.path.basename(f)[3:-4]
    if name == 'props':
        continue
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f)
    ob = next((o for o in bpy.context.scene.objects if o.name.endswith('_Solid')), None)
    me = ob.data
    me.transform(ob.matrix_world)
    co = np.zeros(len(me.vertices) * 3, dtype=np.float64)
    me.vertices.foreach_get('co', co)
    co = co.reshape(-1, 3)
    lo, hi = co.min(0), co.max(0)
    polys = [tuple(p.vertices) for p in me.polygons]
    tree = BVHTree.FromPolygons(co.tolist(), polys, all_triangles=False)
    nx, ny = int((hi[0] - lo[0]) / cell) + 2, int((hi[1] - lo[1]) / cell) + 2
    top = np.zeros((ny, nx), dtype=np.float32)
    bs = [np.zeros((ny, nx), dtype=bool) for _ in range(3)]
    for j in range(ny):
        y = float(lo[1]) + (j + 0.5) * cell
        for i in range(nx):
            x = float(lo[0]) + (i + 0.5) * cell
            r = tree.ray_cast(Vector((x, y, 400.0)), Vector((0, 0, -1)), 500.0)
            if r[0] is None:
                continue
            top[j, i] = r[0].z
            for k, z in enumerate((1.2, 2.4, 6.0)):
                o = Vector((x, y, z))
                for d in DIRS:
                    if tree.ray_cast(o, d, 0.38)[0] is not None:
                        bs[k][j, i] = True
                        break
    np.savez_compressed(f'{OUT}/heights/{name}_b.npz', top=top, b1=bs[0], b2=bs[1], b3=bs[2], x0=float(lo[0]), y0=float(lo[1]), cell=cell, minx=float(lo[0]), maxx=float(hi[0]), miny=float(lo[1]), maxy=float(hi[1]), maxz=float(hi[2]))
    print('[blocked]', name, f'{hi[0]-lo[0]:.1f}x{hi[1]-lo[1]:.1f}x{hi[2]-lo[2]:.1f}', 'b1 占比', round(float(bs[0].mean()), 3), flush=True)
print('[blocked] 完成', flush=True)
