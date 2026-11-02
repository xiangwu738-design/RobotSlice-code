# 受击闪光材质 /Game/Robot/Materials/M_HitFlash：叠加（Additive）、无光照，自发光 = 颜色 × 强度 × (0.35 + 菲涅尔)，
# 机体被打中时作为 Overlay Material 盖在整台机体上闪一下（C++ 里改 Flash / FlashColor 参数）。
# 用法：UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_make_hitflash.py"
import unreal

PATH = '/Game/Robot/Materials'
NAME = 'M_HitFlash'
FULL = PATH + '/' + NAME

MEL = unreal.MaterialEditingLibrary
if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    m = unreal.EditorAssetLibrary.load_asset(FULL)
    MEL.delete_all_material_expressions(m)
else:
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())

m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('used_with_skeletal_mesh', True)
m.set_editor_property('used_with_static_lighting', False)

color = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, -100)
color.set_editor_property('parameter_name', 'FlashColor')
color.set_editor_property('default_value', unreal.LinearColor(1.0, 0.55, 0.25, 1.0))

flash = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 100)
flash.set_editor_property('parameter_name', 'Flash')
flash.set_editor_property('default_value', 0.0)

fres = MEL.create_material_expression(m, unreal.MaterialExpressionFresnel, -900, 250)
fres.set_editor_property('exponent', 2.5)
fres.set_editor_property('base_reflect_fraction', 0.0)

base = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -900, 420)
base.set_editor_property('r', 0.35)

add = MEL.create_material_expression(m, unreal.MaterialExpressionAdd, -600, 300)
MEL.connect_material_expressions(fres, '', add, 'A')
MEL.connect_material_expressions(base, '', add, 'B')

mul1 = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 0)
MEL.connect_material_expressions(color, '', mul1, 'A')
MEL.connect_material_expressions(flash, '', mul1, 'B')

mul2 = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 100)
MEL.connect_material_expressions(mul1, '', mul2, 'A')
MEL.connect_material_expressions(add, '', mul2, 'B')
MEL.connect_material_property(mul2, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

MEL.recompile_material(m)
unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log('[hitflash] saved ' + FULL)
