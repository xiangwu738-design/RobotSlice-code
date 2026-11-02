"""肩甲擦头统计（10-31，大剑举刀过顶）：打开一台阵营机体的绑骨 .blend（lookdev/rig/factions/<台>.blend），把动作 FBX 一帧帧套上去，
用 BVH 查「头」（挂 head 的件）和「肩」（挂 clavicle_l/r、upperarm_l/r 的件，分左右）有没有互相穿进去，打印每个动作穿了几帧、最严重的帧。
参考姿势里本来就贴着的三角面对数当基线（超过才算）。
blender -b lookdev/rig/factions/<台>.blend -P tools/shoulder_head_clip.py -- <每秒取几帧> <动作.fbx 通配符> [...]"""
import glob
import json
import sys
from collections import defaultdict

import bpy
from mathutils.bvhtree import BVHTree

args = sys.argv[sys.argv.index('--') + 1:]
fps_take = float(args[0])
files = []
for pat in args[1:]:
    files += sorted(glob.glob(pat, recursive=True))
scene = bpy.context.scene
arm = next(o for o in scene.objects if o.type == 'ARMATURE')
robot = next(o for o in scene.objects if o.type == 'MESH' and o.parent == arm)
me = robot.data
me.calc_loop_triangles()
gname = {g.index: g.name for g in robot.vertex_groups}
vbone = [gname[max(v.groups, key=lambda g: g.weight).group] if v.groups else '' for v in me.vertices]
groups = defaultdict(list)
for t in me.loop_triangles:
    b = vbone[t.vertices[0]]
    if b == 'head':
        groups['头'].append(tuple(t.vertices))
    elif b in ('clavicle_l', 'upperarm_l'):
        groups['左肩'].append(tuple(t.vertices))
    elif b in ('clavicle_r', 'upperarm_r'):
        groups['右肩'].append(tuple(t.vertices))
PAIRS = [('头', '左肩'), ('头', '右肩')]
used = sorted({i for k in groups for tri in groups[k] for i in tri})


def trees():
    dg = bpy.context.evaluated_depsgraph_get()
    ev = robot.evaluated_get(dg)
    m = ev.to_mesh()
    co = {i: m.vertices[i].co.copy() for i in used}
    ev.to_mesh_clear()
    out = {}
    for k, tris in groups.items():
        idx = sorted({i for t in tris for i in t})
        remap = {i: j for j, i in enumerate(idx)}
        out[k] = BVHTree.FromPolygons([co[i] for i in idx], [[remap[i] for i in t] for t in tris], all_triangles=True)
    return out


arm.animation_data_create()
for pb in arm.pose.bones:
    pb.matrix_basis.identity()
base = trees()
base_hits = {p: len(base[p[0]].overlap(base[p[1]])) if p[0] in base and p[1] in base else 0 for p in PAIRS}
report = {'blend': bpy.data.filepath, 'tris': {k: len(v) for k, v in groups.items()}, 'base': {'×'.join(p): n for p, n in base_hits.items()}, 'clips': {}}
print('[shoulder] 三角面', report['tris'], '参考姿势', base_hits)
for f in files:
    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.fbx(filepath=f, automatic_bone_orientation=False)
    except Exception as e:
        print('[shoulder] 读不了', f, e)
        continue
    new = [o for o in bpy.data.objects if o not in before]
    src = next((o for o in new if o.type == 'ARMATURE'), None)
    act = src.animation_data.action if src and src.animation_data else None
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    if act is None:
        continue
    arm.animation_data.action = act
    if hasattr(arm.animation_data, 'action_slot') and getattr(act, 'slots', None):
        arm.animation_data.action_slot = act.slots[0]
    f0, f1 = act.frame_range
    src_fps = scene.render.fps / scene.render.fps_base  # FBX 导入会把场景帧率改成文件的
    name = f.replace('\\', '/').split('/')[-1][:-4]
    n = int((f1 - f0) / src_fps * fps_take)
    hits = []
    for i in range(n + 1):
        fr = f0 + i / fps_take * src_fps
        scene.frame_set(int(fr), subframe=fr - int(fr))
        t = trees()
        row = {}
        for p in PAIRS:
            if p[0] in t and p[1] in t:
                row[p[1]] = max(0, len(t[p[0]].overlap(t[p[1]])) - base_hits[p])
        if any(row.values()):
            hits.append((round(i / fps_take, 3), row))
    report['clips'][name] = {'frames': n + 1, 'hit': hits}
    worst = sorted(hits, key=lambda h: -sum(h[1].values()))[:5]
    print('[shoulder] %-36s %d/%d 帧擦头 最严重 %s' % (name, len(hits), n + 1, worst))
    bpy.data.actions.remove(act)
out = args[-1] if args[-1].endswith('.json') else None
if out:
    json.dump(report, open(out, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
