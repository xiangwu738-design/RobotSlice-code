"""枪的握把：侧视剪影（沿 Y 打射线）在原点附近的 ASCII 图，逐行找包含握把中心的那一段，拟合握把轴线。
单位厘米（人形尺寸）。blender -b --factory-startup -P gun_grip.py -- <fbx> ..."""
import bpy
import sys
import math
from mathutils import Vector
from mathutils.bvhtree import BVHTree

files = sys.argv[sys.argv.index('--') + 1:]
STEP = 0.5


def load(path):
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=path)
    ob = next(o for o in bpy.data.objects if o.type == 'MESH')
    dg = bpy.context.evaluated_depsgraph_get()
    m = ob.matrix_world
    me = ob.evaluated_get(dg).to_mesh()
    verts = [m @ v.co * 100 for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    return BVHTree.FromPolygons(verts, polys), verts


for f in files:
    bvh, V = load(f)
    name = f.replace('\\', '/').rsplit('/', 1)[-1]
    xs = [round(-16 + i * STEP, 2) for i in range(int(32 / STEP) + 1)]
    zs = [round(14 - i * STEP, 2) for i in range(int(32 / STEP) + 1)]

    def occ(x, z):
        h = bvh.ray_cast(Vector((x, -30, z)), Vector((0, 1, 0)), 60)
        return h[0] is not None

    def width(x, z):
        a = bvh.ray_cast(Vector((x, -30, z)), Vector((0, 1, 0)), 60)
        b = bvh.ray_cast(Vector((x, 30, z)), Vector((0, -1, 0)), 60)
        if a[0] is None or b[0] is None:
            return None
        return a[0].y, b[0].y

    grid = {(x, z): occ(x, z) for x in xs for z in zs}
    print(f'[g] {name}  (每格 {STEP} 厘米，列 x -16..16，行 z 14..-18，+ 是原点)')
    for z in zs[::2]:
        line = ''.join('+' if abs(x) < 0.01 and abs(z) < 0.01 else ('#' if grid[(x, z)] else '.') for x in xs[::1])
        print(f'[g] {z:6.1f} {line}')
    # 每行包含 x=0 附近的连续段
    rows = []
    for z in zs:
        if not grid.get((0.0, z)):
            continue
        i0 = xs.index(0.0)
        a = i0
        while a > 0 and grid[(xs[a - 1], z)]:
            a -= 1
        b = i0
        while b < len(xs) - 1 and grid[(xs[b + 1], z)]:
            b += 1
        w = width(0.0, z)
        rows.append((z, xs[a], xs[b], w))
    for z, a, b, w in rows:
        if -12 <= z <= 6:
            print(f'[g]   z={z:5.1f} 段 x {a:6.1f} ~ {b:6.1f}（长 {b - a:4.1f}，中 {(a + b) / 2:5.1f}）  y {w[0]:.1f}~{w[1]:.1f}' if w else f'[g]   z={z:5.1f} 段 {a}~{b}')
