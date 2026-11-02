"""列出游戏里用到的特效（Niagara）引用的材质：混合模式、着色模型、自发光有没有接、父材质，查「白天特效发黑」用。
UnrealEditor-Cmd RobotSlice.uproject -run=pythonscript -script="D:/RobotSlice/tools/ue_fx_materials.py" -nullrhi"""
import unreal

SYSTEMS = [
    '/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01',
    '/Game/AAAVFX/Course_01/Particles/Ex_01_Sword_Trail/Trail_01',
    '/Game/AAAVFX/Course_01/Particles/Ex_02_Earth_Explosion/Exp_01',
    '/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01',
    '/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_02',
    '/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01',
    '/Game/AAAVFX/Course_04/Particles/Ex_03_Muzzle',
]
reg = unreal.AssetRegistryHelpers.get_asset_registry()
opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
seen = {}


def walk(pkg, depth):
    """沿依赖往下找材质（发射器资源也会引用材质），最多三层"""
    if depth > 3:
        return
    for dep in reg.get_dependencies(pkg, opts) or []:
        d = str(dep)
        if not d.startswith('/Game/') or d in seen:
            continue
        datas = reg.get_assets_by_package_name(d)
        cls = str(datas[0].asset_class_path.asset_name) if datas else '?'
        seen[d] = cls
        if cls in ('NiagaraEmitter', 'NiagaraSystem', 'MaterialInstanceConstant'):
            walk(d, depth + 1)


for s in SYSTEMS:
    walk(s, 0)
mel = unreal.MaterialEditingLibrary
for pkg, cls in sorted(seen.items()):
    if cls not in ('Material', 'MaterialInstanceConstant'):
        continue
    asset = unreal.load_asset(pkg)
    base = asset.get_base_material() if cls == 'MaterialInstanceConstant' else asset
    blend = base.get_editor_property('blend_mode')
    shading = base.get_editor_property('shading_model')
    emis = mel.get_material_property_input_node(base, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    base_col = mel.get_material_property_input_node(base, unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.log(f'[fxmat] {cls:26s} {pkg}  底材质 {base.get_path_name().split(".")[0]}  混合 {blend}  着色 {shading}  '
               f'自发光 {type(emis).__name__ if emis else "无"}  底色 {type(base_col).__name__ if base_col else "无"}')
unreal.log(f'[fxmat] 共 {len(seen)} 个依赖')
