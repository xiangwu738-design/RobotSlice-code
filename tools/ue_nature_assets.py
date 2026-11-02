"""自然图细化用的新资产（只新建，不动共用资产）：
- 贴图 SourceAssets/nature_tex → /Game/Env/Nature/Textures（苔藓 / 墙面污渍）
- 母材质 /Game/Env/Nature/Materials/M_RuinNature：按 UV 的高光 / 光泽度贴图（和 M_KitSG 同参数）+ 世界坐标的风化层
  （墙面竖向污渍、楼脚泥土、朝上面长苔藓、大尺度明暗起伏），废墟每个材质一个实例 MI_RN_*，贴图沿用 /Game/Env/Ruins 的
- 音效 SourceAssets/nature_audio/*.wav → /Game/Audio/Nature/S_Nat_*（全部循环）+ 衰减 SA_Nat_Small / Mid / Large
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="tools/ue_nature_assets.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
SRC = 'D:/RobotSlice/SourceAssets'
TEX = '/Game/Env/Nature/Textures'
MAT = '/Game/Env/Nature/Materials'
RUIN_MI = '/Game/Env/Ruins/Materials'
AUD = '/Game/Audio/Nature'
log = lambda *a: unreal.log('[nature-assets] ' + ' '.join(str(x) for x in a))


def task(filename, dest, name):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t



def fresh_material(path):
    """按脚本重建材质：先把引用它的实例 / 网格解开（之后脚本会重新挂上），删掉旧资产再新建。
    （delete_all_material_expressions 在 5.8 里删不干净，残留的同名参数节点会打架）"""
    folder, name = path.rsplit('/', 1)
    if eas.does_asset_exist(path):
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        opt = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
        for ref in ar.get_referencers(path, opt) or []:
            for ap in eas.list_assets(str(ref), recursive=False, include_folder=False) or [str(ref)]:
                obj = eas.load_asset(ap)
                if isinstance(obj, unreal.MaterialInstanceConstant):
                    obj.set_editor_property('parent', None)
                    eas.save_loaded_asset(obj)
                elif isinstance(obj, unreal.StaticMesh):
                    sm = obj.get_editor_property('static_materials')
                    for slot in sm:
                        slot.set_editor_property('material_interface', None)
                    obj.set_editor_property('static_materials', sm)
                    eas.save_loaded_asset(obj)
                else:
                    log('还有别的引用，不删', ap)
                    mat = eas.load_asset(path)
                    mel.delete_all_material_expressions(mat)
                    return mat
        if not eas.delete_asset(path):
            log('删不掉旧材质', path)
    mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    log('新建材质', path, '节点', mel.get_num_material_expressions(mat))
    return mat


# ---------------------------------------------------------------- 贴图
TEXTURES = {
    'T_Nat_Moss_C': ('nature_tex/polyhaven/aerial_grass_rock/aerial_grass_rock_diff_1k.jpg', 1024),
    'T_Nat_WallStain_C': ('nature_tex/poliigon/混凝土墙001/ConcreteWall001_COL_VAR1_HIRES.jpg', 2048),
    'T_Nat_WallRust_C': ('nature_tex/poliigon/混凝土墙007/ConcreteWall007_COL_VAR1_HIRES.jpg', 2048),
}
tools.import_asset_tasks([task(f'{SRC}/{f}', TEX, n) for n, (f, _) in TEXTURES.items()])
for n, (_, size) in TEXTURES.items():
    t = eas.load_asset(f'{TEX}/{n}')
    t.set_editor_property('max_texture_size', size)
    eas.save_loaded_asset(t)
log('贴图', list(TEXTURES))

# ---------------------------------------------------------------- 母材质
MASTER = f'{MAT}/M_RuinNature'
m = fresh_material(MASTER)   # 每次按脚本重建
m.set_editor_property('used_with_nanite', True)
m.set_editor_property('used_with_instanced_static_meshes', True)
P = unreal.MaterialProperty
S = unreal.MaterialSamplerType
E = unreal


def node(cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, ao, b, bi):
    if not mel.connect_material_expressions(a, ao, b, bi):
        raise RuntimeError(f'连不上 {a.get_name()}.{ao} -> {b.get_name()}.{bi}')


def scalar(name, v, x, y):
    return node(E.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def vec(name, c, x, y):
    return node(E.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*c, 1))


def op(cls, a, b, x, y, ao='', bo=''):
    n = node(cls, x, y)
    if isinstance(a, (int, float)):
        n.set_editor_property('const_a', a)
    else:
        link(a, ao, n, 'A')
    if isinstance(b, (int, float)):
        n.set_editor_property('const_b', b)
    else:
        link(b, bo, n, 'B')
    return n


def mask(a, chans, x, y, ao=''):
    n = node(E.MaterialExpressionComponentMask, x, y, **{c: (c in chans) for c in 'rgba'})
    link(a, ao, n, '')
    return n


def one(cls, a, x, y, ao=''):
    n = node(cls, x, y)
    link(a, ao, n, '')
    return n


def lerp(a, b, alpha, x, y):
    n = node(E.MaterialExpressionLinearInterpolate, x, y)
    for v, pin in ((a, 'A'), (b, 'B'), (alpha, 'Alpha')):
        if isinstance(v, (int, float)):
            n.set_editor_property('const_' + pin.lower(), v)
        else:
            link(v, '', n, pin)
    return n


def tex(name, default, sampler, x, y, uv=None):
    n = node(E.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name, texture=eas.load_asset(default), sampler_type=sampler)
    if uv is not None:
        link(uv, '', n, 'UVs')
    return n


FLAT = '/Game/Env/Textures/T_FlatNormal'
WHITE = '/Engine/EngineMaterials/DefaultDiffuse'
# 原贴图（按 UV）
diff = tex('Diffuse', WHITE, S.SAMPLERTYPE_COLOR, -2200, -800)
spec = tex('Spec', WHITE, S.SAMPLERTYPE_COLOR, -2200, -500)
gloss = tex('Gloss', WHITE, S.SAMPLERTYPE_COLOR, -2200, -250)
nor = tex('Normal', FLAT, S.SAMPLERTYPE_NORMAL, -2200, 0)
tint = vec('Tint', (1, 1, 1), -2200, -1000)
base = op(E.MaterialExpressionMultiply, diff, tint, -1900, -850, 'RGB')
rough = one(E.MaterialExpressionOneMinus, gloss, -1900, -250, 'R')

# 世界坐标
wp = node(E.MaterialExpressionWorldPosition, -3200, 400)
nws = node(E.MaterialExpressionVertexNormalWS, -3200, 700)
opos = node(E.MaterialExpressionObjectPositionWS, -3200, 1000)
wx = mask(wp, 'r', -3000, 300)
wy = mask(wp, 'g', -3000, 400)
wz = mask(wp, 'b', -3000, 500)
nx = one(E.MaterialExpressionAbs, mask(nws, 'r', -3000, 650), -2850, 650)
ny = one(E.MaterialExpressionAbs, mask(nws, 'g', -3000, 750), -2850, 750)
nz = mask(nws, 'b', -3000, 850)

# 1) 墙面竖向污渍：沿 X / Y 两个方向投影，按法线混合；只取亮度变化（相对平均亮度的比例）乘到底色
gw = scalar('GrimeWidth', 2400.0, -3000, 1200)     # 一张污渍图在墙上铺多宽（厘米）
gh = scalar('GrimeHeight', 600.0, -3000, 1300)     # 多高（图是 4:1）
negz = op(E.MaterialExpressionMultiply, wz, -1.0, -2800, 500)
v = op(E.MaterialExpressionDivide, negz, gh, -2650, 500)
ua = op(E.MaterialExpressionDivide, wy, gw, -2650, 400)
ub = op(E.MaterialExpressionDivide, wx, gw, -2650, 300)
uva = op(E.MaterialExpressionAppendVector, ua, v, -2500, 420)
uvb = op(E.MaterialExpressionAppendVector, ub, v, -2500, 320)
grime_obj = node(E.MaterialExpressionTextureObjectParameter, -2600, 150, parameter_name='GrimeTexObj', texture=eas.load_asset(f'{TEX}/T_Nat_WallStain_C'))
ga = node(E.MaterialExpressionTextureSample, -2300, 300)
gb = node(E.MaterialExpressionTextureSample, -2300, 550)
link(uva, '', ga, 'UVs')
link(uvb, '', gb, 'UVs')
link(grime_obj, '', ga, 'Tex')
link(grime_obj, '', gb, 'Tex')
nsum = op(E.MaterialExpressionAdd, op(E.MaterialExpressionAdd, nx, ny, -2700, 700), 0.001, -2550, 700)
wa = op(E.MaterialExpressionDivide, nx, nsum, -2400, 700)      # 法线朝 X → 用 YZ 投影（A）
grime = lerp(gb, ga, wa, -2100, 450)
gmean = scalar('GrimeMean', 0.33, -2100, 650)
gnorm = op(E.MaterialExpressionDivide, grime, gmean, -1950, 450)
gamt = scalar('GrimeAmount', 0.7, -1950, 650)
gmul = lerp(1.0, gnorm, gamt, -1800, 450)
gmul_c = node(E.MaterialExpressionClamp, -1650, 450, min_default=0.25, max_default=1.35)
link(gmul, '', gmul_c, '')
c1 = op(E.MaterialExpressionMultiply, base, gmul_c, -1500, -600)

# 2) 大尺度明暗起伏（每栋楼、每段墙不一样）：苔藓图按 90 米铺开取亮度
macro_uv = op(E.MaterialExpressionDivide, mask(wp, 'rg', -2800, 1500), scalar('MacroScale', 9000.0, -2800, 1600), -2600, 1500)
moss_obj = node(E.MaterialExpressionTextureObjectParameter, -2600, 1750, parameter_name='MossTex', texture=eas.load_asset(f'{TEX}/T_Nat_Moss_C'))
macro_s = node(E.MaterialExpressionTextureSample, -2400, 1500)
link(macro_uv, '', macro_s, 'UVs')
link(moss_obj, '', macro_s, 'Tex')
lum = node(E.MaterialExpressionDotProduct, -2200, 1500)
link(macro_s, 'RGB', lum, 'A')
link(node(E.MaterialExpressionConstant3Vector, -2400, 1650, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1)), '', lum, 'B')
macro = lerp(1.0, op(E.MaterialExpressionDivide, lum, 0.126, -2050, 1500), scalar('MacroAmount', 0.35, -2050, 1650), -1900, 1500)
c2 = op(E.MaterialExpressionMultiply, c1, macro, -1350, -600)

# 3) 楼脚泥土：离物体原点（楼底）DirtHeight 以内渐变到泥色
oz = mask(opos, 'b', -3000, 1000)
h = op(E.MaterialExpressionSubtract, wz, oz, -2800, 1000)
hn = op(E.MaterialExpressionDivide, h, scalar('DirtHeight', 900.0, -2800, 1100), -2650, 1000)
dirt = one(E.MaterialExpressionOneMinus, one(E.MaterialExpressionSaturate, hn, -2500, 1000), -2350, 1000)
dirt2 = op(E.MaterialExpressionMultiply, op(E.MaterialExpressionMultiply, dirt, dirt, -2200, 1000), scalar('DirtAmount', 0.75, -2200, 1100), -2050, 1000)
dirt_col = op(E.MaterialExpressionMultiply, c2, vec('DirtColor', (0.42, 0.36, 0.27), -1500, 1100), -1350, 1000)
c3 = lerp(c2, dirt_col, dirt2, -1200, -600)

# 4) 朝上的面长苔藓（平台、窗台、断口顶面），用苔藓图俯视投影 + 低频起伏做斑块
moss_uv = op(E.MaterialExpressionDivide, mask(wp, 'rg', -2800, 1900), scalar('MossScale', 400.0, -2800, 2000), -2600, 1900)
moss_s = node(E.MaterialExpressionTextureSample, -2400, 1900)
link(moss_uv, '', moss_s, 'UVs')
link(moss_obj, '', moss_s, 'Tex')
up = one(E.MaterialExpressionSaturate, op(E.MaterialExpressionMultiply, op(E.MaterialExpressionSubtract, nz, 0.55, -2700, 850), 5.0, -2550, 850), -2400, 850)
patch = one(E.MaterialExpressionSaturate, op(E.MaterialExpressionMultiply, op(E.MaterialExpressionSubtract, lum, 0.105, -2050, 1300), 25.0, -1900, 1300), -1750, 1300)
mmask = op(E.MaterialExpressionMultiply, op(E.MaterialExpressionMultiply, up, patch, -1600, 850), scalar('MossAmount', 0.85, -1600, 950), -1450, 850)
moss_col = op(E.MaterialExpressionMultiply, moss_s, vec('MossTint', (0.85, 1.05, 0.9), -2200, 2050), -2050, 1900, 'RGB')
c4 = lerp(c3, moss_col, mmask, -1050, -600)
# 5) 去饱和：远景废墟别太花（集装箱、红锈板），和大气里的山、树同一个色调
desat = node(E.MaterialExpressionDesaturation, -900, -600)
link(c4, '', desat, '')
link(scalar('Desaturate', 0.25, -1050, -700), '', desat, 'Fraction')
rough2 = lerp(rough, 0.95, mmask, -1050, -250)
spec2 = lerp(mask(spec, 'r', -1300, -450, 'RGB'), 0.2, mmask, -1050, -450)
metal = scalar('Metallic', 0.0, -1050, -100)

mel.connect_material_property(desat, '', P.MP_BASE_COLOR)
mel.connect_material_property(spec2, '', P.MP_SPECULAR)
mel.connect_material_property(rough2, '', P.MP_ROUGHNESS)
mel.connect_material_property(metal, '', P.MP_METALLIC)
mel.connect_material_property(nor, 'RGB', P.MP_NORMAL)
mel.recompile_material(m)
eas.save_loaded_asset(m)
log('母材质', MASTER, '节点', mel.get_num_material_expressions(m))

# ---------------------------------------------------------------- 废墟材质实例：贴图参数抄 /Game/Env/Ruins 的，风化强度按材质类型
made = []
for path in eas.list_assets(RUIN_MI, recursive=False, include_folder=False):
    src = eas.load_asset(path)
    if not isinstance(src, unreal.MaterialInstanceConstant):
        continue
    key = src.get_name()[3:]          # MI_concreteBlocks → concreteBlocks
    name = f'MI_RN_{key}'
    dst_path = f'{MAT}/{name}'
    mi = eas.load_asset(dst_path) if eas.does_asset_exist(dst_path) else tools.create_asset(
        name, MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', m)
    for p in src.get_editor_property('texture_parameter_values'):
        mel.set_material_instance_texture_parameter_value(mi, str(p.parameter_info.name), p.parameter_value)
    for p in src.get_editor_property('scalar_parameter_values'):
        mel.set_material_instance_scalar_parameter_value(mi, str(p.parameter_info.name), p.parameter_value)
    k = key.lower()
    is_glass = k.startswith('windows')
    is_metal = 'metal' in k or 'container' in k or 'ship' in k or 'plane' in k or 'cans' in k
    if is_metal:
        mel.set_material_instance_texture_parameter_value(mi, 'GrimeTexObj', eas.load_asset(f'{TEX}/T_Nat_WallRust_C'))
    mel.set_material_instance_scalar_parameter_value(mi, 'GrimeAmount', 0.25 if is_glass else 0.6 if is_metal else 0.75)
    mel.set_material_instance_scalar_parameter_value(mi, 'MossAmount', 0.0 if is_glass else 0.6 if is_metal else 0.9)
    mel.set_material_instance_scalar_parameter_value(mi, 'DirtAmount', 0.5 if is_glass else 0.75)
    if is_metal:
        mel.set_material_instance_scalar_parameter_value(mi, 'Metallic', 0.3)
    if 'container' in k or 'metalplate' in k or k == 'metalrust':   # 彩色集装箱、红锈板远看太跳
        mel.set_material_instance_scalar_parameter_value(mi, 'Desaturate', 0.5)
        mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(0.8, 0.8, 0.8, 1))
    if is_glass:   # 残楼的窗应该是黑洞洞的，原贴图是亮玻璃
        mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(0.22, 0.23, 0.25, 1))
    eas.save_loaded_asset(mi)
    made.append(name)
log('废墟材质实例', len(made), made)

# ---------------------------------------------------------------- 音效
wavs = sorted(f for f in os.listdir(f'{SRC}/nature_audio') if f.endswith('.wav'))
tools.import_asset_tasks([task(f'{SRC}/nature_audio/{f}', AUD, 'S_Nat_' + f[:-4]) for f in wavs])
for f in wavs:
    s = eas.load_asset(f'{AUD}/S_Nat_{f[:-4]}')
    s.set_editor_property('looping', True)
    eas.save_loaded_asset(s)
# 衰减：内半径内满音量，往外自然衰减到 0；远处滤掉高频（空气吸收）
ATT = {'SA_Nat_Small': (1200.0, 5000.0), 'SA_Nat_Mid': (2500.0, 7000.0), 'SA_Nat_Large': (5000.0, 14000.0)}
for name, (inner, falloff) in ATT.items():
    path = f'{AUD}/{name}'
    att = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(name, AUD, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    st = att.get_editor_property('attenuation')
    st.set_editor_property('attenuation_shape', unreal.AttenuationShape.SPHERE)
    st.set_editor_property('attenuation_shape_extents', unreal.Vector(inner, 0.0, 0.0))
    st.set_editor_property('falloff_distance', falloff)
    st.set_editor_property('distance_algorithm', unreal.AttenuationDistanceModel.NATURAL_SOUND)
    try:   # 空气吸收：越远高频越少
        st.set_editor_property('attenuate_with_lpf', True)
        st.set_editor_property('lpf_radius_min', inner)
        st.set_editor_property('lpf_radius_max', inner + falloff)
        st.set_editor_property('lpf_frequency_at_min', 20000.0)
        st.set_editor_property('lpf_frequency_at_max', 2500.0)
    except Exception as e:
        log('低通设置不了', e)
    try:   # 走进声源中间时不硬切左右
        st.set_editor_property('non_spatialized_radius_start', inner * 0.3)
        st.set_editor_property('non_spatialized_radius_end', inner * 0.1)
    except Exception as e:
        log('非定位半径设置不了', e)
    att.set_editor_property('attenuation', st)
    eas.save_loaded_asset(att)
log('音效', len(wavs), '衰减', list(ATT))

# ---------------------------------------------------------------- Bob Ross 场景里的摄影测量巨石（Nanite，复杂碰撞，8K 贴图压到 4K）
PROPS = '/Game/Env/Nature/Props'
BR = f'{SRC}/bobross'
if os.path.exists(f'{BR}/Rock.fbx'):
    tools.import_asset_tasks([task(f'{BR}/textures/texture_1001_png_002.png', TEX, 'T_Nat_ScanBoulder_C')])
    t = eas.load_asset(f'{TEX}/T_Nat_ScanBoulder_C')
    t.set_editor_property('max_texture_size', 4096)
    eas.save_loaded_asset(t)
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', True)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('generate_lightmap_u_vs', False)
    t = task(f'{BR}/Rock.fbx', PROPS, 'SM_Nat_ScanBoulder')
    t.options = ui
    tools.import_asset_tasks([t])
    # 扫描石头的材质：只有一张颜色图；粗糙度按亮度（亮的地衣更糙）
    path = f'{MAT}/M_Nat_Scan'
    m = fresh_material(path)
    m.set_editor_property('used_with_nanite', True)
    m.set_editor_property('used_with_instanced_static_meshes', True)
    col = node(E.MaterialExpressionTextureSampleParameter2D, -800, 0, parameter_name='Color',
               texture=eas.load_asset(f'{TEX}/T_Nat_ScanBoulder_C'), sampler_type=S.SAMPLERTYPE_COLOR)
    tintc = node(E.MaterialExpressionVectorParameter, -800, -200, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    bc = op(E.MaterialExpressionMultiply, col, tintc, -500, -100, 'RGB')
    lumr = node(E.MaterialExpressionDotProduct, -500, 150)
    link(col, 'RGB', lumr, 'A')
    link(node(E.MaterialExpressionConstant3Vector, -800, 250, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1)), '', lumr, 'B')
    rr = lerp(0.95, 0.7, lumr, -300, 150)
    mel.connect_material_property(bc, '', P.MP_BASE_COLOR)
    mel.connect_material_property(rr, '', P.MP_ROUGHNESS)
    spc = node(E.MaterialExpressionConstant, -300, 300, r=0.35)
    mel.connect_material_property(spc, '', P.MP_SPECULAR)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    # 实例：稍压暗（扫描原色偏亮），保持原来的冷灰地衣色
    mip = f'{MAT}/MI_Nat_ScanBoulder'
    mi = eas.load_asset(mip) if eas.does_asset_exist(mip) else tools.create_asset(
        'MI_Nat_ScanBoulder', MAT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', m)
    mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(0.82, 0.82, 0.8, 1))
    eas.save_loaded_asset(mi)
    mesh = eas.load_asset(f'{PROPS}/SM_Nat_ScanBoulder')
    nan = mesh.get_editor_property('nanite_settings')
    nan.enabled = True
    mesh.set_editor_property('nanite_settings', nan)
    bs = mesh.get_editor_property('body_setup')
    if bs:
        bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm = mesh.get_editor_property('static_materials')
    for s in sm:
        s.set_editor_property('material_interface', m)
    mesh.set_editor_property('static_materials', sm)
    eas.save_loaded_asset(mesh)
    b = mesh.get_bounding_box()
    log('扫描巨石', mesh.get_path_name(), '尺寸(米)', [round(v / 100, 2) for v in ((b.max - b.min).x, (b.max - b.min).y, (b.max - b.min).z)])
