"""护木截面：在 x 处从正下方往上打到护木底面，再从底面往里 1.5 厘米处往左右、往上量。UE 模型坐标（厘米）。
blender -b --factory-startup -P fore_probe.py -- fbx x1,x2,... [fbx xs ...]"""
import bpy
import sys
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index('--') + 1:]
for i in range(0, len(a), 2):
    f, xs = a[i], [float(v) for v in a[i + 1].split(',')]
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=f)
    ob = next(o for o in bpy.data.objects if o.type == 'MESH')
    m = ob.matrix_world
    verts = [Vector(((m @ v.co).x * 100, -(m @ v.co).y * 100, (m @ v.co).z * 100)) for v in ob.data.vertices]
    bvh = BVHTree.FromPolygons(verts, [tuple(p.vertices) for p in ob.data.polygons])
    rows = []
    for x in xs:
        h = bvh.ray_cast(Vector((x, 0, -40)), Vector((0, 0, 1)), 80)
        if h[0] is None:
            rows.append(f'x{x}:空')
            continue
        zb = h[0].z
        o = Vector((x, 0, zb + 1.5))
        side = [bvh.ray_cast(o, Vector((0, s, 0)), 30) for s in (1, -1)]
        up = bvh.ray_cast(o, Vector((0, 0, 1)), 30)
        # 外轮廓宽：从外面往里打
        wl = bvh.ray_cast(Vector((x, 30, zb + 1.5)), Vector((0, -1, 0)), 60)
        wr = bvh.ray_cast(Vector((x, -30, zb + 1.5)), Vector((0, 1, 0)), 60)
        rows.append(f'x{x}: 底 z{zb:.1f} 外宽 y {wr[0].y if wr[0] else 0:.1f}~{wl[0].y if wl[0] else 0:.1f}  内到面 左右 {side[0][3] if side[0][0] else -1:.1f}/{side[1][3] if side[1][0] else -1:.1f} 上 {up[3] if up[0] else -1:.1f}')
    print(f'[fore] {f.rsplit("/", 1)[-1]} ' + ' | '.join(rows))
