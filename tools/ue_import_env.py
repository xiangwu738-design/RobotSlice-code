"""在 RobotSlice 工程里跑：导入 Poly Haven 场景素材。
- 模型（glTF，自带材质贴图）→ /Game/Env/PolyHaven/<id>，开 Nanite，碰撞用模型本身形状
- 地面墙面贴图 → /Game/Env/Textures/<id>，建世界坐标对齐的母材质 M_WorldPBR（带湿度、积水）和各材质实例
- 打印每个模型的尺寸（米），用来排布场景
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_import_env.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

SRC = 'D:/RobotSlice/SourceAssets/polyhaven'
tools = unreal.AssetToolsHelpers.get_asset_tools()
eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary


def import_task(filename, dest, name=None):
    t = unreal.AssetImportTask()
    t.filename = filename
    t.destination_path = dest
    if name:
        t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    return t


# ---------------------------------------------------------------- 模型
models = sorted(os.listdir(f'{SRC}/models'))
tasks = []
for mid in models:
    gltf = [f for f in os.listdir(f'{SRC}/models/{mid}') if f.endswith('.gltf')]
    if gltf and not eas.does_directory_exist(f'/Game/Env/PolyHaven/{mid}'):
        tasks.append(import_task(f'{SRC}/models/{mid}/{gltf[0]}', f'/Game/Env/PolyHaven/{mid}'))
if tasks:
    tools.import_asset_tasks(tasks)

for mid in models:
    for path in eas.list_assets(f'/Game/Env/PolyHaven/{mid}', recursive=True, include_folder=False):
        a = eas.load_asset(path)
        if not isinstance(a, unreal.StaticMesh):
            continue
        nanite = a.get_editor_property('nanite_settings')
        has_masked = 'fence' in mid or 'cables' in mid
        if not nanite.enabled and not has_masked:
            nanite.enabled = True
            a.set_editor_property('nanite_settings', nanite)
        bs = a.get_editor_property('body_setup')
        if bs:
            bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        eas.save_loaded_asset(a)
        b = a.get_bounding_box()
        size = (b.max - b.min) / 100
        unreal.log(f'[env] {path} 尺寸 {size.x:.2f} x {size.y:.2f} x {size.z:.2f} 米 中心 {(b.max + b.min).x / 200:.2f},{(b.max + b.min).y / 200:.2f},{b.min.z / 100:.2f}(底)')

# ---------------------------------------------------------------- 贴图
tex_ids = sorted(os.listdir(f'{SRC}/textures'))
tasks = []
for tid in tex_ids:
    for f in os.listdir(f'{SRC}/textures/{tid}'):
        name = 'T_' + os.path.splitext(f)[0]
        if not eas.does_asset_exist(f'/Game/Env/Textures/{tid}/{name}'):
            tasks.append(import_task(f'{SRC}/textures/{tid}/{f}', f'/Game/Env/Textures/{tid}', name))
if tasks:
    tools.import_asset_tasks(tasks)
for tid in tex_ids:
    for path in eas.list_assets(f'/Game/Env/Textures/{tid}', recursive=False, include_folder=False):
        t = eas.load_asset(path)
        if '_nor_gl_' in path:
            t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            t.set_editor_property('srgb', False)
        elif '_arm_' in path:
            t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
            t.set_editor_property('srgb', False)
        eas.save_loaded_asset(t)


# ---------------------------------------------------------------- 世界坐标对齐的母材质
def make_world_material():
    path = '/Game/Env/Materials/M_WorldPBR'
    if eas.does_asset_exist(path):
        m = eas.load_asset(path)
        mel.delete_all_material_expressions(m)  # 每次清空节点重连，改了节点也能生效
    else:
        m = tools.create_asset('M_WorldPBR', '/Game/Env/Materials', unreal.Material, unreal.MaterialFactoryNew())
    P = unreal.MaterialProperty
    wat = unreal.load_object(None, '/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture')
    wan = unreal.load_object(None, '/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal.WorldAlignedNormal')
    assert wat and wan, '世界对齐材质函数没加载到'

    def node(cls, x, y, **props):
        n = mel.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def tex_param(name, x, y, sampler, default):
        # 默认贴图的类型必须和采样类型一致，否则整个材质编译失败、游戏里显示默认灰格子
        return node(unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, sampler_type=sampler,
                    texture=eas.load_asset(default))

    def scalar(name, value, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    tile = scalar('TileSize', 300.0, -1600, 0)
    tile3 = node(unreal.MaterialExpressionAppendVector, -1400, 0)
    tile2 = node(unreal.MaterialExpressionAppendVector, -1500, 0)
    mel.connect_material_expressions(tile, '', tile2, 'A')
    mel.connect_material_expressions(tile, '', tile2, 'B')
    mel.connect_material_expressions(tile2, '', tile3, 'A')
    mel.connect_material_expressions(tile, '', tile3, 'B')

    def world_aligned(func, tex, y):
        fc = node(unreal.MaterialExpressionMaterialFunctionCall, -1100, y)
        fc.set_function(func) if hasattr(fc, 'set_function') else fc.set_editor_property('material_function', func)
        mel.connect_material_expressions(tex, '', fc, 'TextureObject')
        mel.connect_material_expressions(tile3, '', fc, 'TextureSize')
        return fc

    t_col = tex_param('BaseColorTex', -1400, -400, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                      '/Game/Env/Textures/asphalt_02/T_asphalt_02_diff_2k')
    t_nor = tex_param('NormalTex', -1400, 300, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                      '/Game/Env/Textures/asphalt_02/T_asphalt_02_nor_gl_2k')
    t_arm = tex_param('ARMTex', -1400, 650, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
                      '/Game/Env/Textures/asphalt_02/T_asphalt_02_arm_2k')
    col = world_aligned(wat, t_col, -400)
    nor = world_aligned(wan, t_nor, 300)
    arm = world_aligned(wat, t_arm, 650)

    tint = node(unreal.MaterialExpressionVectorParameter, -900, -600, parameter_name='Tint',
                default_value=unreal.LinearColor(1, 1, 1, 1))
    tinted = node(unreal.MaterialExpressionMultiply, -700, -450)
    mel.connect_material_expressions(col, 'XYZ Texture', tinted, 'A')
    mel.connect_material_expressions(tint, '', tinted, 'B')

    mask = node(unreal.MaterialExpressionComponentMask, -800, 650, r=False, g=True, b=False, a=False)
    mel.connect_material_expressions(arm, 'XYZ Texture', mask, '')
    metal_mask = node(unreal.MaterialExpressionComponentMask, -800, 800, r=False, g=False, b=True, a=False)
    mel.connect_material_expressions(arm, 'XYZ Texture', metal_mask, '')
    ao_mask = node(unreal.MaterialExpressionComponentMask, -800, 950, r=True, g=False, b=False, a=False)
    mel.connect_material_expressions(arm, 'XYZ Texture', ao_mask, '')

    # 湿度：整体变暗、变光滑；积水：按世界坐标的噪声挑出一块块水洼，几乎镜面、法线压平
    wet = scalar('Wetness', 0.0, -900, 1100)
    puddle_amt = scalar('PuddleAmount', 0.0, -1300, 1250)
    wpos = node(unreal.MaterialExpressionWorldPosition, -1500, 1400)
    noise = node(unreal.MaterialExpressionNoise, -1300, 1400, scale=0.0025, levels=4, output_min=0.0, output_max=1.0)
    mel.connect_material_expressions(wpos, '', noise, 'Position')
    one_minus = node(unreal.MaterialExpressionOneMinus, -1100, 1250)
    mel.connect_material_expressions(puddle_amt, '', one_minus, '')
    sub = node(unreal.MaterialExpressionSubtract, -950, 1350)
    mel.connect_material_expressions(noise, '', sub, 'A')
    mel.connect_material_expressions(one_minus, '', sub, 'B')
    gain = node(unreal.MaterialExpressionMultiply, -800, 1350, const_b=12.0)
    mel.connect_material_expressions(sub, '', gain, 'A')
    puddle = node(unreal.MaterialExpressionSaturate, -650, 1350)
    mel.connect_material_expressions(gain, '', puddle, '')
    wet_total = node(unreal.MaterialExpressionMax, -500, 1150)
    mel.connect_material_expressions(wet, '', wet_total, 'A')
    mel.connect_material_expressions(puddle, '', wet_total, 'B')

    dark = node(unreal.MaterialExpressionLinearInterpolate, -350, -450, const_a=1.0, const_b=0.55)
    mel.connect_material_expressions(wet_total, '', dark, 'Alpha')
    base = node(unreal.MaterialExpressionMultiply, -200, -400)
    mel.connect_material_expressions(tinted, '', base, 'A')
    mel.connect_material_expressions(dark, '', base, 'B')

    wet_rough = node(unreal.MaterialExpressionMultiply, -500, 650, const_b=0.25)
    mel.connect_material_expressions(mask, '', wet_rough, 'A')
    rough = node(unreal.MaterialExpressionLinearInterpolate, -300, 650)
    mel.connect_material_expressions(mask, '', rough, 'A')
    mel.connect_material_expressions(wet_rough, '', rough, 'B')
    mel.connect_material_expressions(wet, '', rough, 'Alpha')
    rough2 = node(unreal.MaterialExpressionLinearInterpolate, -150, 700, const_b=0.04)
    mel.connect_material_expressions(rough, '', rough2, 'A')
    mel.connect_material_expressions(puddle, '', rough2, 'Alpha')

    flat = node(unreal.MaterialExpressionConstant3Vector, -500, 300, constant=unreal.LinearColor(0, 0, 1, 1))
    nrm = node(unreal.MaterialExpressionLinearInterpolate, -300, 300)
    mel.connect_material_expressions(nor, 'XYZ Texture', nrm, 'A')
    mel.connect_material_expressions(flat, '', nrm, 'B')
    mel.connect_material_expressions(puddle, '', nrm, 'Alpha')

    mel.connect_material_property(base, '', P.MP_BASE_COLOR)
    mel.connect_material_property(rough2, '', P.MP_ROUGHNESS)
    mel.connect_material_property(metal_mask, '', P.MP_METALLIC)
    mel.connect_material_property(ao_mask, '', P.MP_AMBIENT_OCCLUSION)
    mel.connect_material_property(nrm, '', P.MP_NORMAL)
    m.set_editor_property('tangent_space_normal', False)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    unreal.log(f'[env] M_WorldPBR 节点数 {mel.get_num_material_expressions(m)}')
    return m


# (贴图, 铺贴尺寸厘米, 湿度, 积水, 色调)
SURFACES = {
    'MI_Asphalt': ('asphalt_02', 400, 0.55, 0.55, (0.8, 0.82, 0.86)),
    'MI_AsphaltDirty': ('asphalt_03', 400, 0.45, 0.4, (0.85, 0.85, 0.88)),
    'MI_ConcreteFloor': ('concrete_floor_worn_02', 350, 0.35, 0.25, (0.9, 0.9, 0.92)),
    'MI_ConcreteWall': ('concrete_wall_006', 300, 0.15, 0.0, (0.75, 0.77, 0.8)),
    'MI_ConcreteWall2': ('concrete_wall_008', 300, 0.15, 0.0, (0.7, 0.72, 0.76)),
    'MI_Corrugated': ('corrugated_iron_02', 250, 0.25, 0.0, (0.8, 0.84, 0.9)),
    'MI_Container': ('container_side', 300, 0.25, 0.0, (1.0, 1.0, 1.0)),
    'MI_FactoryWall': ('factory_wall', 300, 0.15, 0.0, (0.85, 0.87, 0.9)),
    'MI_MetalPlate': ('blue_metal_plate', 200, 0.3, 0.0, (0.75, 0.8, 0.85)),
    'MI_Brick': ('brick_wall_02', 250, 0.2, 0.0, (0.7, 0.68, 0.68)),
}
master = make_world_material()
for name, (tid, tile, wet, puddle, tint) in SURFACES.items():
    path = f'/Game/Env/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, '/Game/Env/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)
    for param, suffix in (('BaseColorTex', 'diff'), ('NormalTex', 'nor_gl'), ('ARMTex', 'arm')):
        tex = eas.load_asset(f'/Game/Env/Textures/{tid}/T_{tid}_{suffix}_2k')
        if tex:
            mel.set_material_instance_texture_parameter_value(mi, param, tex)
    mel.set_material_instance_scalar_parameter_value(mi, 'TileSize', tile)
    mel.set_material_instance_scalar_parameter_value(mi, 'Wetness', wet)
    mel.set_material_instance_scalar_parameter_value(mi, 'PuddleAmount', puddle)
    mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*tint, 1))
    eas.save_loaded_asset(mi)
unreal.log(f'[env] 完成：{len(models)} 个模型、{len(SURFACES)} 个地面墙面材质')
