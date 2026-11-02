"""11-02 去组件收尾（kind=py）：确认 MI_Kit_Hydraulics / MI_Kit_Pipes 已经不带贴图参数（母材质是 MI_Robot_Frame / MI_Robot_Rubber），
再删组件贴图 T_Kit_*（资产注册表的引用关系要保存后才刷新，所以上一步没删掉）。日志 [kfclean]。"""
import unreal

eal = unreal.EditorAssetLibrary
log = lambda *a: unreal.log_warning('[kfclean] ' + ' '.join(str(x) for x in a))
ok = True
for slot in ('MI_Kit_Hydraulics', 'MI_Kit_Pipes'):
    mi = unreal.load_asset(f'/Game/Robot/Materials/{slot}')
    par = mi.get_editor_property('parent') if mi else None
    tex = [p for p in (mi.get_editor_property('texture_parameter_values') if mi else [])]
    log(slot, '母材质', par.get_name() if par else None, '贴图参数', len(tex))
    if tex or par is None or par.get_name().startswith('M_WeaponPBR'):
        ok = False
if not ok:
    log('材质还没换好，不删贴图')
else:
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/Robot/Textures', recursive=True):
        if str(a.asset_name).startswith('T_Kit_'):
            p = str(a.package_name)
            log('删', p, eal.delete_asset(p))
log('完成')
