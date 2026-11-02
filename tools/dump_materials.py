"""列出 .blend 里每个材质的节点类型和贴图，判断能不能直接按 UV 贴图在 UE 里重建。
blender -b 文件.blend -P dump_materials.py"""
import bpy


def images_in(tree, seen=None):
    seen = seen or set()
    out = []
    for n in tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            links = [l.to_socket.name + '@' + l.to_node.bl_idname.replace('ShaderNode', '') for o in n.outputs for l in o.links]
            out.append((n.image.name, n.image.size[0], n.image.colorspace_settings.name, links))
        elif n.bl_idname == 'ShaderNodeGroup' and n.node_tree and n.node_tree.name not in seen:
            seen.add(n.node_tree.name)
            out += images_in(n.node_tree, seen)
    return out


for m in bpy.data.materials:
    if not m.use_nodes or m.users == 0:
        continue
    kinds = sorted({n.bl_idname.replace('ShaderNode', '') for n in m.node_tree.nodes} - {'NodeReroute', 'NodeFrame'})
    print('[mat]', m.name, 'users', m.users, kinds)
    for img in images_in(m.node_tree):
        print('[mat]    ', img)
