"""在动作草稿工程里跑：算出游戏里各把刀挂在右手（hand_r）上的变换。
每个动作包：武器骨头 ik_hand_gun 相对右手的变换（原动作里取一帧，握法全程不变）× 包里武器模型的轴向 → 换成我们的刀模型
（我们的约定：刀身沿 +X、刃口朝 -Z、原点在握把）。锤子的原点在柄尾、手是滑握，握点取右手投到锤柄上的位置。
输出 C++ 里直接用的位置和四元数。
UnrealEditor-Cmd AnimLab.uproject -run=pythonscript -script="D:/RobotSlice/tools/animlab_blade_mounts.py" -nullrhi"""
import unreal

lib = unreal.AnimationLibrary
reg = unreal.AssetRegistryHelpers.get_asset_registry()
M = unreal.MathLibrary
V = unreal.Vector

# 名字: (包目录, 动作, 包里武器模型的刀身轴, 我们的 +Z（刀背）对应的轴, 武器骨头上的插槽偏移, 握点取右手投到刀身轴上)
PACKS = {
    'Longsword': ('/Game/LongswordAnimsetPro', 'Longs_Idle1', V(0, 1, 0), V(0, 0, 1), V(0, 0, 0), False),
    'SwordPro': ('/Game/SwordAnimsetPro', 'Sword1h_Idle', V(0, 0, 1), V(0, 1, 0), V(0, 0, -3), False),
    'GreatSword': ('/Game/GreatSword', 'GreatSword_Idle', V(1, 0, 0), V(0, 0, 1), V(0, 0, 0), False),
    'TwinSword': ('/Game/TwinSwordAnimsetBase', 'TwinSword_Idle', V(1, 0, 0), V(0, 0, 1), V(0, 0, 0), False),
    'Hammer': ('/Game/HammerAnimationSet', 'Idle_InPlace', V(0, 0, 1), V(0, 1, 0), V(0, 0, 0), True),
    'Dagger': ('/Game/WMAnimation', 'TwinDaggers_Idle', V(1, 0, 0), V(0, 0, 1), V(0, 0, 0), False),
}


def cs(seq, bone, t):
    path = list(lib.find_bone_path_to_root(seq, bone))
    xf = unreal.Transform()
    for b in reversed(path):
        xf = M.compose_transforms(lib.get_bone_pose_for_time(seq, b, t, False), xf)
    return xf


for name, (root, anim, blade_axis, up_axis, socket_off, on_axis) in PACKS.items():
    d = next((d for d in reg.get_assets_by_path(root, True) if str(d.asset_name) == anim and str(d.asset_class_path.asset_name) == 'AnimSequence'), None)
    if not d:
        unreal.log(f'[mount] {name} 找不到 {anim}')
        continue
    seq = d.get_asset()
    hand = cs(seq, 'hand_r', 0.0)
    gun = cs(seq, 'ik_hand_gun', 0.0)
    weapon_in_hand = M.make_relative_transform(gun, hand)       # 武器骨头在右手空间
    origin = socket_off
    if on_axis:
        # 右手在武器空间的位置，投到刀身轴上（锤柄穿过手掌，不是手腕）
        hand_in_weapon = M.invert_transform(weapon_in_hand).translation
        along = hand_in_weapon.x * blade_axis.x + hand_in_weapon.y * blade_axis.y + hand_in_weapon.z * blade_axis.z
        origin = V(blade_axis.x * along, blade_axis.y * along, blade_axis.z * along)
    rot = M.make_rot_from_xz(blade_axis, up_axis)                # 我们的 X → 包里刀身轴，Z → 刀背轴
    ours_in_weapon = unreal.Transform(origin, rot, V(1, 1, 1))
    ours_in_hand = M.compose_transforms(ours_in_weapon, weapon_in_hand)
    p = ours_in_hand.translation
    q = ours_in_hand.rotation
    x = q.get_axis_x()
    z = q.get_axis_z()
    unreal.log(f'[mount] {name}: 位置 FVector({p.x:.2f}f, {p.y:.2f}f, {p.z:.2f}f)  四元数 FQuat({q.x:.5f}f, {q.y:.5f}f, {q.z:.5f}f, {q.w:.5f}f)  '
               f'刀身沿手 ({x.x:.2f},{x.y:.2f},{x.z:.2f}) 刀背沿手 ({z.x:.2f},{z.y:.2f},{z.z:.2f})')
unreal.log('[mount] 完成')
