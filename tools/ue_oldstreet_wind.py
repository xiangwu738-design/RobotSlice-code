"""老街树叶随风摆（读 tools/oldstreet_wind.json）。不重搭地图：
1) 树叶母材质 M_OS_Leaf 的世界位置偏移接一个自定义节点（已经接过就不再建，只改参数默认值）；
2) 地图里 Full_Tree / Low_Tree 两个实例化组件设「离镜头多远以外不算偏移」，存图。
摆动按 X 的 100 米周期算（sin(2π·X/周期 × 整数)），所以循环的每一份树摆得一样，过接缝不跳；外层叶子（顶点色 R 亮）摆得多。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_oldstreet_wind.py" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
关掉：oldstreet_wind.json 的 amp_cm 设 0 再跑一次。
"""
import json

import unreal

DEST = '/Game/Env/OldStreet'
LEAF = f'{DEST}/Materials/M_OS_Leaf'
DST_MAP = '/Game/Maps/L_OldStreet'
W = json.load(open('D:/RobotSlice/tools/oldstreet_wind.json', encoding='utf-8'))
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
log = lambda *a: unreal.log_warning('[oswind] ' + ' '.join(str(x) for x in a))

CODE = '''
float Tau = 6.2831853;
float px = WP.x * (Tau / max(Period, 1.0));
float ph = sin(px * 3.0 + WP.y * 0.0021) * 1.7 + cos(px * 7.0 - WP.y * 0.0013) * 1.1 + WP.z * 0.004;
float g = 1.0 - Gust + Gust * (0.5 + 0.5 * sin(T * 0.37 + px * 2.0 + WP.y * 0.0007));
float s = sin(T * Freq + ph) * g + 0.35 * sin(T * Freq * 2.3 + ph * 1.7);
float w = saturate(Weight * 1.3);
return float3(s * 0.75, s * 0.45, s * 0.25) * Amp * w;
'''


def node(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {out}->{inp}'


def scalar(m, name, v, x, y):
    return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=float(v))


m = eas.load_asset(LEAF)
if not m:
    raise RuntimeError('找不到 ' + LEAF)
existing = None
try:
    existing = mel.get_material_property_input_node(m, P.MP_WORLD_POSITION_OFFSET)
except Exception as e:
    log('查不到已有的偏移节点（当没有）', e)
if existing:
    log('M_OS_Leaf 已经接了世界位置偏移，不再建节点；数值在下面的子实例上改')
else:
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1400, 900)
    try:
        wp.set_editor_property('world_position_shader_offset', unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    except Exception as e:
        log('世界位置节点设不了「不含偏移」', e)
    t = node(m, unreal.MaterialExpressionTime, -1400, 1000)
    vc = node(m, unreal.MaterialExpressionVertexColor, -1400, 1100)
    c = node(m, unreal.MaterialExpressionCustom, -900, 950)
    c.set_editor_property('code', CODE)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property('description', 'OSWind')
    names = ['WP', 'T', 'Weight', 'Amp', 'Freq', 'Gust', 'Period']
    lst = []
    for nm in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', nm)
        lst.append(ci)
    c.set_editor_property('inputs', lst)
    link(wp, '', c, 'WP')
    link(t, '', c, 'T')
    link(vc, 'R', c, 'Weight')
    link(scalar(m, 'WindAmp', W['amp_cm'], -1200, 1250), '', c, 'Amp')
    link(scalar(m, 'WindFreq', W['freq'], -1200, 1330), '', c, 'Freq')
    link(scalar(m, 'WindGust', W['gust'], -1200, 1410), '', c, 'Gust')
    link(scalar(m, 'LoopPeriod', W['period_cm'], -1200, 1490), '', c, 'Period')
    mel.connect_material_property(c, '', P.MP_WORLD_POSITION_OFFSET)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    log('M_OS_Leaf 接上随风摆')

# 子实例上设参数（第二次跑改数值也走这里）
n_mi = 0
for path in eas.list_assets(f'{DEST}/Materials', recursive=False):
    a = eas.load_asset(path.split('.')[0])
    if isinstance(a, unreal.MaterialInstanceConstant) and a.get_editor_property('parent') == m:
        for k, v in (('WindAmp', W['amp_cm']), ('WindFreq', W['freq']), ('WindGust', W['gust']), ('LoopPeriod', W['period_cm'])):
            mel.set_material_instance_scalar_parameter_value(a, k, float(v))
        eas.save_loaded_asset(a)
        n_mi += 1
log('树叶材质实例', n_mi, '个已设参数')

unreal.EditorLoadingAndSavingUtils.load_map(DST_MAP)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
n = 0
for act in sub.get_all_level_actors():
    if act.get_actor_label() in ('Full_Tree', 'Low_Tree'):
        for comp in act.get_components_by_class(unreal.InstancedStaticMeshComponent):
            try:
                comp.set_editor_property('world_position_offset_disable_distance', int(W['disable_distance_cm']))
                n += 1
            except Exception as e:
                log('设不了偏移距离', act.get_actor_label(), e)
log('树组件', n, '个设了偏移距离', W['disable_distance_cm'])
unreal.EditorLoadingAndSavingUtils.save_map(world, DST_MAP)
log('已存')
