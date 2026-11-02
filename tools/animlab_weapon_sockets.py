"""在动作草稿工程（D:/RobotSlice_AnimLab）里跑：列出各动作包人形上的武器插槽（挂在哪根骨头、相对位置和朝向），
以及包里自带的武器模型的包围盒（看刀身沿哪根轴、原点在哪），给游戏里各把刀的握持姿势用。
UnrealEditor-Cmd AnimLab.uproject -run=pythonscript -script="D:/RobotSlice/tools/animlab_weapon_sockets.py" -nullrhi"""
import unreal

PACKS = ['GreatSword', 'HammerAnimationSet', 'LongswordAnimsetPro', 'SwordAnimsetPro', 'TwinSwordAnimsetBase', 'WMAnimation']
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for pack in PACKS:
    datas = reg.get_assets_by_path(f'/Game/{pack}', True)
    for d in datas:
        cls = str(d.asset_class_path.asset_name)
        if cls not in ('SkeletalMesh', 'StaticMesh'):
            continue
        a = d.get_asset()
        name = str(d.package_name)
        if cls == 'SkeletalMesh':
            n = a.num_sockets()
            socks = []
            for i in range(n):
                s = a.get_socket_by_index(i)
                if not s:
                    continue
                l = s.get_editor_property('relative_location')
                r = s.get_editor_property('relative_rotation')
                socks.append(f'{s.get_editor_property("socket_name")}@{s.get_editor_property("bone_name")} 位置({l.x:.1f},{l.y:.1f},{l.z:.1f}) 旋转(P{r.pitch:.1f},Y{r.yaw:.1f},R{r.roll:.1f})')
            b = a.get_bounds().box_extent if hasattr(a, 'get_bounds') else None
            unreal.log(f'[sock] {name} 骨骼网格 插槽 {len(socks)}：' + ' | '.join(socks))
        else:
            bb = a.get_bounding_box()
            unreal.log(f'[sock] {name} 静态网格 范围 ({bb.min.x:.0f},{bb.min.y:.0f},{bb.min.z:.0f}) ~ ({bb.max.x:.0f},{bb.max.y:.0f},{bb.max.z:.0f})')
unreal.log('[sock] 完成')
