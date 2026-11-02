"""彩蛋底子模型普查（11-02）：Sketchfab 下载的机械狗 / 无人机放进 Inbox/_easter/dog 或 Inbox/_easter/drone 以后先跑这个。只读，不改原文件。
  blender -b --factory-startup -P tools/blender_easter_survey.py -- dog   [模型文件] [键=值 ...]
  blender -b --factory-startup -P tools/blender_easter_survey.py -- drone [模型文件] [键=值 ...]
不写模型文件就按 easter_<dog|drone>.json 的 base_file，再不写就在目录里自动找（.blend > .fbx > .glb > .gltf）。键=值 临时改配置（例 forward=+Y）。
输出（Inbox/_easter/survey/）：
  <dog|drone>_survey.json   文件、单位、每个物体（类型、父子、位置、尺寸、面数、材质、修改器）、骨架（每根骨头的位置）、动作（名字、帧数、动到哪些骨头）、
                            材质（有没有贴图 / 自发光、按规则会分到哪个槽）、自动判断的前方 / 缩放、狗的身体 / 腿分段、无人机的旋翼
  <dog|drone>/*.png + _sheet.json   转到游戏朝向（+X 前）、缩放到游戏尺寸后的六张图 + 分段 / 旋翼彩色图
  日志 Inbox/_easter/logs/survey_<dog|drone>_*.txt
然后 py -3 tools/easter_sheet.py Inbox/_easter/survey/<dog|drone> 拼成一张图。
"""
import json
import os
import sys

import bmesh
import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import easter_blender_lib as L  # noqa: E402


def survey():
    args = L.script_args()
    if not args or args[0] not in ('dog', 'drone'):
        raise L.EasterError('第一个参数要写 dog 或 drone')
    which = args[0]
    rest = args[1:]
    explicit = rest.pop(0) if rest and '=' not in rest[0] and not rest[0].startswith('--') else ''
    cfg = L.load_config(which, rest)
    path = L.find_base(which, explicit or cfg.get('base_file', ''))
    objs = L.load_base(path)
    sc = bpy.context.scene
    res = {'which': which, 'file': L.rel(path), 'size_mb': round(os.path.getsize(path) / 1e6, 2), 'blender': bpy.app.version_string,
           'unit_scale': sc.unit_settings.scale_length, 'fps': sc.render.fps, 'frame_range': [sc.frame_start, sc.frame_end],
           'objects': [], 'armatures': [], 'actions': [], 'materials': [], 'images': []}
    L.log(f'场景 {sc.name}，单位 {sc.unit_settings.system} ×{sc.unit_settings.scale_length}，{sc.render.fps} 帧/秒，帧 {sc.frame_start}..{sc.frame_end}')

    # ---- 物体
    for o in objs:
        e = {'name': o.name, 'type': o.type, 'parent': o.parent.name if o.parent else None,
             'parent_type': o.parent_type if o.parent else None, 'parent_bone': o.parent_bone or None,
             'loc_world': [round(x, 4) for x in o.matrix_world.translation], 'scale_world': [round(x, 4) for x in o.matrix_world.to_scale()],
             'hide_render': o.hide_render, 'modifiers': [(m.type, m.name, getattr(m, 'object', None) and m.object.name) for m in getattr(o, 'modifiers', [])],
             'anim': bool(o.animation_data and o.animation_data.action)}
        if o.type == 'MESH':
            me = o.data
            b = L.obj_box(o)
            e.update(verts=len(me.vertices), tris=L.tris_of(me), materials=[s.material.name if s.material else None for s in o.material_slots],
                     vertex_groups=len(o.vertex_groups), shape_keys=bool(me.shape_keys), uv_layers=len(me.uv_layers),
                     color_attributes=[c.name for c in me.color_attributes],
                     box_min=[round(x, 4) for x in b.lo] if b else None, box_max=[round(x, 4) for x in b.hi] if b else None)
            L.log(f'  网格 {o.name}：{e["verts"]} 顶点 {e["tris"]} 三角，材质 {e["materials"]}，顶点组 {e["vertex_groups"]}，修改器 {e["modifiers"]}，'
                  f'父 {e["parent"]}{"/" + o.parent_bone if o.parent_bone else ""}，盒 {b and (tuple(round(x, 3) for x in b.lo), tuple(round(x, 3) for x in b.hi))}'
                  f'{"，不渲染" if o.hide_render else ""}')
        else:
            L.log(f'  {o.type} {o.name}：父 {e["parent"]}，位置 {e["loc_world"]}，缩放 {e["scale_world"]}')
        res['objects'].append(e)

    # ---- 骨架
    for a in [o for o in objs if o.type == 'ARMATURE']:
        bones = []
        for b in a.data.bones:
            h = a.matrix_world @ b.head_local
            t = a.matrix_world @ b.tail_local
            bones.append({'name': b.name, 'parent': b.parent.name if b.parent else None, 'deform': b.use_deform,
                          'head': [round(x, 4) for x in h], 'tail': [round(x, 4) for x in t], 'length': round((t - h).length, 4)})
        res['armatures'].append({'name': a.name, 'bones': bones, 'pose_position': a.data.pose_position})
        L.log(f'  骨架 {a.name}：{len(bones)} 根骨头（变形 {sum(b["deform"] for b in bones)}）')
        for b in bones[:200]:
            L.log(f'    {b["name"]} ← {b["parent"]} 头 {b["head"]} 尾 {b["tail"]}{"" if b["deform"] else "（不变形）"}')

    # ---- 动作
    for act in bpy.data.actions:
        fcs = L.action_fcurves(act)
        bones = sorted({fc.data_path.split('"')[1] for fc in fcs if fc.data_path.startswith('pose.bones["')})
        fr = list(act.frame_range)
        e = {'name': act.name, 'frames': [round(fr[0], 2), round(fr[1], 2)], 'fcurves': len(fcs), 'bones': bones[:80], 'n_bones': len(bones),
             'object_channels': sorted({fc.data_path for fc in fcs if not fc.data_path.startswith('pose.')}),
             'slots': [s.name_display for s in getattr(act, 'slots', [])]}
        res['actions'].append(e)
        L.log(f'  动作 {act.name}：帧 {e["frames"]}（{(fr[1] - fr[0]) / max(sc.render.fps, 1):.2f} 秒），曲线 {len(fcs)}，骨头 {len(bones)}，'
              f'物体通道 {e["object_channels"]}，槽 {e["slots"]}')
    if not res['actions']:
        L.log('  没有动作（狗走路由代码程序化驱动，不要紧）')

    # ---- 材质 / 贴图
    rules = cfg['base_slot_rules']
    for m in bpy.data.materials:
        if not m.users:
            continue
        tr = L.mat_traits(m)
        slot, glow, why = L.classify_material(m, rules)
        imgs = []
        if m.use_nodes and m.node_tree:
            imgs = [n.image.name for n in m.node_tree.nodes if n.bl_idname == 'ShaderNodeTexImage' and n.image]
        res['materials'].append({'name': m.name, **tr, 'images': imgs, 'slot': slot, 'glow': glow, 'rule': why})
        L.log(f'  材质 {m.name}：贴图 {imgs}，自发光 {tr["emission"]}，底色亮度 {tr["lum"]:.2f} → {slot}{"（灯）" if glow else ""} [{why}]')
    for i in bpy.data.images:
        if i.type != 'IMAGE':
            continue
        ok = bool(i.packed_file) or os.path.exists(bpy.path.abspath(i.filepath))
        res['images'].append({'name': i.name, 'size': list(i.size), 'packed': bool(i.packed_file), 'exists': ok})
        if not ok:
            L.warn(f'贴图丢了：{i.name} {i.filepath}（预览会是纯色，不影响建模）')

    # ---- 摆到游戏朝向和尺寸（和 build 一样的算法）
    meshes, arms, info = L.prepare_base(objs, cfg, which)
    res['auto'] = {'forward': info['forward'], 'forward_how': info['forward_how'], 'up': info['up'], 'scale': info['scale'],
                   'game_box': info['box'].to_json()}
    work, _srcs = L.bake_base(meshes)
    res['tris_total'] = sum(L.tris_of(w.data) for w in work)
    out_dir = os.path.join(L.DIRS['survey'], which)
    sheet = []
    note = f'前方 {info["forward"]}（{info["forward_how"]}）×{info["scale"]:.3f}'
    try:
        L.render_views(work, out_dir, which, ['front34', 'side', 'front', 'top', 'rear34'], sheet, color_type='TEXTURE', note=note)
    except Exception as ex:
        L.warn(f'预览渲染失败（不影响普查）：{ex}')

    legend = []
    if which == 'dog':
        coords = [v for w in work for v in L.mesh_coords(w.data)]
        bb, knee, cx, cy = L.dog_split(coords, cfg)
        res['dog'] = {'body_bottom_m': round(bb / L.M, 4), 'knee_m': round(knee / L.M, 4), 'body_center_m': [round(cx / L.M, 4), round(cy / L.M, 4)]}
        # 按分段把面拆成彩色物体，看分得对不对
        groups = {}
        for w in work:
            bm = bmesh.new()
            bm.from_mesh(w.data)
            cls = {}
            for f in bm.faces:
                c = L.dog_class(f.calc_center_median(), bb, knee, cx, cy)
                cls.setdefault(c, []).append(f.index)
            bm.free()
            for c, ids in cls.items():
                groups.setdefault(c, []).append(L.subset_object(w, ids, f'{c}_{w.name}'))
        counts = {c: sum(len(o.data.polygons) for o in g) for c, g in groups.items()}
        res['dog']['faces_per_class'] = counts
        L.log(f'分段（面数）：{counts}')
        missing = [f'leg_{k}_{s}' for k in L.LEG_KEYS for s in ('upper', 'lower') if f'leg_{k}_{s}' not in counts]
        if missing:
            L.warn(f'有的腿段是空的：{missing}（前方 / 身体底 / 膝盖可能不对）')
        legend = L.color_legend(sorted(groups.items()))
        try:
            L.render_views([o for g in groups.values() for o in g], out_dir, 'dog_分段', ['front34', 'side', 'top'], sheet, color_type='OBJECT',
                           note=f'身体底 {bb / L.M:.2f} 米 膝盖 {knee / L.M:.2f} 米')
        except Exception as ex:
            L.warn(f'分段图渲染失败：{ex}')
    else:
        rotors = L.find_rotors(work, cfg)
        res['drone'] = {k: {'hub_m': [round(x / L.M, 4) for x in r['hub']], 'dia_m': round(r['dia'] / L.M, 4), 'parts': len(r['parts']), 'how': r['how']}
                        for k, r in rotors.items()}
        if len(rotors) < 4:
            L.warn(f'只找到 {len(rotors)} 个旋翼 {sorted(rotors)}：看日志里的散件清单，配置 rotor_objects（物体名正则）或 rotor_hubs_m + rotor_radius_m')
        parts = L.split_rotors(rotors)
        groups = [(f'旋翼 {k}', [o]) for k, o in sorted(parts.items())] + [('机身（其余）', work)]
        legend = L.color_legend(groups)
        try:
            L.render_views([o for _, g in groups for o in g], out_dir, 'drone_旋翼', ['front34', 'top'], sheet, color_type='OBJECT')
        except Exception as ex:
            L.warn(f'旋翼图渲染失败：{ex}')
    L.write_sheet(out_dir, sheet, f'彩蛋底子普查 {which}：{os.path.basename(path)}', legend, [note, f'三角面 {res["tris_total"]}'])
    os.makedirs(L.DIRS['survey'], exist_ok=True)
    jp = os.path.join(L.DIRS['survey'], f'{which}_survey.json')
    json.dump(res, open(jp, 'w', encoding='utf-8'), ensure_ascii=False, indent=1, default=str)
    L.log(f'普查结果 {L.rel(jp)}；游戏尺寸包围盒 {info["box"]}')


if __name__ == '__main__':
    which = next((a for a in L.script_args() if a in ('dog', 'drone')), 'x')
    L.run_main('easter', f'survey_{which}', survey)
