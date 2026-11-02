"""肩部无人机的材质（11-01，用户：「无人机材质也做一下吧」）。可重复跑。
方格的原因：所有肩部模型的槽位都指着引擎的 WorldGridMaterial（ue_import_shoulder.py 里 for s in sm 改的是数组元素的拷贝，没写回去）。
做法：模型 UV 是组件库拼的（有的范围 -191..39），按 UV 贴不了，母材质 M_DronePBR 用物体空间三向投影 + 屏幕空间曲率做涂装、磨损、边缘高光；
槽名不动，ApplyPaint / ApplyTeamColor 按槽名认：PaintDark 的 BaseColor 还跟玩家副漆走，Accent 的 BaseColor 被覆盖也不影响（UseBrand=1 用 BrandColor），Glow 还是队伍色。
不勾 Nanite 用途：这些模型都关着 Nanite（脚本会检查），图里又有 DDX，勾了反而可能整个材质编译失败、打包后变默认灰。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_drone_materials.py" -unattended -nosplash
"""
import os
import re
import shutil

import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MAT_DIR = '/Game/Weapons/Materials'
TEX_DIR = '/Game/Weapons/Textures'
MESH_DIR = '/Game/Weapons/Meshes'
BACKUP = 'D:/归档/RobotSlice_无人机材质前_20261101'
MASTER = 'M_DronePBR'


REPORT = []


def log(msg):
    unreal.log(f'[dronemat] {msg}')
    REPORT.append(msg)


def srgb(hexstr):
    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(*[f(int(hexstr[i:i + 2], 16)) for i in (0, 2, 4)], 1.0)


BRAND = {'VHI': srgb('C23A2E'), 'NEC': srgb('EE8A1C'), 'PHASE': srgb('3CC8DC')}


# ---------------------------------------------------------------- 备份
def disk_path(pkg):
    return 'D:/RobotSlice/Content/' + pkg[len('/Game/'):] + '.uasset'


def backup(pkg):
    src = disk_path(pkg)
    dst = os.path.join(BACKUP, pkg[len('/Game/'):] + '.uasset')
    if os.path.exists(src) and not os.path.exists(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)


# ---------------------------------------------------------------- 贴图：从城区贴图复制一份（不动城区的；常驻显存，照片和近景不糊）
TEXTURES = {'T_Drone_Grime_D': '/Game/Env/City/Textures/T_KB3D_CTS_MetalPaintGrayA_D', 'T_Drone_Grime_N': '/Game/Env/City/Textures/T_KB3D_CTS_MetalPaintGrayA_N'}
for name, src in TEXTURES.items():
    path = f'{TEX_DIR}/{name}'
    if not eas.does_asset_exist(path):
        assert eas.duplicate_asset(src, path), f'复制贴图失败 {src}'
    t = eas.load_asset(path)
    # 三向投影在自定义节点里采样：流送系统不知道用到多细，不常驻会一直停在低级 Mip
    t.set_editor_property('never_stream', True)
    t.set_editor_property('virtual_texture_streaming', False)
    eas.save_asset(path, only_if_is_dirty=False)


# ---------------------------------------------------------------- 母材质
def hlsl(name):
    """去掉 // 注释（着色器编译遇到中文注释会失败，材质退回默认灰格子）"""
    lines = open(f'D:/RobotSlice/tools/shaders/{name}', encoding='utf-8').read().splitlines()
    code = '\n'.join(re.sub('//.*$', '', ln) for ln in lines)
    assert all(ord(ch) < 128 for ch in code), f'{name} 去掉注释后还有非 ASCII 字符'
    return code


CUSTOMS = {'DroneSurface': 'drone_surface.hlsl', 'DroneColor': 'drone_color.hlsl', 'DroneRough': 'drone_rough.hlsl', 'DroneNormal': 'drone_normal.hlsl', 'DroneEmissive': 'drone_emissive.hlsl'}

SCALARS = {'Tile': 1.0 / 30.0, 'EdgeScale': 0.6, 'WearAmt': 1.0, 'ChipAmt': 0.8, 'SeamStepX': 16.0, 'SeamStepZ': 40.0, 'SeamW': 0.3, 'SeamOfs': 0.0, 'SeamAmt': 1.0,
           'UseBrand': 0.0, 'PaintGain': 1.0, 'GrimeAmt': 0.6, 'EdgeLight': 1.6, 'BandW': 0.0, 'LedR': 0.8, 'LedPower': 0.0, 'Rough': 0.45, 'RoughVar': 0.2, 'WornRough': 0.32,
           'Metallic': 0.35, 'NormalAmt': 0.8}
VECTORS = {'BaseColor': unreal.LinearColor(0.0395, 0.0497, 0.0612, 1), 'BrandColor': BRAND['NEC'], 'WornColor': unreal.LinearColor(0.56, 0.58, 0.61, 1),
           'Band': unreal.LinearColor(1, 0, 0, 0), 'LedPos': unreal.LinearColor(0, 0, 0, 0), 'LedStep': unreal.LinearColor(2.4, 0, 0, 0), 'LedColor': BRAND['NEC']}


def custom_node(m, desc, code, out_type, names, x, y):
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


def make_master():
    path = f'{MAT_DIR}/{MASTER}'
    m = tools.create_asset(MASTER, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property('tangent_space_normal', False)  # 法线直接给世界空间
    node = lambda cls, x, y: mel.create_material_expression(m, cls, x, y)
    y = {'v': -1600}

    def param(name):
        y['v'] += 90
        if name in SCALARS:
            n = node(unreal.MaterialExpressionScalarParameter, -2000, y['v'])
            n.set_editor_property('default_value', SCALARS[name])
        else:
            n = node(unreal.MaterialExpressionVectorParameter, -2000, y['v'])
            n.set_editor_property('default_value', VECTORS[name])
        n.set_editor_property('parameter_name', name)
        return n

    P_ = {n: param(n) for n in list(SCALARS) + list(VECTORS)}
    pos = node(unreal.MaterialExpressionLocalPosition, -1700, -600)
    vn = node(unreal.MaterialExpressionVertexNormalWS, -1900, -420)
    to_local = node(unreal.MaterialExpressionTransform, -1700, -420)
    to_local.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    to_local.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    assert mel.connect_material_expressions(vn, '', to_local, '')
    norm = node(unreal.MaterialExpressionNormalize, -1500, -420)
    assert mel.connect_material_expressions(to_local, '', norm, '')

    def deriv(cls, src, yy):
        d = node(cls, -1300, yy)
        assert mel.connect_material_expressions(src, '', d, '')
        return d

    dnx, dny = deriv(unreal.MaterialExpressionDDX, norm, -300), deriv(unreal.MaterialExpressionDDY, norm, -200)
    dpx, dpy = deriv(unreal.MaterialExpressionDDX, pos, -100), deriv(unreal.MaterialExpressionDDY, pos, 0)

    def tex_obj(name, path_, yy):
        n = node(unreal.MaterialExpressionTextureObjectParameter, -1300, yy)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('texture', eas.load_asset(path_))
        return n

    grime_d = tex_obj('GrimeTex', f'{TEX_DIR}/T_Drone_Grime_D', 150)
    grime_n = tex_obj('GrimeNormal', f'{TEX_DIR}/T_Drone_Grime_N', 300)
    F2, F3, F4 = unreal.CustomMaterialOutputType.CMOT_FLOAT2, unreal.CustomMaterialOutputType.CMOT_FLOAT3, unreal.CustomMaterialOutputType.CMOT_FLOAT4

    def hook(c, mapping):
        for k, src in mapping.items():
            assert mel.connect_material_expressions(src, '', c, k), f'接不上 {k}'

    # 表面遮罩
    names = ['Tex', 'P', 'N', 'DNx', 'DNy', 'DPx', 'DPy', 'Tile', 'EdgeScale', 'WearAmt', 'ChipAmt', 'SeamStepX', 'SeamStepZ', 'SeamW', 'SeamOfs', 'SeamAmt']
    surf = custom_node(m, 'DroneSurface', hlsl('drone_surface.hlsl'), F4, names, -900, -600)
    hook(surf, {'Tex': grime_d, 'P': pos, 'N': norm, 'DNx': dnx, 'DNy': dny, 'DPx': dpx, 'DPy': dpy, **{k: P_[k] for k in names[7:]}})
    # 底色
    names = ['S', 'P', 'Paint', 'Brand', 'UseBrand', 'PaintGain', 'GrimeAmt', 'EdgeLight', 'Worn', 'Band', 'BandW', 'LedPos', 'LedStep', 'LedR', 'LedPower']
    col = custom_node(m, 'DroneColor', hlsl('drone_color.hlsl'), F3, names, -400, -700)
    hook(col, {'S': surf, 'P': pos, 'Paint': P_['BaseColor'], 'Brand': P_['BrandColor'], 'UseBrand': P_['UseBrand'], 'PaintGain': P_['PaintGain'], 'GrimeAmt': P_['GrimeAmt'], 'EdgeLight': P_['EdgeLight'],
               'Worn': P_['WornColor'], 'Band': P_['Band'], 'BandW': P_['BandW'], 'LedPos': P_['LedPos'], 'LedStep': P_['LedStep'], 'LedR': P_['LedR'], 'LedPower': P_['LedPower']})
    # 粗糙度 / 金属度
    names = ['S', 'Rough', 'RoughVar', 'WornRough', 'Metallic']
    rm = custom_node(m, 'DroneRough', hlsl('drone_rough.hlsl'), F2, names, -400, -350)
    hook(rm, {'S': surf, 'Rough': P_['Rough'], 'RoughVar': P_['RoughVar'], 'WornRough': P_['WornRough'], 'Metallic': P_['Metallic']})

    def mask(ch, yy):
        c = node(unreal.MaterialExpressionComponentMask, -100, yy)
        for k in 'rgba':
            c.set_editor_property(k, k == ch)
        assert mel.connect_material_expressions(rm, '', c, '')
        return c

    # 法线：物体空间算好转世界
    names = ['Tex', 'P', 'N', 'Tile', 'Amount']
    nor = custom_node(m, 'DroneNormal', hlsl('drone_normal.hlsl'), F3, names, -400, -100)
    hook(nor, {'Tex': grime_n, 'P': pos, 'N': norm, 'Tile': P_['Tile'], 'Amount': P_['NormalAmt']})
    to_world = node(unreal.MaterialExpressionTransform, -100, -100)
    to_world.set_editor_property('transform_source_type', unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    to_world.set_editor_property('transform_type', unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    assert mel.connect_material_expressions(nor, '', to_world, '')
    # 自发光：白天补偿同 tools/ue_fx_daylight.py：max(1, 曝光倒数×1.3)^0.85
    eye = node(unreal.MaterialExpressionEyeAdaptationInverse, -1300, 500)
    k = node(unreal.MaterialExpressionConstant, -1300, 600)
    k.set_editor_property('r', 1.3)
    eyek = node(unreal.MaterialExpressionMultiply, -1150, 520)
    assert mel.connect_material_expressions(eye, '', eyek, 'A') and mel.connect_material_expressions(k, '', eyek, 'B')
    one = node(unreal.MaterialExpressionConstant, -1150, 620)
    one.set_editor_property('r', 1.0)
    mx = node(unreal.MaterialExpressionMax, -1000, 540)
    assert mel.connect_material_expressions(eyek, '', mx, 'A') and mel.connect_material_expressions(one, '', mx, 'B')
    ex = node(unreal.MaterialExpressionConstant, -1000, 640)
    ex.set_editor_property('r', 0.85)
    day = node(unreal.MaterialExpressionPower, -850, 560)
    assert mel.connect_material_expressions(mx, '', day, 'Base') and mel.connect_material_expressions(ex, '', day, 'Exp')
    time = node(unreal.MaterialExpressionTime, -850, 680)
    names = ['P', 'LedPos', 'LedStep', 'LedColor', 'LedR', 'LedPower', 'T', 'Day']
    em = custom_node(m, 'DroneEmissive', hlsl('drone_emissive.hlsl'), F3, names, -400, 200)
    hook(em, {'P': pos, 'LedPos': P_['LedPos'], 'LedStep': P_['LedStep'], 'LedColor': P_['LedColor'], 'LedR': P_['LedR'], 'LedPower': P_['LedPower'], 'T': time, 'Day': day})
    Prop = unreal.MaterialProperty
    assert mel.connect_material_property(col, '', Prop.MP_BASE_COLOR)
    assert mel.connect_material_property(mask('r', -420), '', Prop.MP_ROUGHNESS)
    assert mel.connect_material_property(mask('g', -300), '', Prop.MP_METALLIC)
    assert mel.connect_material_property(to_world, '', Prop.MP_NORMAL)
    assert mel.connect_material_property(em, '', Prop.MP_EMISSIVE_COLOR)
    # 用途：静态网格不用勾；实例化勾上不亏；Nanite 不勾（见文件头）
    mel.set_material_usage(m, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    mel.recompile_material(m)
    eas.save_asset(path, only_if_is_dirty=False)
    log('建了母材质 ' + path)
    return m


def update_master():
    """已经有了：只换节点代码（母材质被 C++ 启动时加载的模型引用着，清空节点重建可能崩）"""
    m = eas.load_asset(f'{MAT_DIR}/{MASTER}')
    done = 0
    todo = [mel.get_material_property_input_node(m, p) for p in (unreal.MaterialProperty.MP_BASE_COLOR, unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_METALLIC,
                                                                 unreal.MaterialProperty.MP_NORMAL, unreal.MaterialProperty.MP_EMISSIVE_COLOR)]
    seen = set()
    while todo:
        e = todo.pop()
        if not e or e.get_name() in seen:
            continue
        seen.add(e.get_name())
        if isinstance(e, unreal.MaterialExpressionCustom):
            desc = str(e.get_editor_property('description'))
            if desc in CUSTOMS:
                e.set_editor_property('code', hlsl(CUSTOMS[desc]))
                done += 1
        todo += mel.get_inputs_for_material_expression(m, e)
    assert done == len(CUSTOMS), f'只找到 {done} 个自定义节点'
    mel.recompile_material(m)
    eas.save_asset(f'{MAT_DIR}/{MASTER}', only_if_is_dirty=False)
    log('更新了母材质的节点代码')
    return m


master = update_master() if eas.does_asset_exist(f'{MAT_DIR}/{MASTER}') else make_master()


# ---------------------------------------------------------------- 实例
def instance(name, scalars=None, vectors=None):
    path = f'{MAT_DIR}/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    eas.save_asset(path, only_if_is_dirty=False)
    return mi


def lc(*v):
    return unreal.LinearColor(*v, *([0.0] * (4 - len(v))))


# 深色机械件（支架、绞盘）：不跟涂装，发黑的钢
frame = instance('MI_Drone_Frame', {'Metallic': 0.9, 'Rough': 0.42, 'RoughVar': 0.25, 'ChipAmt': 0.35, 'WearAmt': 0.8, 'SeamAmt': 0.0, 'PaintGain': 1.0, 'EdgeLight': 2.2, 'NormalAmt': 0.9},
                 {'BaseColor': lc(0.085, 0.09, 0.098, 1)})
nozzle = instance('MI_Drone_Nozzle', {'Metallic': 1.0, 'Rough': 0.58, 'RoughVar': 0.3, 'ChipAmt': 0.15, 'WearAmt': 0.5, 'SeamAmt': 0.0, 'GrimeAmt': 1.0}, {'BaseColor': lc(0.03, 0.03, 0.032, 1)})
claw = instance('MI_Drone_Claw', {'Metallic': 1.0, 'Rough': 0.36, 'RoughVar': 0.25, 'ChipAmt': 0.0, 'WearAmt': 0.0, 'SeamAmt': 0.0, 'EdgeLight': 1.0}, {'BaseColor': lc(0.33, 0.35, 0.37, 1)})


def dim(c, k):
    return unreal.LinearColor(c.r * k, c.g * k, c.b * k, 1.0)


# 厂商色件：压暗一点、带点金属，不要塑料块（色条 / 指示灯还用原色）
accents = {b: instance(f'MI_Drone_Accent_{b}', {'UseBrand': 1.0, 'Metallic': 0.3, 'Rough': 0.44, 'RoughVar': 0.3, 'GrimeAmt': 0.9, 'ChipAmt': 0.8, 'WearAmt': 1.0, 'SeamAmt': 0.0, 'EdgeLight': 0.5},
                       {'BrandColor': dim(c, 0.72)}) for b, c in BRAND.items()}
# 捕获网筒口四个配重：模型里是 Accent 槽，整块上色太抢眼；做成深色金属 + 一圈相位青。PaintGain=0 甩掉 ApplyPaint 覆盖的 BaseColor，底色取一成厂商色
net_weight = instance('MI_Drone_Net_Weight', {'UseBrand': 0.1, 'PaintGain': 0.0, 'Metallic': 0.85, 'Rough': 0.4, 'ChipAmt': 0.5, 'WearAmt': 1.0, 'SeamAmt': 0.0, 'EdgeLight': 1.5, 'BandW': 1.1},
                      {'BrandColor': BRAND['PHASE'], 'Band': lc(30.5, 0, 0)})
# 发光槽还是队伍色（ApplyTeamColor 按槽名认），只把亮度压下来：机体那份 20 倍在照片棚里是一块发白的板
glow_path = f'{MAT_DIR}/MI_Drone_Glow'
glow = eas.load_asset(glow_path) if eas.does_asset_exist(glow_path) else tools.create_asset('MI_Drone_Glow', MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
glow.set_editor_property('parent', eas.load_asset('/Game/Robot/Materials/M_RobotMaster'))
mel.set_material_instance_scalar_parameter_value(glow, 'EmissiveStrength', 7.0)
mel.set_material_instance_vector_parameter_value(glow, 'BaseColor', lc(0, 0, 0, 1))
mel.set_material_instance_vector_parameter_value(glow, 'Emissive', unreal.LinearColor(0.15, 0.75, 1.0, 1.0))
mel.update_material_instance(glow)
eas.save_asset(glow_path, only_if_is_dirty=False)


# ---------------------------------------------------------------- 模型表：厂商、接缝、指示灯位置（x 占长度的比例，取那一段里最高的顶点）、厂商色条
# band：(轴方向 × 位置（厘米，向量长度就是位置，要负的把方向反过来）, 宽度 厘米)；只给没有 Accent 槽的护盾、EMP，其余靠模型自带的识别条
DRONES = {
    # led：(x 比例下限, 上限) = 取那一段靠中线最高的顶点；或 dict(at=(x, y, z)) 直接给坐标（平板上没有顶点）。Y 取绝对值，两侧对称各一组
    'SM_MissilePod': dict(tag='Missile', brand='VHI', led=dict(at=(17.0, 9.0, 13.0)), seam={'SeamStepX': 17.0, 'SeamStepZ': 14.0, 'SeamOfs': 6.0}),
    'SM_Artillery': dict(tag='Artillery', brand='NEC', led=(0.62, 0.82), seam={'SeamStepX': 15.0, 'SeamStepZ': 60.0, 'SeamOfs': 3.0, 'SeamAmt': 0.6}),
    'SM_ShieldEmitter': dict(tag='Shield', brand='NEC', led=(0.45, 0.7), seam={'SeamAmt': 0.0}, band=(lc(0, 0, 9.0), 1.1)),
    'SM_GrappleLauncher': dict(tag='Grapple', brand='NEC', led=(0.32, 0.47), seam={'SeamStepX': 13.0, 'SeamStepZ': 90.0, 'SeamOfs': 2.0, 'SeamAmt': 0.7}),
    'SM_NetLauncher': dict(tag='Net', brand='PHASE', led=(0.25, 0.45), seam={'SeamStepX': 14.0, 'SeamStepZ': 90.0, 'SeamOfs': 5.0, 'SeamAmt': 0.7}),
    'SM_SmokeLauncher': dict(tag='Smoke', brand='VHI', led=(0.02, 0.2), seam={'SeamStepX': 11.0, 'SeamStepZ': 90.0, 'SeamOfs': 1.0, 'SeamAmt': 0.7}),
    'SM_EmpLauncher': dict(tag='Emp', brand='PHASE', led=(0.05, 0.3), seam={'SeamStepX': 11.0, 'SeamStepZ': 90.0, 'SeamOfs': 4.0, 'SeamAmt': 0.7}, band=(lc(8.0, 0, 0), 0.8)),
    # 11-01 并入肩部的四件（lookdev/make_shoulder_tactical.py）：near = 离这个坐标最近的顶点放指示灯
    # 紧急脱离的色条垂直推进器轴（推进器翘 58 度），方向 × 长度：(cos58, 0, sin58) × 11 厘米
    'SM_BurstPack': dict(tag='Burst', brand='PHASE', led=dict(near=(0.0, 9.0, 14.0)), seam={'SeamAmt': 0.0}, band=(lc(5.8, 0, 9.3), 1.3)),
    'SM_LaunchSpring': dict(tag='Launch', brand='VHI', led=dict(near=(0.0, 19.0, 24.0)), seam={'SeamAmt': 0.0}),
    'SM_ShockRing': dict(tag='Shock', brand='VHI', led=dict(near=(0.0, 25.0, 5.0)), seam={'SeamAmt': 0.0}),
    'SM_MineLayer': dict(tag='MineLayer', brand='NEC', led=dict(near=(-12.0, 15.0, 10.0)), seam={'SeamStepX': 15.0, 'SeamStepZ': 90.0, 'SeamOfs': 2.0, 'SeamAmt': 0.5}),
    # 地上的雷：发光槽要亮（代码按队伍色闪），用机体那份，不用压暗的 MI_Drone_Glow
    'SM_Mine': dict(tag='Mine', brand='NEC', led=None, seam={'SeamAmt': 0.0}, glow='robot'),
}
# 炮弹、弹罐、导弹、网：同一个 bug，改回导入脚本本来想挂的机体材质（爪子用磨亮的钢）
OTHERS = ['SM_Missile', 'SM_Shell', 'SM_Grenade', 'SM_NetOpen', 'SM_NetWrapped']


def top_point(mesh, lo, hi):
    """长度方向 lo..hi 这一段、靠中线的顶点里最高的那个（放指示灯）"""
    desc = mesh.get_static_mesh_description(0)
    n = desc.get_vertex_count()
    vs = [desc.get_vertex_position(unreal.VertexID(i)) for i in range(n)]
    x0, x1 = min(v.x for v in vs), max(v.x for v in vs)
    y0, y1 = min(v.y for v in vs), max(v.y for v in vs)
    ymid = (y0 + y1) / 2
    near = [v for v in vs if x0 + (x1 - x0) * lo <= v.x <= x0 + (x1 - x0) * hi and abs(v.y - ymid) < (y1 - y0) * 0.3] or vs
    return max(near, key=lambda v: v.z)


def led_point(mesh, led):
    """指示灯位置：None 不要灯；(下限, 上限) 见 top_point；dict(at=...) 直接给；dict(near=...) 离它最近的顶点"""
    if not led:
        return unreal.Vector(0, 0, 0)
    if not isinstance(led, dict):
        return top_point(mesh, *led)
    if 'at' in led:
        return unreal.Vector(*led['at'])
    desc = mesh.get_static_mesh_description(0)
    t = unreal.Vector(*led['near'])
    vs = [desc.get_vertex_position(unreal.VertexID(i)) for i in range(desc.get_vertex_count())]
    return min(vs, key=lambda v: (v.x - t.x) ** 2 + (v.y - t.y) ** 2 + (v.z - t.z) ** 2)


def set_slots(mesh, mapping):
    """按索引写回：不能 for s in sm 改元素（那是拷贝）"""
    for i, s in enumerate(mesh.get_editor_property('static_materials')):
        slot = str(s.get_editor_property('material_slot_name'))
        mi = mapping(slot)
        if not mi:
            unreal.log_warning(f'[dronemat] {mesh.get_name()} 槽 {slot} 没有材质')
            continue
        mesh.set_material(i, mi)
    ns = mesh.get_editor_property('nanite_settings')
    assert not ns.get_editor_property('enabled'), f'{mesh.get_name()} 开着 Nanite，M_DronePBR 没勾 Nanite 用途'


def robot(slot):
    p = f'/Game/Robot/Materials/{slot}'
    return eas.load_asset(p) if eas.does_asset_exist(p) else None


def slots_text(mesh):
    return [str(s.get_editor_property('material_slot_name')) + '=' + s.get_editor_property('material_interface').get_name() for s in mesh.get_editor_property('static_materials')]


for name, d in DRONES.items():
    pkg = f'{MESH_DIR}/{name}'
    backup(pkg)
    mesh = eas.load_asset(pkg)
    brand = d['brand']
    p = led_point(mesh, d['led'])
    # 涂漆金属：金属度高一点才不像塑料，金属度高底色要提亮
    scal = {'Metallic': 0.7, 'Rough': 0.4, 'RoughVar': 0.3, 'PaintGain': 1.8, 'GrimeAmt': 0.85, 'NormalAmt': 1.0, 'ChipAmt': 0.5, 'LedPower': 7.0 if d['led'] else 0.0, 'LedR': 0.8, 'BandW': 0.0, **d['seam']}
    vec = {'LedPos': lc(p.x, abs(p.y), p.z, 0), 'LedStep': lc(-2.3, 0, 0, 0), 'LedColor': BRAND[brand], 'BrandColor': BRAND[brand]}
    if 'band' in d:
        vec['Band'] = d['band'][0]
        scal['BandW'] = d['band'][1]
    paint = instance(f'MI_Drone_{d["tag"]}_Paint', scal, vec)
    table = {'MI_Robot_PaintDark': paint, 'MI_Robot_Frame': frame, 'MI_Robot_Nozzle': nozzle, 'MI_Robot_Accent': net_weight if d['tag'] == 'Net' else accents[brand], 'MI_Robot_Glow': robot('MI_Robot_Glow') if d.get('glow') == 'robot' else glow}
    set_slots(mesh, lambda slot: table.get(slot))
    eas.save_asset(pkg, only_if_is_dirty=False)
    b = mesh.get_bounding_box()
    log(f'{name}（{brand}）范围 ({b.min.x:.1f}, {b.min.y:.1f}, {b.min.z:.1f})..({b.max.x:.1f}, {b.max.y:.1f}, {b.max.z:.1f}) 指示灯 ({p.x:.1f}, {p.y:.1f}, {p.z:.1f})，槽 {slots_text(mesh)}')

pkg = f'{MESH_DIR}/SM_GrappleClaw'
backup(pkg)
mesh = eas.load_asset(pkg)
set_slots(mesh, lambda slot: claw)
eas.save_asset(pkg, only_if_is_dirty=False)

for name in OTHERS:
    pkg = f'{MESH_DIR}/{name}'
    backup(pkg)
    mesh = eas.load_asset(pkg)
    set_slots(mesh, robot)
    eas.save_asset(pkg, only_if_is_dirty=False)
    log(f'{name} 槽 {slots_text(mesh)}')

# 整个武器目录再扫一遍：还有没有指着默认材质的槽
for p in eas.list_assets(MESH_DIR, recursive=False):
    a = eas.load_asset(p)
    if isinstance(a, unreal.StaticMesh):
        bad = [str(s.get_editor_property('material_slot_name')) for s in a.get_editor_property('static_materials') if not s.get_editor_property('material_interface') or s.get_editor_property('material_interface').get_name() == 'WorldGridMaterial']
        if bad:
            unreal.log_warning(f'[dronemat] {a.get_name()} 还是默认材质：{bad}')
log('完成')
open('D:/RobotSlice/Saved/drone_materials.txt', 'w', encoding='utf-8').write(chr(10).join(REPORT))
