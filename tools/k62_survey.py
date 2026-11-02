"""K62 苏联建筑（Kitbash3D Soviet Blocks）普查：每个物体的名字 / 集合 / 包围盒 / 面数 / 材质槽，材质节点里的贴图。只读，不存 .blend。
blender -b Inbox/K62_苏联建筑.blend -P tools/k62_survey.py -- Inbox/_k62/survey.json
"""
import json, os, sys
import bpy
from mathutils import Vector

out = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
objs = []
for o in bpy.data.objects:
    e = {'name': o.name, 'type': o.type, 'parent': o.parent.name if o.parent else None,
         'cols': [c.name for c in o.users_collection], 'scenes': [s.name for s in o.users_scene], 'hide_render': o.hide_render,
         'mods': [m.type for m in o.modifiers]}
    if o.type == 'MESH' and o.data:
        pts = [o.matrix_world @ Vector(c) for c in o.bound_box]
        e['min'] = [min(p[i] for p in pts) for i in range(3)]
        e['max'] = [max(p[i] for p in pts) for i in range(3)]
        e['polys'] = len(o.data.polygons)
        e['tris'] = sum(len(p.vertices) - 2 for p in o.data.polygons)
        e['mats'] = [s.material.name if s.material else None for s in o.material_slots]
        e['mesh'] = o.data.name
        e['loc'] = list(o.matrix_world.translation)
        e['scale'] = list(o.matrix_world.to_scale())
    objs.append(e)
mats = {}
for m in bpy.data.materials:
    d = {'users': m.users, 'nodes': [], 'links': []}
    if m.use_nodes and m.node_tree:
        for n in m.node_tree.nodes:
            nd = {'type': n.bl_idname, 'name': n.name}
            if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                nd['image'] = n.image.name
                nd['file'] = n.image.filepath
                nd['cs'] = n.image.colorspace_settings.name
                nd['mapping'] = n.interpolation
            if n.bl_idname == 'ShaderNodeBsdfPrincipled':
                nd['inputs'] = {i.name: (list(i.default_value) if hasattr(i.default_value, '__len__') else i.default_value)
                                for i in n.inputs if not i.is_linked and hasattr(i, 'default_value') and i.name in ('Base Color', 'Roughness', 'Metallic', 'Alpha', 'Transmission Weight', 'Emission Strength', 'Emission Color')}
            d['nodes'].append(nd)
        for l in m.node_tree.links:
            d['links'].append([l.from_node.name, l.from_socket.name, l.to_node.name, l.to_socket.name])
    mats[m.name] = d
imgs = {i.name: {'file': i.filepath, 'packed': bool(i.packed_file), 'size': list(i.size), 'cs': i.colorspace_settings.name,
                 'bytes': i.packed_file.size if i.packed_file else 0} for i in bpy.data.images}
json.dump({'objects': objs, 'materials': mats, 'images': imgs, 'collections': {c.name: [x.name for x in c.children] for c in bpy.data.collections},
           'scenes': {s.name: len(s.objects) for s in bpy.data.scenes}, 'unit': bpy.context.scene.unit_settings.scale_length},
          open(out, 'w', encoding='utf-8'), ensure_ascii=False)
print('SURVEY OK', out, len(objs), flush=True)
