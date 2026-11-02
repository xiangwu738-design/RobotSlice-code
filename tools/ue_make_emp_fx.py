"""在 RobotSlice 工程里跑：EMP 特效材质 + 更新烟片材质的代码。可以重复跑（已有的材质只换自定义节点的代码再重编）。
- /Game/Weapons/Materials/M_EmpArc：电弧（细圆柱）用，叠加、不受光；中线发白两边发蓝（tools/shaders/emp_arc.hlsl）
- /Game/Weapons/Materials/M_EmpShell：EMP 冲击球壳用，叠加、不受光、双面；边缘亮 + 和地面 / 机体相交处一圈亮环（tools/shaders/emp_shell.hlsl）
- /Game/Weapons/Materials/M_EmpField：EMP 作用范围（10-30 第二版，代替球壳）：屏幕空间贴花，地面扩散环 + 外缘环 + 六边形网格 + 淡场（tools/shaders/emp_field.hlsl）
- /Game/Weapons/Materials/M_SmokePuff：只换代码（tools/shaders/smoke_puff.hlsl，烟片加径向软遮罩去方边）
两个 EMP 材质都乘白天补偿 max(1, 曝光倒数×1.3)^0.85（和 tools/ue_fx_daylight.py 一样：夜里不变，白天自动曝光暗十几档时补回来）。
参数：Emissive 颜色、Strength 亮度、Fade（球壳淡出）。代码 AMechGrenade（球壳、炸开的电弧）、UMechShoulderComponent（中招机体身上的电弧）里设。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_make_emp_fx.py" -AllowCommandletRendering -RenderOffscreen
"""
import re

import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DEST = '/Game/Weapons/Materials'


def hlsl(name):
    """自定义节点的代码：去掉 // 注释（注释是中文，着色器编译遇到非 ASCII 字符会失败）"""
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), f'{name} 去掉注释后还有非 ASCII 字符'
    return code


def find_custom(m, prop):
    todo = [mel.get_material_property_input_node(m, prop)]
    while todo:
        e = todo.pop()
        if not e:
            continue
        if isinstance(e, unreal.MaterialExpressionCustom):
            return e
        todo += mel.get_inputs_for_material_expression(m, e)
    return None


def update_code(path, prop, code):
    """材质已经有了：只换自定义节点的代码再重编（游戏代码加载着这些材质，清空节点重建会触发 !IsRooted 断言崩溃）"""
    if not eas.does_asset_exist(path):
        return False
    m = eas.load_asset(path)
    c = find_custom(m, prop)
    if not c:
        unreal.log_warning(f'[empfx] {path} 找不到自定义节点')
        return False
    c.set_editor_property('code', code)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[empfx] 更新材质代码 {path}')
    return True


def day_boost(m, x, y):
    eye = mel.create_material_expression(m, unreal.MaterialExpressionEyeAdaptationInverse, x, y)
    k = mel.create_material_expression(m, unreal.MaterialExpressionConstant, x, y + 100)
    k.set_editor_property('r', 1.3)
    eyek = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, x + 150, y + 20)
    mel.connect_material_expressions(eye, '', eyek, 'A')
    mel.connect_material_expressions(k, '', eyek, 'B')
    one = mel.create_material_expression(m, unreal.MaterialExpressionConstant, x + 150, y + 120)
    one.set_editor_property('r', 1.0)
    mx = mel.create_material_expression(m, unreal.MaterialExpressionMax, x + 300, y + 40)
    mel.connect_material_expressions(eyek, '', mx, 'A')
    mel.connect_material_expressions(one, '', mx, 'B')
    ex = mel.create_material_expression(m, unreal.MaterialExpressionConstant, x + 300, y + 140)
    ex.set_editor_property('r', 0.85)
    pw = mel.create_material_expression(m, unreal.MaterialExpressionPower, x + 450, y + 60)
    mel.connect_material_expressions(mx, '', pw, 'Base')
    mel.connect_material_expressions(ex, '', pw, 'Exponent')
    return pw


def custom_node(m, code, names, desc):
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -300, 0)
    c.set_editor_property('code', code)
    c.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property('description', desc)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    c.set_editor_property('inputs', ins)
    return c


def additive_unlit(name, two_sided):
    m = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', two_sided)
    return m


def emp_arc():
    name = 'M_EmpArc'
    path = f'{DEST}/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('emp_arc.hlsl')):
        return
    m = additive_unlit(name, False)
    names = ['Fres', 'Color', 'Strength', 'Day']
    c = custom_node(m, hlsl('emp_arc.hlsl'), names, 'EmpArc')
    fres = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -800, -300)
    fres.set_editor_property('exponent', 1.5)
    fres.set_editor_property('base_reflect_fraction', 0.0)
    col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -800, -150)
    col.set_editor_property('parameter_name', 'Emissive')
    col.set_editor_property('default_value', unreal.LinearColor(0.5, 0.78, 1.0, 1.0))
    st = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 0)
    st.set_editor_property('parameter_name', 'Strength')
    st.set_editor_property('default_value', 8.0)
    day = day_boost(m, -1400, 200)
    for n, e in zip(names, [fres, col, st, day]):
        mel.connect_material_expressions(e, '', c, n)
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[empfx] 新建 {path}')


def emp_shell():
    name = 'M_EmpShell'
    path = f'{DEST}/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('emp_shell.hlsl')):
        return
    m = additive_unlit(name, True)
    names = ['Fres', 'DepthDiff', 'Color', 'Strength', 'Fade', 'Day']
    c = custom_node(m, hlsl('emp_shell.hlsl'), names, 'EmpShell')
    fres = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -800, -400)
    fres.set_editor_property('exponent', 3.0)
    fres.set_editor_property('base_reflect_fraction', 0.0)
    sd = mel.create_material_expression(m, unreal.MaterialExpressionSceneDepth, -1000, -250)
    pd = mel.create_material_expression(m, unreal.MaterialExpressionPixelDepth, -1000, -150)
    diff = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -800, -200)
    mel.connect_material_expressions(sd, '', diff, 'A')
    mel.connect_material_expressions(pd, '', diff, 'B')
    col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -800, -50)
    col.set_editor_property('parameter_name', 'Emissive')
    col.set_editor_property('default_value', unreal.LinearColor(0.45, 0.75, 1.0, 1.0))
    st = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 80)
    st.set_editor_property('parameter_name', 'Strength')
    st.set_editor_property('default_value', 2.0)
    fade = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 200)
    fade.set_editor_property('parameter_name', 'Fade')
    fade.set_editor_property('default_value', 1.0)
    day = day_boost(m, -1500, 350)
    for n, e in zip(names, [fres, diff, col, st, fade, day]):
        mel.connect_material_expressions(e, '', c, n)
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[empfx] 新建 {path}')


def emp_field():
    """EMP 作用范围（10-30 第二版）：屏幕空间贴花，挂在覆盖范围的球上。叠加、不受光、双面、不做深度测试；
    代码按场景深度还原背后的表面，画扩散环、外缘环、地面六边形网格、淡淡的空中场（tools/shaders/emp_field.hlsl）。
    参数：Center（炸点，世界坐标）、Prm（扩散半径、范围、时间、镜头在范围里）、Emissive、Strength。亮度按曝光倒数归一（不走共用的白天补偿）"""
    name = 'M_EmpField'
    path = f'{DEST}/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('emp_field.hlsl')):
        return
    m = additive_unlit(name, True)
    m.set_editor_property('disable_depth_test', True)
    names = ['WPos', 'Cam', 'SceneD', 'PixD', 'Sgn', 'Center', 'Prm', 'Color', 'Strength', 'EAI', 'Tm']
    c = custom_node(m, hlsl('emp_field.hlsl'), names, 'EmpField')
    wpos = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, -600)
    cam = mel.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, -900, -500)
    sd = mel.create_material_expression(m, unreal.MaterialExpressionSceneDepth, -900, -400)
    pd = mel.create_material_expression(m, unreal.MaterialExpressionPixelDepth, -900, -300)
    sgn = mel.create_material_expression(m, unreal.MaterialExpressionTwoSidedSign, -900, -200)
    ctr = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, -100)
    ctr.set_editor_property('parameter_name', 'Center')
    ctr.set_editor_property('default_value', unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    prm = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 50)
    prm.set_editor_property('parameter_name', 'Prm')
    prm.set_editor_property('default_value', unreal.LinearColor(0.0, 700.0, 10.0, 0.0))
    col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 200)
    col.set_editor_property('parameter_name', 'Emissive')
    col.set_editor_property('default_value', unreal.LinearColor(0.32, 0.62, 1.0, 1.0))
    st = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 350)
    st.set_editor_property('parameter_name', 'Strength')
    st.set_editor_property('default_value', 1.0)
    eai = mel.create_material_expression(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 450)
    tm = mel.create_material_expression(m, unreal.MaterialExpressionTime, -900, 550)
    for n, e in zip(names, [wpos, cam, sd, pd, sgn, ctr, prm, col, st, eai, tm]):
        # 向量参数默认那个输出只有 RGB（5.8 里取 .w 编译不过），Prm 四个分量都要用 → 接 RGBA 输出
        mel.connect_material_expressions(e, 'RGBA' if n == 'Prm' else '', c, n)
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[empfx] 新建 {path}')


def use_rgba(path, prop, input_name):
    """自定义节点的某个输入改接向量参数的 RGBA 输出（默认输出只有 RGB，着色器里用 .a / .w 会编译失败、整个材质换成默认材质）"""
    m = eas.load_asset(path)
    c = find_custom(m, prop)
    names = [str(i.get_editor_property('input_name')) for i in c.get_editor_property('inputs')]
    srcs = mel.get_inputs_for_material_expression(m, c)
    src = srcs[names.index(input_name)]
    ok = mel.connect_material_expressions(src, 'RGBA', c, input_name)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[empfx] {path} 的 {input_name} 改接 RGBA：{ok}')


def ensure_ism(path):
    """电弧画成 ISM（一串圆柱）：材质要勾「用于实例化静态网格」，否则游戏里换成默认材质"""
    m = eas.load_asset(path)
    if not m.get_editor_property('used_with_instanced_static_meshes'):
        m.set_editor_property('used_with_instanced_static_meshes', True)
        mel.recompile_material(m)
        eas.save_loaded_asset(m)
        unreal.log(f'[empfx] {path} 勾上 used_with_instanced_static_meshes')


def dump_custom_inputs(path, prop):
    """查自定义节点每个输入接的是什么（没接的输入在着色器里是 0）"""
    m = eas.load_asset(path)
    c = find_custom(m, prop)
    names = [str(i.get_editor_property('input_name')) for i in c.get_editor_property('inputs')]
    srcs = mel.get_inputs_for_material_expression(m, c)
    out = []
    for n, s in zip(names, srcs):
        out.append(f'{n}={type(s).__name__ if s else "没接"}')
    unreal.log(f'[empfx] {path} 自定义节点输入：{", ".join(out)}')
    return m, c, dict(zip(names, srcs))


emp_arc()
emp_shell()
emp_field()
ensure_ism(f'{DEST}/M_EmpArc')
use_rgba(f'{DEST}/M_EmpField', unreal.MaterialProperty.MP_EMISSIVE_COLOR, 'Prm')
use_rgba(f'{DEST}/M_EmpShell', unreal.MaterialProperty.MP_EMISSIVE_COLOR, 'Color')  # 球壳着色器用 Color.a，一直编译不过（10-30 查到；球壳已经不用）
# 烟片：只换代码（材质由 tools/ue_import_shoulder.py 建）；顺便查输入有没有接上
update_code(f'{DEST}/M_SmokePuff', unreal.MaterialProperty.MP_BASE_COLOR, hlsl('smoke_puff.hlsl'))
m, c, ins = dump_custom_inputs(f'{DEST}/M_SmokePuff', unreal.MaterialProperty.MP_BASE_COLOR)
# 不透明度那根线：自定义节点输出 float4，经一个分量遮罩取 A。遮罩节点新建时 R 默认是勾上的，只勾 A 的话输出是 (R, A) 两个分量，
# 接到不透明度上只取第一个 = 烟的颜色（约 0.74 的常数）——整片烟一个透明度 = 方板，贴图和边缘遮罩全白算。这里只留 A
op = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_OPACITY)
if isinstance(op, unreal.MaterialExpressionComponentMask):
    flags = [op.get_editor_property(k) for k in 'rgba']
    unreal.log(f'[empfx] M_SmokePuff 不透明度遮罩 RGBA = {flags}')
    if flags != [False, False, False, True]:
        for k in 'rgb':
            op.set_editor_property(k, False)
        op.set_editor_property('a', True)
        mel.recompile_material(m)
        eas.save_loaded_asset(m)
        unreal.log('[empfx] M_SmokePuff 不透明度遮罩改成只取 A')
else:
    unreal.log_warning(f'[empfx] M_SmokePuff 不透明度接的是 {type(op).__name__}')
if not ins.get('UV'):
    # 贴图坐标没接上：自定义节点里 UV 永远是 0，整片烟一个浓度 = 方板（以前以为是贴片组件传不进坐标）
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -800, -600)
    ok = mel.connect_material_expressions(uv, '', c, 'UV')
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[empfx] M_SmokePuff 补接 UV：{ok}')
    dump_custom_inputs(f'{DEST}/M_SmokePuff', unreal.MaterialProperty.MP_BASE_COLOR)
unreal.log('[empfx] 完成')
