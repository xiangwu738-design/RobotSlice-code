"""K051：量每块路面模块的接口（桥面顶面在四个方向最外侧的那排顶点的范围），拼路网用。
blender -b K051.blend -P tools/k051_road_ends.py"""
import bpy
for o in bpy.data.objects:
    if o.type != 'MESH' or not o.name.startswith('KB3D_HTS_Road_') or not o.name.endswith('_Main'):
        continue
    vs = [o.matrix_world @ v.co for v in o.data.vertices]
    top = max(v.z for v in vs)
    tv = [v for v in vs if v.z > top - 0.15]
    out = []
    for ax, sgn, name in ((0, 1, '+X'), (0, -1, '-X'), (1, 1, '+Y'), (1, -1, '-Y')):
        ext = max(sgn * v[ax] for v in tv)
        cap = [v for v in tv if sgn * v[ax] > ext - 0.08]
        other = 1 - ax
        out.append(f'{name}@{sgn*ext:.2f}[{min(v[other] for v in cap):.2f},{max(v[other] for v in cap):.2f}]')
    print('[ends]', o.name.replace('KB3D_HTS_', ''), 'top', round(top, 2), ' '.join(out))
