"""建世界坐标贴图的母材质 M_WorldPBR（地面、墙面、方块用），以及各表面的材质实例。
不用引擎的 WorldAlignedTexture 函数（和法线/遮罩采样类型冲突会编译失败），改成：按表面法线最朝向的轴，
从 XY / YZ / XZ 三种投影里挑一种当 UV，再用普通贴图采样。带湿度（整体变暗变光滑）和积水（世界坐标噪声挑出水洼）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_world_material.py" -AllowCommandletRendering -RenderOffscreen
"""
import unreal

eas = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
P = unreal.MaterialProperty
PATH = '/Game/Env/Materials/M_WorldPBR'

if eas.does_asset_exist(PATH):
    m = eas.load_asset(PATH)
    mel.delete_all_material_expressions(m)
else:
    m = tools.create_asset('M_WorldPBR', '/Game/Env/Materials', unreal.Material, unreal.MaterialFactoryNew())


def node(cls, x, y, **props):
    n = mel.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        n.set_editor_property(k, v)
    return n


def link(a, out, b, inp):
    ok = mel.connect_material_expressions(a, out, b, inp)
    assert ok, f'连线失败 {a.get_name()}.{out} -> {b.get_name()}.{inp}'


def scalar(name, value, x, y):
    return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def mask(src, r, g, b, x, y):
    n = node(unreal.MaterialExpressionComponentMask, x, y, r=r, g=g, b=b, a=False)
    link(src, '', n, '')
    return n


# ---- 世界坐标 / 铺贴尺寸
wpos = node(unreal.MaterialExpressionWorldPosition, -2200, 0)
tile = scalar('TileSize', 300.0, -2200, 150)
p = node(unreal.MaterialExpressionDivide, -2000, 50)
link(wpos, '', p, 'A')
link(tile, '', p, 'B')
uv_xy = mask(p, True, True, False, -1800, -100)
uv_yz = mask(p, False, True, True, -1800, 0)
uv_xz = mask(p, True, False, True, -1800, 100)

# ---- 按法线挑投影：|n.z| 最大用 XY（地面、顶面），否则 |n.x|>|n.y| 用 YZ，否则 XZ
nrm = node(unreal.MaterialExpressionVertexNormalWS, -2200, 350)
an = node(unreal.MaterialExpressionAbs, -2000, 350)
link(nrm, '', an, '')
nx = mask(an, True, False, False, -1800, 300)
ny = mask(an, False, True, False, -1800, 400)
nz = mask(an, False, False, True, -1800, 500)
side = node(unreal.MaterialExpressionIf, -1550, 50)
link(nx, '', side, 'A')
link(ny, '', side, 'B')
link(uv_yz, '', side, 'A > B')
link(uv_xz, '', side, 'A == B')
link(uv_xz, '', side, 'A < B')
nxy = node(unreal.MaterialExpressionMax, -1550, 400)
link(nx, '', nxy, 'A')
link(ny, '', nxy, 'B')
uv = node(unreal.MaterialExpressionIf, -1350, 0)
link(nz, '', uv, 'A')
link(nxy, '', uv, 'B')
link(uv_xy, '', uv, 'A > B')
link(side, '', uv, 'A == B')
link(side, '', uv, 'A < B')


def tex(name, sampler, default, x, y):
    t = node(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name,
             texture=eas.load_asset(default), sampler_type=sampler)
    link(uv, '', t, 'UVs')
    return t


S = unreal.MaterialSamplerType
col = tex('BaseColorTex', S.SAMPLERTYPE_COLOR, '/Game/Env/Textures/asphalt_02/T_asphalt_02_diff_2k', -1100, -400)
nor = tex('NormalTex', S.SAMPLERTYPE_NORMAL, '/Game/Env/Textures/asphalt_02/T_asphalt_02_nor_gl_2k', -1100, 0)
arm = tex('ARMTex', S.SAMPLERTYPE_MASKS, '/Game/Env/Textures/asphalt_02/T_asphalt_02_arm_2k', -1100, 400)

# ---- 色调
tint = node(unreal.MaterialExpressionVectorParameter, -900, -600, parameter_name='Tint',
            default_value=unreal.LinearColor(1, 1, 1, 1))
tinted = node(unreal.MaterialExpressionMultiply, -700, -450)
link(col, 'RGB', tinted, 'A')
link(tint, '', tinted, 'B')

# ---- 湿度与积水
wet = scalar('Wetness', 0.0, -1100, 800)
puddle_amt = scalar('PuddleAmount', 0.0, -1100, 950)
noise = node(unreal.MaterialExpressionNoise, -1100, 1100, scale=0.0025, levels=4, output_min=0.0, output_max=1.0)
inv = node(unreal.MaterialExpressionOneMinus, -900, 950)
link(puddle_amt, '', inv, '')
sub = node(unreal.MaterialExpressionSubtract, -750, 1050)
link(noise, '', sub, 'A')
link(inv, '', sub, 'B')
gain = node(unreal.MaterialExpressionMultiply, -600, 1050, const_b=12.0)
link(sub, '', gain, 'A')
up = node(unreal.MaterialExpressionSaturate, -600, 1200)  # 只有朝上的面积水
nz_up = mask(nrm, False, False, True, -800, 1250)
link(nz_up, '', up, '')
pud0 = node(unreal.MaterialExpressionSaturate, -450, 1050)
link(gain, '', pud0, '')
puddle = node(unreal.MaterialExpressionMultiply, -300, 1100)
link(pud0, '', puddle, 'A')
link(up, '', puddle, 'B')
wet_total = node(unreal.MaterialExpressionMax, -150, 900)
link(wet, '', wet_total, 'A')
link(puddle, '', wet_total, 'B')

dark = node(unreal.MaterialExpressionLinearInterpolate, -350, -450, const_a=1.0, const_b=0.5)
link(wet_total, '', dark, 'Alpha')
base = node(unreal.MaterialExpressionMultiply, -150, -400)
link(tinted, '', base, 'A')
link(dark, '', base, 'B')

wet_rough = node(unreal.MaterialExpressionMultiply, -800, 450, const_b=0.3)
link(arm, 'G', wet_rough, 'A')
rough = node(unreal.MaterialExpressionLinearInterpolate, -600, 450)
link(arm, 'G', rough, 'A')
link(wet_rough, '', rough, 'B')
link(wet, '', rough, 'Alpha')
rough2 = node(unreal.MaterialExpressionLinearInterpolate, -400, 500, const_b=0.03)
link(rough, '', rough2, 'A')
link(puddle, '', rough2, 'Alpha')

flat = node(unreal.MaterialExpressionConstant3Vector, -800, 150, constant=unreal.LinearColor(0, 0, 1, 1))
nrm_out = node(unreal.MaterialExpressionLinearInterpolate, -600, 100)
link(nor, 'RGB', nrm_out, 'A')
link(flat, '', nrm_out, 'B')
link(puddle, '', nrm_out, 'Alpha')

# ---- 自发光贴图（立面亮窗之类），默认黑图、强度 0
BLACK = next(p for p in ('/Engine/EngineResources/Black', '/Engine/EngineMaterials/BlackSquareTexture',
                         '/Engine/EngineResources/DefaultTexture') if eas.does_asset_exist(p))
emt = tex('EmissiveTex', S.SAMPLERTYPE_COLOR, BLACK, -1100, 1400)
estr = scalar('EmissiveStrength', 0.0, -900, 1550)
emis = node(unreal.MaterialExpressionMultiply, -700, 1450)
link(emt, 'RGB', emis, 'A')
link(estr, '', emis, 'B')
mel.connect_material_property(emis, '', P.MP_EMISSIVE_COLOR)

mel.connect_material_property(base, '', P.MP_BASE_COLOR)
mel.connect_material_property(rough2, '', P.MP_ROUGHNESS)
mel.connect_material_property(arm, 'B', P.MP_METALLIC)
mel.connect_material_property(arm, 'R', P.MP_AMBIENT_OCCLUSION)
mel.connect_material_property(nrm_out, '', P.MP_NORMAL)
m.set_editor_property('tangent_space_normal', True)
mel.recompile_material(m)
eas.save_loaded_asset(m)

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
for name, (tid, tile_cm, wetness, puddle_v, tint_v) in SURFACES.items():
    path = f'/Game/Env/Materials/{name}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        name, '/Game/Env/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', m)
    for param, suffix in (('BaseColorTex', 'diff'), ('NormalTex', 'nor_gl'), ('ARMTex', 'arm')):
        t = eas.load_asset(f'/Game/Env/Textures/{tid}/T_{tid}_{suffix}_2k')
        if t:
            mel.set_material_instance_texture_parameter_value(mi, param, t)
    mel.set_material_instance_scalar_parameter_value(mi, 'TileSize', tile_cm)
    mel.set_material_instance_scalar_parameter_value(mi, 'Wetness', wetness)
    mel.set_material_instance_scalar_parameter_value(mi, 'PuddleAmount', puddle_v)
    mel.set_material_instance_vector_parameter_value(mi, 'Tint', unreal.LinearColor(*tint_v, 1))
    eas.save_loaded_asset(mi)
unreal.log(f'[mat] M_WorldPBR 重建完成，{len(SURFACES)} 个材质实例')
