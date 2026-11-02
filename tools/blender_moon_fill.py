"""月球基地：高程图以外的补网格（moon_terrain_bake.py 的 fill.npz），96 米一格。blender -b --factory-startup -P tools/blender_moon_fill.py -- fill.npz 输出.fbx"""
import sys

import bpy
import numpy as np

args = sys.argv[sys.argv.index('--') + 1:]
d = np.load(args[0])
h, m, cell, x0, y0 = d['h'], d['mask'], float(d['cell']), float(d['x0']), float(d['y0'])
H, W = h.shape
jj, ii = np.mgrid[0:H, 0:W]
X = ii * cell + x0
Y = jj * cell + y0
cv = m[:-1, :-1] | m[1:, :-1] | m[:-1, 1:] | m[1:, 1:]
used = np.zeros((H, W), bool)
used[:-1, :-1] |= cv
used[1:, :-1] |= cv
used[:-1, 1:] |= cv
used[1:, 1:] |= cv
vid = -np.ones((H, W), np.int64)
vid[used] = np.arange(int(used.sum()))
co = np.stack((X[used].astype(np.float32), (-Y[used]).astype(np.float32), h[used].astype(np.float32)), -1).ravel()
j, i = np.nonzero(cv)
quads = np.stack((vid[j, i], vid[j + 1, i], vid[j + 1, i + 1], vid[j, i + 1]), -1)
nq = len(quads)
me = bpy.data.meshes.new('SM_Moon_FarFill')
me.vertices.add(int(used.sum()))
me.vertices.foreach_set('co', co)
me.loops.add(nq * 4)
me.loops.foreach_set('vertex_index', quads.ravel().astype(np.int32))
me.polygons.add(nq)
me.polygons.foreach_set('loop_start', (np.arange(nq) * 4).astype(np.int32))
me.update(calc_edges=True)
me.validate(verbose=False)
ob = bpy.data.objects.new('SM_Moon_FarFill', me)
bpy.context.scene.collection.objects.link(ob)
print('[fill] 面', nq * 2, '第一面法线 z', me.polygons[0].normal.z, flush=True)
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.fbx(filepath=args[1], use_selection=True, object_types={'MESH'}, apply_unit_scale=True, mesh_smooth_type='FACE', use_mesh_modifiers=False,
                         add_leaf_bones=False, bake_anim=False, axis_forward='-Z', axis_up='Y')
print('[fill] 写', args[1], flush=True)
