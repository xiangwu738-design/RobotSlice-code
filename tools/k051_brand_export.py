"""相位电子园区的标识网格：PHASE 占位标识（docs/世界观/标识/占位/PHASE_白.svg，已转曲线）+ 主办方 CONSENT 文字牌。
每个都挤出成薄板（厚 4 厘米），立在 XZ 平面、正面朝 -Y，宽 1 米、原点在底边中点；UE 里按需要放大。
blender -b --factory-startup -P tools/k051_brand_export.py -- 输出目录
"""
import os
import sys

import bpy
from mathutils import Matrix, Vector

OUT = sys.argv[sys.argv.index('--') + 1]
os.makedirs(OUT, exist_ok=True)
SVG = 'D:/RobotSlice/docs/世界观/标识/占位/PHASE_白.svg'
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)


def finish(objs, name, depth=0.04):
    """曲线 → 挤出 → 网格，合并、正面朝 -Y、宽归一到 1 米、原点在底边中点"""
    meshes = []
    for o in objs:
        if o.type == 'CURVE' or o.type == 'FONT':
            o.data.extrude = 0.5
            o.data.fill_mode = 'BOTH' if o.type == 'CURVE' else o.data.fill_mode
        bpy.context.view_layer.objects.active = o
        bpy.ops.object.select_all(action='DESELECT')
        o.select_set(True)
        bpy.ops.object.convert(target='MESH')
        meshes.append(bpy.context.view_layer.objects.active)
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.data.transform(ob.matrix_world)
    ob.matrix_world = Matrix.Identity(4)
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    w = hi.x - lo.x
    # 平放在 XY 的字 → 立起来：Y 变 Z，厚度方向变 Y；宽 1 米、厚 depth
    sx = 1.0 / w
    sz = depth / max(hi.z - lo.z, 1e-6)
    ob.data.transform(Matrix.Translation(-Vector(((lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2))))
    ob.data.transform(Matrix.Diagonal((sx, sx, sz, 1.0)))
    ob.data.transform(Matrix.Rotation(1.5707963, 4, 'X'))     # +Y → +Z（字立起来），挤出方向 Z → -Y
    ob.name = ob.data.name = name
    mat = bpy.data.materials.new('Logo')
    ob.data.materials.clear()
    ob.data.materials.append(mat)
    vs = [v.co for v in ob.data.vertices]
    print('[brand]', name, '尺寸', [round(max(v[i] for v in vs) - min(v[i] for v in vs), 3) for i in range(3)], '面', len(ob.data.polygons))
    return ob


before = set(bpy.data.objects)
bpy.ops.import_curve.svg(filepath=SVG)
logo = finish([o for o in bpy.data.objects if o not in before], 'SM_TP_LogoPHASE')
bpy.ops.object.text_add()
t = bpy.context.active_object
t.data.body = 'CONSENT'
t.data.size = 1.0
t.data.space_character = 1.15
consent = finish([t], 'SM_TP_SignCONSENT')
bpy.ops.object.select_all(action='DESELECT')
logo.select_set(True)
consent.select_set(True)
bpy.ops.export_scene.fbx(filepath=f'{OUT}/brand.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                         mesh_smooth_type='FACE', add_leaf_bones=False, bake_anim=False)
print('[brand] 导出', f'{OUT}/brand.fbx')
