"""把 tools/shoulder_head_proxy.py 给九台算的简化碰撞体（头几个球、两边肩 / 上臂的采样点）拼成 C++ 表 Source/RobotSlice/MechShoulderClearData.inl。
py -3 tools/shoulder_head_table.py <放 proxy_<台>.json 的目录>"""
import json
import os
import sys

SRC = sys.argv[1]
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'Source', 'RobotSlice', 'MechShoulderClearData.inl')
KEYS = ['NEC_L', 'NEC_M', 'NEC_H', 'VHI_L', 'VHI_M', 'VHI_H', 'PHASE_L', 'PHASE_M', 'PHASE_H']
lines = ['// 自动生成（tools/shoulder_head_table.py，数据来自 tools/shoulder_head_proxy.py 在 lookdev/rig/factions/<台>.blend 上算的），别手改。',
         '// 每台：头的几个球（网格绑定姿势坐标，厘米：前 +Y、左 +X、上 +Z；半径）、左 / 右肩上臂的采样点（坐标 + 挂在哪根骨头：0 锁骨、1 上臂）。',
         '']
body = []
for k in KEYS:
    p = os.path.join(SRC, f'proxy_{k}.json')
    if not os.path.isfile(p):
        print('缺', p)
        continue
    d = json.load(open(p, encoding='utf-8'))
    hs = d['head_spheres']
    lines.append(f'static const float {k}_Head[][4] = {{' + ', '.join('{%.2ff, %.2ff, %.2ff, %.2ff}' % tuple(h) for h in hs) + '};')
    for s in ('l', 'r'):
        pts = d['arm'][s]
        rows = ['{%.1ff, %.1ff, %.1ff, %d}' % (x, y, z, 0 if b.startswith('clavicle') else 1) for x, y, z, b in pts]
        lines.append(f'static const float {k}_Arm_{s}[][4] = {{')
        for i in range(0, len(rows), 8):
            lines.append('\t' + ', '.join(rows[i:i + 8]) + ',')
        lines.append('};')
    body.append(f'\t{{TEXT("{k}"), {k}_Head, UE_ARRAY_COUNT({k}_Head), {{{k}_Arm_l, {k}_Arm_r}}, {{UE_ARRAY_COUNT({k}_Arm_l), UE_ARRAY_COUNT({k}_Arm_r)}}}},')
    print(k, '头球', len(hs), '肩点', len(d['arm']['l']), len(d['arm']['r']), '验证', d.get('total'))
lines.append('')
lines.append('static const FShoulderProxy ShoulderProxies[] = {')
lines += body
lines.append('};')
open(OUT, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('写好', OUT)
