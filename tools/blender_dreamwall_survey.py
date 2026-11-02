"""看科幻峡谷包（Inbox/科幻峡谷blender源文件/Files/3D/Scene_0N.blend）的结构：物体、修改器参数、几何节点、尺寸、面数、材质。
给梦核旷野图（L_Dreamfield）拆「一面墙」用。
blender -b Scene_02.blend -P tools/blender_dreamwall_survey.py -- 输出.json
"""
import json
import sys

import bpy

out = sys.argv[sys.argv.index('--') + 1:][0]
deps = bpy.context.evaluated_depsgraph_get()
res = {'objects': [], 'materials': [], 'images': [], 'world': None, 'unit_scale': bpy.context.scene.unit_settings.scale_length}


def mod_info(m):
    d = {'name': m.name, 'type': m.type, 'show_viewport': m.show_viewport, 'show_render': m.show_render}
    if m.type == 'ARRAY':
        d.update(count=m.count, fit_type=m.fit_type, use_relative_offset=m.use_relative_offset,
                 relative_offset=list(m.relative_offset_displace), use_constant_offset=m.use_constant_offset,
                 constant_offset=list(m.constant_offset_displace), use_merge=m.use_merge_vertices,
                 offset_object=m.offset_object.name if m.offset_object else None)
    elif m.type == 'MIRROR':
        d.update(axis=list(m.use_axis), bisect=list(m.use_bisect_axis), flip=list(m.use_bisect_flip_axis),
                 mirror_object=m.mirror_object.name if m.mirror_object else None, use_clip=m.use_clip)
    elif m.type == 'NODES':
        d.update(group=m.node_group.name if m.node_group else None)
    elif m.type in ('SUBSURF', 'MULTIRES'):
        d.update(levels=getattr(m, 'levels', None), render_levels=getattr(m, 'render_levels', None))
    elif m.type == 'BEVEL':
        d.update(width=m.width, segments=m.segments)
    elif m.type == 'SOLIDIFY':
        d.update(thickness=m.thickness)
    elif m.type == 'DISPLACE':
        d.update(strength=m.strength, texture=m.texture.name if m.texture else None)
    elif m.type == 'BOOLEAN':
        d.update(operation=m.operation, object=m.object.name if m.object else None)
    return d


for o in bpy.data.objects:
    d = {'name': o.name, 'type': o.type, 'parent': o.parent.name if o.parent else None,
         'loc': [round(v, 3) for v in o.location], 'rot': [round(v, 4) for v in o.rotation_euler],
         'scale': [round(v, 4) for v in o.scale], 'hide_render': o.hide_render, 'hide_viewport': o.hide_viewport,
         'collections': [c.name for c in o.users_collection], 'modifiers': [mod_info(m) for m in getattr(o, 'modifiers', [])]}
    if o.type == 'MESH':
        me = o.data
        d['base'] = {'verts': len(me.vertices), 'faces': len(me.polygons), 'mesh': me.name, 'users': me.users}
        xs = [v.co for v in me.vertices]
        if xs:
            d['base']['min'] = [round(min(c[i] for c in xs), 2) for i in range(3)]
            d['base']['max'] = [round(max(c[i] for c in xs), 2) for i in range(3)]
        d['materials'] = [s.material.name if s.material else None for s in o.material_slots]
        try:
            ev = o.evaluated_get(deps)
            em = ev.to_mesh()
            d['eval'] = {'verts': len(em.vertices), 'faces': len(em.polygons)}
            if len(em.vertices):
                mw = o.matrix_world
                cs = [mw @ v.co for v in em.vertices]
                d['eval']['wmin'] = [round(min(c[i] for c in cs), 2) for i in range(3)]
                d['eval']['wmax'] = [round(max(c[i] for c in cs), 2) for i in range(3)]
            ev.to_mesh_clear()
        except Exception as e:
            d['eval_err'] = str(e)
    if o.type == 'LIGHT':
        d['light'] = {'type': o.data.type, 'energy': o.data.energy, 'color': list(o.data.color)}
    if o.type == 'CAMERA':
        d['camera'] = {'lens': o.data.lens}
    res['objects'].append(d)

for m in bpy.data.materials:
    md = {'name': m.name, 'users': m.users, 'nodes': []}
    if m.use_nodes and m.node_tree:
        for n in m.node_tree.nodes:
            nd = {'name': n.name, 'type': n.bl_idname}
            if n.bl_idname == 'ShaderNodeBsdfPrincipled':
                vals = {}
                for k in ('Base Color', 'Roughness', 'Metallic', 'Emission Color', 'Emission Strength'):
                    s = n.inputs.get(k)
                    if s is not None:
                        vals[k] = (list(s.default_value) if hasattr(s.default_value, '__len__') else s.default_value) if not s.is_linked else 'linked'
                nd['vals'] = vals
            if n.bl_idname == 'ShaderNodeAttribute':
                nd['attr'] = n.attribute_name
            if n.bl_idname == 'ShaderNodeEmission':
                nd['vals'] = {'color': list(n.inputs['Color'].default_value), 'strength': n.inputs['Strength'].default_value}
            if n.bl_idname == 'ShaderNodeTexImage' and n.image:
                nd['image'] = n.image.name
            if n.bl_idname == 'ShaderNodeValToRGB':
                nd['ramp'] = [(round(e.position, 3), [round(c, 3) for c in e.color]) for e in n.color_ramp.elements]
            md['nodes'].append(nd)
    res['materials'].append(md)
for im in bpy.data.images:
    res['images'].append({'name': im.name, 'path': im.filepath, 'size': list(im.size), 'packed': bool(im.packed_file)})
for ng in bpy.data.node_groups:
    res.setdefault('node_groups', []).append({'name': ng.name, 'type': ng.bl_idname, 'nodes': [n.bl_idname for n in ng.nodes]})
w = bpy.context.scene.world
if w and w.use_nodes:
    res['world'] = [{'type': n.bl_idname, 'image': getattr(getattr(n, 'image', None), 'name', None),
                     'strength': n.inputs['Strength'].default_value if 'Strength' in n.inputs else None} for n in w.node_tree.nodes]
res['render'] = {'engine': bpy.context.scene.render.engine, 'camera': bpy.context.scene.camera.name if bpy.context.scene.camera else None}
json.dump(res, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print('[survey] 写出', out, len(res['objects']), '个物体')
