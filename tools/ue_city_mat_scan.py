"""城区图（L_City）材质体检：找出用引擎默认材质（WorldGridMaterial / DefaultMaterial 等 /Engine/ 下的）或灰色占位材质的网格。
两部分：
  1. 关卡：每个静态网格组件的每个槽，实际生效的材质（组件覆盖 > 网格资产自己的槽）。
  2. 资产：/Game/Env/City/Meshes、/Game/Env/City/Buildings 下每个网格资产自己槽上挂的材质（ue_import_city.py fix 那步写的，坑①说存不住）。
结果写 Saved/city_mat_scan.json，日志 [matscan]。不改任何东西。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript "-script=D:/RobotSlice/tools/ue_city_mat_scan.py [地图]" -nullrhi -unattended -nosplash
"""
import json
import sys

import unreal

MAP = next((a for a in sys.argv[1:] if a.startswith('/Game/')), '/Game/Maps/L_City')
OUT = 'D:/RobotSlice/Saved/city_mat_scan.json'
eas = unreal.EditorAssetLibrary
GRAY_WORDS = ('worldgrid', 'defaultmaterial', 'default_material', 'placeholder', 'basicshape', 'gray', 'grey', 'checker')


def log(msg):
    unreal.log(f'[matscan] {msg}')


def bad(path):
    """引擎默认 / 灰色占位材质"""
    if not path:
        return 'None'
    low = path.lower()
    if low.startswith('/engine/'):
        return 'engine'
    if any(w in low for w in GRAY_WORDS):
        return 'gray'
    return ''


def mat_path(m):
    return m.get_path_name().split('.')[0] if m else ''


# ---------------------------------------------------------------- 资产
assets = {}
for folder in ('/Game/Env/City/Meshes', '/Game/Env/City/Buildings'):
    for p in eas.list_assets(folder, recursive=False, include_folder=False):
        a = eas.load_asset(p.split('.')[0])
        if not isinstance(a, unreal.StaticMesh):
            continue
        slots = []
        for s in a.get_editor_property('static_materials'):
            slots.append([str(s.get_editor_property('material_slot_name')), mat_path(s.get_editor_property('material_interface'))])
        assets[a.get_path_name().split('.')[0]] = slots
asset_bad = {k: [s for s in v if bad(s[1])] for k, v in assets.items()}
asset_bad = {k: v for k, v in asset_bad.items() if v}
n_slots = sum(len(v) for v in assets.values())
log(f'资产：网格 {len(assets)} 个、槽 {n_slots} 个，自己槽上是默认 / 空材质的网格 {len(asset_bad)} 个、槽 {sum(len(v) for v in asset_bad.values())} 个')
for k in list(asset_bad)[:4]:
    log(f'  例 {k}: {asset_bad[k][:3]}')

# ---------------------------------------------------------------- 关卡
unreal.EditorLoadingAndSavingUtils.load_map(MAP)
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
hist = {}
problems = {}
n_comp = 0
for actor in sub.get_all_level_actors():
    for c in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = c.get_editor_property('static_mesh')
        if not mesh:
            continue
        n_comp += 1
        vis = c.is_visible() and not actor.is_hidden_ed()
        try:
            hidden_game = bool(actor.get_editor_property('hidden'))
        except Exception:
            hidden_game = False
        ext = actor.get_actor_bounds(False)[1]
        size = [round(ext.x * 2 / 100, 1), round(ext.y * 2 / 100, 1), round(ext.z * 2 / 100, 1)]
        mslots = mesh.get_editor_property('static_materials')
        for i in range(c.get_num_materials()):
            m = c.get_material(i)
            mp = mat_path(m)
            slot = str(mslots[i].get_editor_property('material_slot_name')) if i < len(mslots) else f'#{i}'
            h = hist.setdefault(mp, {'n': 0, 'area': 0.0})
            h['n'] += 1
            h['area'] += size[0] * size[1] + size[1] * size[2] + size[0] * size[2]
            why = bad(mp)
            if why:
                key = f'{mesh.get_path_name().split(".")[0]} | {slot}'
                e = problems.setdefault(key, {'mat': mp, 'why': why, 'actors': [], 'size': size, 'hidden': hidden_game or not vis})
                e['actors'].append(actor.get_actor_label())
log(f'关卡 {MAP}：静态网格组件 {n_comp} 个；用默认 / 占位材质的（网格 | 槽）{len(problems)} 种')
for k, v in sorted(problems.items(), key=lambda kv: -max(kv[1]['size'])):
    log(f'  {k} → {v["mat"]}（{v["why"]}）{len(v["actors"])} 处，大小 {v["size"]} 米，隐藏 {v["hidden"]}，例 {v["actors"][:3]}')
top = sorted(hist.items(), key=lambda kv: -kv[1]['area'])[:40]
for k, v in top:
    log(f'  材质 {k}: {v["n"]} 个槽，包围盒面积和 {v["area"]:.0f}')
json.dump({'map': MAP, 'asset_bad': asset_bad, 'assets': assets, 'problems': problems,
           'hist': {k: v for k, v in hist.items()}}, open(OUT, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
log(f'写好 {OUT}')
