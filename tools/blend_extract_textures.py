"""从 Blender 材质库里把指定材质用到的内嵌贴图原样存出来（不解包到 .blend 旁边，不跑库里的脚本）。
blender -b --factory-startup --disable-autoexec 库.blend -P blend_extract_textures.py -- 输出目录 材质名1 材质名2 ...
每个材质一个子目录，文件名保持原名；另写 _nodes.txt 记下贴图接到了哪个输入（判断哪张是颜色 / 法线 / 粗糙度）。
"""
import os
import sys

import bpy

args = sys.argv[sys.argv.index('--') + 1:]
out, names = args[0], args[1:]


def walk(tree, seen, rows):
    for n in tree.nodes:
        if n.bl_idname == 'ShaderNodeTexImage' and n.image:
            outs = sorted({f'{l.to_node.bl_idname.replace("ShaderNode", "")}.{l.to_socket.name}' for o in n.outputs for l in o.links})
            rows.append((n.image, n.image.colorspace_settings.name, outs))
        elif n.bl_idname == 'ShaderNodeGroup' and n.node_tree and n.node_tree.name not in seen:
            seen.add(n.node_tree.name)
            walk(n.node_tree, seen, rows)


for name in names:
    m = bpy.data.materials.get(name)
    if not m or not m.use_nodes:
        print('[extract] 没有材质', name, flush=True)
        continue
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    rows = []
    walk(m.node_tree, set(), rows)
    lines = []
    for img, cs, outs in rows:
        fn = os.path.basename(bpy.path.abspath(img.filepath)) or (img.name + '.png')
        path = os.path.join(d, fn)
        if img.packed_file:
            with open(path, 'wb') as f:
                f.write(img.packed_file.data)
        elif os.path.exists(bpy.path.abspath(img.filepath)):
            with open(bpy.path.abspath(img.filepath), 'rb') as s, open(path, 'wb') as f:
                f.write(s.read())
        else:
            print('[extract] 缺图', name, img.name, flush=True)
            continue
        lines.append(f'{fn}\t{tuple(img.size)}\t{cs}\t{",".join(outs)}')
    with open(os.path.join(d, '_nodes.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('[extract]', name, len(lines), '张', flush=True)
