"""彩蛋外壳零件来源普查（11-02）：看阵营机体每根骨头上挂了哪些零件、组件库 .blend 在哪，用来改 easter_dog.json / easter_drone.json 的 pieces。只读。
  blender -b --factory-startup -P tools/blender_easter_parts.py -- find
      在工程里找可能的零件来源：阵营机体 .blend（lookdev/rig/factions）、机甲组件库 .blend（文件名含 机甲 / 零件 / mech / kit / part）、
      肩部无人机的 FBX（Inbox/_肩部候选/游戏模型*）。结果 Inbox/_easter/parts/find.json
  blender -b --factory-startup -P tools/blender_easter_parts.py -- PHASE_L
      这台阵营机体按骨头分组：每组的三角面、散件数、尺寸、材质 → 日志 + 每组一张图（Inbox/_easter/parts/PHASE_L/）
  blender -b --factory-startup -P tools/blender_easter_parts.py -- PHASE_L head
      只看骨头 head（正则，全匹配）上的散件：按大小编号（就是零件 src.pick 写数字时的编号），每块一张图
  blender -b --factory-startup -P tools/blender_easter_parts.py -- PHASE_L @MI_Robot_Glow
      按材质（@ 后面是正则）看散件：灯条 / 镜头这种
然后 py -3 tools/easter_sheet.py Inbox/_easter/parts/<台>[_<骨头>] 拼图。
组件库（机甲 / 机甲2 / 机械零件2 …）用现成的：blender -b 库.blend --python tools/blend_asset_survey.py -- 输出目录 → py -3 -I tools/blend_survey_sheet.py 输出目录的上一级。
"""
import json
import os
import re
import sys

import bmesh
import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import easter_blender_lib as L  # noqa: E402

KIT_RE = r'(?i)机甲|零件|组件|mech|kit|part|robot'


def find():
    res = {'factions': [], 'kits': [], 'fbx': []}
    fdir = os.path.join(L.ROOT, 'lookdev', 'rig', 'factions')
    if os.path.isdir(fdir):
        res['factions'] = sorted(L.rel(os.path.join(fdir, f)) for f in os.listdir(fdir) if f.endswith('.blend'))
    else:
        L.warn(f'没有 {fdir}（阵营机体的 .blend 不在这里的话，配置 faction_blend 要改）')
    L.log(f'阵营机体 .blend：{res["factions"] or "没找到"}')
    for top in ('Inbox', 'lookdev', 'SourceAssets'):
        base = os.path.join(L.ROOT, top)
        for dp, dn, fn in os.walk(base):
            depth = dp[len(base):].count(os.sep)
            if depth > 5:
                dn[:] = []
                continue
            for f in fn:
                p = os.path.join(dp, f)
                if f.lower().endswith('.blend') and re.search(KIT_RE, f) and 'factions' not in dp:
                    res['kits'].append({'path': L.rel(p), 'mb': round(os.path.getsize(p) / 1e6, 1),
                                        'has_cats': os.path.exists(os.path.join(dp, 'blender_assets.cats.txt'))})
                if f.lower().endswith('.fbx') and '肩部候选' in dp:
                    res['fbx'].append(L.rel(p))
    for k in res['kits']:
        L.log(f'组件库候选 {k["path"]}（{k["mb"]} MB{"，有资产分类" if k["has_cats"] else ""}）')
    if not res['kits']:
        L.warn('没找到文件名像组件库的 .blend（README：肩部无人机的零件来自「机甲」「机甲2」「机械零件2」这几个库）；'
               'kit_dirs 里加它们所在的目录，或零件 src.lib 写全路径')
    for f in res['fbx']:
        L.log(f'肩部 FBX {f}')
    os.makedirs(L.DIRS['parts'], exist_ok=True)
    p = os.path.join(L.DIRS['parts'], 'find.json')
    json.dump(res, open(p, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    L.log(f'写了 {L.rel(p)}')


def faction(key, sel):
    cfg = L.load_config('dog', [])
    o = L.faction_object(cfg, key)
    bm, dom = L.faction_bmesh(o)
    mats = list(o.data.materials)
    bm.faces.ensure_lookup_table()
    groups = {}
    for f in bm.faces:
        b = L.face_bone(f, dom)
        mn =mats[f.material_index].name if f.material_index < len(mats) and mats[f.material_index] else ''
        if sel is None:
            k = b or '(无骨头)'
        elif sel.startswith('@'):
            if not re.search(sel[1:], mn):
                continue
            k = 'sel'
        else:
            if not re.fullmatch(sel, b or ''):
                continue
            k = 'sel'
        groups.setdefault(k, []).append(f.index)
    if not groups:
        raise L.EasterError(f'{key} 里没有匹配 {sel!r} 的面')
    tag = key if sel is None else f'{key}_{re.sub(r"[^A-Za-z0-9]+", "_", sel).strip("_")}'
    out_dir = os.path.join(L.DIRS['parts'], tag)
    sheet = []
    info = []
    src = L.obj_from_bm(f'{key}_all', bm, mats)
    bm.free()
    src.hide_render = True
    if sel is None:
        for b, ids in sorted(groups.items(), key=lambda kv: -len(kv[1])):
            part = L.subset_object(src, ids, f'{key}_{b}')
            pbm = bmesh.new()
            pbm.from_mesh(part.data)
            comps = L.components(pbm)
            pbm.free()
            box = L.obj_box(part)
            mcount = {}
            for p in part.data.polygons:
                m = part.data.materials[p.material_index] if p.material_index < len(part.data.materials) else None
                mcount[m.name if m else None] = mcount.get(m.name if m else None, 0) + 1
            e = {'bone': b, 'tris': L.tris_of(part.data), 'parts': len(comps), 'size': [round(x, 1) for x in box.size], 'materials': mcount}
            info.append(e)
            L.log(f'  骨头 {b}：{e["tris"]} 三角，散件 {e["parts"]} 块，尺寸 {e["size"]}（机体单位），材质 {mcount}')
            try:
                L.render_views([part], out_dir, b, [('front34', ('左前 3/4', (1.0, -0.85, 0.55)))], sheet, color_type='MATERIAL', res=(480, 480),
                               note=f'{e["tris"]} 三角 {e["parts"]} 块')
            except Exception as ex:
                L.warn(f'{b} 渲染失败：{ex}')
            part.hide_render = True
    else:
        part = L.subset_object(src, groups['sel'], f'{key}_sel')
        pbm = bmesh.new()
        pbm.from_mesh(part.data)
        pbm.faces.index_update()
        comps = L.components(pbm)
        # 排序和 easter_blender_lib.pick_parts 一样（包围盒对角线从大到小），编号就是 src.pick 的数字
        st = sorted(((c, L.part_stats(c)) for c in comps), key=lambda cs: -cs[1]['diag'])
        ids_list = [[f.index for f in c] for c, _ in st]
        stats = [s for _, s in st]
        pbm.free()
        objs = [L.subset_object(part, ids, f'{tag}_{i:02d}') for i, ids in enumerate(ids_list)]
        part.hide_render = True
        for i, (ob, s) in enumerate(zip(objs, stats)):
            e = {'pick': i, 'tris': s['tris'], 'size': [round(x, 1) for x in s['box'].size], 'long': round(s['long'], 2), 'round': round(s['round'], 2),
                 'flat': round(s['flat'], 2), 'center': [round(x, 1) for x in s['box'].center]}
            info.append(e)
            L.log(f'  pick {i}：{e["tris"]} 三角，尺寸 {e["size"]}，细长 {e["long"]} 圆 {e["round"]} 扁 {e["flat"]}，中心 {e["center"]}')
            if i < 48:
                try:
                    L.render_views([ob], out_dir, f'{i:02d}', [('front34', ('左前 3/4', (1.0, -0.85, 0.55)))], sheet, color_type='MATERIAL', res=(400, 400),
                                   note=f'pick {i} · {e["tris"]} 三角 · 细长 {e["long"]}')
                except Exception as ex:
                    L.warn(f'pick {i} 渲染失败：{ex}')
            ob.hide_render = True
        L.log(f'自动挑法对应：largest = pick 0；longest = 细长最大；roundest = 圆最大（小于最大块 8% 的碎件不参加）')
    L.write_sheet(out_dir, sheet, f'零件来源 {key} {sel or "按骨头"}（{len(info)} 组）')
    json.dump(info, open(os.path.join(out_dir, '_parts.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)


def main():
    args = L.script_args()
    if not args:
        raise L.EasterError('参数：find | <阵营台名，如 PHASE_L> [骨头正则 | @材质正则]')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if args[0] == 'find':
        find()
    else:
        faction(args[0], args[1] if len(args) > 1 else None)


if __name__ == '__main__':
    L.run_main('easter', 'parts_' + '_'.join(re.sub(r'[^A-Za-z0-9]+', '', a) for a in L.script_args()[:2]), main)
