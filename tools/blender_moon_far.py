"""月球基地远景：整张 Apollo 17 高程图（16 米一格，高度和近处一样放大）做成一个网格，中间 Landscape 盖着的地方挖掉。真实数据，不是程序地形。
blender -b --factory-startup -P tools/blender_moon_far.py -- Inbox/_moon_nasa/work/far.npz 输出.fbx
Blender 坐标 (x = UE X, y = -UE Y)，原点 = 大穹顶中心。"""
import sys
import bpy
import numpy as np

args = sys.argv[sys.argv.index('--') + 1:]
d = np.load(args[0])
h, valid, cell, x0, y0 = d['h'], d['valid'], float(d['cell']), float(d['x0']), float(d['y0'])
H, W = h.shape
HALF = 1008.0
INSET = 24.0
jj, ii = np.mgrid[0:H, 0:W]
X = ii * cell + x0
Y = jj * cell + y0
# 接缝：Landscape 边缘外 150 米内往下沉一点，不和 Landscape 抢
out = np.maximum(np.abs(X), np.abs(Y)) - HALF
sink = 1.2 * np.clip(1 - out / 150.0, 0, 1)
Z = h - sink
# 格子（四个角都有效，而且不在 Landscape 里面）
cv = valid[:-1, :-1] & valid[1:, :-1] & valid[:-1, 1:] & valid[1:, 1:]
inside = (np.maximum(np.abs(X[:-1, :-1]), np.abs(Y[:-1, :-1])) < HALF - INSET) & (np.maximum(np.abs(X[1:, 1:]), np.abs(Y[1:, 1:])) < HALF - INSET)
cv &= ~inside
print('[far] 格子', int(cv.sum()), '面', int(cv.sum()) * 2, flush=True)
used = np.zeros((H, W), bool)
used[:-1, :-1] |= cv
used[1:, :-1] |= cv
used[:-1, 1:] |= cv
used[1:, 1:] |= cv
vid = -np.ones((H, W), np.int64)
vid[used] = np.arange(int(used.sum()))
vx = X[used].astype(np.float32)
vy = (-Y[used]).astype(np.float32)
vz = Z[used].astype(np.float32)
co = np.stack((vx, vy, vz), -1).ravel()
j, i = np.nonzero(cv)
# 法线朝上（Blender 坐标里 y 取反了，顺序：(i,j) (i,j+1) (i+1,j+1) (i+1,j) 俯视逆时针）
quads = np.stack((vid[j, i], vid[j + 1, i], vid[j + 1, i + 1], vid[j, i + 1]), -1)
nq = len(quads)
me = bpy.data.meshes.new('SM_Moon_FarTerrain')
me.vertices.add(len(vx))
me.vertices.foreach_set('co', co)
me.loops.add(nq * 4)
me.loops.foreach_set('vertex_index', quads.ravel().astype(np.int32))
me.polygons.add(nq)
me.polygons.foreach_set('loop_start', (np.arange(nq) * 4).astype(np.int32))
me.update(calc_edges=True)
me.validate(verbose=False)
ob = bpy.data.objects.new('SM_Moon_FarTerrain', me)
bpy.context.scene.collection.objects.link(ob)
n0 = me.polygons[0].normal.z
print('[far] 第一面法线 z', n0, '顶点', len(vx), '范围', vx.min(), vx.max(), vy.min(), vy.max(), vz.min(), vz.max(), flush=True)
assert n0 > 0, '法线朝下'
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.fbx(filepath=args[1], use_selection=True, object_types={'MESH'}, apply_unit_scale=True, mesh_smooth_type='FACE', use_mesh_modifiers=False,
                         add_leaf_bones=False, bake_anim=False, axis_forward='-Z', axis_up='Y')
print('[far] 写', args[1], flush=True)
