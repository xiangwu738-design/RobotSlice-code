"""从一个 Blender 场景里挑几个物体单独导出（摄影测量石头 / 崖壁这类），每个一份 FBX + 贴图 + 预览图。
blender -b --factory-startup --disable-autoexec 场景.blend -P blender_export_props.py -- 输出目录 物体名1 物体名2 ...
- 修改器应用后导出，原点挪到底面中心，旋转缩放应用掉；米制（apply_unit_scale）
- 材质用到的内嵌贴图原样写到 输出目录/textures，props.json 记下每个物体的尺寸、面数、材质和贴图接在哪
- 每个物体在空场景里用 Eevee 出一张预览（灰背景 + 太阳光）
"""
import json
import math
import os
import re
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
OUT, names = args[0], args[1:]
os.makedirs(f'{OUT}/textures', exist_ok=True)
clean = lambda s: re.sub(r'[^A-Za-z0-9]', '_', s)
src_scene = bpy.context.scene
info = {}


def images(tree, seen=None):
    seen = seen or set()
    rows = []
    for n in tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            outs = sorted({f'{l.to_node.bl_idname.replace("ShaderNode", "")}.{l.to_socket.name}' for o in n.outputs for l in o.links})
            rows.append((n.image, outs))
        elif n.bl_idname == 'ShaderNodeGroup' and n.node_tree and n.node_tree.name not in seen:
            seen.add(n.node_tree.name)
            rows += images(n.node_tree, seen)
    return rows


for name in names:
    src = bpy.data.objects.get(name)
    if not src:
        print('[props] 没有物体', name, flush=True)
        continue
    dg = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(src.evaluated_get(dg))   # 修改器已应用（不含粒子）
    mesh.transform(src.matrix_world)
    lo = Vector((min(v.co.x for v in mesh.vertices), min(v.co.y for v in mesh.vertices), min(v.co.z for v in mesh.vertices)))
    hi = Vector((max(v.co.x for v in mesh.vertices), max(v.co.y for v in mesh.vertices), max(v.co.z for v in mesh.vertices)))
    mesh.transform(__import__('mathutils').Matrix.Translation(-Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, lo.z))))
    scn = bpy.data.scenes.new('P_' + name)
    obj = bpy.data.objects.new(clean(name), mesh)
    scn.collection.objects.link(obj)
    tex = {}
    for m in mesh.materials:
        if m and m.use_nodes:
            for img, outs in images(m.node_tree):
                fn = clean(img.name) + (os.path.splitext(bpy.path.abspath(img.filepath))[1] or '.png')   # 用图像数据块名：同名文件可能是不同的图
                if img.packed_file:
                    open(f'{OUT}/textures/{fn}', 'wb').write(img.packed_file.data)
                tex[fn] = dict(size=list(img.size), colorspace=img.colorspace_settings.name, to=outs, material=m.name)
    tris = sum(len(p.vertices) - 2 for p in mesh.polygons)
    size = [round(v, 2) for v in (hi - lo)]
    info[clean(name)] = dict(source=name, size_m=size, tris=tris, materials=[m.name for m in mesh.materials if m], textures=tex)
    print('[props]', name, '尺寸', size, '三角面', tris, '贴图', list(tex), flush=True)

    # 导出
    bpy.context.window.scene = scn
    for o in scn.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{clean(name)}.fbx', use_selection=True, object_types={'MESH'}, apply_unit_scale=True,
                             apply_scale_options='FBX_SCALE_ALL', mesh_smooth_type='FACE', path_mode='STRIP', embed_textures=False)

    # 预览
    sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN'))
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(50), 0, math.radians(35))
    scn.collection.objects.link(sun)
    cam = bpy.data.objects.new('Cam', bpy.data.cameras.new('Cam'))
    scn.collection.objects.link(cam)
    r = max(size) * 1.15
    cam.location = Vector((r * 0.9, -r * 1.3, size[2] * 0.6 + r * 0.35))
    d = Vector((0, 0, size[2] * 0.45)) - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    cam.data.lens = 40
    scn.camera = cam
    w = bpy.data.worlds.new('W')
    w.use_nodes = True
    w.node_tree.nodes['Background'].inputs[0].default_value = (0.35, 0.38, 0.42, 1)
    scn.world = w
    eng = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items]
    scn.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in eng else 'BLENDER_EEVEE'
    scn.render.resolution_x, scn.render.resolution_y = 800, 600
    scn.render.filepath = f'{OUT}/preview_{clean(name)}.png'
    bpy.ops.render.render(write_still=True, scene=scn.name)

json.dump(info, open(f'{OUT}/props.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[props] 完成', len(info), flush=True)
