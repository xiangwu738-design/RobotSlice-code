"""查看一个 .blend 场景：物体数、面数、材质、贴图是否齐全、包围范围、集合；用场景相机出一张 Eevee 预览图。
blender -b 文件.blend -P inspect_blend.py -- 预览图.png"""
import sys
import bpy
from mathutils import Vector

out = sys.argv[sys.argv.index('--') + 1]
args = sys.argv[sys.argv.index('--') + 1:]
if len(args) > 1:  # 第二个参数：贴图目录，按文件名补上丢失的贴图
    bpy.ops.file.find_missing_files(directory=args[1])
sc = bpy.context.scene
meshes = [o for o in sc.objects if o.type == 'MESH']
deps = bpy.context.evaluated_depsgraph_get()
tris = 0
lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
for o in meshes:
    tris += sum(len(p.vertices) - 2 for p in o.data.polygons)
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        lo = Vector(map(min, lo, w))
        hi = Vector(map(max, hi, w))
types = {}
for o in sc.objects:
    types[o.type] = types.get(o.type, 0) + 1
print('[blend] 物体', types, '三角面约', tris)
print('[blend] 范围', tuple(round(v, 1) for v in lo), '~', tuple(round(v, 1) for v in hi), '单位缩放', sc.unit_settings.scale_length)
print('[blend] 材质', len(bpy.data.materials), '贴图', len(bpy.data.images),
      '内嵌', sum(1 for i in bpy.data.images if i.packed_file), '缺失',
      sum(1 for i in bpy.data.images if i.source == 'FILE' and not i.packed_file and not __import__('os').path.exists(bpy.path.abspath(i.filepath))))
print('[blend] 集合', [(c.name, len(c.all_objects)) for c in bpy.data.collections][:30])
uniq = {}
for o in meshes:
    uniq[o.data.name] = uniq.get(o.data.name, 0) + 1
print('[blend] 不同网格', len(uniq), '实例最多的', sorted(uniq.items(), key=lambda kv: -kv[1])[:10])
names = sorted({o.name.rstrip('0123456789.') for o in meshes})
print('[blend] 物体名样例', names[:60])
print('[blend] 渲染器', sc.render.engine, '相机', sc.camera.name if sc.camera else None, '灯', types.get('LIGHT', 0))
if sc.camera:
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = 1280, 720, 100
    sc.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print('[blend] 预览', out)
