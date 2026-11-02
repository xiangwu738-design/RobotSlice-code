"""在 RobotSlice 工程里跑：导入绑在模板人形骨架上的机体 FBX，建母材质和各部位材质实例并挂上。
--kit 导出的机体还带素材包贴图材质（MI_Kit_*，贴图在 lookdev/kit/tex/ue，用武器的贴图 PBR 母材质）和 8 根减震小骨头（damper_*，
导入时并进 SK_Mannequin 骨架；UMechMeshComponent 每帧让每对互相对准）。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_import_robot.py 机体.fbx"
"""
import sys

import unreal

FBX = sys.argv[1]
DEST = '/Game/Robot'
SKELETON = '/Game/Characters/Mannequins/Meshes/SK_Mannequin'
PHYSICS = '/Game/Characters/Mannequins/Rigs/PA_Mannequin'

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
KIT_TEX = 'D:/RobotSlice/lookdev/kit/tex/ue'
WEAPON_PBR = '/Game/Weapons/Materials/M_WeaponPBR'


def lin(hexstr):
    h = hexstr.lstrip('#')
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]


# ---- 母材质：底色 / 金属度 / 粗糙度 / 自发光，加一点按世界坐标的粗糙度起伏，免得像塑料
def make_master():
    path = f'{DEST}/Materials/M_RobotMaster'
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    m = tools.create_asset('M_RobotMaster', f'{DEST}/Materials', unreal.Material, unreal.MaterialFactoryNew())
    P = unreal.MaterialProperty

    def node(cls, x, y):
        return mel.create_material_expression(m, cls, x, y)

    base = node(unreal.MaterialExpressionVectorParameter, -600, -200)
    base.set_editor_property('parameter_name', 'BaseColor')
    base.set_editor_property('default_value', unreal.LinearColor(0.1, 0.12, 0.14, 1))
    metal = node(unreal.MaterialExpressionScalarParameter, -600, 0)
    metal.set_editor_property('parameter_name', 'Metallic')
    rough = node(unreal.MaterialExpressionScalarParameter, -600, 100)
    rough.set_editor_property('parameter_name', 'Roughness')
    rough.set_editor_property('default_value', 0.5)
    var = node(unreal.MaterialExpressionScalarParameter, -600, 200)
    var.set_editor_property('parameter_name', 'RoughnessVariation')
    var.set_editor_property('default_value', 0.12)
    noise = node(unreal.MaterialExpressionNoise, -900, 250)
    noise.set_editor_property('scale', 0.08)
    noise.set_editor_property('levels', 4)
    noise.set_editor_property('output_min', -1.0)
    noise.set_editor_property('output_max', 1.0)
    mul = node(unreal.MaterialExpressionMultiply, -400, 220)
    add = node(unreal.MaterialExpressionAdd, -250, 120)
    mel.connect_material_expressions(noise, '', mul, 'A')
    mel.connect_material_expressions(var, '', mul, 'B')
    mel.connect_material_expressions(rough, '', add, 'A')
    mel.connect_material_expressions(mul, '', add, 'B')
    emis = node(unreal.MaterialExpressionVectorParameter, -600, 350)
    emis.set_editor_property('parameter_name', 'Emissive')
    emis.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 1))
    estr = node(unreal.MaterialExpressionScalarParameter, -600, 500)
    estr.set_editor_property('parameter_name', 'EmissiveStrength')
    emul = node(unreal.MaterialExpressionMultiply, -300, 400)
    mel.connect_material_expressions(emis, '', emul, 'A')
    mel.connect_material_expressions(estr, '', emul, 'B')
    mel.connect_material_property(base, '', P.MP_BASE_COLOR)
    mel.connect_material_property(metal, '', P.MP_METALLIC)
    mel.connect_material_property(add, '', P.MP_ROUGHNESS)
    mel.connect_material_property(emul, '', P.MP_EMISSIVE_COLOR)
    m.set_editor_property('used_with_skeletal_mesh', True)
    mel.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m


# 和效果图同一套配色：(底色, 金属度, 粗糙度, 自发光色, 自发光强度)
SLOTS = {
    'Paint': ('#5a636c', 0.0, 0.5, None, 0), 'PaintDark': ('#383f46', 0.0, 0.55, None, 0),
    'Accent': ('#cf5f1b', 0.0, 0.46, None, 0), 'Frame': ('#3d4044', 1.0, 0.42, None, 0),
    'Chrome': ('#c9cdd0', 1.0, 0.14, None, 0), 'Rubber': ('#151617', 0.0, 0.8, None, 0),
    'Plastic': ('#1c1e21', 0.0, 0.45, None, 0), 'Lens': ('#050607', 0.0, 0.05, None, 0),
    'Glow': ('#000000', 0.0, 0.5, '#a8e4ff', 20.0), 'Stencil': ('#d6d6d0', 0.0, 0.6, None, 0),
    'Nozzle': ('#2a2826', 1.0, 0.55, None, 0), 'Hazard': ('#cf5f1b', 0.0, 0.5, None, 0),
    'Label': ('#e2e1da', 0.0, 0.55, None, 0), 'Ink': ('#121212', 0.0, 0.6, None, 0), 'Lamp': ('#000000', 0.0, 0.5, '#f2f5ff', 15.0),
}


def make_instance(master, slot):
    name = f'MI_Robot_{slot}'
    path = f'{DEST}/Materials/{name}'
    mi = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else \
        tools.create_asset(name, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)
    color, metal, rough, ecol, estr = SLOTS[slot]
    mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', unreal.LinearColor(*lin(color), 1))
    mel.set_material_instance_scalar_parameter_value(mi, 'Metallic', metal)
    mel.set_material_instance_scalar_parameter_value(mi, 'Roughness', rough)
    if ecol:
        mel.set_material_instance_vector_parameter_value(mi, 'Emissive', unreal.LinearColor(*lin(ecol), 1))
        mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', estr)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mi


def make_kit_instance(slot):
    """素材包贴图材质：MI_Kit_Hydraulics / MI_Kit_Pipes，母材质用武器的 M_WeaponPBR（贴图 PBR，要打上骨骼网格用的标记）。"""
    tag = slot.replace('MI_Kit_', '')
    master = unreal.load_asset(WEAPON_PBR)
    if master is None:
        unreal.log_error(f'[robot] 没有 {WEAPON_PBR}，{slot} 用不了贴图材质')
        return None
    if not master.get_editor_property('used_with_skeletal_mesh'):
        master.set_editor_property('used_with_skeletal_mesh', True)
        mel.recompile_material(master)
        eal.save_loaded_asset(master, False)
    TC = unreal.TextureCompressionSettings
    params = {}
    for role, param, srgb, comp in (('BaseColor', 'BaseColorTex', True, TC.TC_DEFAULT), ('Normal', 'NormalTex', False, TC.TC_NORMALMAP),
                                    ('Metallic', 'MetallicTex', False, TC.TC_MASKS), ('Roughness', 'RoughnessTex', False, TC.TC_MASKS)):
        t = unreal.AssetImportTask()
        t.filename = f'{KIT_TEX}/{tag}_{role}.png'
        t.destination_path = f'{DEST}/Textures'
        t.destination_name = f'T_Kit_{tag}_{role}'
        t.automated = True
        t.replace_existing = True
        t.save = False
        tools.import_asset_tasks([t])
        tex = unreal.load_asset(f'{DEST}/Textures/T_Kit_{tag}_{role}')
        tex.set_editor_property('srgb', srgb)
        tex.set_editor_property('compression_settings', comp)
        if role == 'Normal':
            tex.set_editor_property('flip_green_channel', True)  # Blender 素材是 OpenGL 法线（绿通道朝上），UE 是 DirectX
        eal.save_loaded_asset(tex, False)
        params[param] = tex
    path = f'{DEST}/Materials/{slot}'
    mi = unreal.load_asset(path) if eal.does_asset_exist(path) else         tools.create_asset(slot, f'{DEST}/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', master)
    for param, tex in params.items():
        mel.set_material_instance_texture_parameter_value(mi, param, tex)
    mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', 0.0)
    eal.save_loaded_asset(mi, False)
    return mi


# ---- 导入骨骼网格（用模板人形的骨架，模板动画直接能用）
# 用老的 FBX 导入器：新的（Interchange）在命令行里给骨架并进减震骨头时，会一直等 SKM_Manny_Simple 重建、卡死不动
for cvar in ('Interchange.FeatureFlags.Import.FBX', 'r.Interchange.FeatureFlags.Import.FBX'):
    unreal.SystemLibrary.execute_console_command(None, f'{cvar} 0')
task = unreal.AssetImportTask()
task.filename = FBX
task.destination_path = f'{DEST}/Meshes'
task.destination_name = 'SKM_Robot_v3'
task.automated = True
task.replace_existing = True
task.save = True
ui = unreal.FbxImportUI()
ui.import_mesh = True
ui.import_as_skeletal = True
ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
ui.skeleton = unreal.load_asset(SKELETON)
ui.import_animations = False
ui.import_materials = False
ui.import_textures = False
ui.create_physics_asset = False
ui.skeletal_mesh_import_data.set_editor_property('import_morph_targets', False)
ui.skeletal_mesh_import_data.set_editor_property('normal_import_method', unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
task.options = ui
tools.import_asset_tasks([task])
mesh = unreal.load_asset(f'{DEST}/Meshes/SKM_Robot_v3')
if mesh is None:
    unreal.log_error('[robot] 导入失败：没有生成 SKM_Robot_v3（看上面的骨架不匹配之类的错误）')
    raise SystemExit(1)

master = make_master()
mats = list(mesh.get_editor_property('materials'))
for i, sm in enumerate(mats):
    # 覆盖导入时老导入器会沿用旧网格的槽名（新多出来的 MI_Kit_Pipes 顶了旧的 MI_Robot_Chrome 的名字），按 FBX 里的原名来
    name = str(sm.get_editor_property('imported_material_slot_name')) or str(sm.get_editor_property('material_slot_name'))
    sm.set_editor_property('material_slot_name', name)
    mats[i] = sm
    slot = name.replace('MI_Robot_', '')
    if slot in SLOTS:
        sm.set_editor_property('material_interface', make_instance(master, slot))
        mats[i] = sm
    elif name.startswith('MI_Kit_'):
        mi = make_kit_instance(name)
        if mi:
            sm.set_editor_property('material_interface', mi)
            mats[i] = sm
mesh.set_editor_property('materials', mats)
pa = unreal.load_asset(PHYSICS)
if pa:
    mesh.set_editor_property('physics_asset', pa)
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
# 导入时新骨头（减震 damper_*）并进了模板骨架，骨架也要存，否则只在这次的内存里
skeleton = mesh.get_editor_property('skeleton')
unreal.log(f'[robot] 存骨架 {skeleton.get_path_name()}：{eal.save_loaded_asset(skeleton, False)}')
b = mesh.get_bounds()
unreal.log(f'[robot] 导入完成 {mesh.get_path_name()} 骨架={mesh.get_editor_property("skeleton").get_name()} '
           f'材质槽={[str(m.get_editor_property("material_slot_name")) for m in mats]} 包围盒半径={b.sphere_radius:.1f}')
