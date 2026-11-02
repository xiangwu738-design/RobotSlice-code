"""近战特效材质（10-31 用户：「近战武器的特效能否再丰富一点」）。可以重复跑：已有的材质只换自定义节点的代码再重编。
- /Game/Weapons/Materials/M_BladeArc：刀光（扫掠面，UMechMeleeFx 每帧按刀根、刀尖建网格），叠加、不受光、双面（tools/shaders/blade_arc.hlsl）
- /Game/Weapons/Materials/M_SlashScar：刀痕（屏幕空间贴花，挂在被砍中骨头上的小球），叠加、不受光、双面、不做深度测试
  （scar_recon.hlsl 还原背后的表面 → 世界转小球局部坐标 → scar_draw.hlsl 画刀口平面和装甲的交线）
- /Game/Weapons/Materials/M_ShockRing：冲击环 / 弹反闪光（正方形面片），叠加、不受光、双面（shock_ring.hlsl）
- 武器母材质 M_WeaponPBR：自发光加一项刀刃热光（blade_heat.hlsl，参数 HeatPrm / HeatColor，默认热度 0 = 和以前一样）
数值都按「屏幕上的亮度」写，乘曝光倒数抵掉自动曝光（白天夜里屏幕上一样亮）。
11-01 刀光第二版（用户：「目前的刀光总体是过于朴素」），另建三个材质，第一版 M_BladeArc 不动（-MechTrailOld 还用它）：
- M_BladeTrail：刀光（白热前沿 + 主体色 + 快散时的深色、一条条流动的拉丝、刀尖外侧辉光、尾巴碎成块带亮边、钝器的气压波纹；blade_trail.hlsl）
- M_BladeSpark：刀尖洒的火花 / 光点（朝镜头、顺着速度拉长的面片；blade_spark.hlsl）
- M_BladeGhost：重斩、闪避斩的刀身残影（轮廓光；blade_ghost.hlsl）
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_make_melee_fx.py" -AllowCommandletRendering -RenderOffscreen -unattended
只建 / 更新第二版刀光的三个材质：-script="D:/RobotSlice/tools/ue_make_melee_fx.py trail"
"""
import re
import sys

import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DEST = '/Game/Weapons/Materials'
P = unreal.MaterialProperty


def hlsl(name):
    """自定义节点的代码：去掉 // 注释（注释是中文，着色器编译遇到非 ASCII 字符会失败）"""
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), f'{name} 去掉注释后还有非 ASCII 字符'
    return code


def customs(m):
    """材质里所有自定义节点，按描述（description）索引"""
    out = {}
    todo = [mel.get_material_property_input_node(m, P.MP_EMISSIVE_COLOR)]
    seen = set()
    while todo:
        e = todo.pop()
        if not e or e.get_name() in seen:
            continue
        seen.add(e.get_name())
        if isinstance(e, unreal.MaterialExpressionCustom):
            out[str(e.get_editor_property('description'))] = e
        todo += mel.get_inputs_for_material_expression(m, e)
    return out


def update_codes(path, codes):
    """材质已经有了：只换自定义节点的代码再重编（游戏代码加载着这些材质，清空节点重建会触发 !IsRooted 断言崩溃）"""
    if not eas.does_asset_exist(path):
        return False
    m = eas.load_asset(path)
    found = customs(m)
    for desc, code in codes.items():
        if desc not in found:
            unreal.log_warning(f'[meleefx] {path} 找不到自定义节点 {desc}')
            return False
        found[desc].set_editor_property('code', code)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[meleefx] 更新材质代码 {path}')
    return True


def node(m, cls, x, y):
    return mel.create_material_expression(m, cls, x, y)


def vec(m, name, value, x, y):
    n = node(m, unreal.MaterialExpressionVectorParameter, x, y)
    n.set_editor_property('parameter_name', name)
    n.set_editor_property('default_value', value)
    return n


def custom_node(m, code, names, desc, out_type, x=-300, y=0):
    c = node(m, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property('code', code)
    c.set_editor_property('output_type', out_type)
    c.set_editor_property('description', desc)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    return c


def wire(m, c, pairs):
    """pairs: [(输入名, 节点, 输出名)]"""
    for name, e, out in pairs:
        if not mel.connect_material_expressions(e, out, c, name):
            unreal.log_warning(f'[meleefx] 接不上 {name}')


def additive_unlit(name, two_sided):
    m = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', two_sided)
    return m


def finish(m, path):
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[meleefx] 新建 {path}')


F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


def blade_arc():
    path = f'{DEST}/M_BladeArc'
    if update_codes(path, {'BladeArc': hlsl('blade_arc.hlsl')}):
        return
    m = additive_unlit('M_BladeArc', True)
    c = custom_node(m, hlsl('blade_arc.hlsl'), ['UV', 'VC', 'Prm', 'Color', 'Core', 'EAI'], 'BladeArc', F3)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -900, -300)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, -200)
    prm = vec(m, 'Prm', unreal.LinearColor(1.0, 0.08, 1.0, 0.6), -900, -50)
    col = vec(m, 'Color', unreal.LinearColor(0.75, 0.88, 1.0, 1.0), -900, 100)
    core = vec(m, 'CoreColor', unreal.LinearColor(2.0, 2.0, 2.0, 1.0), -900, 250)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 400)
    # 向量参数默认那个输出只有 RGB（5.8 里取 .w 编译不过），Prm 四个分量都要用 → 接 RGBA 输出
    wire(m, c, [('UV', uv, ''), ('VC', vc, ''), ('Prm', prm, 'RGBA'), ('Color', col, ''), ('Core', core, ''), ('EAI', eai, '')])
    mel.connect_material_property(c, '', P.MP_EMISSIVE_COLOR)
    finish(m, path)


def slash_scar():
    path = f'{DEST}/M_SlashScar'
    if update_codes(path, {'ScarRecon': hlsl('scar_recon.hlsl'), 'ScarDraw': hlsl('scar_draw.hlsl')}):
        return
    m = additive_unlit('M_SlashScar', True)
    m.set_editor_property('disable_depth_test', True)
    rec = custom_node(m, hlsl('scar_recon.hlsl'), ['WPos', 'Cam', 'SceneD', 'PixD', 'Ctr'], 'ScarRecon', F3, -900, 0)
    wpos = node(m, unreal.MaterialExpressionWorldPosition, -1400, -300)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1400, -200)
    sd = node(m, unreal.MaterialExpressionSceneDepth, -1400, -100)
    pd = node(m, unreal.MaterialExpressionPixelDepth, -1400, 0)
    ctr = node(m, unreal.MaterialExpressionObjectPositionWS, -1400, 100)
    wire(m, rec, [('WPos', wpos, ''), ('Cam', cam, ''), ('SceneD', sd, ''), ('PixD', pd, ''), ('Ctr', ctr, '')])
    # 世界 → 小球局部（小球挂在骨头上：刀痕跟着那块装甲走，渲染时用的就是这一帧的变换，不会落后一帧）
    tp = node(m, unreal.MaterialExpressionTransformPosition, -650, 0)
    tp.set_editor_property('transform_source_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    tp.set_editor_property('transform_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    assert mel.connect_material_expressions(rec, '', tp, ''), '接不上 TransformPosition'
    draw = custom_node(m, hlsl('scar_draw.hlsl'), ['LP', 'Prm', 'Prm2', 'EAI'], 'ScarDraw', F3, -300, 0)
    prm = vec(m, 'Prm', unreal.LinearColor(1.0, 20.0, 1.5, -10.0), -650, 150)
    prm2 = vec(m, 'Prm2', unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -650, 300)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -650, 450)
    wire(m, draw, [('LP', tp, ''), ('Prm', prm, 'RGBA'), ('Prm2', prm2, 'RGBA'), ('EAI', eai, '')])
    mel.connect_material_property(draw, '', P.MP_EMISSIVE_COLOR)
    finish(m, path)


def shock_ring():
    path = f'{DEST}/M_ShockRing'
    if update_codes(path, {'ShockRing': hlsl('shock_ring.hlsl')}):
        return
    m = additive_unlit('M_ShockRing', True)
    c = custom_node(m, hlsl('shock_ring.hlsl'), ['UV', 'Prm', 'Color', 'EAI'], 'ShockRing', F3)
    uv = node(m, unreal.MaterialExpressionTextureCoordinate, -900, -200)
    prm = vec(m, 'Prm', unreal.LinearColor(0.0, 0.08, 1.0, 0.0), -900, -50)
    col = vec(m, 'Color', unreal.LinearColor(1.0, 0.8, 0.55, 1.0), -900, 100)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 250)
    wire(m, c, [('UV', uv, ''), ('Prm', prm, 'RGBA'), ('Color', col, ''), ('EAI', eai, '')])
    mel.connect_material_property(c, '', P.MP_EMISSIVE_COLOR)
    finish(m, path)


def weapon_heat():
    """武器母材质加刀刃热光：原来的自发光（遮罩 × 队伍色 × 强度）+ BladeHeat"""
    path = f'{DEST}/M_WeaponPBR'
    m = eas.load_asset(path)
    found = customs(m)
    if 'BladeHeat' in found:
        found['BladeHeat'].set_editor_property('code', hlsl('blade_heat.hlsl'))
        mel.recompile_material(m)
        eas.save_loaded_asset(m)
        unreal.log(f'[meleefx] 更新刀刃热光代码 {path}')
        return
    old = mel.get_material_property_input_node(m, P.MP_EMISSIVE_COLOR)
    c = custom_node(m, hlsl('blade_heat.hlsl'), ['LPos', 'LNrm', 'HeatPrm', 'HeatColor', 'EAI'], 'BladeHeat', F3, -150, 1400)
    wpos = node(m, unreal.MaterialExpressionWorldPosition, -900, 1400)
    tp = node(m, unreal.MaterialExpressionTransformPosition, -650, 1400)
    tp.set_editor_property('transform_source_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    tp.set_editor_property('transform_type', unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    assert mel.connect_material_expressions(wpos, '', tp, ''), '接不上 TransformPosition'
    nrm = node(m, unreal.MaterialExpressionVertexNormalWS, -900, 1550)
    tv = node(m, unreal.MaterialExpressionTransform, -650, 1550)
    tv.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    tv.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    assert mel.connect_material_expressions(nrm, '', tv, ''), '接不上 Transform'
    hp = vec(m, 'HeatPrm', unreal.LinearColor(0.0, 10.0, -1.0, 100.0), -650, 1700)
    hc = vec(m, 'HeatColor', unreal.LinearColor(1.0, 0.5, 0.15, 1.0), -650, 1850)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -650, 2000)
    wire(m, c, [('LPos', tp, ''), ('LNrm', tv, ''), ('HeatPrm', hp, 'RGBA'), ('HeatColor', hc, ''), ('EAI', eai, '')])
    add = node(m, unreal.MaterialExpressionAdd, 100, 1100)
    mel.connect_material_expressions(old, '', add, 'A')
    mel.connect_material_expressions(c, '', add, 'B')
    mel.connect_material_property(add, '', P.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[meleefx] {path} 加上刀刃热光（原自发光 {old.get_class().get_name() if old else None}）')


def dump(path):
    a = eas.load_asset(path)
    if not a:
        return
    vs = {str(n): mel.get_material_instance_vector_parameter_value(a, n) for n in mel.get_vector_parameter_names(a)} if isinstance(a, unreal.MaterialInstance) else {}
    ss = {str(n): round(mel.get_material_instance_scalar_parameter_value(a, n), 3) for n in mel.get_scalar_parameter_names(a)} if isinstance(a, unreal.MaterialInstance) else {}
    base = a.get_base_material()
    unreal.log(f'[meleefx] {path}：底材质 {base.get_path_name()} 混合 {base.get_editor_property("blend_mode")} 颜色 {vs} 标量 {ss}')


def texcoord(m, index, x, y):
    n = node(m, unreal.MaterialExpressionTextureCoordinate, x, y)
    n.set_editor_property('coordinate_index', index)
    return n


def blade_trail():
    """预乘透明（AlphaComposite）：自定义节点返回 float4 = (加上去的光, 把背后压暗多少)。
    改了节点结构（不只是代码）要先把旧的 M_BladeTrail.uasset 挪走再跑（游戏代码加载着它，就地清空节点重建会崩）"""
    path = f'{DEST}/M_BladeTrail'
    if update_codes(path, {'BladeTrail': hlsl('blade_trail.hlsl')}):
        return
    m = tools.create_asset('M_BladeTrail', DEST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ALPHA_COMPOSITE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    names = ['UV', 'UV1', 'VC', 'Prm', 'Prm2', 'Prm3', 'Prm4', 'Hot', 'Color', 'Deep', 'Core', 'EAI']
    c = custom_node(m, hlsl('blade_trail.hlsl'), names, 'BladeTrail', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    LC = unreal.LinearColor
    uv = texcoord(m, 0, -900, -600)
    uv1 = texcoord(m, 1, -900, -500)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, -400)
    prm = vec(m, 'Prm', LC(1.0, 0.05, 0.5, 0.7), -900, -250)
    prm2 = vec(m, 'Prm2', LC(0.0, 10.0, 3.0, 0.8), -900, -100)
    prm3 = vec(m, 'Prm3', LC(0.6, 1.5, 12.0, 0.4), -900, 50)
    prm4 = vec(m, 'Prm4', LC(0.0, 0.0, 0.0, 0.0), -900, 200)
    hot = vec(m, 'Hot', LC(2.2, 2.4, 2.8, 1.0), -900, 350)
    col = vec(m, 'Color', LC(0.35, 0.6, 1.3, 1.0), -900, 500)
    deep = vec(m, 'Deep', LC(0.06, 0.14, 0.5, 1.0), -900, 650)
    core = vec(m, 'CoreColor', LC(3.0, 3.2, 3.6, 1.0), -900, 800)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 950)
    wire(m, c, [('UV', uv, ''), ('UV1', uv1, ''), ('VC', vc, ''), ('Prm', prm, 'RGBA'), ('Prm2', prm2, 'RGBA'), ('Prm3', prm3, 'RGBA'),
                ('Prm4', prm4, 'RGBA'), ('Hot', hot, ''), ('Color', col, ''), ('Deep', deep, ''), ('Core', core, ''), ('EAI', eai, '')])
    rgb = node(m, unreal.MaterialExpressionComponentMask, 0, -100)
    for ch, on in (('r', True), ('g', True), ('b', True), ('a', False)):
        rgb.set_editor_property(ch, on)
    alpha = node(m, unreal.MaterialExpressionComponentMask, 0, 100)
    for ch, on in (('r', False), ('g', False), ('b', False), ('a', True)):
        alpha.set_editor_property(ch, on)
    assert mel.connect_material_expressions(c, '', rgb, ''), '接不上 RGB'
    assert mel.connect_material_expressions(c, '', alpha, ''), '接不上 A'
    mel.connect_material_property(rgb, '', P.MP_EMISSIVE_COLOR)
    mel.connect_material_property(alpha, '', P.MP_OPACITY)
    finish(m, path)


def blade_spark():
    path = f'{DEST}/M_BladeSpark'
    if update_codes(path, {'BladeSpark': hlsl('blade_spark.hlsl')}):
        return
    m = additive_unlit('M_BladeSpark', True)
    c = custom_node(m, hlsl('blade_spark.hlsl'), ['UV', 'VC', 'VA', 'Prm', 'EAI'], 'BladeSpark', F3)
    uv = texcoord(m, 0, -900, -200)
    vc = node(m, unreal.MaterialExpressionVertexColor, -900, -100)
    prm = vec(m, 'Prm', unreal.LinearColor(6.0, 0.6, 0.0, 0.0), -900, 50)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 200)
    # 顶点色默认那个输出只有 RGB，A 单独接
    wire(m, c, [('UV', uv, ''), ('VC', vc, ''), ('VA', vc, 'A'), ('Prm', prm, 'RGBA'), ('EAI', eai, '')])
    mel.connect_material_property(c, '', P.MP_EMISSIVE_COLOR)
    finish(m, path)


def blade_ghost():
    path = f'{DEST}/M_BladeGhost'
    if update_codes(path, {'BladeGhost': hlsl('blade_ghost.hlsl')}):
        return
    m = additive_unlit('M_BladeGhost', False)
    c = custom_node(m, hlsl('blade_ghost.hlsl'), ['Nrm', 'Cam', 'Prm', 'Color', 'EAI'], 'BladeGhost', F3)
    nrm = node(m, unreal.MaterialExpressionVertexNormalWS, -900, -200)
    cam = node(m, unreal.MaterialExpressionCameraVectorWS, -900, -100)
    prm = vec(m, 'Prm', unreal.LinearColor(1.0, 2.5, 0.15, 0.0), -900, 50)
    col = vec(m, 'Color', unreal.LinearColor(0.6, 0.8, 1.2, 1.0), -900, 200)
    eai = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 350)
    wire(m, c, [('Nrm', nrm, ''), ('Cam', cam, ''), ('Prm', prm, 'RGBA'), ('Color', col, ''), ('EAI', eai, '')])
    mel.connect_material_property(c, '', P.MP_EMISSIVE_COLOR)
    finish(m, path)


if 'trail' in sys.argv[1:]:
    blade_trail()
    blade_spark()
    blade_ghost()
else:
    blade_arc()
    slash_scar()
    shock_ring()
    weapon_heat()
    blade_trail()
    blade_spark()
    blade_ghost()
    dump('/Game/Weapons/Materials/MI_Tracer')
