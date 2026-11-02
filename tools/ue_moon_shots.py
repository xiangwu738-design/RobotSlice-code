"""月球基地 L_Moon 成图（照 ue_techpark_shots.py 改）：游戏里（-game -MechBeauty）暂停，把地图里的 ShotCam 挪到各机位拍 HighResShot，记每个机位的帧率。
机位用 UE 坐标（米，X 东、Y 南、Z 上）。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Moon -game -MechBeauty -RenderOffscreen -unattended -nosplash -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_moon_shots.py"
环境变量：MOON_SHOTS_DIR 输出目录（默认 Saved/Screenshots/Moon/now），MOON_SHOTS_ONLY 只拍某几个（逗号隔开），MOON_SHOTS_WAIT 每个机位等几秒，MOON_SHOTS_RES 分辨率。
"""
import math
import os
import time

import unreal

OUT = os.environ.get('MOON_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/Moon/now').replace(chr(92), '/')
WAIT = float(os.environ.get('MOON_SHOTS_WAIT', '5.0'))
RES = os.environ.get('MOON_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('MOON_SHOTS_ONLY', '').split(',') if s]
GUS_INI = 'D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini'
os.makedirs(OUT, exist_ok=True)

# 名字, 机位（米）, 看向（米）, 水平 FOV, 机体位置（None = 不显示）。高度写字符串 'g5' = 那里的地面以上 5 米
SHOTS = [
    ('a_top', (0, 25, 4000), (0, 25, 0), 30.8, None),                     # 俯视布局（远处小视角，近似正交）
    ('b_spawn_w', (-178, 12, 'g4.5'), (-20, -20, 14), 72, (-160, 6)),     # 西队出生区往东：前挡、穹顶（第三人称高度），机体在画面里
    ('c_north_road', (-92, -80, 'g4.5'), (70, -100, 8), 72, None),        # 北路：机库和穹顶之间
    ('d_north_plaza', (-58, -118, 'g4.5'), (0, -215, 14), 72, None),      # 北广场往北看大平台
    ('e_south_tanks', (-82, 96, 'g4.5'), (20, 130, 14), 72, None),        # 南路储罐和拉索塔
    ('f_dome_in', (-40, 6, 'g3.5'), (12, -4, 12), 80, None),               # 穹顶里（地面高 5 米）往中间看
    ('g_low_fly', (-250, -160, 'g30'), (20, -30, 6), 66, None),           # 低空飞行：西北往基地
    ('h_mid_fly', (300, 240, 'g70'), (0, 0, 8), 60, None),                # 东南高处俯看整个基地
    ('i_earth', (-120, 130, 'g12'), (155, -300, 150), 62, None),          # 从西南往东北看：基地 + 地球
    ('j_massif', (120, -40, 'g35'), (-1500, -900, 300), 62, None),        # 往西北看：真实的大山
    ('k_crater', (-452, -300, 'g7'), (-395, -275, -6), 70, None),         # 外圈环形坑
    ('l_rim_view', (420, 310, 'g25'), (0, 0, 10), 55, None),              # 东南山坡上回看基地
    ('m_ground_soil', (-300, 120, 'g2.0'), (-260, 60, 1), 78, None),      # 月面近景（看贴图）
    ('n_south_ridge', (-10, 330, 'g5'), (400, 700, 40), 68, None),        # 往东南看山坡
    ('o_west_hill', (-180, -140, 'g4'), (-700, 200, 20), 70, None),       # 往西南看外圈和山
    ('p_mech_earth', (-60, -125, 'g3.5'), (80, -330, 90), 70, (-46, -135)),  # 北广场，机体 + 地球
]

world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
log = lambda *a: unreal.log('[shots] ' + ' '.join(str(x) for x in a))


def ini_res():
    # 带 -ForceRes 时引擎退出会把分辨率写回 GameUserSettings.ini：退出前把分辨率设回原值（11-01 发现）
    try:
        kv = dict(l.strip().split('=', 1) for l in open(GUS_INI, encoding='utf-8-sig') if l.startswith('ResolutionSize'))
        return int(kv['ResolutionSizeX']), int(kv['ResolutionSizeY'])
    except Exception:
        return None


ORIG_RES = ini_res()


def ground(x, y):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x * 100, y * 100, 300000), unreal.Vector(x * 100, y * 100, -300000), unreal.TraceTypeQuery.ECC_VISIBILITY,
                                                 False, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple() if hit else None
    return t[4].z / 100 if t and t[0] else 0.0


def resolve(x, y, z):
    if isinstance(z, str):
        return ground(x, y) + float(z[1:])
    return z


def place(cam, shot):
    name, c, look, fov, mech = shot
    cz = resolve(c[0], c[1], c[2])
    lz = resolve(look[0], look[1], look[2])
    loc = unreal.Vector(c[0] * 100, c[1] * 100, cz * 100)
    d = unreal.Vector(look[0] * 100 - loc.x, look[1] * 100 - loc.y, lz * 100 - loc.z)
    p = state['pawn']
    p.set_actor_hidden_in_game(mech is None)
    if mech is not None:
        gz = ground(mech[0], mech[1])
        p.set_actor_location(unreal.Vector(mech[0] * 100, mech[1] * 100, (gz + 1.45) * 100), False, True)
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    if name == 'a_top':
        yaw, pitch = -90.0, -90.0      # 屏幕上方 = 北（-Y）
    root = cam.get_editor_property('root_component')
    if root.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE:
        root.set_mobility(unreal.ComponentMobility.MOVABLE)
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    log(name, '机位', c, 'yaw', round(yaw), 'pitch', round(pitch, 1))


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_Moon.L_Moon')
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if state['phase'] == 'wait':
        if pawn and state['t'] > 3.5:
            try:
                pc.set_editor_property('should_perform_full_tick_when_paused', True)
            except Exception as e:
                log('设不了暂停时更新相机', e)
            unreal.GameplayStatics.set_game_paused(world, True)
            cams = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
            state['cam'] = cams[0] if cams else None
            state['pawn'] = pawn
            state['phase'] = 'shoot'
            log('机体位置', pawn.get_actor_location(), '重力', world.get_world_settings().get_gravity_z() if hasattr(world.get_world_settings(), 'get_gravity_z') else '?')
        return
    if state['phase'] == 'shoot':
        cam = state['cam']
        if state['i'] >= 0 and state['t'] - state['shot_t'] > WAIT * 0.4:
            if state.get('real0') is None:
                state['real0'] = time.perf_counter()
                state['frames'] = 0
            state['frames'] += 1
        todo = [s for s in SHOTS if not ONLY or s[0] in ONLY]
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
            name = todo[state['i']][0]
            fps = state['frames'] / max(1e-3, time.perf_counter() - state['real0'])
            res = '2000x2000' if name == 'a_top' else RES
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot {res} filename={OUT}/{name}.png')
            log('截图', f'{OUT}/{name}.png', '这一机位后半段平均帧率', round(fps, 1))
            state['taken'] = True
            state['taken_t'] = state['t']
            return
        if state['i'] < 0 or (state.get('taken') and state['t'] - state['taken_t'] > 1.0):
            state['taken'] = False
            state['i'] += 1
            state['shot_t'] = state['t']
            state['frames'] = 0
            state['real0'] = None
            if state['i'] >= len(todo):
                state['phase'] = 'quit'
                return
            shot = todo[state['i']]
            place(cam, shot)
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        if ORIG_RES:
            try:
                gus = unreal.GameUserSettings.get_game_user_settings()
                gus.set_screen_resolution(unreal.IntPoint(*ORIG_RES))
                gus.apply_resolution_settings(False)
                gus.save_settings()
            except Exception as e:
                log('还原分辨率失败', e)
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)
