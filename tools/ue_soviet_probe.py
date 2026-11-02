"""苏联板楼：看下雪粒子的参数（Cascade 模块）和冬季包网格的材质 / 碰撞情况。只读。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_soviet_probe.py" -nullrhi
"""
import unreal

eas = unreal.EditorAssetLibrary
out = open('D:/RobotSlice/Inbox/_k62/probe.txt', 'w', encoding='utf-8')


def p(*a):
    out.write(' '.join(str(x) for x in a) + '\n')


def val(m, name):
    try:
        v = m.get_editor_property(name)
    except Exception:
        return None
    if hasattr(v, 'get_editor_property'):
        for k in ('distribution',):
            try:
                d = v.get_editor_property(k)
                r = {}
                for kk in ('constant', 'min', 'max', 'min_value', 'max_value', 'min_vec', 'max_vec'):
                    try:
                        r[kk] = str(d.get_editor_property(kk))
                    except Exception:
                        pass
                return d.get_class().get_name(), r
            except Exception:
                pass
    return str(v)


PS = ['/Game/ANGRY_MESH/NaturePack_WinterEnvironment/ParticleSystems/' + n for n in ('P_Winter_SnowFall_Calm', 'P_Winter_SnowFall_Storm', 'P_Winter_SnowStorm_Small', 'P_Winter_SnowStorm_Medium', 'P_Winter_SnowStorm_Big')] + \
     ['/Game/VFXWeatherPack/Particles/' + n for n in ('P_Snow_Gentle', 'P_Snow_Blizzard', 'P_Ambient_Fog')]
for path in PS:
    ps = eas.load_asset(path)
    p('==', path, ps.get_class().get_name() if ps else None)
    if not ps:
        continue
    for pname in ('fixed_relative_bounding_box', 'use_fixed_relative_bounding_box', 'system_update_mode', 'warmup_time', 'warmup_tick_rate', 'delay'):
        p('  ', pname, val(ps, pname))
    try:
        ems = ps.get_editor_property('emitters')
    except Exception as e:
        p('  读不到 emitters', e)
        continue
    for em in ems:
        p('  emitter', em.get_name(), em.get_class().get_name())
        try:
            lods = em.get_editor_property('lod_levels')
        except Exception as e:
            p('    读不到 lod_levels', e)
            continue
        lod = lods[0]
        req = lod.get_editor_property('required_module')
        for k in ('material', 'emitter_duration', 'emitter_loops', 'screen_alignment', 'use_local_space'):
            p('    req', k, val(req, k))
        sp = lod.get_editor_property('spawn_module')
        p('    spawn rate', val(sp, 'rate'), 'scale', val(sp, 'rate_scale'))
        td = lod.get_editor_property('type_data_module')
        p('    typedata', td.get_class().get_name() if td else None)
        for m in lod.get_editor_property('modules'):
            cls = m.get_class().get_name()
            info = {}
            for k in ('start_location', 'start_velocity', 'lifetime', 'start_size', 'start_radius', 'start_height', 'velocity', 'acceleration', 'color_over_life', 'alpha_over_life', 'scale_over_life', 'start_location_x', 'box_extent'):
                v = val(m, k)
                if v is not None:
                    info[k] = v
            p('    module', cls, info)
for t in ('Trees/SM_Tree_A_02', 'Trees/SM_Tree_C_03', 'Props/SM_Fence_B_01', 'Rocks/SM_Rock_Group_01', 'Grasses/SM_Grass_A_05'):
    sm = eas.load_asset('/Game/ANGRY_MESH/NaturePack_WinterEnvironment/Meshes/' + t)
    bs = sm.get_editor_property('body_setup') if sm else None
    p('==', t, sm.get_editor_property('nanite_settings').enabled if sm else None, [str(s.get_editor_property('material_interface').get_name()) for s in sm.get_editor_property('static_materials')] if sm else None,
      bs.get_editor_property('collision_trace_flag') if bs else None)
out.close()
unreal.log('[probe] 完成')
