"""刀柄截面：沿 X 每 1 厘米，从柄心往外 8 个方向打射线量半径（先用附近顶点估柄心）。单位厘米（人形尺寸）。"""
import bpy
import sys
import math
from mathutils import Vector
from mathutils.bvhtree import BVHTree

files = sys.argv[sys.argv.index('--') + 1:]


def load(path):
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=path)
    ob = next(o for o in bpy.data.objects if o.type == 'MESH')
    m = ob.matrix_world
    me = ob.data
    verts = [m @ v.co * 100 for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    return BVHTree.FromPolygons(verts, polys), verts


for f in files:
    bvh, V = load(f)
    name = f.replace('\\', '/').rsplit('/', 1)[-1]
    print(f'[b] {name}')
    for xi in range(-36, 14, 2):
        S = [p for p in V if abs(p.x - xi) < 0.6]
        c = Vector((0, 0))
        for _ in range(3):
            T = [p for p in S if math.hypot(p.y - c.x, p.z - c.y) < 4.0]
            if not T:
                break
            c = Vector(((min(p.y for p in T) + max(p.y for p in T)) / 2, (min(p.z for p in T) + max(p.z for p in T)) / 2))
        if not S:
            continue
        o = Vector((xi, c.x, c.y))
        rs = []
        for a in range(0, 360, 45):
            dv = Vector((0, math.cos(math.radians(a)), math.sin(math.radians(a))))
            h = bvh.ray_cast(o, dv, 20)
            rs.append(h[3] if h[0] is not None else -1)
        # rs: +y, +y+z, +z, -y+z, -y, -y-z, -z, +y-z
        print(f'[b]   x={xi:4d} 心 y{c.x:5.2f} z{c.y:5.2f}  半径 +y{rs[0]:4.1f} +z{rs[2]:4.1f} -y{rs[4]:4.1f} -z{rs[6]:4.1f}  斜 {rs[1]:4.1f} {rs[3]:4.1f} {rs[5]:4.1f} {rs[7]:4.1f}')
