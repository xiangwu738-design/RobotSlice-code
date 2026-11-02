"""在 RobotSlice 工程里跑：导入战斗用的武器模型和音效。
- SourceAssets/weapons/*.fbx → /Game/Weapons/Meshes，材质槽名就是机体材质实例名，直接挂 /Game/Robot/Materials 里的同名实例
- SourceAssets/sfx/*.wav → /Game/Audio/Combat（boost_loop、rain_loop 设为循环），再建一个衰减设置 SA_Combat
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="ue_import_combat_assets.py" -AllowCommandletRendering -RenderOffscreen
"""
import os

import unreal

eas = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'D:/RobotSlice/SourceAssets'


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


# ---------------------------------------------------------------- 武器模型
ui = unreal.FbxImportUI()
ui.set_editor_property('import_mesh', True)
ui.set_editor_property('import_as_skeletal', False)
ui.set_editor_property('import_materials', False)
ui.set_editor_property('import_textures', False)
ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)
ui.static_mesh_import_data.set_editor_property('combine_meshes', True)
ui.static_mesh_import_data.set_editor_property('auto_generate_collision', False)
files = sorted(f for f in os.listdir(f'{SRC}/weapons') if f.endswith('.fbx'))
tools.import_asset_tasks([task(f'{SRC}/weapons/{f}', '/Game/Weapons/Meshes', os.path.splitext(f)[0], ui) for f in files])
for f in files:
    mesh = eas.load_asset(f'/Game/Weapons/Meshes/{os.path.splitext(f)[0]}')
    sm = mesh.get_editor_property('static_materials')
    for s in sm:
        slot = str(s.get_editor_property('material_slot_name'))
        mi = eas.load_asset(f'/Game/Robot/Materials/{slot}')
        if mi:
            s.set_editor_property('material_interface', mi)
    mesh.set_editor_property('static_materials', sm)
    eas.save_loaded_asset(mesh)
    b = mesh.get_bounding_box()
    unreal.log(f'[combat] 武器 {mesh.get_name()} 尺寸 {(b.max - b.min) / 100} 米，材质 {[str(s.get_editor_property("material_slot_name")) for s in sm]}')

# ---------------------------------------------------------------- 曳光弹材质：机体母材质上的纯发光实例
mel = unreal.MaterialEditingLibrary
path = '/Game/Weapons/Materials/MI_Tracer'
mi = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
    'MI_Tracer', '/Game/Weapons/Materials', unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
mi.set_editor_property('parent', eas.load_asset('/Game/Robot/Materials/M_RobotMaster'))
mel.set_material_instance_vector_parameter_value(mi, 'BaseColor', unreal.LinearColor(0, 0, 0, 1))
mel.set_material_instance_vector_parameter_value(mi, 'Emissive', unreal.LinearColor(1.0, 0.42, 0.12, 1))
mel.set_material_instance_scalar_parameter_value(mi, 'EmissiveStrength', 60.0)
eas.save_loaded_asset(mi)

# ---------------------------------------------------------------- 音效
wavs = sorted(f for f in os.listdir(f'{SRC}/sfx') if f.endswith('.wav'))
tools.import_asset_tasks([task(f'{SRC}/sfx/{f}', '/Game/Audio/Combat', 'S_' + os.path.splitext(f)[0]) for f in wavs])
for f in wavs:
    s = eas.load_asset('/Game/Audio/Combat/S_' + os.path.splitext(f)[0])
    if 'loop' in f:
        s.set_editor_property('looping', True)
        eas.save_loaded_asset(s)
path = '/Game/Audio/SA_Combat'
att = eas.load_asset(path) if eas.does_asset_exist(path) else tools.create_asset(
    'SA_Combat', '/Game/Audio', unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
st = att.get_editor_property('attenuation')
st.set_editor_property('falloff_distance', 9000.0)
st.set_editor_property('attenuation_shape_extents', unreal.Vector(600.0, 0.0, 0.0))
att.set_editor_property('attenuation', st)
eas.save_loaded_asset(att)
unreal.log(f'[combat] 音效 {len(wavs)} 个，衰减 {path}')
