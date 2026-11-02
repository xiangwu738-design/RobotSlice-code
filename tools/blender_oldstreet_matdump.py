"""老旧街道：把用到的材质节点参数（映射、伽马、色相饱和度、颜色渐变）写成 JSON（只读）。
blender -b 老旧街道.blend -P tools/blender_oldstreet_matdump.py -- 输出.json"""
import json, os, sys
import bpy
out = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
R = {}


def sock(s):
    try:
        v = s.default_value
        return list(v) if hasattr(v, '__len__') and not isinstance(v, str) else v
    except Exception:
        return None


for m in bpy.data.materials:
    if not m.use_nodes or m.node_tree is None:
        continue
    rows = []
    for n in m.node_tree.nodes:
        row = {'n': n.name, 't': n.type}
        if n.type == 'MAPPING':
            row.update(vtype=n.vector_type, loc=sock(n.inputs['Location']), rot=sock(n.inputs['Rotation']), scale=sock(n.inputs['Scale']))
        elif n.type == 'GAMMA':
            row.update(g=sock(n.inputs['Gamma']), linked=n.inputs['Color'].is_linked)
        elif n.type == 'HUE_SAT':
            row.update(h=sock(n.inputs['Hue']), s=sock(n.inputs['Saturation']), v=sock(n.inputs['Value']), f=sock(n.inputs['Fac']))
        elif n.type == 'BRIGHTCONTRAST':
            row.update(b=sock(n.inputs['Bright']), c=sock(n.inputs['Contrast']))
        elif n.type == 'VALTORGB':
            row.update(ramp=[(round(e.position, 3), [round(c, 3) for c in e.color]) for e in n.color_ramp.elements])
        elif n.type == 'BSDF_PRINCIPLED':
            row.update(base=sock(n.inputs['Base Color']), rough=sock(n.inputs['Roughness']), metal=sock(n.inputs['Metallic']), estr=sock(n.inputs['Emission Strength']), ecol=sock(n.inputs['Emission Color']), alpha=sock(n.inputs['Alpha']))
        elif n.type == 'TEX_IMAGE':
            row.update(img=n.image.name if n.image else None, ext=n.extension, cs=n.image.colorspace_settings.name if n.image else None, proj=n.projection)
        elif n.type == 'TEX_COORD':
            row.update(used=[o.name for o in n.outputs if o.is_linked])
        elif n.type == 'MIX':
            row.update(fac=sock(n.inputs[0]), blend=n.blend_type)
        elif n.type == 'BUMP':
            row.update(strength=sock(n.inputs['Strength']), dist=sock(n.inputs['Distance']))
        elif n.type == 'NORMAL_MAP':
            row.update(strength=sock(n.inputs['Strength']))
        elif n.type == 'DISPLACEMENT':
            row.update(scale=sock(n.inputs['Scale']), mid=sock(n.inputs['Midlevel']))
        elif n.type == 'GROUP':
            row.update(group=n.node_tree.name if n.node_tree else None)
        else:
            continue
        links = []
        for o in n.outputs:
            for l in o.links:
                links.append(f'{o.name}->{l.to_node.name}.{l.to_socket.name}')
        row['to'] = links
        rows.append(row)
    R[m.name] = rows
json.dump(R, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('[oldstreet] ok', len(R))
