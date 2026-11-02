"""Blender：浅河水面（看得见，岸线弯曲，全在 |Y|<=500 内）+ 浅河碰撞盒（规格严格 X±12000 Y±500 Z0~3，隐藏，带 Water 标签用）
blender -b --factory-startup --python canyon_water.py"""
import bpy, math, os
import numpy as np
OUT = r"D:\RobotSlice\SourceAssets\canyon\meshes"
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)


def noise1(x, seed, scale):
    r = np.random.default_rng(seed)
    g = r.random(4000)
    t = x / scale + 1000
    i = np.floor(t).astype(int); f = t - i; f = f * f * (3 - 2 * f)
    return g[i] + (g[i + 1] - g[i]) * f


def mesh(name, V, F, uvs=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in V], [], [tuple(f) for f in F])
    if uvs is not None:
        uv = me.uv_layers.new(name="UVMap")
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi = me.loops[li].vertex_index
                uv.data[li].uv = uvs[vi]
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


# 水面（米）
xs = np.arange(-12000, 12001, 100.0)
cols = 10
taper = np.clip((12000 - np.abs(xs)) / 700.0, 0, 1) ** 0.6
yl = -500 + 20 + noise1(xs, 1, 900) * 90 + noise1(xs, 2, 230) * 25
yr = 500 - 20 - noise1(xs, 3, 900) * 90 - noise1(xs, 4, 230) * 25
mid = (yl + yr) * 0.5
yl = mid + (yl - mid) * taper; yr = mid + (yr - mid) * taper
V = []; UV = []
for i, x in enumerate(xs):
    for j in range(cols + 1):
        y = yl[i] + (yr[i] - yl[i]) * j / cols
        V.append((x / 100, y / 100, 0.02)); UV.append((x / 400, y / 400))
F = []
for i in range(len(xs) - 1):
    for j in range(cols):
        a = i * (cols + 1) + j
        F.append((a, a + cols + 1, a + cols + 2, a + 1))
w = mesh("SM_Canyon_Water", V, F, UV)
# 碰撞盒（规格）
x0, x1, y0, y1, z0, z1 = -120, 120, -5, 5, 0.0, 0.03
BV = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
BF = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
c = mesh("SM_Canyon_WaterCol", BV, BF, [(v[0] / 4, v[1] / 4) for v in BV])
for ob in (w, c):
    bpy.ops.object.select_all(action="DESELECT"); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, ob.name + ".fbx"), use_selection=True, object_types={"MESH"}, apply_unit_scale=True,
                             mesh_smooth_type="FACE", add_leaf_bones=False, bake_anim=False, axis_forward="-Y", axis_up="Z")
    print("[water]", ob.name, len(ob.data.polygons))
