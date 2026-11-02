"""在 RobotSlice 工程里跑：把 blender_export_groups.py 导出的一组素材（groups.fbx + groups.json + textures/）导进来。
- 贴图 → <目标>/Textures（最大边长压到 2048；法线图翻绿通道，Blender 用的是 OpenGL 法线）
- 母材质 M_KitSG（按 UV 贴图的高光 / 光泽度流程：Diffuse、Spec、Gloss、Normal）+ 每个材质一个实例
- 每组一个静态网格 → <目标>/Meshes（Nanite、复杂碰撞），按槽名挂材质
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_import_kit.py 源目录 /Game/目标路径" -AllowCommandletRendering -RenderOffscreen
"""
import json
import os
import re
import sys

import unreal

SRC, DEST = sys.argv[1], sys.argv[2]
MAX_TEX = 2048
eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
data = json.load(open(f'{SRC}/groups.json', encoding='utf-8'))
MASTER = '/Game/Env/Materials/M_KitSG'


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


def clean(s):
    return re.sub(r'[^A-Za-z0-9]', '_', s)


# ---------------------------------------------------------------- 母材质
FLAT_NORMAL = '/Game/Env/Textures/T_FlatNormal'  # 引擎里找不到平法线图，自己做一张 8×8 的
if not eas.does_asset_exist(FLAT_NORMAL):
    tools.import_asset_tasks([task('D:/RobotSlice/SourceAssets/common/T_FlatNormal.png', '/Game/Env/Textures', 'T_FlatNormal')])
    t = eas.load_asset(FLAT_NORMAL)
    t.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
    t.set_editor_property('srgb', False)
    eas.save_loaded_asset(t)


def make_master():
    if eas.does_asset_exist(MASTER):
        m = eas.load_asset(MASTER)
        nor = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_NORMAL)
        if nor and not nor.get_editor_property('texture'):  # 早先建的版本法线参数没有默认图，补上
            nor.set_editor_property('texture', eas.load_asset(FLAT_NORMAL))
            mel.recompile_material(m)
            eas.save_loaded_asset(m)
        return m
    m = tools.create_asset('M_KitSG', '/Game/Env/Materials', unreal.Material, unreal.MaterialFactoryNew())
    P = unreal.MaterialProperty
    S = unreal.MaterialSamplerType

    def node(cls, x, y, **props):
        n = mel.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def tex(name, sampler, default, x, y):
        return node(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name,
                    texture=eas.load_asset(default), sampler_type=sampler)

    diff = tex('Diffuse', S.SAMPLERTYPE_COLOR, '/Engine/EngineMaterials/DefaultDiffuse', -900, -300)
    spec = tex('Spec', S.SAMPLERTYPE_COLOR, '/Engine/EngineMaterials/DefaultDiffuse', -900, 0)
    gloss = tex('Gloss', S.SAMPLERTYPE_COLOR, '/Engine/EngineMaterials/DefaultDiffuse', -900, 250)
    nor = tex('Normal', S.SAMPLERTYPE_NORMAL, FLAT_NORMAL, -900, 500)
    tint = node(unreal.MaterialExpressionVectorParameter, -600, -450, parameter_name='Tint', default_value=unreal.LinearColor(1, 1, 1, 1))
    col = node(unreal.MaterialExpressionMultiply, -400, -300)
    mel.connect_material_expressions(diff, 'RGB', col, 'A')
    mel.connect_material_expressions(tint, '', col, 'B')
    rough = node(unreal.MaterialExpressionOneMinus, -600, 250)
    mel.connect_material_expressions(gloss, 'R', rough, '')
    metal = node(unreal.MaterialExpressionScalarParameter, -600, 100, parameter_name='Metallic', default_value=0.0)
    mel.connect_material_property(col, '', P.MP_BASE_COLOR)
    mel.connect_material_property(spec, 'R', P.MP_SPECULAR)
    mel.connect_material_property(rough, '', P.MP_ROUGHNESS)
    mel.connect_material_property(metal, '', P.MP_METALLIC)
    mel.connect_material_property(nor, 'RGB', P.MP_NORMAL)
    mel.recompile_material(m)
    eas.save_loaded_asset(m)
    return m


master = make_master()

# ---------------------------------------------------------------- 贴图
tasks = []
for f in sorted(os.listdir(f'{SRC}/textures')):
    name = 'T_' + clean(os.path.splitext(f)[0])
    if not eas.does_asset_exist(f'{DEST}/Textures/{name}'):
        tasks.append(task(f'{SRC}/textures/{f}', f'{DEST}/Textures', name))
if tasks:
    tools.import_asset_tasks(tasks)
    for t in tasks:
        tex = eas.load_asset(f'{DEST}/Textures/{t.destination_name}')
        tex.set_editor_property('max_texture_size', MAX_TEX)
        if 'normal' in t.destination_name.lower():
            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property('srgb', False)
            tex.set_editor_property('flip_green_channel', True)
        eas.save_loaded_asset(tex)
unreal.log(f'[kit] 贴图新导入 {len(tasks)} 张')

# ---------------------------------------------------------------- 材质实例
mats = {}
for mname, info in data['materials'].items():
    iname = 'MI_' + clean(mname)
    path = f'{DEST}/Materials/{iname}'
    mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
        iname, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)
    for role, f in info['textures'].items():
        param = {'Diffuse': 'Diffuse', 'Spec': 'Spec', 'Gloss': 'Gloss', 'Normal': 'Normal'}.get(role)
        if param:
            mel.set_material_instance_texture_parameter_value(
                mi, param, eas.load_asset(f'{DEST}/Textures/T_' + clean(os.path.splitext(f)[0])))
    if 'metal' in mname.lower():
        mel.set_material_instance_scalar_parameter_value(mi, 'Metallic', 0.4)
    eas.save_loaded_asset(mi)
    mats[clean(mname)] = mi

# ---------------------------------------------------------------- 网格
if not eas.does_directory_exist(f'{DEST}/Meshes') or not eas.list_assets(f'{DEST}/Meshes', recursive=False):
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
    tools.import_asset_tasks([task(f'{SRC}/groups.fbx', f'{DEST}/Meshes', options=ui)])
n = 0
for path in eas.list_assets(f'{DEST}/Meshes', recursive=False, include_folder=False):
    a = eas.load_asset(path)
    if not isinstance(a, unreal.StaticMesh):
        continue
    nan = a.get_editor_property('nanite_settings')
    nan.enabled = True
    a.set_editor_property('nanite_settings', nan)
    bs = a.get_editor_property('body_setup')
    if bs:
        bs.set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm = a.get_editor_property('static_materials')
    for s in sm:
        hit = mats.get(clean(str(s.get_editor_property('material_slot_name'))))
        if hit:
            s.set_editor_property('material_interface', hit)
    a.set_editor_property('static_materials', sm)
    eas.save_loaded_asset(a)
    n += 1
unreal.log(f'[kit] {DEST}：网格 {n} 个，材质实例 {len(mats)} 个')
