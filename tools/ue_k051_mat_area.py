"""统计相位电子园区里每种 K051 材质占多少面积（按布局表摆放次数和缩放加权），分朝上的面 / 竖直面 / 朝下的面。
用来决定调哪个材质实例会影响哪些大面（比如桥面、基地台面、楼体立面是哪种材质）。只读，不改资源。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_k051_mat_area.py" -nullrhi
"""
import json
from collections import defaultdict

import unreal

DEST = '/Game/Env/TechPark/Meshes'
L = json.load(open('D:/RobotSlice/Inbox/_k051/layout.json', encoding='utf-8'))
count = defaultdict(float)
for it in L['items']:
    if it['piece'].startswith(('TPB_', 'BRAND_')):
        continue
    k = it['piece'] + ('_No' + ''.join(it['skip']) if it.get('skip') else '') + ('_Only' + ''.join(it['only']) if it.get('only') else '')
    count[k] += it['scale'][0] * it['scale'][1]   # 面积按缩放平方
eas = unreal.EditorAssetLibrary
sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.StaticMeshEditorSubsystem()
tot = defaultdict(lambda: [0.0, 0.0, 0.0])
per = defaultdict(lambda: defaultdict(lambda: [0.0, 0.0, 0.0]))
for k, w in count.items():
    for cls in ('Solid', 'Deco'):
        p = f'{DEST}/SM_HTS_{k}_{cls}'
        if not eas.does_asset_exist(p):
            continue
        sm = eas.load_asset(p)
        mats = sm.get_editor_property('static_materials')
        n = sm.get_num_sections(0)
        for s in range(n):
            v, tri, nrm, uv, tan = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(sm, 0, s)
            idx = sme.get_lod_material_slot(sm, 0, s) if hasattr(sme, 'get_lod_material_slot') else s   # 拿不到映射时按导入顺序：第 s 段 = 第 s 个材质槽
            slot = str(mats[min(idx, len(mats) - 1)].get_editor_property('material_slot_name')).replace('KB3D_HTS_', '')
            for i in range(0, len(tri), 3):
                a, b, c = v[tri[i]], v[tri[i + 1]], v[tri[i + 2]]
                cr = (b - a).cross(c - a)
                area = cr.length() * 0.5 / 10000.0   # 平方米
                nz = cr.z / max(cr.length(), 1e-6)
                j = 0 if nz > 0.7 else (2 if nz < -0.7 else 1)
                tot[slot][j] += area * w
                per[k][slot][j] += area * w
unreal.log('[matarea] 材质  朝上 / 竖直 / 朝下（平方米，已按摆放次数和缩放加权）')
for slot, (u, s, d) in sorted(tot.items(), key=lambda x: -sum(x[1])):
    unreal.log(f'[matarea] {slot:16s} {u:9.0f} {s:9.0f} {d:9.0f}')
for k in sorted(per):
    if k.startswith(('Road', 'Bldg')):
        top = sorted(per[k].items(), key=lambda x: -x[1][0])[:3]
        side = sorted(per[k].items(), key=lambda x: -x[1][1])[:3]
        unreal.log(f'[matarea] {k:22s} 朝上: ' + ', '.join(f'{a} {b[0]:.0f}' for a, b in top) + ' | 竖直: ' + ', '.join(f'{a} {b[1]:.0f}' for a, b in side))
