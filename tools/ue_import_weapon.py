"""在 RobotSlice 工程里跑：导入摆正后的武器（lookdev/weapon_normalize.py 的输出）和它的贴图，挂武器母材质 M_WeaponPBR 的材质实例。
资源名：SM_<名字>、MI_<名字>、T_<名字>_BaseColor 等。贴图最大 2048（显存紧）；自发光贴图只取亮度当遮罩，颜色由参数 Emissive 给
（机体按队伍设成青 / 红）；BaseTint 给底色整体染色（枪包是白色，压成冷灰）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_import_weapon.py <名字> <fbx> <贴图目录> [底色 r,g,b] [自发光强度]" -AllowCommandletRendering -RenderOffscreen
"""
import glob
import sys

import unreal

NAME, FBX, TEX = sys.argv[1], sys.argv[2], sys.argv[3]
TINT = [float(v) for v in sys.argv[4].split(',')] if len(sys.argv) > 4 else [1.0, 1.0, 1.0]
GLOW = float(sys.argv[5]) if len(sys.argv) > 5 else 8.0
DEST = '/Game/Weapons'
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
TC = unreal.TextureCompressionSettings

# ---- 贴图：(文件名结尾, 资源名后缀, sRGB, 压缩)
MAPS = [('BaseColor', 'BaseColor', True, TC.TC_DEFAULT), ('Normal', 'Normal', False, TC.TC_NORMALMAP),
        ('Metallic', 'Metallic', False, TC.TC_MASKS), ('Roughness', 'Roughness', False, TC.TC_MASKS),
        ('Emissive', 'Emissive', True, TC.TC_DEFAULT)]
textures = {}
for key, suffix, srgb, comp in MAPS:
    files = glob.glob(f'{TEX}/*_{key}.*')
    if not files:
        unreal.log(f'[weapon] {NAME} 没有 {key} 贴图')
        continue
    t = unreal.AssetImportTask()
    t.filename = files[0]
    t.destination_path = f'{DEST}/Textures'
    t.destination_name = f'T_{NAME}_{suffix}'
    t.automated = True
    t.replace_existing = True
    t.save = False
    tools.import_asset_tasks([t])
    tex = unreal.load_asset(f'{DEST}/Textures/T_{NAME}_{suffix}')
    tex.set_editor_property('srgb', srgb)
    tex.set_editor_property('compression_settings', comp)
    tex.set_editor_property('max_texture_size', 2048)
    eal.save_loaded_asset(tex, False)
    textures[suffix] = tex


# ---- 母材质：贴图 PBR（底色可染）+ 自发光遮罩 × 颜色参数
def make_master():
    path = f'{DEST}/Materials/M_WeaponPBR'
    if eal.does_asset_exist(path):
        m = unreal.load_asset(path)
        if 'BaseTint' in [str(n) for n in mel.get_vector_parameter_names(m)]:
            if not m.get_editor_property('used_with_nanite'):
                m.set_editor_property('used_with_nanite', True)
                mel.recompile_material(m)
                eal.save_loaded_asset(m, False)
            return m
        # 第一版没有 BaseTint：连同用它的旧实例一起重建
        for p in eal.list_assets(f'{DEST}/Materials', recursive=False):
            a = eal.load_asset(p)
            if isinstance(a, unreal.MaterialInstanceConstant) and a.get_editor_property('parent') == m:
                eal.delete_asset(p)
        eal.delete_asset(path)
    m = tools.create_asset('M_WeaponPBR', f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
    P = unreal.MaterialProperty
    ST = unreal.MaterialSamplerType

    def node(cls, x, y):
        return mel.create_material_expression(m, cls, x, y)

    def tex_param(name, x, y, sampler, default):
        n = node(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('sampler_type', sampler)
        n.set_editor_property('texture', unreal.load_asset(default))
        return n

    def param(cls, name, value, x, y):
        n = node(cls, x, y)
        n.set_editor_property('parameter_name', name)
        n.set_editor_property('default_value', value)
        return n

    base = tex_param('BaseColorTex', -900, -300, ST.SAMPLERTYPE_COLOR, '/Engine/EngineResources/DefaultTexture')
    normal = tex_param('NormalTex', -900, 0, ST.SAMPLERTYPE_NORMAL, '/Engine/EngineMaterials/DefaultNormal')
    metal = tex_param('MetallicTex', -900, 300, ST.SAMPLERTYPE_MASKS, '/Engine/EngineResources/Black')
    rough = tex_param('RoughnessTex', -900, 600, ST.SAMPLERTYPE_MASKS, '/Engine/EngineResources/WhiteSquareTexture')
    emis = tex_param('EmissiveTex', -900, 900, ST.SAMPLERTYPE_COLOR, '/Engine/EngineResources/Black')
    tint = param(unreal.MaterialExpressionVectorParameter, 'BaseTint', unreal.LinearColor(1, 1, 1, 1), -600, -150)
    bmul = node(unreal.MaterialExpressionMultiply, -400, -250)
    mel.connect_material_expressions(base, 'RGB', bmul, 'A')
    mel.connect_material_expressions(tint, '', bmul, 'B')
    rough_mul = param(unreal.MaterialExpressionScalarParameter, 'RoughnessScale', 1.0, -600, 700)
    rmul = node(unreal.MaterialExpressionMultiply, -400, 600)
    mel.connect_material_expressions(rough, 'R', rmul, 'A')
    mel.connect_material_expressions(rough_mul, '', rmul, 'B')
    # 自发光遮罩：贴图三通道取最大
    mx1 = node(unreal.MaterialExpressionMax, -600, 900)
    mx2 = node(unreal.MaterialExpressionMax, -450, 950)
    mel.connect_material_expressions(emis, 'R', mx1, 'A')
    mel.connect_material_expressions(emis, 'G', mx1, 'B')
    mel.connect_material_expressions(mx1, '', mx2, 'A')
    mel.connect_material_expressions(emis, 'B', mx2, 'B')
    color = param(unreal.MaterialExpressionVectorParameter, 'Emissive', unreal.LinearColor(0.15, 0.75, 1.0, 1.0), -600, 1100)
    strength = param(unreal.MaterialExpressionScalarParameter, 'EmissiveStrength', 8.0, -600, 1250)
    e1 = node(unreal.MaterialExpressionMultiply, -300, 1000)
    e2 = node(unreal.MaterialExpressionMultiply, -150, 1050)
    mel.connect_material_expressions(mx2, '', e1, 'A')
    mel.connect_material_expressions(color, '', e1, 'B')
    mel.connect_material_expressions(e1, '', e2, 'A')
    mel.connect_material_expressions(strength, '', e2, 'B')
    mel.connect_material_property(bmul, '', P.MP_BASE_COLOR)
    mel.connect_material_property(normal, 'RGB', P.MP_NORMAL)
    mel.connect_material_property(metal, 'R', P.MP_METALLIC)
    mel.connect_material_property(rmul, '', P.MP_ROUGHNESS)
    mel.connect_material_property(e2, '', P.MP_EMISSIVE_COLOR)
    m.set_editor_property('used_with_nanite', True)  # 武器网格开 Nanite（每台机体两把武器二十万三角形），不打标记会在运行时现编译
    mel.recompile_material(m)
    eal.save_loaded_asset(m, False)
    return m


master = make_master()
mi_path = f'{DEST}/Materials/MI_{NAME}'
mi = unreal.load_asset(mi_path) if eal.does_asset_exist(mi_path) else \
    tools.create_asset(f'MI_{NAME}', f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mi.set_editor_property('parent', master)
for suffix, p in (('BaseColor', 'BaseColorTex'), ('Normal', 'NormalTex'), ('Metallic', 'MetallicTex'), ('Roughness', 'RoughnessTex'),
                  ('Emissive', 'EmissiveTex')):
    if suffix in textures:
        mel.set_material_instance_texture_parameter_value(mi, p, textures[suffix])
mel.set_material_instance_vector_parameter_value(mi, 'BaseTint', unreal.LinearColor(*TINT, 1.0))
mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', GLOW)
eal.save_loaded_asset(mi, False)

# ---- 网格
t = unreal.AssetImportTask()
t.filename = FBX
t.destination_path = f'{DEST}/Meshes'
t.destination_name = f'SM_{NAME}'
t.automated = True
t.replace_existing = True
t.save = False
ui = unreal.FbxImportUI()
ui.import_mesh = True
ui.import_as_skeletal = False
ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
ui.import_materials = False
ui.import_textures = False
ui.static_mesh_import_data.set_editor_property('combine_meshes', True)
ui.static_mesh_import_data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
ui.static_mesh_import_data.set_editor_property('auto_generate_collision', False)
t.options = ui
tools.import_asset_tasks([t])
mesh = unreal.load_asset(f'{DEST}/Meshes/SM_{NAME}')
mats = list(mesh.get_editor_property('static_materials'))
for i, sm in enumerate(mats):
    sm.set_editor_property('material_interface', mi)
    sm.set_editor_property('material_slot_name', f'MI_{NAME}')
    mats[i] = sm
mesh.set_editor_property('static_materials', mats)
ns = mesh.get_editor_property('nanite_settings')
ns.set_editor_property('enabled', True)
mesh.set_editor_property('nanite_settings', ns)
eal.save_loaded_asset(mesh, False)
b = mesh.get_bounding_box()
unreal.log(f'[weapon] SM_{NAME} 包围盒 ({b.min.x:.0f},{b.min.y:.0f},{b.min.z:.0f}) ~ ({b.max.x:.0f},{b.max.y:.0f},{b.max.z:.0f}) 厘米，'
           f'三角形 {mesh.get_num_triangles(0)}，Nanite {mesh.get_editor_property("nanite_settings").get_editor_property("enabled")}，贴图 {sorted(textures)}，底色 {TINT}')
