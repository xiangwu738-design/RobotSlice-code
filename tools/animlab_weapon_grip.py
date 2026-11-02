"""在动作草稿工程里跑：从各动作包的原动作（UE4 人形）里算出「武器骨头相对右手」的变换——游戏里的机体骨架没有这些武器骨头，
只能把武器挂在右手上，按这个相对变换摆。每个动作取几帧，看握法是不是一直不变（双手武器换手时会变）。
UnrealEditor-Cmd AnimLab.uproject -run=pythonscript -script="D:/RobotSlice/tools/animlab_weapon_grip.py" -nullrhi"""
import unreal

lib = unreal.AnimationLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
STANDARD = {'root', 'pelvis', 'spine_01', 'spine_02', 'spine_03', 'clavicle_l', 'upperarm_l', 'lowerarm_l', 'hand_l', 'clavicle_r', 'upperarm_r',
            'lowerarm_r', 'hand_r', 'neck_01', 'head', 'thigh_l', 'calf_l', 'foot_l', 'ball_l', 'thigh_r', 'calf_r', 'foot_r', 'ball_r',
            'ik_foot_root', 'ik_foot_l', 'ik_foot_r', 'ik_hand_root', 'ik_hand_l', 'ik_hand_r'}
# 组：动作包目录、要看的动作名（取第一个找到的）
GROUPS = {
    'GreatSword': ('/Game/GreatSword', ['GreatSword_Idle', 'GreatSword_Common_Idle', 'GreatSword_Attack01', 'GreatSword_Attack06']),
    'Hammer': ('/Game/HammerAnimationSet', ['Idle_InPlace', 'Attack1_InPlace', 'LeapAttack_InPlace']),
    'Longsword': ('/Game/LongswordAnimsetPro', ['Longs_Idle1', 'Longs_Attack_R', 'Longs_Attack_Thrust']),
    'SwordPro': ('/Game/SwordAnimsetPro', ['Sword1h_Idle', 'Attack_Place_fast_Rdown_1']),
    'TwinSword': ('/Game/TwinSwordAnimsetBase', ['TwinSword_Idle', 'TwinSword_Attack01']),
    'TwinDaggers': ('/Game/WMAnimation', ['TwinDaggers_Idle']),
}


def cs(seq, bone, t):
    """骨头在组件空间里的变换：沿父链把局部变换乘上去"""
    path = list(lib.find_bone_path_to_root(seq, bone))
    xf = unreal.Transform()
    for b in reversed(path):
        local = lib.get_bone_pose_for_time(seq, b, t, False)
        xf = unreal.MathLibrary.compose_transforms(local, xf)
    return xf


for group, (root, names) in GROUPS.items():
    datas = [d for d in reg.get_assets_by_path(root, True) if str(d.asset_class_path.asset_name) == 'AnimSequence']
    by_name = {str(d.asset_name): d for d in datas}
    for want in names:
        d = by_name.get(want)
        if not d:
            unreal.log(f'[grip] {group} 找不到 {want}')
            continue
        seq = d.get_asset()
        tracks = [str(n) for n in lib.get_animation_track_names(seq)]
        extra = [n for n in tracks if n not in STANDARD and not any(k in n for k in ('thumb', 'index', 'middle', 'ring', 'pinky', 'lowerarm_twist', 'upperarm_twist', 'calf_twist', 'thigh_twist'))]
        length = lib.get_sequence_length(seq)
        unreal.log(f'[grip] {group}/{want} {length:.2f}s 额外骨头 {extra}')
        for bone in [b for b in extra if 'ik_hand_gun' in b.lower() or 'weapon' in b.lower() or 'sword' in b.lower() or 'prop' in b.lower()][:3]:
            rows = []
            for k in range(5):
                t = length * k / 4.0
                hand = cs(seq, 'hand_r', t)
                w = cs(seq, bone, t)
                rel = unreal.MathLibrary.make_relative_transform(w, hand)
                p, r = rel.translation, rel.rotation.rotator()
                hand_l = cs(seq, 'hand_l', t)
                rel_l = unreal.MathLibrary.make_relative_transform(hand_l, w).translation
                rows.append(f't{t:.2f}: 位置({p.x:.1f},{p.y:.1f},{p.z:.1f}) 旋转(P{r.pitch:.0f},Y{r.yaw:.0f},R{r.roll:.0f}) 左手在武器里({rel_l.x:.0f},{rel_l.y:.0f},{rel_l.z:.0f})')
            unreal.log(f'[grip]   {bone} 相对 hand_r: ' + ' | '.join(rows))
hm = unreal.load_asset('/Game/HammerAnimationSet/Meshes/SK_HammerMesh')
if hm:
    b = hm.get_bounds()
    unreal.log(f'[grip] 锤子模型 中心 ({b.origin.x:.0f},{b.origin.y:.0f},{b.origin.z:.0f}) 半长 ({b.box_extent.x:.0f},{b.box_extent.y:.0f},{b.box_extent.z:.0f})')
unreal.log('[grip] 完成')
