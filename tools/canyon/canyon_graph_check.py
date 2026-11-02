"""Blender：拿 GraphCanyon 的路点连线去打地形（视觉块 + 碰撞块），看有没有被挡住。
每条边在路点上方 120 / 220 厘米各打一条射线（坡道边沿坡面走）；吊笼边、闸门边跳过（中间是动的东西）。
blender -b --factory-startup --python canyon_graph_check.py"""
import json, os
import numpy as np
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

MESH = r"D:\RobotSlice\Inbox\_canyon\mesh"
L = 900.0


def load(tag):
    lay = json.load(open(os.path.join(MESH, f"{tag}_layout.json")))
    V = []; F = []; off = 0
    for t in lay["tiles"]:
        d = np.load(os.path.join(MESH, f"{tag}_tile{t['tile']}.npz"))
        V.append(d["verts"]); F.append(d["tris"] + off); off += len(d["verts"])
    V = np.concatenate(V); F = np.concatenate(F)
    return BVHTree.FromPolygons([Vector(v) for v in V], [tuple(f) for f in F])


nodes = {}
edges = []
def N(name, x, y, z):
    nodes[name] = (x, y, z)
    return name
def E(a, b, skip=False):
    edges.append((a, b, skip))

N('Foundry', 0, 0, 0); N('C0', 0, -1000, 0); N('C1', 0, 1000, 0); N('CaveDoorC', 0, 2200, 0); N('CaveC', 0, 3200, 0)
N('Bridge', 0, 0, L); N('BridgeS', 0, -2150, L); N('BridgeN', 0, 2150, L)
E('Foundry', 'C0'); E('Foundry', 'C1'); E('C0', 'C1'); E('C1', 'CaveDoorC'); E('CaveDoorC', 'CaveC'); E('Bridge', 'BridgeS'); E('Bridge', 'BridgeN')
for S in (-1, 1):
    k = 'W' if S < 0 else 'E'
    n = lambda nm, x, y, z: N(nm + k, S * x, y, z)
    FS1 = n('FS1', 3000, -1000, 0); FS2 = n('FS2', 6000, -1000, 0); FN1 = n('FN1', 3000, 1000, 0); FN2 = n('FN2', 6000, 1000, 0)
    SA = n('StationA', 8800, 1000, 0); SP = n('Spawn', 12000, 0, 0)
    RSB = n('RampSBot', 1250, -1500, 0); RST = n('RampSTop', 3750, -1500, L); LS1 = n('LedgeS1', 3750, -2150, L); LS2 = n('LedgeS2', 6000, -2150, L)
    SB = n('StationB', 8200, -2150, L); LB = n('LiftBot', 7400, -1350, 0); LT = n('LiftTop', 7400, -1350, L)
    RNB = n('RampNBot', 1250, 1500, 0); RNT = n('RampNTop', 3750, 1500, L); LN1 = n('LedgeN1', 3750, 2150, L); LN2 = n('LedgeN2', 8000, 2150, L)
    C1_ = n('Cave1', 3000, 3200, 0); CG = n('CaveGate', 4500, 3200, 0); C2_ = n('Cave2', 6500, 3200, 0); C3_ = n('Cave3', 8000, 3200, 0)
    CDH = n('CaveDoorH', 8000, 2200, 0)
    for a, b in [('C0', FS1), (FS1, FS2), ('C1', FN1), (FN1, FN2), (FS1, FN1), (FN2, SA), (FS2, SA), (SA, SP), (FS2, SP),
                 ('C0', RSB), (FS1, RSB), (RSB, RST), (RST, LS1), (LS1, LS2), (LS2, SB), ('BridgeS', LS1),
                 ('CaveC', C1_), (C2_, C3_), (C3_, CDH), (CDH, SA), (CDH, FN2),
                 (FS2, LB), (LT, SB), (LT, LS2), ('C1', RNB), (FN1, RNB), (RNB, RNT), (RNT, LN1), (LN1, LN2), ('BridgeN', LN1)]:
        E(a, b)
    E(C1_, CG, True); E(CG, C2_, True); E(LB, LT, True)

out = {}
for tag in ('vis', 'col'):
    bvh = load(tag)
    bad = []
    for a, b, skip in edges:
        if skip:
            continue
        pa, pb = Vector(nodes[a]), Vector(nodes[b])
        for h in (120.0, 220.0):
            s, e = pa + Vector((0, 0, h)), pb + Vector((0, 0, h))
            d = e - s
            hit = bvh.ray_cast(s, d.normalized(), d.length)
            if hit[0] is not None:
                bad.append(dict(edge=f'{a}->{b}', h=h, at=[round(v) for v in hit[0]], frac=round(hit[3] / d.length, 2)))
    out[tag] = bad
    print(f'[graph:{tag}] 边 {sum(1 for e in edges if not e[2])}，被挡 {len(bad)}')
    for b in bad:
        print('   ', b)
json.dump(out, open(os.path.join(MESH, 'graph_check.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
