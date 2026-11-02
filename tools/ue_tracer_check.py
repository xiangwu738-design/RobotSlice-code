"""11-02 用户：子弹（曳光）在打包版里都是黑的，编辑器里正常（kind=py）。
① 看 MI_Tracer 和它母材质的用途标记（实例化网格 / Nanite 等）和发光参数；② 翻打包版的运行日志，找缺用途标记、默认材质、Tracer 相关的行。日志标签 [tracer]。"""
import glob
import os
import time

import unreal

log = lambda *a: unreal.log_warning('[tracer] ' + ' '.join(str(x) for x in a))
mi = unreal.load_asset('/Game/Weapons/Materials/MI_Tracer')
log('MI_Tracer', mi, type(mi).__name__ if mi else '')
m = mi
chain = []
while m and isinstance(m, unreal.MaterialInstance):
    chain.append(m.get_path_name())
    try:
        for p in m.get_editor_property('vector_parameter_values'):
            log('  向量参数', m.get_name(), p.parameter_info.name, p.parameter_value)
        for p in m.get_editor_property('scalar_parameter_values'):
            log('  标量参数', m.get_name(), p.parameter_info.name, p.parameter_value)
    except Exception as e:
        log('  读参数失败', e)
    m = m.get_editor_property('parent')
if m:
    chain.append(m.get_path_name())
    for prop in ('used_with_instanced_static_meshes', 'used_with_nanite', 'used_with_static_lighting', 'used_with_skeletal_mesh', 'blend_mode', 'shading_model',
                 'two_sided', 'used_with_niagara_meshes', 'used_with_niagara_sprites'):
        try:
            log('  母材质', m.get_name(), prop, m.get_editor_property(prop))
        except Exception as e:
            log('  母材质', prop, '读不到', e)
log('继承链', ' → '.join(chain))
mesh = unreal.load_asset('/Engine/BasicShapes/Cylinder')
if mesh:
    try:
        log('Cylinder nanite', mesh.get_editor_property('nanite_settings').get_editor_property('enabled'))
    except Exception as e:
        log('Cylinder nanite 读不到', e)
pats = ['D:/RobotSlice_Build/Windows/RobotSlice/Saved/Logs/*.log', os.path.join(os.environ.get('LOCALAPPDATA', ''), 'RobotSlice', 'Saved', 'Logs', '*.log')]
files = sorted({f for p in pats for f in glob.glob(p)}, key=os.path.getmtime)[-4:]
keys = ('usage flag', 'Default Material', 'DefaultMaterial', 'Tracer', 'MI_Tracer', 'Failed to load', 'Shader', 'Cylinder')
for f in files:
    lines = open(f, 'rb').read().decode('utf-8', errors='replace').splitlines()
    hits = [l for l in lines if any(k in l for k in keys)]
    log('打包版日志', f, time.strftime('%m-%d %H:%M', time.localtime(os.path.getmtime(f))), len(lines), '行，相关', len(hits), '行')
    seen = set()
    for l in hits:
        k = l[l.find(']', 30) + 1:][:200] if ']' in l else l[:200]
        if k in seen:
            continue
        seen.add(k)
        log('  |', l.strip()[:240])
        if len(seen) >= 25:
            break
