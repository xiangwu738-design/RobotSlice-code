import unreal
mel = unreal.MaterialEditingLibrary
eas = unreal.EditorAssetLibrary
m = eas.load_asset('/Game/Env/Materials/M_WorldPBR')
for e in mel.get_material_expressions(m) if hasattr(mel, 'get_material_expressions') else []:
    pass
fn = unreal.load_object(None, '/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture')
tmp = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_Tmp', '/Game/Tmp', unreal.Material, unreal.MaterialFactoryNew())
fc = mel.create_material_expression(tmp, unreal.MaterialExpressionMaterialFunctionCall, 0, 0)
fc.set_editor_property('material_function', fn)
unreal.log(f'[dbg] has set_function {hasattr(fc, "set_function")}')
unreal.log(f'[dbg] inputs {mel.get_material_expression_input_names(fc)}')
unreal.log(f'[dbg] outputs {[str(o) for o in mel.get_material_expression_output_names(fc)] if hasattr(mel, "get_material_expression_output_names") else "?"}')
t = mel.create_material_expression(tmp, unreal.MaterialExpressionTextureObjectParameter, -300, 0)
for name in ('TextureObject', 'Texture Object', 'TextureObject (T2d)'):
    unreal.log(f'[dbg] connect {name}: {mel.connect_material_expressions(t, "", fc, name)}')
eas.delete_asset('/Game/Tmp/M_Tmp')
