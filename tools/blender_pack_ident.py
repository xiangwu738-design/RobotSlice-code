"""11-02 认素材包（只读，不保存）：blender -b 文件.blend -P tools/blender_pack_ident.py -- 输出.json
统计材质 / 物体 / 集合名里的前缀（Kitbash3D 的材质名是 KB3D_<套装代号>_...），以及文件里写的作者、贴图文件名前缀，帮着找原产品。"""
import bpy
import collections
import json
import os
import re
import sys

out = sys.argv[sys.argv.index('--') + 1]
pref = collections.Counter()
for m in bpy.data.materials:
    mm = re.match(r'([A-Za-z0-9]+_[A-Za-z0-9]+)_', m.name)
    if mm:
        pref[mm.group(1)] += 1
imgs = collections.Counter()
for im in bpy.data.images:
    n = os.path.basename(im.filepath or im.name)
    mm = re.match(r'([A-Za-z0-9]+_[A-Za-z0-9]+)_', n)
    if mm:
        imgs[mm.group(1)] += 1
info = {
    'file': bpy.data.filepath,
    'material_prefixes': pref.most_common(8),
    'image_prefixes': imgs.most_common(8),
    'collections': [c.name for c in bpy.data.collections][:25],
    'objects_sample': [o.name for o in bpy.data.objects][:15],
    'n_objects': len(bpy.data.objects), 'n_materials': len(bpy.data.materials),
    'scene_names': [s.name for s in bpy.data.scenes],
    'texts': [t.name for t in bpy.data.texts][:10],
}
json.dump(info, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
