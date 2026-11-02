"""苏联街道小品（Sketchfab）普查：每个源文件导入后打印物体、尺寸、面数、材质和贴图连线；拍一张 Workbench 图（旁边放 2.5 米机体红柱）。
blender -b --factory-startup -P tools/soviet_props_survey.py -- 素材根目录 输出.json 输出图片目录
素材来自网上，.blend 只用 append（不跑里面的脚本，blender 默认不开自动脚本，也不要加 -y）
"""
import json
import math
import os
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
ROOT, OUT, IMG = args[0], args[1], args[2]
os.makedirs(IMG, exist_ok=True)
SRC = {
    'zhiguli': 'zhiguli-soviet-cars/source/zhiguli.fbx',
    'niva': 'lada-niva/source/model.fbx',
    'busstop1': 'old-soviet-bus-stop/source/BusStop_low.fbx',
    'busstop2': 'soviet-bus-stop-2/source/busstop2.fbx',
    'light1': 'soviet-streetlight-pack/source/streetlight pack.fbx',
    'light2': None,  # 文件名是俄文乱码，运行时找
    'signs': 'modular-russian-road-signs-pack-urban/source/RRSP.fbx',
    'payphone': 'old-soviet-payphone/source/model_x/model/model.dae',
    'slide': 'rusty-soviet-slide/source/Gorka.blend',
    'swings': 'ussr-swings/source/x/SwingsBaked/source/Swings.fbx',
    'seesaw': 'seesaw-type-1/source/x/seesaw-type1/seesaw-type1.blend',
    'rocket': 'soviet-rocket-play-scape/source/Soviet PlayScape.fbx',
    'bench': 'old-soviet-bench/source/Bench.fbx',
    'trash': 'old-soviet-trash-can/source/trash can.fbx',
    'flower': 'soviet-flower-bed/source/FlowerBed.fbx',
    'garage': 'soviet-garages-block-psx/source/Garaji.obj',
    'deadtrees': 'dead-trees-demo/source/Dead Trees_Demo_Sketchfab.blend',
    'wintertrees': 'low-poly-winter-tree-pack/source/WINTER_TREE_PACK_x/WINTER_TREE_PACK/SOURCE/WINTER_TREE_PACK.fbx',
}
d2 = os.path.join(ROOT, 'soviet-streetlight-pack-remake-and-part-2/source')
SRC['light2'] = os.path.relpath(os.path.join(d2, next(f for f in os.listdir(d2) if f.lower().endswith('.fbx'))), ROOT)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def load(path):
    low = path.lower()
    if low.endswith('.fbx'):
        bpy.ops.import_scene.fbx(filepath=path)
    elif low.endswith('.obj'):
        bpy.ops.wm.obj_import(filepath=path)
    elif low.endswith('.dae'):
        bpy.ops.wm.collada_import(filepath=path)
    elif low.endswith('.blend'):
        with bpy.data.libraries.load(path, link=False) as (src, dst):
            dst.objects = list(src.objects)
        for o in dst.objects:
            if o is not None:
                bpy.context.scene.collection.objects.link(o)


res = {}
for key, rel in SRC.items():
    reset()
    path = os.path.join(ROOT, rel)
    e = {'src': rel}
    try:
        load(path)
    except Exception as ex:
        e['error'] = str(ex)[:300]
        res[key] = e
        print('[props] 失败', key, ex, flush=True)
        continue
    objs = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    e['objects'] = []
    all_pts = []
    for o in objs:
        pts = [o.matrix_world @ Vector(c) for c in o.bound_box]
        all_pts += pts
        lo = [min(p[i] for p in pts) for i in range(3)]
        hi = [max(p[i] for p in pts) for i in range(3)]
        mats = []
        for s in o.material_slots:
            m = s.material
            if not m:
                continue
            tx = []
            if m.use_nodes and m.node_tree:
                for n in m.node_tree.nodes:
                    if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                        tx.append(os.path.basename(n.image.filepath or n.image.name))
            mats.append({'name': m.name, 'tex': tx})
        e['objects'].append({'name': o.name, 'tris': sum(len(p.vertices) - 2 for p in o.data.polygons), 'size': [round(hi[i] - lo[i], 2) for i in range(3)],
                             'min': [round(x, 2) for x in lo], 'mats': mats, 'hidden': o.hide_render})
    if all_pts:
        lo = Vector([min(p[i] for p in all_pts) for i in range(3)])
        hi = Vector([max(p[i] for p in all_pts) for i in range(3)])
        e['size'] = [round(hi[i] - lo[i], 2) for i in range(3)]
        e['min'] = [round(x, 2) for x in lo]
        e['tris'] = sum(x['tris'] for x in e['objects'])
        # 图：斜视 Workbench
        sc = bpy.context.scene
        sc.render.engine = 'BLENDER_WORKBENCH'
        sc.display.shading.light = 'STUDIO'
        sc.display.shading.color_type = 'TEXTURE'
        sc.display.shading.show_cavity = True
        sc.render.resolution_x, sc.render.resolution_y = 640, 480
        sc.render.image_settings.file_format = 'JPEG'
        cd = bpy.data.cameras.new('c')
        cd.type = 'ORTHO'
        c = bpy.data.objects.new('c', cd)
        sc.collection.objects.link(c)
        sc.camera = c
        ctr = (lo + hi) / 2
        dia = (hi - lo).length
        c.location = ctr + Vector((0.7, -0.7, 0.45)).normalized() * (dia * 2 + 1)
        c.rotation_euler = (ctr - c.location).to_track_quat('-Z', 'Y').to_euler()
        cd.ortho_scale = dia * 1.25
        cd.clip_end = dia * 10 + 100
        sc.render.filepath = os.path.join(IMG, key + '.jpg')
        try:
            bpy.ops.render.render(write_still=True)
        except Exception as ex:
            e['render_error'] = str(ex)[:200]
    res[key] = e
    print('[props]', key, e.get('size'), e.get('tris'), len(e.get('objects', [])), flush=True)
    json.dump(res, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[props] 完成', flush=True)
