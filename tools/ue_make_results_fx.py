"""结算场景（MechResultsStage）的三个材质，在 RobotSlice 工程里跑。可以重复跑（已有的材质只换自定义节点的代码再重编）。
- /Game/Results/M_ResultsFloor：地面 / 墙。不透明、受光；参数 Color、Roughness、GridColor、GridIntensity、Center（场景中心，世界坐标）、
  Prm（网格间距、线宽、淡出半径、1 = 竖墙）。自发光是一层很淡的网格线（tools/shaders/results_floor.hlsl）
- /Game/Results/M_ResultsBeam：光柱（拉长的圆锥）。叠加、不受光、双面；参数 Color、Strength、Origin、Dir、Prm（长度、边缘指数、软边距离、
  沿长度衰减指数）（tools/shaders/results_beam.hlsl）
- /Game/Results/M_ResultsGlow：地环 / 光条（平面、细长方块）。叠加、不受光；参数 Color、Strength、Prm（环半径、环宽、中间软光、1 = 整块发光）
  （tools/shaders/results_glow.hlsl）
亮度都不做曝光补偿：结算场景是固定曝光（mech.Results.Exposure）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_make_results_fx.py" -AllowCommandletRendering -RenderOffscreen
"""
import re

import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DEST = '/Game/Results'


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
    """材质已经有了：只换自定义节点的代码再重编（游戏代码加载着这些材质时清空节点重建会 !IsRooted 崩）"""
    if not eas.does_asset_exist(path):
        return False
    m = eas.load_asset(path)
    c = find_custom(m, prop)
    if not c:
        unreal.log_warning(f'[resultsfx] {path} 找不到自定义节点')
        return False
    c.set_editor_property('code', code)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[resultsfx] 更新材质代码 {path}')
    return True


def custom_node(m, code, names, desc, x=-300, y=0):
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
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


def vparam(m, name, value, x, y):
    p = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, x, y)
    p.set_editor_property('parameter_name', name)
    p.set_editor_property('default_value', unreal.LinearColor(*value))
    return p


def sparam(m, name, value, x, y):
    p = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property('parameter_name', name)
    p.set_editor_property('default_value', value)
    return p


def connect_inputs(m, c, names, nodes, rgba=()):
    # 向量参数默认那个输出只有 RGB；要用 .w 的（Prm）接 RGBA 输出（5.8 里取 .w 编译不过）
    for n, e in zip(names, nodes):
        mel.connect_material_expressions(e, 'RGBA' if n in rgba else '', c, n)


def floor():
    name = 'M_ResultsFloor'
    path = f'{DEST}/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('results_floor.hlsl')):
        return
    m = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    col = vparam(m, 'Color', (0.016, 0.018, 0.022, 1.0), -700, -400)
    rough = sparam(m, 'Roughness', 0.22, -700, -250)
    metal = sparam(m, 'Metallic', 0.0, -700, -150)
    mel.connect_material_property(col, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(metal, '', unreal.MaterialProperty.MP_METALLIC)
    names = ['WPos', 'Center', 'Prm', 'GridColor', 'GridIntensity']
    c = custom_node(m, hlsl('results_floor.hlsl'), names, 'ResultsGrid', -300, 100)
    wpos = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -900, 0)
    ctr = vparam(m, 'Center', (0.0, 0.0, -380000.0, 0.0), -900, 100)
    prm = vparam(m, 'Prm', (200.0, 0.012, 2600.0, 0.0), -900, 250)
    gcol = vparam(m, 'GridColor', (0.25, 0.85, 1.0, 1.0), -900, 400)
    gint = sparam(m, 'GridIntensity', 1.0, -900, 550)
    connect_inputs(m, c, names, [wpos, ctr, prm, gcol, gint], rgba=('Prm',))
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[resultsfx] 新建 {path}')


def additive_unlit(name, two_sided):
    m = tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', two_sided)
    return m


def beam():
    name = 'M_ResultsBeam'
    path = f'{DEST}/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('results_beam.hlsl')):
        return
    m = additive_unlit(name, True)
    names = ['WPos', 'Nrm', 'CamV', 'DepthDiff', 'Origin', 'Dir', 'Prm', 'Color', 'Strength']
    c = custom_node(m, hlsl('results_beam.hlsl'), names, 'ResultsBeam')
    wpos = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1000, -500)
    nrm = mel.create_material_expression(m, unreal.MaterialExpressionVertexNormalWS, -1000, -400)
    cam = mel.create_material_expression(m, unreal.MaterialExpressionCameraVectorWS, -1000, -300)
    sd = mel.create_material_expression(m, unreal.MaterialExpressionSceneDepth, -1200, -200)
    pd = mel.create_material_expression(m, unreal.MaterialExpressionPixelDepth, -1200, -100)
    diff = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -1000, -150)
    mel.connect_material_expressions(sd, '', diff, 'A')
    mel.connect_material_expressions(pd, '', diff, 'B')
    org = vparam(m, 'Origin', (0.0, 0.0, -378000.0, 0.0), -1000, 0)
    dr = vparam(m, 'Dir', (0.0, 0.0, -1.0, 0.0), -1000, 150)
    prm = vparam(m, 'Prm', (1100.0, 1.6, 70.0, 0.9), -1000, 300)
    col = vparam(m, 'Color', (0.55, 0.82, 1.0, 1.0), -1000, 450)
    st = sparam(m, 'Strength', 0.5, -1000, 600)
    connect_inputs(m, c, names, [wpos, nrm, cam, diff, org, dr, prm, col, st], rgba=('Prm',))
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[resultsfx] 新建 {path}')


def glow():
    name = 'M_ResultsGlow'
    path = f'{DEST}/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('results_glow.hlsl')):
        return
    m = additive_unlit(name, False)
    names = ['UV', 'Prm', 'Color', 'Strength']
    c = custom_node(m, hlsl('results_glow.hlsl'), names, 'ResultsGlow')
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -900, -300)
    prm = vparam(m, 'Prm', (0.8, 0.05, 0.1, 0.0), -900, -150)
    col = vparam(m, 'Color', (0.25, 0.6, 1.0, 1.0), -900, 0)
    st = sparam(m, 'Strength', 6.0, -900, 150)
    connect_inputs(m, c, names, [uv, prm, col, st], rgba=('Prm',))
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[resultsfx] 新建 {path}')


floor()
beam()
glow()
unreal.log('[resultsfx] 完成')
