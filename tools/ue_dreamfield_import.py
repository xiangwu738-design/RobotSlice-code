"""梦核旷野图（L_Dreamfield）的资源导入：网格、贴图、材质 → /Game/Env/Dreamfield。可重复跑（已有的覆盖 / 只改材质节点代码）。
先跑 Blender 侧 tools/blender_dreamwall_export.py（wall / clouds / ground）把 FBX 写到 Inbox/_dreamwall/export；
HDR 天空和行星贴图先拷到 SourceAssets/dreamfield（UE 导入路径不放中文）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_dreamfield_import.py [tex] [mat] [mesh]" -AllowCommandletRendering -RenderOffscreen -unattended
不带参数 = 三步都做。材质的着色代码在 tools/shaders/dreamfield_*.hlsl（中文注释由脚本去掉）。
"""
import json
import re
import sys

import unreal

SRC = 'D:/RobotSlice/Inbox/_dreamwall/export'
SA = 'D:/RobotSlice/SourceAssets/dreamfield'
DEST = '/Game/Env/Dreamfield'
STEPS = [a for a in sys.argv[1:] if a in ('tex', 'mat', 'mesh', 'ground', 'panel', 'lawn')] or ['tex', 'mat', 'mesh']   # ground = 只重导两块地面，panel = 只导墙面面板图，lawn = 只导草坪贴图
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
log = lambda *a: unreal.log('[dreamfield] ' + ' '.join(str(x) for x in a))


def task(filename, dest, name=None, options=None):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    if name:
        t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    if options:
        t.options = options
    return t


# ---------------------------------------------------------------- 贴图
if 'tex' in STEPS:
    tools.import_asset_tasks([task(f'{SA}/kloppenheim_06_puresky_4k.hdr', f'{DEST}/Textures', 'T_DF_SkyCube'),
                              task(f'{SA}/Texture_Planet_02.jpg', f'{DEST}/Textures', 'T_DF_Planet')])
    sky = eas.load_asset(f'{DEST}/Textures/T_DF_SkyCube')
    log('天空贴图类型', sky.get_class().get_name() if sky else None)
    pl = eas.load_asset(f'{DEST}/Textures/T_DF_Planet')
    if pl:
        pl.set_editor_property('max_texture_size', 2048)
        eas.save_loaded_asset(pl)
if 'tex' in STEPS or 'lawn' in STEPS:
    # 草坪贴图（Kikuyu 草叶撒出来的，tools/dreamfield_lawn_textures.py）
    tools.import_asset_tasks([task(f'{SA}/T_DF_Lawn_C.png', f'{DEST}/Textures', 'T_DF_Lawn_C'),
                              task(f'{SA}/T_DF_Lawn_N.png', f'{DEST}/Textures', 'T_DF_Lawn_N')])
    # 底色当线性数据读（不按 sRGB 解码）：贴图只当明暗细节、平均 0.5，材质里除以 0.5 正好是 ×1，地面整体亮度不变
    # （第一次按 sRGB 读，线性平均只有 0.2 出头，整片地面暗了一半，和草丛一个颜色，草丛全「融」进地里）
    lc_ = eas.load_asset(f'{DEST}/Textures/T_DF_Lawn_C')
    if lc_:
        lc_.set_editor_property('srgb', False)
        eas.save_loaded_asset(lc_)
    ln_ = eas.load_asset(f'{DEST}/Textures/T_DF_Lawn_N')
    if ln_:
        ln_.set_editor_property('srgb', False)
        ln_.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        eas.save_loaded_asset(ln_)
if 'tex' in STEPS or 'panel' in STEPS:
    # 墙面面板图（原包 Texture.jpg，tools/dreamfield_panel_textures.py 整理成 2048² 的底色 + 法线）
    tools.import_asset_tasks([task(f'{SA}/T_DW_Panel_C.png', f'{DEST}/Textures', 'T_DW_Panel_C'),
                              task(f'{SA}/T_DW_Panel_N.png', f'{DEST}/Textures', 'T_DW_Panel_N')])
    tn_ = eas.load_asset(f'{DEST}/Textures/T_DW_Panel_N')
    if tn_:
        tn_.set_editor_property('srgb', False)
        tn_.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        eas.save_loaded_asset(tn_)

# ---------------------------------------------------------------- 材质
def hlsl(name):
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), name
    return code


def new_material(name, rebuild=True):
    """已有的材质：删掉全部节点重连（材质是脚本生成的，没人手改）；没有就新建"""
    path = f'{DEST}/Materials/{name}'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        # delete_all_material_expressions 一次不一定删得干净（11-01 发现 M_DF_Grass 里还留着第一版没连线的旧节点，
        # 其中一个采样引用着环境包的 SATR-GrGrassBase-C，打包会把它也带上）→ 删到一个不剩为止
        for _ in range(12):
            if mel.get_num_material_expressions(m) == 0:
                break
            mel.delete_all_material_expressions(m)
        left = mel.get_num_material_expressions(m)
        if left:
            unreal.log_warning(f'[dreamfield] {name} 旧节点删不干净：还剩 {left} 个')
        else:
            log(name, '旧节点已清空')
    else:
        m = tools.create_asset(name, f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
    for flag in ('used_with_nanite', 'used_with_instanced_static_meshes'):
        try:
            m.set_editor_property(flag, True)
        except Exception as e:
            log('设不了', flag, e)
    return m


def node(m, cls, x, y):
    return mel.create_material_expression(m, cls, x, y)


def custom(m, code, names, desc, out_type, x, y):
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


def scalar(m, name, v, x, y):
    s = node(m, unreal.MaterialExpressionScalarParameter, x, y)
    s.set_editor_property('parameter_name', name)
    s.set_editor_property('default_value', v)
    return s


def vec(m, name, v, x, y):
    s = node(m, unreal.MaterialExpressionVectorParameter, x, y)
    s.set_editor_property('parameter_name', name)
    s.set_editor_property('default_value', unreal.LinearColor(v[0], v[1], v[2], 1.0))
    return s


def link(a, out, b, inp):
    ok = mel.connect_material_expressions(a, out, b, inp)
    if not ok:
        unreal.log_warning(f'[dreamfield] 连不上 {a.get_name()}.{out} → {b.get_name()}.{inp}')
    return ok


def finish(m):
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    log('材质', m.get_path_name())


def make_mi(name, parent, scalars=None, vectors=None):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', parent)
    mel.clear_all_material_instance_parameters(mi)    # 母材质改过参数（10-31 第二版面板材质），旧的覆盖值清掉再设
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    eas.save_loaded_asset(mi)
    return mi


# 面板材质参数（10-31 第二版，见 tools/shaders/dreamfield_panel.hlsl）。Tile 是贴图一格多大：原包墙 5 米、甲板 10 米；
# 墙放大了 1.2 倍、机体视角又远（几十到几百米），原尺度的格栅只有 3 厘米、远看全糊成一片 → 墙用 12 米一格
# 第三轮（用户「亮面更白、阴影更蓝更深」）：底色提到 0.84–0.95 偏冷白，缝隙压暗 0.6→0.75，朝下的面压暗（UnderDark）
PANEL = {'Tile': 1200.0, 'Var': 0.1, 'CavityDark': 0.75, 'FadeH': 9000.0, 'BumpScale': 2.8, 'GlowDensity': 0.006, 'Rough': 0.6, 'UnderDark': 0.7,
         'Spec': 0.5}
PANEL_V = {'Tint': (0.80, 0.90, 0.97), 'GlowColor': (3.2, 2.6, 2.2)}   # 冷白略带青（原图亮面）

if 'mat' in STEPS:
    # 面板（墙、甲板、平台、小件）：世界坐标三向投影的 UV → 采原包面板图的颜色和法线 → 底色 / 自发光 / 世界空间法线
    m = new_material('M_DW_Panel')
    m.set_editor_property('tangent_space_normal', False)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -1500, 100)
    params = {}
    y = 300
    for k in ('Tile', 'Tint', 'Var', 'CavityDark', 'FadeH', 'BumpScale', 'GlowColor', 'GlowDensity', 'UnderDark'):
        params[k] = vec(m, k, PANEL_V[k], -1500, y) if k in PANEL_V else scalar(m, k, PANEL[k], -1500, y)
        y += 90
    cu = custom(m, hlsl('dreamfield_panel_uv.hlsl'), ['WP', 'N', 'Tile'], 'PanelUV', unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1150, 0)
    link(wp, '', cu, 'WP')
    link(nn, '', cu, 'N')
    link(params['Tile'], '', cu, 'Tile')
    tc = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -850, -150)
    tc.set_editor_property('parameter_name', 'PanelColor')
    tn = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -850, 150)
    tn.set_editor_property('parameter_name', 'PanelNormal')
    t_c = eas.load_asset(f'{DEST}/Textures/T_DW_Panel_C')
    t_n = eas.load_asset(f'{DEST}/Textures/T_DW_Panel_N')
    if t_c:
        tc.set_editor_property('texture', t_c)
    if t_n:
        tn.set_editor_property('texture', t_n)
        tn.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    link(cu, '', tc, 'UVs')
    link(cu, '', tn, 'UVs')
    pn = ['Tile', 'Tint', 'Var', 'CavityDark', 'FadeH', 'BumpScale', 'GlowColor', 'GlowDensity', 'UnderDark']
    names = ['WP', 'N', 'C', 'NT', 'Mode'] + pn
    code = hlsl('dreamfield_panel.hlsl')
    outs = {}
    for mode, desc, yy in ((0.0, 'PanelBase', 0), (1.0, 'PanelGlow', 300), (2.0, 'PanelNormal', 600)):
        c_ = custom(m, code, names, desc, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, yy)
        link(wp, '', c_, 'WP')
        link(nn, '', c_, 'N')
        link(tc, 'RGB', c_, 'C')
        link(tn, 'RGB', c_, 'NT')
        mc = node(m, unreal.MaterialExpressionConstant, -650, yy + 100)
        mc.set_editor_property('r', mode)
        link(mc, '', c_, 'Mode')
        for k in pn:
            link(params[k], '', c_, k)
        outs[desc] = c_
    mel.connect_material_property(outs['PanelBase'], '', P.MP_BASE_COLOR)
    mel.connect_material_property(outs['PanelGlow'], '', P.MP_EMISSIVE_COLOR)
    mel.connect_material_property(outs['PanelNormal'], '', P.MP_NORMAL)
    mel.connect_material_property(scalar(m, 'Rough', PANEL['Rough'], -400, 900), '', P.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, 'Spec', PANEL['Spec'], -400, 1000), '', P.MP_SPECULAR)
    finish(m)
    make_mi('MI_DW_Wall', m)
    # 平台顶面是以后堡垒的地面：格子小一点、凹凸弱一点、不发光，偏暖一点的浅灰（和墙分开）
    make_mi('MI_DW_Platform', m, {'Tile': 800.0, 'Var': 0.06, 'GlowDensity': 0.0, 'FadeH': 1500.0, 'BumpScale': 1.4, 'UnderDark': 1.0},
            {'Tint': (0.50, 0.49, 0.50)})
    make_mi('MI_DW_Deck', m, {'Tile': 1200.0, 'GlowDensity': 0.0, 'UnderDark': 0.2, 'Spec': 0.15}, {'Tint': (0.66, 0.70, 0.78)})
    # 衬墙（楼柱群背后那一整面）压暗：楼柱之间的缝看进去是暗的，远看墙面才有层次
    make_mi('MI_DW_Backing', m, {'Tile': 1800.0, 'GlowDensity': 0.012, 'FadeH': 15000.0}, {'Tint': (0.15, 0.20, 0.31)})   # 第三轮：衬墙在太阳下也是深蓝灰（第二轮受光后是一片中灰偏紫，中间调太多）

    # 地面（第二版 11-01，用户「旷野精细度太低」）：大尺度草色噪声 + Kikuyu 草叶撒出来的草坪贴图（两个尺度）+ 近处法线细节，世界坐标、网格不用 UV
    m = new_material('M_DF_Grass')
    m.set_editor_property('tangent_space_normal', False)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1500, 0)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -1500, 100)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1500, 200)
    tA = scalar(m, 'LawnTileA', 380.0, -1500, 300)
    tB = scalar(m, 'LawnTileB', 1270.0, -1500, 390)
    cu = custom(m, 'float2 p = float2(WP.x, -WP.y);\nreturn p / TileA;', ['WP', 'TileA'], 'LawnUVA', unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1200, -100)
    cv = custom(m, 'float a = radians(37.0);\nfloat s = sin(a);\nfloat c = cos(a);\nfloat2 p = float2(WP.x, -WP.y);\n'
                   'return float2(c * p.x - s * p.y, s * p.x + c * p.y) / TileB + float2(0.37, 0.61);', ['WP', 'TileB'], 'LawnUVB',
                unreal.CustomMaterialOutputType.CMOT_FLOAT2, -1200, 100)
    link(wp, '', cu, 'WP')
    link(tA, '', cu, 'TileA')
    link(wp, '', cv, 'WP')
    link(tB, '', cv, 'TileB')
    lc = eas.load_asset(f'{DEST}/Textures/T_DF_Lawn_C')
    ln = eas.load_asset(f'{DEST}/Textures/T_DF_Lawn_N')
    smp = {}
    for key, uvn, tex_, y in (('C1', cu, lc, -300), ('C2', cv, lc, -150), ('N1', cu, ln, 0), ('N2', cv, ln, 150)):
        t_ = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, y)
        t_.set_editor_property('parameter_name', 'LawnColor' if key[0] == 'C' else 'LawnNormal')
        if tex_:
            t_.set_editor_property('texture', tex_)
        t_.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if key[0] == 'N' else
                               unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        link(uvn, '', t_, 'UVs')
        smp[key] = t_
    gp = ['ColorA', 'ColorB', 'ColorC', 'MacroScale', 'DetailAmount', 'BumpScale', 'FadeDist']
    gv = {'ColorA': (0.267, 0.348, 0.046), 'ColorB': (0.139, 0.29, 0.035), 'ColorC': (0.087, 0.209, 0.029)}   # 第三轮太阳 7→11 勒克斯，草色 ×0.58 保持原来的亮度
    gs = {'MacroScale': 6000.0, 'DetailAmount': 0.7, 'BumpScale': 0.8, 'FadeDist': 15000.0}
    gparams = {}
    y = 500
    for k in gp:
        gparams[k] = vec(m, k, gv[k], -900, y) if k in gv else scalar(m, k, gs[k], -900, y)
        y += 90
    code = hlsl('dreamfield_grass.hlsl')
    outs = {}
    for mode, desc, yy in ((0.0, 'GrassBase', 0), (1.0, 'GrassNormal', 400)):
        c_ = custom(m, code, ['WP', 'N', 'CamPos', 'C1', 'C2', 'N1', 'N2', 'Mode'] + gp, desc, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, yy)
        link(wp, '', c_, 'WP')
        link(nn, '', c_, 'N')
        link(cam, '', c_, 'CamPos')
        for key in ('C1', 'C2', 'N1', 'N2'):
            link(smp[key], 'RGB', c_, key)
        mc = node(m, unreal.MaterialExpressionConstant, -650, yy + 200)
        mc.set_editor_property('r', mode)
        link(mc, '', c_, 'Mode')
        for k in gp:
            link(gparams[k], '', c_, k)
        outs[desc] = c_
    mel.connect_material_property(outs['GrassBase'], '', P.MP_BASE_COLOR)
    mel.connect_material_property(outs['GrassNormal'], '', P.MP_NORMAL)
    mel.connect_material_property(scalar(m, 'Rough', 0.92, -400, 800), '', P.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, 'Spec', 0.3, -400, 900), '', P.MP_SPECULAR)
    finish(m)
    make_mi('MI_DF_Grass', m)

    # 封锁大门、电梯井用的几种材质（第二版 11-01）
    # 黄黑警示条纹
    m = new_material('M_DW_Hazard')
    wp = node(m, unreal.MaterialExpressionWorldPosition, -900, 0)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -900, 100)
    ch = custom(m, hlsl('dreamfield_hazard.hlsl'), ['WP', 'N', 'Width', 'ColorA', 'ColorB'], 'Hazard', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -400, 0)
    link(wp, '', ch, 'WP')
    link(nn, '', ch, 'N')
    link(scalar(m, 'Width', 120.0, -900, 200), '', ch, 'Width')
    link(vec(m, 'ColorA', (0.55, 0.36, 0.02), -900, 300), '', ch, 'ColorA')
    link(vec(m, 'ColorB', (0.025, 0.025, 0.03), -900, 400), '', ch, 'ColorB')
    mel.connect_material_property(ch, '', P.MP_BASE_COLOR)
    mel.connect_material_property(scalar(m, 'Rough', 0.55, -400, 300), '', P.MP_ROUGHNESS)
    finish(m)
    make_mi('MI_DW_Hazard', m)
    # 状态灯：不受光的纯自发光（亮度按固定曝光 EV100 0.85 调）。第一次 14 太亮：色调映射把高光压成了肉粉色，5 还是橙红，
    # 3 在白天看得出是亮着的灯、颜色是正红
    m = new_material('M_DW_Light')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    em = node(m, unreal.MaterialExpressionMultiply, -400, 0)
    link(vec(m, 'Color', (1.0, 0.012, 0.006), -800, 0), '', em, 'A')
    link(scalar(m, 'Intensity', 3.0, -800, 150), '', em, 'B')
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    finish(m)
    make_mi('MI_DW_LightRed', m)
    make_mi('MI_DW_LightAmber', m, {'Intensity': 5.0}, {'Color': (1.0, 0.4, 0.04)})
    # 深色钢（大门门框、加固横梁、电梯井架、轿厢）和大门门扇：面板材质换颜色、凹凸弱一点、不发光
    pm = eas.load_asset(f'{DEST}/Materials/M_DW_Panel')
    make_mi('MI_DW_Steel', pm, {'Tile': 600.0, 'GlowDensity': 0.0, 'BumpScale': 1.6, 'CavityDark': 0.6, 'UnderDark': 1.0, 'FadeH': 100000.0,
                                'Var': 0.08}, {'Tint': (0.26, 0.27, 0.3)})
    make_mi('MI_DW_GateLeaf', pm, {'Tile': 900.0, 'GlowDensity': 0.0, 'BumpScale': 2.2, 'CavityDark': 0.6, 'UnderDark': 1.0, 'FadeH': 100000.0,
                                   'Var': 0.12}, {'Tint': (0.5, 0.52, 0.56)})

    # 草叶 / 白花（StyleHex 免费草花包，用户 10-31 下载到 /Game/StyleHex_Studio）：在包里的材质实例下面再派生一份调颜色，包里的不动。
    # 草叶底部偏深、尖上是和地面一样的亮黄绿（远处剔除后接得上）；花丛的叶子染成同一种绿，花瓣用图集原色
    SH = '/Game/StyleHex_Studio/Free_Packs/FREE_Stylized_Grass_Flowers_Foliage/Materials/Mat_Foliage'
    for mi_name, parent, vecs in (('MI_DF_GrassBlades', f'{SH}/MI_Fol_Grass_AlphaCut', {'Base Color': (0.075, 0.186, 0.023), 'Top Color': (0.255, 0.348, 0.052)}),
                                  ('MI_DF_Flowers', f'{SH}/MI_Fol_Foliage_Atlas', {'Custom Color': (0.128, 0.29, 0.041)})):
        par = eas.load_asset(parent) if eas.does_asset_exist(parent) else None
        if not par:
            log('没有 StyleHex 草花包，跳过', mi_name)
            continue
        make_mi(mi_name, par, None, vecs)
        base = par.get_base_material()
        for flag in ('used_with_instanced_static_meshes',):
            if not base.get_editor_property(flag):      # HISM 上用的材质要有这个用法标记，不然游戏里退回默认材质
                base.set_editor_property(flag, True)
                eas.save_loaded_asset(base)
                log('补用法标记', base.get_path_name(), flag)

    # 天空球：不受光、双面；HDR 立方体贴图按视线方向采样，再调成粉蓝
    m = new_material('M_DF_Sky')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1300, 0)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1300, 100)
    cd = custom(m, hlsl('dreamfield_sky_dir.hlsl'), ['WP', 'CamPos', 'Angle'], 'SkyDir', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -1000, 0)
    link(wp, '', cd, 'WP')
    link(cam, '', cd, 'CamPos')
    link(scalar(m, 'Angle', 0.0, -1300, 200), '', cd, 'Angle')
    cs = node(m, unreal.MaterialExpressionTextureSampleParameterCube, -700, 0)
    cs.set_editor_property('parameter_name', 'SkyCube')
    cube = eas.load_asset(f'{DEST}/Textures/T_DF_SkyCube')
    if cube:
        cs.set_editor_property('texture', cube)
        try:
            cs.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if not cube.get_editor_property('srgb')
                                   else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        except Exception as e:
            log('天空采样类型', e)
    link(cd, '', cs, 'UVs')
    dd = custom(m, 'return normalize(WP - CamPos);', ['WP', 'CamPos'], 'ViewDir', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -700, 300)
    link(wp, '', dd, 'WP')
    link(cam, '', dd, 'CamPos')
    sp = ['Scale', 'ClampMax', 'HorizonTint', 'ZenithTint', 'HorizonWidth', 'Pastel', 'Desat', 'BelowColor', 'HorizonBlend']
    sv = {'HorizonTint': (1.15, 0.9, 1.05), 'ZenithTint': (0.96, 1.0, 1.06), 'BelowColor': (1.72, 1.45, 1.78)}   # BelowColor = 高度雾颜色
    ss = {'Scale': 1.19, 'ClampMax': 6.0, 'HorizonWidth': 0.45, 'Pastel': 0.7, 'Desat': 0.05, 'HorizonBlend': 0.07}   # 曝光 EV 0.6→0.85 后天空 ×1.19 保持原亮度
    cc = custom(m, hlsl('dreamfield_sky.hlsl'), ['Sky', 'D'] + sp, 'SkyGrade', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -350, 0)
    link(cs, 'RGB', cc, 'Sky')
    link(dd, '', cc, 'D')
    y = 400
    for k in sp:
        link(vec(m, k, sv[k], -700, y) if k in sv else scalar(m, k, ss[k], -700, y), '', cc, k)
        y += 90
    mel.connect_material_property(cc, '', P.MP_EMISSIVE_COLOR)
    finish(m)
    make_mi('MI_DF_Sky', m)

    # 行星：不受光，峡谷包的行星花纹 + 粉色大气边
    m = new_material('M_DF_Planet')
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    tx = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -800, 0)
    tx.set_editor_property('parameter_name', 'Tex')
    ptex = eas.load_asset(f'{DEST}/Textures/T_DF_Planet')
    if ptex:
        tx.set_editor_property('texture', ptex)
    nn = node(m, unreal.MaterialExpressionVertexNormalWS, -800, 250)
    wp = node(m, unreal.MaterialExpressionWorldPosition, -1100, 350)
    cam = node(m, unreal.MaterialExpressionCameraPositionWS, -1100, 450)
    vd = custom(m, 'return normalize(WP - CamPos);', ['WP', 'CamPos'], 'ViewDir', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, 350)
    link(wp, '', vd, 'WP')
    link(cam, '', vd, 'CamPos')
    pp_ = ['Tint', 'Glow', 'RimColor', 'Lit', 'Veil', 'VeilColor']
    cp = custom(m, hlsl('dreamfield_planet.hlsl'), ['Tex', 'N', 'V'] + pp_, 'Planet', unreal.CustomMaterialOutputType.CMOT_FLOAT3, -350, 0)
    link(tx, 'RGB', cp, 'Tex')
    link(nn, '', cp, 'N')
    link(vd, '', cp, 'V')
    link(vec(m, 'Tint', (1.0, 0.62, 0.8), -800, 550), '', cp, 'Tint')
    link(scalar(m, 'Glow', 2.6, -800, 640), '', cp, 'Glow')
    link(scalar(m, 'Veil', 0.35, -800, 910), '', cp, 'Veil')
    link(vec(m, 'VeilColor', (1.3, 1.1, 1.35), -800, 1000), '', cp, 'VeilColor')
    link(vec(m, 'RimColor', (1.2, 0.8, 1.1), -800, 730), '', cp, 'RimColor')
    link(vec(m, 'Lit', (-0.6, -0.3, 0.6), -800, 820), '', cp, 'Lit')
    mel.connect_material_property(cp, '', P.MP_EMISSIVE_COLOR)
    finish(m)
    make_mi('MI_DF_Planet', m, {'Glow': 3.1}, {'Lit': (-0.498, -0.637, 0.588), 'VeilColor': (1.55, 1.31, 1.6)})   # Lit 指向太阳（ue_build_dreamfield.py 的 SUN_YAW / SUN_PITCH）
    make_mi('MI_DF_Moon', m, {'Glow': 2.9, 'Veil': 0.2}, {'Tint': (1.0, 0.72, 0.45), 'RimColor': (0.6, 0.45, 0.4), 'Lit': (-0.498, -0.637, 0.588)})

    # 云：受光的软白，带一点自发光（背光面不发灰），不投影（摆放时关）
    m = new_material('M_DF_Cloud')
    col = vec(m, 'Color', (0.6, 0.59, 0.62), -600, 0)   # 第三轮太阳 11 勒克斯，云的底色压低、自发光提高，看上去和以前一样
    mel.connect_material_property(col, '', P.MP_BASE_COLOR)
    em = node(m, unreal.MaterialExpressionMultiply, -300, 200)
    link(col, '', em, 'A')
    link(scalar(m, 'SelfLight', 0.85, -600, 200), '', em, 'B')
    mel.connect_material_property(em, '', P.MP_EMISSIVE_COLOR)
    mel.connect_material_property(scalar(m, 'Rough', 1.0, -600, 300), '', P.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, 'Spec', 0.0, -600, 400), '', P.MP_SPECULAR)
    finish(m)
    make_mi('MI_DF_Cloud', m)

# ---------------------------------------------------------------- 网格
def import_fbx(fbx, dest, name=None):
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', False)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('generate_lightmap_u_vs', False)
    smd.set_editor_property('build_nanite', True)
    tools.import_asset_tasks([task(fbx, dest, name=name, options=ui)])


if 'mesh' in STEPS or 'ground' in STEPS:
    MD = f'{DEST}/Meshes'
    if 'mesh' in STEPS:
        import_fbx(f'{SRC}/dw_wall.fbx', MD, 'SM_DW_WallDetail')
        import_fbx(f'{SRC}/dw_deck.fbx', MD, 'SM_DW_DeckTile')
        import_fbx(f'{SRC}/dw_bits.fbx', MD)
        import_fbx(f'{SRC}/df_clouds.fbx', MD)
    import_fbx(f'{SRC}/df_ground_near.fbx', MD, 'SM_DF_GroundNear')
    import_fbx(f'{SRC}/df_ground_far.fbx', MD, 'SM_DF_GroundFar')
    mats = {'SM_DW_': f'{DEST}/Materials/MI_DW_Wall', 'SM_DF_Cloud': f'{DEST}/Materials/MI_DF_Cloud',
            'SM_DF_Ground': f'{DEST}/Materials/MI_DF_Grass'}
    for path in eas.list_assets(MD, recursive=False, include_folder=False):
        a = eas.load_asset(path)
        if not isinstance(a, unreal.StaticMesh):
            continue
        name = a.get_name()
        nan = a.get_editor_property('nanite_settings')
        if not nan.enabled:
            nan.enabled = True
            a.set_editor_property('nanite_settings', nan)
        bs = a.get_editor_property('body_setup')
        if bs:
            # 只有近处地面用网格本身当碰撞；墙、甲板、小件、云不碰撞（墙和平台的碰撞是摆放时另加的方块）
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if name == 'SM_DF_GroundNear'
                                   else unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        # 资产上的材质槽（unreal.Array 里的结构体是副本，改完要放回新数组再写，见 README 城区一节的坑）
        mi = next((eas.load_asset(v) for k, v in mats.items() if name.startswith(k)), None)
        if mi:
            sm = list(a.get_editor_property('static_materials'))
            for i, s in enumerate(sm):
                s.set_editor_property('material_interface', mi)
                sm[i] = s
            a.set_editor_property('static_materials', sm)
        eas.save_loaded_asset(a)
        try:
            tri = a.get_num_triangles(0)
        except Exception:
            tri = '?'
        log('网格', name, '三角形', tri, 'Nanite', a.get_editor_property('nanite_settings').enabled)
log('完成', STEPS)
