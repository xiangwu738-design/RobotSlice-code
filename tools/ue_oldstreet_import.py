"""老旧街道（L_OldStreet）资源导入 → /Game/Env/OldStreet。可重复跑。先跑 tools/oldstreet_layout.py、oldstreet_textures.py、blender_oldstreet_export.py。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_oldstreet_import.py [tex] [mat] [mesh]" -AllowCommandletRendering -RenderOffscreen -unattended -nosplash
不带参数 = 全做。环境变量 OS_ONLY=SM_OS_Tree,SM_OS_Props 只重导这几个网格。
树叶贴图 T_OS_Leaf_D / _N 是 K059 包树叶图集裁的（原包的叶子贴图是丢的）。
"""
import glob
import json
import os
import sys

import unreal

SRC = 'D:/RobotSlice/Inbox/_oldstreet/export'
DEST = '/Game/Env/OldStreet'
L = json.load(open('D:/RobotSlice/Inbox/_oldstreet/layout.json', encoding='utf-8'))
STEPS = [a for a in sys.argv[1:] if a in ('tex', 'mat', 'mesh')] or ['tex', 'mat', 'mesh']
ONLY = [s for s in os.environ.get('OS_ONLY', '').split(',') if s]
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
ST = unreal.MaterialSamplerType
log = lambda *a: unreal.log('[oldstreet] ' + ' '.join(str(x) for x in a))
WHITE = '/Engine/EngineResources/WhiteSquareTexture'
FLAT_N = '/Engine/EngineMaterials/DefaultNormal'
LEAF_D = f'{DEST}/Textures/T_OS_Leaf_D'
LEAF_N = f'{DEST}/Textures/T_OS_Leaf_N'


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
    tasks = [task(f.replace('\\', '/'), f'{DEST}/Textures') for f in sorted(glob.glob(f'{SRC}/tex/*.png'))]
    tools.import_asset_tasks(tasks)
    for t in tasks:
        name = os.path.splitext(os.path.basename(t.filename))[0]
        a = eas.load_asset(f'{DEST}/Textures/{name}')
        if not a:
            unreal.log_warning(f'[oldstreet] 缺贴图 {name}')
            continue
        if name.endswith('_N'):
            a.set_editor_property('srgb', False)
            a.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif name.endswith('_ARM'):
            a.set_editor_property('srgb', False)
            a.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        else:
            a.set_editor_property('srgb', True)
        eas.save_loaded_asset(a)
    log('贴图', len(tasks), '张')


# ---------------------------------------------------------------- 母材质
def clear(m):
    for _ in range(5):
        mel.delete_all_material_expressions(m)
        if mel.get_num_material_expressions(m) == 0:
            break


def new_material(name):
    path = f'{DEST}/Materials/{name}'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        clear(m)
    else:
        m = tools.create_asset(name, f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
    return m


def node(m, cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    assert mel.connect_material_expressions(a, out, b, inp), f'连线失败 {out}->{inp}'


def scalar(m, name, v, x, y):
    return node(m, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=v)


def finish(m):
    for flag in ('used_with_nanite', 'used_with_instanced_static_meshes'):
        m.set_editor_property(flag, True)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)


def base_chain(m):
    """BaseTex^Gamma × Tint，返回 (底色节点, 贴图采样节点)"""
    d = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -300, parameter_name='BaseTex', texture=eas.load_asset(WHITE), sampler_type=ST.SAMPLERTYPE_COLOR)
    g = scalar(m, 'Gamma', 1.0, -1100, -60)
    pw = node(m, unreal.MaterialExpressionPower, -850, -280)
    link(d, 'RGB', pw, 'Base')
    link(g, '', pw, 'Exp')
    tint = node(m, unreal.MaterialExpressionVectorParameter, -850, -120, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    mul = node(m, unreal.MaterialExpressionMultiply, -620, -240)
    link(pw, '', mul, 'A')
    link(tint, '', mul, 'B')
    return mul, d


def glow_chain(m, base):
    """自发光 = 底色 × Glow × EyeAdaptationInverse（白天曝光下给 1 就是屏幕上 1 的亮度，和园区灯带一个思路）"""
    gl = scalar(m, 'Glow', 0.0, -850, 380)
    inv = node(m, unreal.MaterialExpressionEyeAdaptationInverse, -850, 480)
    a = node(m, unreal.MaterialExpressionMultiply, -620, 400)
    link(base, '', a, 'A')
    link(gl, '', a, 'B')
    b = node(m, unreal.MaterialExpressionMultiply, -420, 420)
    link(a, '', b, 'A')
    link(inv, '', b, 'B')
    mel.connect_material_property(b, '', P.MP_EMISSIVE_COLOR)


def build_flat(name, masked=False):
    m = new_material(name)
    base, d = base_chain(m)
    n = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -1100, 120, parameter_name='NormalTex', texture=eas.load_asset(FLAT_N), sampler_type=ST.SAMPLERTYPE_NORMAL)
    mel.connect_material_property(base, '', P.MP_BASE_COLOR)
    mel.connect_material_property(n, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(scalar(m, 'Roughness', 0.7, -400, 150), '', P.MP_ROUGHNESS)
    mel.connect_material_property(scalar(m, 'Metallic', 0.0, -400, 230), '', P.MP_METALLIC)
    glow_chain(m, base)
    if masked:
        # 原包：颜色的亮度过一个渐变当透明度（亮度 ≤ MaskLo 全不透明，≥ MaskHi 全透明），白底抠掉
        lum = node(m, unreal.MaterialExpressionDesaturation, -850, 620)
        link(d, 'RGB', lum, '')
        lo = scalar(m, 'MaskLo', 0.659, -850, 720)
        hi = scalar(m, 'MaskHi', 0.867, -850, 800)
        sub = node(m, unreal.MaterialExpressionSubtract, -620, 640)
        link(lum, '', sub, 'A')
        link(lo, '', sub, 'B')
        rng = node(m, unreal.MaterialExpressionSubtract, -620, 760)
        link(hi, '', rng, 'A')
        link(lo, '', rng, 'B')
        div = node(m, unreal.MaterialExpressionDivide, -450, 680)
        link(sub, '', div, 'A')
        link(rng, '', div, 'B')
        sat = node(m, unreal.MaterialExpressionSaturate, -320, 680)
        link(div, '', sat, '')
        inv = node(m, unreal.MaterialExpressionOneMinus, -200, 680)
        link(sat, '', inv, '')
        mel.connect_material_property(inv, '', P.MP_OPACITY_MASK)
        m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property('two_sided', True)
        m.set_editor_property('opacity_mask_clip_value', 0.5)
    finish(m)
    return m


def build_pbr(name):
    m = new_material(name)
    base, d = base_chain(m)
    n = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -1100, 120, parameter_name='NormalTex', texture=eas.load_asset(FLAT_N), sampler_type=ST.SAMPLERTYPE_NORMAL)
    arm = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -1100, 380, parameter_name='ARMTex', texture=eas.load_asset(f'{DEST}/Textures/T_OS_Paving_ARM'), sampler_type=ST.SAMPLERTYPE_MASKS)
    r = node(m, unreal.MaterialExpressionMultiply, -620, 300)
    link(arm, 'G', r, 'A')
    link(scalar(m, 'RoughMul', 1.0, -850, 300), '', r, 'B')
    mel.connect_material_property(base, '', P.MP_BASE_COLOR)
    mel.connect_material_property(n, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(r, '', P.MP_ROUGHNESS)
    mel.connect_material_property(arm, 'R', P.MP_AMBIENT_OCCLUSION)
    finish(m)
    return m


def build_leaf(name):
    """树叶：遮罩 + 双面植被（逆光透光，午后太阳从叶子背面照过来发亮）。Nanite 支持遮罩，不开 WPO（树不动，虚拟阴影缓存才稳）"""
    m = new_material(name)
    d = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -300, parameter_name='BaseTex', texture=eas.load_asset(LEAF_D), sampler_type=ST.SAMPLERTYPE_COLOR)
    n = node(m, unreal.MaterialExpressionTextureSampleParameter2D, -1100, 120, parameter_name='NormalTex', texture=eas.load_asset(LEAF_N), sampler_type=ST.SAMPLERTYPE_NORMAL)
    # 顶点色：R = 这片叶子的亮度（里层暗、外层亮），G = 色相（Tint → Tint2）
    vc = node(m, unreal.MaterialExpressionVertexColor, -1100, 500)
    tint = node(m, unreal.MaterialExpressionVectorParameter, -1100, -60, parameter_name='Tint', default_value=unreal.LinearColor(1.0, 1.0, 0.75, 1))
    tint2 = node(m, unreal.MaterialExpressionVectorParameter, -1100, 40, parameter_name='Tint2', default_value=unreal.LinearColor(1.1, 0.95, 0.5, 1))
    lp = node(m, unreal.MaterialExpressionLinearInterpolate, -850, -60)
    link(tint, '', lp, 'A')
    link(tint2, '', lp, 'B')
    link(vc, 'G', lp, 'Alpha')
    mul0 = node(m, unreal.MaterialExpressionMultiply, -700, -240)
    link(d, 'RGB', mul0, 'A')
    link(lp, '', mul0, 'B')
    mul = node(m, unreal.MaterialExpressionMultiply, -520, -240)
    link(mul0, '', mul, 'A')
    link(vc, 'R', mul, 'B')
    subc = node(m, unreal.MaterialExpressionVectorParameter, -850, 300, parameter_name='SubsurfaceTint', default_value=unreal.LinearColor(0.75, 0.8, 0.25, 1))
    sm = node(m, unreal.MaterialExpressionMultiply, -620, 260)
    link(mul, '', sm, 'A')
    link(subc, '', sm, 'B')
    mel.connect_material_property(mul, '', P.MP_BASE_COLOR)
    mel.connect_material_property(n, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(scalar(m, 'Roughness', 0.65, -400, 150), '', P.MP_ROUGHNESS)
    mel.connect_material_property(d, 'A', P.MP_OPACITY_MASK)
    mel.connect_material_property(sm, '', P.MP_SUBSURFACE_COLOR)
    m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    m.set_editor_property('two_sided', True)
    m.set_editor_property('opacity_mask_clip_value', 0.33)
    finish(m)
    return m


def make_mi(name, parent, scalars=None, vectors=None, textures=None):
    path = f'{DEST}/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1))
    for k, v in (textures or {}).items():
        mel.set_material_instance_texture_parameter_value(mi, k, eas.load_asset(v))
    eas.save_loaded_asset(mi)
    return mi


if 'mat' in STEPS:
    FLAT = build_flat('M_OS_Flat')
    MASK = build_flat('M_OS_Mask', masked=True)
    PBR = build_pbr('M_OS_PBR')
    LEAF = build_leaf('M_OS_Leaf')
    T = f'{DEST}/Textures/'
    for slot, c in L['mats'].items():
        name = 'MI_' + slot
        if c.get('leaf'):
            make_mi(name, LEAF, {'Roughness': c.get('rough', 0.65)}, {'Tint': c['tint'], 'Tint2': c['tint2'], 'SubsurfaceTint': c['sss']})
            continue
        sc = {}
        vc = {}
        tx = {}
        if 'D' in c:
            tx['BaseTex'] = T + c['D']
        if 'N' in c:
            tx['NormalTex'] = T + c['N']
        if 'ARM' in c:
            tx['ARMTex'] = T + c['ARM']
        if 'gamma' in c:
            sc['Gamma'] = c['gamma']
        if 'tint' in c:
            vc['Tint'] = c['tint']
        if 'color' in c:
            vc['Tint'] = c['color']
        if 'ARM' in c:
            sc['RoughMul'] = c.get('rough', 1.0)
            make_mi(name, PBR, sc, vc, tx)
            continue
        sc['Roughness'] = c.get('rough', 0.7)
        sc['Metallic'] = c.get('metal', 0.0)
        sc['Glow'] = c.get('glow', 0.0)
        if 'mask' in c:
            sc['MaskLo'], sc['MaskHi'] = c['mask']
            make_mi(name, MASK, sc, vc, tx)
        else:
            make_mi(name, FLAT, sc, vc, tx)
    log('材质实例', len(L['mats']), '个')


# ---------------------------------------------------------------- 网格
def import_fbx(fbx, dest, name):
    ui = unreal.FbxImportUI()
    ui.set_editor_property('import_mesh', True)
    ui.set_editor_property('import_as_skeletal', False)
    ui.set_editor_property('import_materials', False)
    ui.set_editor_property('import_textures', False)
    ui.set_editor_property('import_animations', False)
    ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
    smd = ui.static_mesh_import_data
    smd.set_editor_property('combine_meshes', True)
    smd.set_editor_property('auto_generate_collision', False)
    smd.set_editor_property('generate_lightmap_u_vs', False)
    smd.set_editor_property('build_nanite', True)
    smd.set_editor_property('vertex_color_import_option', unreal.VertexColorImportOption.REPLACE)
    tools.import_asset_tasks([task(fbx, dest, name=name, options=ui)])


if 'mesh' in STEPS:
    MD = f'{DEST}/Meshes'
    COMPLEX = lambda n: n == 'SM_OS_Ground' or (n.startswith('SM_OS_Bld_') and n != 'SM_OS_Bld_B')      # 地面和楼用复杂碰撞（能站屋顶）；别的全不碰撞（树干、窄楼 B 是隐藏圆柱 / 方块）
    names = [os.path.splitext(os.path.basename(f))[0] for f in sorted(glob.glob(f'{SRC}/*.fbx'))]
    for name in names:
        if ONLY and name not in ONLY:
            continue
        import_fbx(f'{SRC}/{name}.fbx', MD, name)
        a = eas.load_asset(f'{MD}/{name}')
        if not a:
            unreal.log_warning(f'[oldstreet] 缺网格 {name}')
            continue
        nan = a.get_editor_property('nanite_settings')
        changed = False
        if not nan.enabled:
            nan.enabled = True
            changed = True
        if name == 'SM_OS_Tree':
            log('Nanite 设置可用属性', [k for k in dir(nan) if not k.startswith('_')][:60])
            try:
                nan.set_editor_property('shape_preservation', unreal.NaniteShapePreservation.PRESERVE_AREA)   # 植被：减面时保持叶子面积，远看不变稀
                changed = True
            except Exception as e:
                log('设不了 preserve area', e)
        if changed:
            a.set_editor_property('nanite_settings', nan)
        bs = a.get_editor_property('body_setup')
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if COMPLEX(name) else unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        sm = list(a.get_editor_property('static_materials'))
        slots = []
        for i, s in enumerate(sm):
            slot = str(s.get_editor_property('material_slot_name'))
            slots.append(slot)
            mi = eas.load_asset(f'{DEST}/Materials/MI_{slot}')
            if mi:
                s.set_editor_property('material_interface', mi)
                sm[i] = s
            else:
                unreal.log_warning(f'[oldstreet] {name} 槽 {slot} 没有材质实例')
        a.set_editor_property('static_materials', sm)
        # 树、道具不要距离场：细碎的东西距离场又大又没用（叶子 3.6 万片）
        if name in ('SM_OS_Tree', 'SM_OS_Props', 'SM_OS_Curb', 'SM_OS_Bollards'):
            try:
                bset = unreal.EditorStaticMeshLibrary.get_lod_build_settings(a, 0)
                bset.set_editor_property('distance_field_resolution_scale', 0.0)
                unreal.EditorStaticMeshLibrary.set_lod_build_settings(a, 0, bset)
            except Exception as e:
                log('设不了距离场', name, e)
        eas.save_loaded_asset(a)
        log('网格', name, 'Nanite', a.get_editor_property('nanite_settings').enabled, '槽', slots)
log('完成', STEPS)
