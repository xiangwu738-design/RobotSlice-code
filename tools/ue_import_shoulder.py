"""在 RobotSlice 工程里跑：导入肩部武器（钩爪、捕获网）的模型和音效。
- Inbox/_肩部候选/游戏模型/*.fbx（lookdev/make_shoulder_game.py 的输出）→ /Game/Weapons/Meshes，材质槽名就是机体材质实例名，直接挂 /Game/Robot/Materials 里的同名实例
- SourceAssets/sfx_shoulder/*.wav → /Game/Audio/Shoulder/S_*（net_shock、shield_hum 循环：网住 / 开盾期间一直响）
- 护盾光面材质 /Game/Weapons/Materials/M_ShieldFill：叠加、不受光、双面，颜色参数 Emissive（队伍色）× 边缘亮（菲涅耳）× 强度 Strength
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_shoulder.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MESHES = 'D:/RobotSlice/Inbox/_肩部候选/游戏模型'
SFX = 'D:/RobotSlice/SourceAssets/sfx_shoulder'


def task(filename, dest, name, options=None):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    if options:
        t.options = options
    return t


# ---------------------------------------------------------------- 护盾材质（模型的材质槽 M_ShieldFill / M_ShieldGrid 挂它们）
# 叠加、不受光、双面：颜色参数 Emissive（队伍色）× 强度 Strength × 白天补偿 max(1, 曝光倒数×1.3)^0.85（白天自动曝光高，不补偿就看不见，
# 和 tools/ue_fx_daylight.py 给特效加的一样）；光面再乘边缘亮（菲涅耳），网格线不乘。已有就清空节点重连（模型引用着，不删）。
mel = unreal.MaterialEditingLibrary


def hlsl(name):
    """自定义节点的代码：tools/shaders 下的文件去掉 // 注释（注释是中文，着色器编译遇到非 ASCII 字符会失败，材质退回默认的灰格子）"""
    import re
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), f'{name} 去掉注释后还有非 ASCII 字符'
    return code


def find_custom(m, prop):
    """从材质属性的输入往上找第一个自定义节点"""
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
    """材质已经有了：只换自定义节点的代码再重编（游戏代码启动时就加载了这些材质，清空节点重建会触发 !IsRooted 断言崩溃）"""
    if not eas.does_asset_exist(path):
        return False
    m = eas.load_asset(path)
    c = find_custom(m, prop)
    if not c:
        return False
    c.set_editor_property('code', code)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[shoulder] 更新材质代码 {path}')
    return True


def shield_material(name, fresnel, strength):
    path = f'/Game/Weapons/Materials/{name}'
    m = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(name, '/Game/Weapons/Materials', unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    color = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, 0)
    color.set_editor_property('parameter_name', 'Emissive')
    color.set_editor_property('default_value', unreal.LinearColor(0.15, 0.75, 1.0, 1.0))
    power = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 200)
    power.set_editor_property('parameter_name', 'Strength')
    power.set_editor_property('default_value', strength)
    eye = mel.create_material_expression(m, unreal.MaterialExpressionEyeAdaptationInverse, -900, 400)
    k = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -900, 500)
    k.set_editor_property('r', 1.3)
    eyek = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -700, 420)
    mel.connect_material_expressions(eye, '', eyek, 'A')
    mel.connect_material_expressions(k, '', eyek, 'B')
    one = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -700, 540)
    one.set_editor_property('r', 1.0)
    mx = mel.create_material_expression(m, unreal.MaterialExpressionMax, -550, 450)
    mel.connect_material_expressions(eyek, '', mx, 'A')
    mel.connect_material_expressions(one, '', mx, 'B')
    exp = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -550, 560)
    exp.set_editor_property('r', 0.85)
    pw = mel.create_material_expression(m, unreal.MaterialExpressionPower, -400, 470)
    mel.connect_material_expressions(mx, '', pw, 'Base')
    mel.connect_material_expressions(exp, '', pw, 'Exponent')
    pw.set_editor_property('desc', 'ShieldDayBoost')
    cs = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 80)
    mel.connect_material_expressions(color, '', cs, 'A')
    mel.connect_material_expressions(power, '', cs, 'B')
    last = cs
    if fresnel:
        fres = mel.create_material_expression(m, unreal.MaterialExpressionFresnel, -600, 250)
        fres.set_editor_property('exponent', 2.5)
        fres.set_editor_property('base_reflect_fraction', 0.12)
        mf = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -400, 120)
        mel.connect_material_expressions(cs, '', mf, 'A')
        mel.connect_material_expressions(fres, '', mf, 'B')
        last = mf
    out = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 200)
    mel.connect_material_expressions(last, '', out, 'A')
    mel.connect_material_expressions(pw, '', out, 'B')
    mel.connect_material_property(out, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)




def day_boost(m, x, y):
    """白天补偿 max(1, 曝光倒数×1.3)^0.85（和 tools/ue_fx_daylight.py 一样）"""
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


def shield_fx():
    """护盾能量面：叠加、不受光、双面；蜂窝 / 闪烁 / 扫描带 / 展开 / 受击波纹 / 相交亮线都在一个自定义节点里（tools/shaders/shield_fx.hlsl）。
    参数：Emissive 队伍色、Strength 亮度、Deploy 展开 0–1、Hit0..Hit3 最近四次受击（UV0 坐标 u, v, 开始时间）"""
    name = 'M_ShieldFX'
    path = f'/Game/Weapons/Materials/{name}'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, hlsl('shield_fx.hlsl')):
        return
    m = tools.create_asset(name, '/Game/Weapons/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property('two_sided', True)
    custom = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property('code', hlsl('shield_fx.hlsl'))
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('description', 'ShieldFX')
    names = ['UV0', 'UV1', 'T', 'Color', 'Strength', 'Deploy', 'H0', 'H1', 'H2', 'H3', 'DepthDiff', 'Fres', 'Day']
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        ins.append(ci)
    custom.set_editor_property('inputs', ins)
    y = -600

    def node(cls, **props):
        nonlocal y
        e = mel.create_material_expression(m, cls, -900, y)
        y += 110
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    uv0 = node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    uv1 = node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1)
    t = node(unreal.MaterialExpressionTime)
    color = node(unreal.MaterialExpressionVectorParameter, parameter_name='Emissive', default_value=unreal.LinearColor(0.15, 0.75, 1.0, 1.0))
    strength = node(unreal.MaterialExpressionScalarParameter, parameter_name='Strength', default_value=1.0)
    deploy = node(unreal.MaterialExpressionScalarParameter, parameter_name='Deploy', default_value=1.0)
    hits = [node(unreal.MaterialExpressionVectorParameter, parameter_name=f'Hit{i}', default_value=unreal.LinearColor(0.0, 0.0, -100.0, 0.0)) for i in range(4)]
    sd = node(unreal.MaterialExpressionSceneDepth)
    pd = node(unreal.MaterialExpressionPixelDepth)
    diff = mel.create_material_expression(m, unreal.MaterialExpressionSubtract, -700, 600)
    mel.connect_material_expressions(sd, '', diff, 'A')
    mel.connect_material_expressions(pd, '', diff, 'B')
    fres = node(unreal.MaterialExpressionFresnel, exponent=3.0, base_reflect_fraction=0.0)
    day = day_boost(m, -1400, 900)
    for n, e in zip(names, [uv0, uv1, t, color, strength, deploy] + hits + [diff, fres, day]):
        mel.connect_material_expressions(e, '', custom, n)
    mel.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)


shield_fx()

# ---------------------------------------------------------------- 干扰烟幕：烟片 M_SmokePuff、箔片 M_ChaffGlint（AMechSmoke 用）
# 烟片贴图用特效包里的写实烟雾序列帧（几行几列、浓度在哪个通道见下面三个常数）
SMOKE_TEX = '/Game/AAAVFX/Course_05/Texture/Smoke_02_FC'
SMOKE_COLS, SMOKE_ROWS, SMOKE_CHANNEL = 8.0, 8.0, 0.0  # 8×8 共 64 帧，一团烟从小长大；浓度在 A


def custom_node(m, code, out_type, names, x=-300, y=0, desc=''):
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
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


def smoke_puff():
    """烟片：半透明、受光（体积无方向光照，白天夜里都跟着场景亮暗）；参数 Frame 序列帧、Rot 旋转、Grow 缩放、Opacity、Tint"""
    name = 'M_SmokePuff'
    path = f'/Game/Weapons/Materials/{name}'
    if update_code(path, unreal.MaterialProperty.MP_BASE_COLOR, hlsl('smoke_puff.hlsl')):
        return
    m = tools.create_asset(name, '/Game/Weapons/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    m.set_editor_property('two_sided', True)
    names = ['Tex', 'UV', 'Frame', 'Cols', 'Rows', 'Rot', 'Grow', 'Opacity', 'Tint', 'Channel', 'Contrast', 'SoftEdge']
    c = custom_node(m, hlsl('smoke_puff.hlsl'), unreal.CustomMaterialOutputType.CMOT_FLOAT4, names, desc='SmokePuff')
    y = -700

    def node(cls, **props):
        nonlocal y
        e = mel.create_material_expression(m, cls, -800, y)
        y += 110
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    tex = node(unreal.MaterialExpressionTextureObjectParameter, parameter_name='Tex', texture=eas.load_asset(SMOKE_TEX))
    uv = node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    sp = lambda n, v: node(unreal.MaterialExpressionScalarParameter, parameter_name=n, default_value=v)
    ins = [tex, uv, sp('Frame', 0.0), sp('Cols', SMOKE_COLS), sp('Rows', SMOKE_ROWS), sp('Rot', 0.0), sp('Grow', 1.0), sp('Opacity', 1.0),
           node(unreal.MaterialExpressionVectorParameter, parameter_name='Tint', default_value=unreal.LinearColor(0.74, 0.76, 0.78, 1.0)),
           sp('Channel', SMOKE_CHANNEL), sp('Contrast', 1.2), node(unreal.MaterialExpressionDepthFade, fade_distance_default=180.0)]
    for n, e in zip(names, ins):
        mel.connect_material_expressions(e, '', c, n)
    rgb = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, 0, -80)
    rgb.set_editor_property('r', True)
    rgb.set_editor_property('g', True)
    rgb.set_editor_property('b', True)
    a = mel.create_material_expression(m, unreal.MaterialExpressionComponentMask, 0, 80)
    # 分量遮罩新建时 R 默认勾着：只加 A 的话输出 (R, A)，不透明度只取第一个分量 = 烟的颜色常数，整片烟变方板（10-30 查出来的）
    for k in 'rgb':
        a.set_editor_property(k, False)
    a.set_editor_property('a', True)
    mel.connect_material_expressions(c, '', rgb, '')
    mel.connect_material_expressions(c, '', a, '')
    mel.connect_material_property(rgb, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(a, '', unreal.MaterialProperty.MP_OPACITY)
    rough = mel.create_material_expression(m, unreal.MaterialExpressionConstant, 0, 220)
    rough.set_editor_property('r', 1.0)
    mel.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    spec = mel.create_material_expression(m, unreal.MaterialExpressionConstant, 0, 320)
    spec.set_editor_property('r', 0.0)
    mel.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)


def chaff_glint():
    """干扰箔片：叠加发光的小圆点（白天补偿），参数 Glint 亮度（代码里一闪一闪）、Emissive 颜色"""
    name = 'M_ChaffGlint'
    path = f'/Game/Weapons/Materials/{name}'
    code = 'float d = length(UV - 0.5) * 2.0;\nfloat core = pow(saturate(1.0 - d), 5.0);\nreturn Color.rgb * Glint * core * 9.0 * Day;'
    if update_code(path, unreal.MaterialProperty.MP_EMISSIVE_COLOR, code):
        return
    m = tools.create_asset(name, '/Game/Weapons/Materials', unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    names = ['UV', 'Color', 'Glint', 'Day']
    c = custom_node(m, code, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, desc='ChaffGlint')
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -700, -200)
    col = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, -80)
    col.set_editor_property('parameter_name', 'Emissive')
    col.set_editor_property('default_value', unreal.LinearColor(0.7, 0.82, 1.0, 1.0))
    gl = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 40)
    gl.set_editor_property('parameter_name', 'Glint')
    gl.set_editor_property('default_value', 1.0)
    day = day_boost(m, -1300, 200)
    for n, e in zip(names, [uv, col, gl, day]):
        mel.connect_material_expressions(e, '', c, n)
    mel.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)


smoke_puff()
chaff_glint()
# 烟片贴图不参与流送：烟片是朝向镜头的贴片组件，不向流送系统报告用了哪张贴图，4096 的序列帧会一直停在最小那级（整张图变成一个平均色，
# 烟片就成了均匀的方板）。限到 2048、常驻显存（约 4MB）
smoke_tex = eas.load_asset(SMOKE_TEX)
unreal.log(f"[shoulder] 烟片贴图 虚拟纹理 {smoke_tex.get_editor_property('virtual_texture_streaming')} 常驻 {smoke_tex.get_editor_property('never_stream')} 分组 {smoke_tex.get_editor_property('lod_group')}")
smoke_tex.set_editor_property('virtual_texture_streaming', False)  # 自定义节点直接采样虚拟纹理会取错（整片烟一个平均色）
smoke_tex.set_editor_property('never_stream', True)
smoke_tex.set_editor_property('max_texture_size', 2048)
eas.save_loaded_asset(smoke_tex)

ui = unreal.FbxImportUI()
ui.set_editor_property('import_mesh', True)
ui.set_editor_property('import_as_skeletal', False)
ui.set_editor_property('import_materials', False)
ui.set_editor_property('import_textures', False)
ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
ui.static_mesh_import_data.set_editor_property('combine_meshes', True)
ui.static_mesh_import_data.set_editor_property('auto_generate_collision', False)
files = sorted(f for f in os.listdir(MESHES) if f.endswith('.fbx'))
tools.import_asset_tasks([task(f'{MESHES}/{f}', '/Game/Weapons/Meshes', os.path.splitext(f)[0], ui) for f in files])
for f in files:
    mesh = eas.load_asset(f'/Game/Weapons/Meshes/{os.path.splitext(f)[0]}')
    sm = mesh.get_editor_property('static_materials')
    # 11-01 修：以前 for s in sm 里 s.set_editor_property 改的是数组元素的拷贝，写不回去，所有槽都留在引擎的 WorldGridMaterial（灰方格）。按索引 set_material。
    # 无人机机身的材质由文件末尾的 ue_drone_materials.py 再覆盖一遍
    for i, s in enumerate(sm):
        slot = str(s.get_editor_property('material_slot_name'))
        mi = eas.load_asset(f'/Game/Robot/Materials/{slot}') if eas.does_asset_exist(f'/Game/Robot/Materials/{slot}') else             eas.load_asset(f'/Game/Weapons/Materials/{slot}')
        if mi:
            mesh.set_material(i, mi)
        else:
            unreal.log_warning(f'[shoulder] 找不到材质 {slot}')
    # 不开 Nanite：新导入器默认会开，而 Nanite 网格用不了半透明 / 叠加材质（护盾会变成默认灰格子）；这些都是跟着机体动的小件，也用不上
    ns = mesh.get_editor_property('nanite_settings')
    if ns.get_editor_property('enabled'):
        ns.set_editor_property('enabled', False)
        unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).set_nanite_settings(mesh, ns, True)
        unreal.log(f'[shoulder] {mesh.get_name()} 关掉 Nanite')
    eas.save_loaded_asset(mesh)
    b = mesh.get_bounding_box()
    unreal.log(f'[shoulder] 模型 {mesh.get_name()} 尺寸 {(b.max - b.min) / 100} 米，范围 {b.min} .. {b.max}，材质 '
               f'{[str(s.get_editor_property("material_slot_name")) for s in sm]}')

wavs = sorted(f for f in os.listdir(SFX) if f.endswith('.wav'))
tools.import_asset_tasks([task(f'{SFX}/{f}', '/Game/Audio/Shoulder', 'S_' + os.path.splitext(f)[0]) for f in wavs])
for f in wavs:
    s = eas.load_asset('/Game/Audio/Shoulder/S_' + os.path.splitext(f)[0])
    if f.startswith('net_shock') or f.startswith('shield_hum'):
        s.set_editor_property('looping', True)
        eas.save_loaded_asset(s)
unreal.log(f'[shoulder] 音效 {len(wavs)} 个')

# 重新导入会把模型的槽重置，无人机机身的材质（M_DronePBR 那套）要再挂一遍
exec(open('D:/RobotSlice/tools/ue_drone_materials.py', encoding='utf-8').read())
