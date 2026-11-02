"""从重定向好的动作包（Inbox/_retarget/<包>，tools/animlab_retarget_uasset.py 的输出）里挑出游戏要用的那些，拷到 Inbox/_retarget_pick/<组>/
（只带这些动作的 locomotion.json），再在 RobotSlice 里一次导入：tools/ue_import_anim_sets.py。
游戏里按名字加载（UMechAnimInstance 的片段表），这里改了名字那边也要改。
py -3 tools/anim_pick.py"""
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Inbox', '_retarget')
DST = os.path.join(ROOT, 'Inbox', '_retarget_pick')

# 组名（= /Game/Robot/Anims/<组>）: (源包, [动作名])
PICKS = {
    # 快刀（素材剑 11）：长剑包，双手
    'Longsword': ('Longsword', ['Longs_Idle1', 'Longs_Attack_R', 'Longs_Attack_L', 'Longs_Attack_RD', 'Longs_Attack_D', 'Longs_Attack_Thrust',
                                'Longs_BlockStart', 'Longs_BlockLoop', 'Longs_BlockImpact', 'Longs_BlockEnd']),
    # 单手刀（13）
    'SwordPro': ('SwordPro', ['Sword1h_Idle', 'Attack_Place_fast_Rdown_1', 'Attack_Place_fast_Ldown_1', 'Attack_Place_fast_T_1', 'Attack_Place_med_R_1',
                              'Attack_Move_strong_Rdown_1', 'Sword1h_Guarding_Idle_1', 'Sword1h_Parry_Mid']),
    # 大剑（14）
    'GreatSword': ('GreatSword', ['GreatSword_Common_Idle_InPlace', 'GreatSword_Attack01_InPlace', 'GreatSword_Attack02_InPlace', 'GreatSword_Attack03_InPlace',
                                  'GreatSword_Attack04_InPlace', 'GreatSword_Attack06_InPlace', 'GreatSword_Defense_Start_InPlace',
                                  'GreatSword_Defense_Loop_InPlace', 'GreatSword_Defense_Hit_InPlace', 'GreatSword_Defense_End_InPlace']),
    # 大剑的待机：用战斗待机（剑一直在右手上）；原来那个 Common_Idle 里剑不在手上，挂在右手的剑会摆歪
    'GreatSwordIdle': ('GreatSword', ['GreatSword_Idle_InPlace']),
    # 双刀（16）
    'TwinSword': ('TwinSword', ['TwinSword_Idle_InPlace', 'TwinSword_Attack01_InPlace', 'TwinSword_Attack02_InPlace', 'TwinSword_Attack03_InPlace',
                                'TwinSword_Attack07_InPlace', 'TwinSword_Attack09_InPlace']),
    # 钝器（20）：锤子包
    'Hammer': ('Hammer', ['Idle_InPlace', 'Attack1_InPlace', 'Attack2_InPlace', 'Attack3_InPlace', 'HeavyAttack1_InPlace', 'LeapAttack_InPlace',
                          'BlockStart_InPlace', 'BlockLoop_InPlace', 'BlockImpact_InPlace', 'BlockEnd_InPlace']),
    # 投掷匕首（03）：双匕首包的待机 + 手枪包里扔手雷的动作
    # 10-30 用户：匕首不能格挡、右键近身挥刀 → 加四段轻击 + 一个重击（双匕首包，单手反握照样能用）
    'Dagger': ('TwinDaggers', ['TwinDaggers_Idle_InPlace', 'TwinDaggers_Attack1_1_InPlace', 'TwinDaggers_Attack1_2_InPlace',
                               'TwinDaggers_Attack3_InPlace', 'TwinDaggers_Attack14_InPlace', 'TwinDaggers_Attack2_InPlace']),
    'Throw': ('Throw', ['Pistol_Grenade_Throw_Single_InPlace']),
    # 长枪（步枪、冲锋枪、霰弹枪、精准步枪、狙击枪共用）
    'Rifle': ('RifleAnimsetPro', ['Rifle_Idle', 'Aim_U', 'Aim_CC', 'Aim_D', 'Rifle_Reload_2', 'Rifle_ShootOnce', 'EquipRifle', 'HolsterRifle']),
    # 双持手枪
    'DualPistol': ('DualPistol', ['Dual_Aim_IDLE', 'Dual_Combat_IDLE', 'Dual_Aim_FIRE_LT', 'Dual_Aim_FIRE_RT', 'Dual_Reload', 'Dual_TakeOut', 'Dual_PutAway']),
    # 飞行（推进器在空中喷射 / 滑翔时）
    'Flight': ('SuperheroFlight', ['A_Flight_Idle_A', 'A_Flight_FastMove_A', 'A_Flight_Hover_Start_A', 'A_SuperheroLanding_A',
                                   'A_Flight_Dodge_A_L', 'A_Flight_Dodge_A_R', 'A_Flight_Dodge_A_U', 'A_Flight_Dodge_A_D']),
    # 起步、急停
    'StartsStops': ('StartsStops', ['SK_stand_to_jog_0_v2', 'SK_jog_to_stand_0_v2']),
    # 10-31 下落循环：换掉 Game Animation Sample 的 M_Neutral_Jump_Loop_Fall（膝盖折过 180 度、脚踝转 180–270 度、单帧转 110–178 度，见 README「移动动作」）。
    # UE4 模板人形的跳跃循环（飞行包里带着，已重定向）：左腿收起、右腿垂着，膝 74–115、髋 58–84 度，每帧最多转 5 度；
    # 片段首尾接不上（最后一帧膝盖一下跳 25 度），游戏里不首尾循环，取中间一段平滑地来回播（UMechAnimInstance 的下落片段时间）
    'Fall': ('SuperheroFlight', ['ThirdPersonJump_Loop']),
    # 10-31 试过把匕首第三段换成 Attack1_3（和 1_1 / 1_2 是同一套连招切开的，1_2 的出口姿势 = 1_3 第 0 帧），游戏里量下来没变顺，没用上；
    # 导入的资源移到了 D:\归档\RobotSlice_刀剑动作旧截图_20261031\DaggerChain。要再试：加回 'DaggerChain': ('TwinDaggers', ['TwinDaggers_Attack1_3_InPlace'])
}

total = missing = 0
for group, (pack, names) in PICKS.items():
    src = os.path.join(SRC, pack)
    dst = os.path.join(DST, group)
    meta_path = os.path.join(src, 'locomotion.json')
    if not os.path.isfile(meta_path):
        print('[pick] 跳过 %s：%s 还没转换' % (group, pack))
        missing += len(names)
        continue
    meta = json.load(open(meta_path, encoding='utf-8'))
    os.makedirs(dst, exist_ok=True)
    keep = {}
    for n in names:
        # 有的包重定向时拆成了原地 / 带位移两版（名字后缀 _InPlace / _Root）：取原地版，拷过来时去掉后缀（游戏里按不带后缀的名字找）
        src_name = n if os.path.isfile(os.path.join(src, n + '.fbx')) else n + '_InPlace'
        f = os.path.join(src, src_name + '.fbx')
        if not os.path.isfile(f):
            print('[pick] 缺 %s/%s' % (pack, n))
            missing += 1
            continue
        shutil.copy2(f, os.path.join(dst, n + '.fbx'))
        keep[n] = meta.get(src_name, {'length': 0.0, 'root_delta': [0, 0, 0], 'speed': 0.0, 'markers': []})
        total += 1
    json.dump(keep, open(os.path.join(dst, 'locomotion.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    print('[pick] %s ← %s：%d 个' % (group, pack, len(keep)))
print('[pick] 共 %d 个，缺 %d 个 → %s' % (total, missing, DST))
