"""石柱山水（L_Pillars）：调查 Inbox/_f25/冲破穹顶.blend 的物体结构（只读，不保存）。
blender -b 冲破穹顶.blend -P tools/blender_pillars_survey.py -- 输出.json
"""
import json
import sys

import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index('--') + 1]
deps = bpy.context.evaluated_depsgraph_get()
res = {'objects': [], 'collections': {}}


def walk(c, path):
    res['collections']['/'.join(path + [c.name])] = {'objs': len(c.objects), 'hide_render': c.hide_render,
                                                     'names': [o.name for o in c.objects][:200]}
    for ch in c.children:
        walk(ch, path + [c.name])


walk(bpy.context.scene.collection, [])
vl = bpy.context.view_layer
for o in bpy.data.objects:
    if o.type not in ('MESH', 'CURVE', 'EMPTY', 'LIGHT', 'CAMERA', 'META'):
        continue
    e = {'name': o.name, 'type': o.type, 'parent': o.parent.name if o.parent else None, 'hide_viewport': o.hide_viewport,
         'hide_render': o.hide_render, 'visible': o.visible_get(), 'cols': [c.name for c in o.users_collection], 'loc': list(o.matrix_world.translation),
         'scale': list(o.matrix_world.to_scale()), 'mods': [(m.type, m.name, m.show_render, m.show_viewport) for m in getattr(o, 'modifiers', [])]}
    if o.type == 'MESH':
        e['polys'] = len(o.data.polygons)
        e['verts'] = len(o.data.vertices)
        e['mats'] = [s.material.name if s.material else None for s in o.material_slots]
        bb = [o.matrix_world @ Vector(c) for c in o.bound_box]
        e['bb_min'] = [min(v[i] for v in bb) for i in range(3)]
        e['bb_max'] = [max(v[i] for v in bb) for i in range(3)]
        try:
            ev = o.evaluated_get(deps)
            e['ev_polys'] = len(ev.data.polygons)
        except Exception as ex:
            e['ev_err'] = str(ex)
    if o.type == 'LIGHT':
        e['light'] = {'type': o.data.type, 'energy': o.data.energy, 'color': list(o.data.color), 'dir': list(o.matrix_world.to_3x3() @ Vector((0, 0, -1)))}
    if o.type == 'CAMERA':
        e['cam'] = {'lens': o.data.lens, 'sensor': o.data.sensor_width, 'fwd': list(o.matrix_world.to_3x3() @ Vector((0, 0, -1)))}
    res['objects'].append(e)
# 实例统计：哪个物体被实例了多少次、由谁实例
inst = {}
for dup in deps.object_instances:
    if dup.is_instance:
        k = (dup.instance_object.name if dup.instance_object else '?', dup.parent.name if dup.parent else '?')
        inst[k] = inst.get(k, 0) + 1
res['instances'] = sorted([[a, b, n] for (a, b), n in inst.items()], key=lambda x: -x[2])
w = bpy.context.scene.world
res['world'] = w.name if w else None
res['unit'] = bpy.context.scene.unit_settings.scale_length
json.dump(res, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('[survey] 写出', OUT, len(res['objects']))
