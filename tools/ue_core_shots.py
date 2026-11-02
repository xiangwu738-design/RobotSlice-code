"""夺芯图新旧对比截图：对战开打后暂停，把地图里带「ShotCam」标签的 CameraActor 挪到各机位拍 HighResShot（运行时挪，不保存）。
机位按场地中心（带「CoreArena」标签的点）和场地半长 L 算，新旧图用同一套相对机位：
  1 远景斜俯（A 端后上方）  2 A 端出生点机体视角看中央  3 铸造台旁看 B 端  4 侧面斜俯  5 正上方俯视
UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Volcano?Core?Team=2" -game -MechAutoBattle -MechNoDraft -RenderOffscreen -unattended -nosound
  -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_core_shots.py"
环境变量：CORE_SHOTS_DIR 输出目录、CORE_SHOTS_L 场地半长（米，旧峡谷 130、新山谷 400）、CORE_SHOTS_PREFIX 文件名前缀
"""
import math
import os
import time

import unreal

OUT = os.environ.get('CORE_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/CoreShots').replace('\\', '/')
L = float(os.environ.get('CORE_SHOTS_L', '130'))
PREFIX = os.environ.get('CORE_SHOTS_PREFIX', '')
WAIT = float(os.environ.get('CORE_SHOTS_WAIT', '6.0'))
os.makedirs(OUT, exist_ok=True)
log = lambda *a: unreal.log('[coreshots] ' + ' '.join(str(x) for x in a))

# 名字, 机位 (x/L, y/L, 米), 看向 (x/L, y/L, 米), FOV, 高度算法：'agl' 离脚下地面，'center' 相对场地中心点（高空机位，脚下可能是崖外低地）
SHOTS = [
    ('1_远景斜俯', (-1.45, -0.55, 0.5 * L), (0.0, 0.0, 0.0), 70, 'center'),
    ('2_A端出生看中央', (-0.93, 0.0, 3.2), (0.0, 0.0, 3.0), 80, 'agl'),
    ('3_铸造台看B端', (-0.16, -0.1, 4.0), (0.7, 0.0, 3.0), 80, 'agl'),
    ('4_侧面斜俯', (0.0, -1.15, 0.38 * L), (0.0, 0.0, 0.0), 70, 'center'),
    ('5_正上方', (0.0, 0.001, 1.9 * L), (0.0, 0.0, 0.0), 60, 'center'),
]
st = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'done': False, 'world': None, 'seen_mech_t': None}


def ground_z(world, x, y, ignore):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 1000000), unreal.Vector(x, y, -1000000),
                                                 unreal.TraceTypeQuery.ECC_VISIBILITY, False, ignore, unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    if hit is None:
        return None
    t = hit.to_tuple()
    return t[4].z if t[0] else None


def tick(dt):
    if st['done']:
        return
    st['t'] += dt
    world = st['world']
    if world is None:
        # -game 里没有编辑器子系统，按关卡路径找 World
        st['world'] = unreal.find_object(None, os.environ.get('CORE_SHOTS_MAP', '/Game/Maps/L_Volcano.L_Volcano'))
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if st['phase'] == 'wait':
        mechs = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn) if 'Mech' in a.get_class().get_name()]
        if mechs and st['seen_mech_t'] is None:
            st['seen_mech_t'] = st['t']
        if st['seen_mech_t'] is None or st['t'] - st['seen_mech_t'] < 6.0 or pc is None:
            return
        arena = unreal.GameplayStatics.get_all_actors_with_tag(world, 'CoreArena')
        cams = unreal.GameplayStatics.get_all_actors_with_tag(world, 'ShotCam')
        if not arena or not cams:
            log('找不到 CoreArena 点或 ShotCam 相机', len(arena), len(cams))
            st['done'] = True
            unreal.SystemLibrary.execute_console_command(world, 'quit')
            return
        st['center'] = arena[0].get_actor_location()
        st['cam'] = cams[0]
        st['mechs'] = mechs
        try:
            pc.set_editor_property('should_perform_full_tick_when_paused', True)
        except Exception as e:
            log('设不了暂停时更新相机', e)
        unreal.GameplayStatics.set_game_paused(world, True)
        unreal.SystemLibrary.execute_console_command(world, 'DisableAllScreenMessages')
        unreal.SystemLibrary.execute_console_command(world, 'showhud')
        root = st['cam'].get_editor_property('root_component')
        if root.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE:
            root.set_mobility(unreal.ComponentMobility.MOVABLE)
        pc.set_view_target_with_blend(st['cam'], 0.0)
        log('场地中心', [round(v / 100, 1) for v in (st['center'].x, st['center'].y, st['center'].z)], '半长', L, '机体', len(mechs))
        st['phase'] = 'shoot'
        st['shot_t'] = st['t']
        return
    if st['phase'] == 'shoot':
        if st['i'] >= 0 and st['t'] - st['shot_t'] > WAIT * 0.4:
            if st.get('real0') is None:
                st['real0'] = time.perf_counter()
                st['frames'] = 0
            st['frames'] += 1
        if st['i'] >= 0 and not st.get('taken') and st['t'] - st['shot_t'] > WAIT:
            name = PREFIX + SHOTS[st['i']][0]
            fps = st['frames'] / max(1e-3, time.perf_counter() - st['real0'])
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot 1920x1080 filename={OUT}/{name}.png')
            log('截图', f'{OUT}/{name}.png', '帧率', round(fps, 1))
            st['taken'] = True
            st['taken_t'] = st['t']
            return
        if st['i'] < 0 or (st.get('taken') and st['t'] - st['taken_t'] > 0.8):
            st['taken'] = False
            st['i'] += 1
            st['shot_t'] = st['t']
            st['real0'] = None
            if st['i'] >= len(SHOTS):
                st['phase'] = 'quit'
                return
            name, c, look, fov, mode = SHOTS[st['i']]
            o = st['center']
            ign = st['mechs']
            cx, cy = o.x + c[0] * L * 100, o.y + c[1] * L * 100
            lx, ly = o.x + look[0] * L * 100, o.y + look[1] * L * 100
            gc = ground_z(world, cx, cy, ign)
            gl = ground_z(world, lx, ly, ign)
            if mode == 'center':
                cz, lz = o.z + c[2] * 100, o.z + look[2] * 100
            else:
                cz = (gc if gc is not None else o.z) + c[2] * 100
                lz = (gl if gl is not None else o.z) + look[2] * 100
            yaw = math.degrees(math.atan2(ly - cy, lx - cx))
            pitch = math.degrees(math.atan2(lz - cz, math.hypot(lx - cx, ly - cy)))
            st['cam'].set_actor_location_and_rotation(unreal.Vector(cx, cy, cz), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
            cc = st['cam'].camera_component
            cc.set_field_of_view(fov)
            cc.set_editor_property('constrain_aspect_ratio', False)
            log(name, '机位(米)', [round(v / 100, 1) for v in (cx, cy, cz)], 'yaw', round(yaw), 'pitch', round(pitch, 1))
        return
    if st['phase'] == 'quit' and st['t'] - st['shot_t'] > 2.0:
        st['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调', OUT, '半长', L)
