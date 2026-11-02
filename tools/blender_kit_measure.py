"""11-02 去组件：量组件零件（只读）。blender -b --factory-startup -P tools/blender_kit_measure.py -- 输出.json
照 ds_common / robot_lookdev 的 kit_source 一样烘进零件自己的主轴坐标系（u 长度方向、v 次方向、w = u×v，中心在原点），然后量：
包围盒三向尺寸；沿 u 分 48 段每段的最大半径（v-w 平面）和截面中心偏移（软管看弯）；连通块数；三角面数；材质名。
自己的替代件按这些「功能尺寸」（长、粗细、缸筒 / 活塞杆分界、弯曲摆幅）从零建，不复制原件网格。"""
import json
import sys

import bpy
import numpy as np

out = sys.argv[sys.argv.index('--') + 1]
LIB = {'机甲2': r'D:/RobotSlice/Inbox/机甲组件5000+/机甲2.blend', '机械零件': r'D:/RobotSlice/Inbox/机甲组件5000+/机械零件.blend',
       '机甲': r'D:/RobotSlice/Inbox/机甲组件5000+/机甲.blend'}
PARTS = [('Hydraulics_T1_02', '机甲2', ((0, 1, 0), (1, 0, 0))), ('Hydraulics_T1_06', '机甲2', ((0, 1, 0), (1, 0, 0))),
         ('Hydraulics_T2_02', '机甲2', ((0, 1, 0), (1, 0, 0))), ('Hydraulics_T2_06', '机甲2', ((0, 1, 0), (1, 0, 0))),
         ('Hydraulics_T2_11', '机甲2', ((0, 1, 0), (1, 0, 0))), ('Pipes_02', '机甲2', None), ('Pipes_07', '机甲2', None),
         ('Pipes_11', '机甲2', None), ('BodyKit_T3_09', '机甲2', None), ('hutch_01', '机械零件', None), ('hutch_03', '机械零件', None)]
res = {}
for name, lib, axes in PARTS:
    try:
        with bpy.data.libraries.load(LIB[lib], link=False) as (src, dst):
            if name not in src.objects:
                res[name] = {'error': 'not in library'}
                continue
            dst.objects = [name]
        ob = dst.objects[0]
        me = ob.data.copy()
        mw, p = ob.matrix_basis.copy(), ob.parent
        while p is not None:
            mw = p.matrix_basis @ mw
            p = p.parent
        me.transform(mw)
        co = np.empty(len(me.vertices) * 3)
        me.vertices.foreach_get('co', co)
        co = co.reshape(-1, 3)
        if axes is None:
            c = co.mean(0)
            a = co[np.argmax(((co - c) ** 2).sum(1))]
            b = co[np.argmax(((co - a) ** 2).sum(1))]
            u = (b - a) / np.linalg.norm(b - a)
            rest = (co - c) - np.outer((co - c) @ u, u)
            v = np.linalg.eigh(rest.T @ rest)[1][:, -1]
            v = v - u * (u @ v)
            v /= np.linalg.norm(v)
            center = (a + b) / 2
        else:
            u, v = np.array(axes[0], float), np.array(axes[1], float)
            center = (co.min(0) + co.max(0)) / 2
        w = np.cross(u, v)
        L = (co - center) @ np.stack([u, v, w]).T
        lo, hi = L.min(0), L.max(0)
        n = 48
        edges = np.linspace(lo[0], hi[0], n + 1)
        prof = []
        for i in range(n):
            m = (L[:, 0] >= edges[i]) & (L[:, 0] <= edges[i + 1])
            if not m.any():
                prof.append(None)
                continue
            s = L[m]
            cv, cw = (s[:, 1].min() + s[:, 1].max()) / 2, (s[:, 2].min() + s[:, 2].max()) / 2
            r = float(np.sqrt((s[:, 1] - cv) ** 2 + (s[:, 2] - cw) ** 2).max())
            prof.append([round(float(edges[i] - lo[0]), 4), round(r, 4), round(float(cv), 4), round(float(cw), 4),
                         round(float(s[:, 1].max() - s[:, 1].min()), 4), round(float(s[:, 2].max() - s[:, 2].min()), 4)])
        tris = sum(len(pp.vertices) - 2 for pp in me.polygons)
        res[name] = {'lib': lib, 'axes': axes, 'size_uvw': [round(float(x), 4) for x in hi - lo], 'tris': tris,
                     'materials': [m.name for m in me.materials if m], 'obj_scale': [round(x, 4) for x in ob.scale],
                     'children': [c.name for c in ob.children][:10], 'profile': prof}
        bpy.data.objects.remove(ob)
    except Exception as e:
        res[name] = {'error': repr(e)}
json.dump(res, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
