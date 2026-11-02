import unreal
for p in ['/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal.WorldAlignedNormal', '/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedNormal', '/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture']:
    a = unreal.load_object(None, p) if '.' in p.split('/')[-1] else unreal.EditorAssetLibrary.load_asset(p)
    unreal.log(f'[fn] {p} -> {a}')
