"""握把探针：给握把中心和「往上」方向（模型 XZ 平面，UE 坐标 = Blender 导入坐标 ×100、y 取反），从中心往外打射线量截面。
沿轴每隔 1.5 厘米一截：前后半宽（垂直于轴、在 XZ 里）、左右半宽（Y），以及截面中心偏移。
blender -b --factory-startup -P grip_probe.py -- fbx cx,cz upx,upz [fbx cx,cz upx,upz ...]"""
import bpy
import sys
import math
from mathutils import Vector
from mathutils.bvhtree import BVHTree

a = sys.argv[sys.argv.index('--') + 1:]


def load(path):
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.ops.import_scene.fbx(filepath=path)
    ob = next(o for o in bpy.data.objects if o.type == 'MESH')
    m = ob.matrix_world
    verts = [(m @ v.co) * 100 for v in ob.data.vertices]
    verts = [Vector((p.x, -p.y, p.z)) for p in verts]   # UE 模型坐标
    polys = [tuple(p.vertices) for p in ob.data.polygons]
    return BVHTree.FromPolygons(verts, polys)


def inside_dist(bvh, o, d):
    """从 o 沿 d 打到第一个面的距离（o 在实体里面时就是到表面的距离）。"""
    h = bvh.ray_cast(o, d, 50)
    return h[3] if h[0] is not None else None


for i in range(0, len(a), 3):
    f, c, up = a[i], [float(v) for v in a[i + 1].split(',')], [float(v) for v in a[i + 2].split(',')]
    bvh = load(f)
    C = Vector((c[0], 0, c[1]))
    U = Vector((up[0], 0, up[1])).normalized()
    P = Vector((U.z, 0, -U.x))   # 垂直于轴、在 XZ 里（往前下）
    Y = Vector((0, 1, 0))
    rows = []
    for s in [x * 1.5 for x in range(-6, 7)]:
        o = C + U * s
        f1, b1 = inside_dist(bvh, o, P), inside_dist(bvh, o, -P)
        l1, r1 = inside_dist(bvh, o, Y), inside_dist(bvh, o, -Y)
        if None in (f1, b1, l1, r1):
            rows.append(f'{s:+.1f}:空')
            continue
        rows.append(f'{s:+.1f}:前{f1:.1f}/后{b1:.1f} 左右{l1:.1f}/{r1:.1f}')
    up_len = inside_dist(bvh, C, U)
    dn_len = inside_dist(bvh, C, -U)
    print(f'[probe] {f.rsplit("/", 1)[-1]} 心({c[0]},{c[1]}) 上({U.x:.2f},{U.z:.2f}) 往上到面 {up_len} 往下到面 {dn_len} | ' + ' | '.join(rows))
