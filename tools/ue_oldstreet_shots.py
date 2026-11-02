"""老旧街道截图和帧率（照 ue_dreamfield_shots.py 改）。游戏里（-game -MechBeauty）第 3.5 秒暂停，把地图里的 ShotCam 挪到各机位拍 HighResShot、记每个机位后半段的平均帧率。不改地图。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_OldStreet -game -MechBeauty -MechHour=16.3 -MechWeather=Clear -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_oldstreet_shots.py"
环境变量：OS_SHOTS_DIR 输出目录，OS_SHOTS_ONLY 只拍哪几个（逗号分隔），OS_SHOTS_WAIT 每个机位等几秒，OS_SHOTS_NOPAWN=1 机体藏起来，
OS_SHOTS_MAP 换地图（对比帧率用，机位表 OS_SHOTS_TABLE=city|techpark），OS_SHOTS_EXEC 开拍前多跑的控制台命令（分号隔开）
"""
import json
import math
import os
import time

import unreal

OUT = os.environ.get('OS_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/OldStreet/now').replace('\\', '/')
WAIT = float(os.environ.get('OS_SHOTS_WAIT', '5.0'))
RES = os.environ.get('OS_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('OS_SHOTS_ONLY', '').split(',') if s]
MAP = os.environ.get('OS_SHOTS_MAP', '/Game/Maps/L_OldStreet.L_OldStreet')
TABLE = os.environ.get('OS_SHOTS_TABLE', 'oldstreet')
GUS_INI = 'D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini'
L = json.load(open('D:/RobotSlice/Inbox/_oldstreet/layout.json', encoding='utf-8'))


def ini_res():
    try:
        kv = dict(l.strip().split('=', 1) for l in open(GUS_INI, encoding='utf-8-sig') if l.startswith('ResolutionSize'))
        return int(kv['ResolutionSizeX']), int(kv['ResolutionSizeY'])
    except Exception:
        return None


ORIG_RES = ini_res()
os.makedirs(OUT, exist_ok=True)

# 名字, 机位 (X, Y, Z 米), 看向 (X, Y, Z), FOV, 机体 (X, Y, 朝向) 或 None
EYE = 3.2    # 机体第三人称镜头大约这个高度
SHOTS = [
    ('seam_fwd', (50.0, 0.0, EYE), (120.0, 0.0, 3.0), 80, None),                 # 站在接缝上顺街向前（X+）
    ('seam_back', (50.0, 0.0, EYE), (-20.0, 0.0, 3.0), 80, None),                # 同一点向后（X-）
    ('seam_side', (50.0, 5.5, 2.0), (50.0, -7.0, 0.0), 90, None),                # 接缝上横着看路面和路缘：地面有没有断口
    ('seam_ground', (44.0, 2.5, 1.2), (56.0, 0.8, 0.0), 70, None),               # 贴地顺着路面穿过接缝
    ('seam_high', (50.0, 0.0, 14.0), (50.0, 0.0, 0.0), 90, None),                # 接缝正上方往下看
    ('street_eye', (-22.0, 1.0, EYE), (28.0, -1.0, 3.5), 75, (-14.0, 0.0, 0.0)),  # 街上视平线 + 机体
    ('street_back', (24.0, 1.0, EYE), (-30.0, -1.0, 3.5), 75, (16.0, 0.0, 180.0)),  # 反方向
    ('cross', (-12.0, -2.0, 4.0), (10.0, 24.0, 4.0), 78, (-3.0, 8.0, 40.0)),     # 路口看东横路
    ('cross_w', (4.0, 3.0, 4.0), (-4.0, -40.0, 4.0), 78, None),                  # 路口看西边广场
    ('author', (-12.0, 0.7, 1.4), (25.0, 2.0, 5.0), 62, None),                   # 接近原包相机的视角
    ('high', (-60.0, -24.0, 38.0), (40.0, 6.0, 2.0), 72, None),                  # 高处俯看（限高 40 米）
    ('high_side', (0.0, 39.0, 38.0), (0.0, -80.0, 0.0), 80, None),               # 贴着隐形墙往街区看
    ('far_fwd', (0.0, 0.0, EYE), (500.0, 0.0, 6.0), 45, None),                   # 顺街看远方（长焦）
    ('far_back', (0.0, 0.0, EYE), (-500.0, 0.0, 6.0), 45, None),
    ('far_high', (0.0, 0.0, 30.0), (500.0, 0.0, 12.0), 60, None),                # 高处顺街看远方
]
if TABLE == 'city':      # 同条件比较：城区图的路口、大道
    SHOTS = [('c_ave', (-70.0, 0.0, 3.2), (40.0, 0.0, 4.0), 75, None), ('c_cross', (-15.0, 10.0, 4.0), (40.0, -40.0, 6.0), 75, None),
             ('c_high', (-90.0, -70.0, 40.0), (20.0, 30.0, 0.0), 72, None), ('c_far', (0.0, 0.0, 3.2), (500.0, 0.0, 6.0), 60, None)]
elif TABLE == 'techpark':
    SHOTS = [('t_plaza', (-60.0, 0.0, 3.2), (40.0, 0.0, 4.0), 75, None), ('t_cross', (-15.0, 30.0, 8.0), (40.0, -30.0, 4.0), 75, None),
             ('t_high', (-100.0, -70.0, 40.0), (20.0, 30.0, 0.0), 72, None), ('t_far', (0.0, 0.0, 3.2), (500.0, 0.0, 6.0), 60, None)]
elif TABLE == 'backdrop':     # 街景贴片的图：不带雾（命令见 oldstreet_shots.sh 的调用），FOV 20.5°，见 oldstreet_layout.py 的 backdrop
    SHOTS = [('bd_fwd', (-540.0, 0.0, 3.2), (0.0, 0.0, 3.2), 20.5, None), ('bd_back', (540.0, 0.0, 3.2), (0.0, 0.0, 3.2), 20.5, None)]

QUAL = ['sg.ResolutionQuality', 'sg.ViewDistanceQuality', 'sg.AntiAliasingQuality', 'sg.ShadowQuality', 'sg.GlobalIlluminationQuality',
        'sg.ReflectionQuality', 'sg.PostProcessQuality', 'sg.TextureQuality', 'sg.EffectsQuality', 'sg.FoliageQuality', 'sg.ShadingQuality']
world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
fps_log = []
log = lambda *a: unreal.log('[osshots] ' + ' '.join(str(x) for x in a))


def trace_z(x, y, pawn):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x * 100, y * 100, 3000), unreal.Vector(x * 100, y * 100, -300),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [pawn] if pawn else [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    tt = hit.to_tuple() if hit else None
    z = next((v.z / 100 for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
    who = next((v.get_actor_label() if hasattr(v, 'get_actor_label') else v.get_name() for v in tt if isinstance(v, unreal.Actor)), None) if tt and tt[0] else None
    return z, who


def place(cam, pawn, shot):
    name, c, look, fov, mech = shot
    loc = unreal.Vector(c[0] * 100, c[1] * 100, c[2] * 100)
    d = unreal.Vector((look[0] - c[0]) * 100, (look[1] - c[1]) * 100, (look[2] - c[2]) * 100)
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    if pawn:
        if mech and not os.environ.get('OS_SHOTS_NOPAWN'):
            pawn.set_actor_hidden_in_game(False)
            try:
                hh = pawn.capsule_component.get_scaled_capsule_half_height()
            except Exception:
                hh = 130.0
            pawn.set_actor_location_and_rotation(unreal.Vector(mech[0] * 100, mech[1] * 100, hh + 8), unreal.Rotator(yaw=mech[2]), False, True)
        else:
            pawn.set_actor_hidden_in_game(True)
    log(name, '机位', [round(v, 1) for v in c], 'yaw', round(yaw), 'pitch', round(pitch, 1), 'fov', fov)


def selfcheck(pawn):
    """碰撞和出生点自检（编辑器命令行里射线打不到刚摆的东西，放到游戏里查）"""
    for nm, x, y in (('路中', 0, 0.9), ('人行道', 20, -5), ('接缝路面', 50, 0.9), ('接缝前 1 米', 49, 0.9), ('接缝后 1 米', 51, 0.9), ('-1 号周期', -60, 0.9), ('+1 号周期', 60, 0.9)):
        log('碰撞自检', nm, trace_z(x, y, pawn))
    for s_ in L['team_spawn']:
        for dy in (-7, 0, 7):
            log('出生点地面', s_['X'], s_['Y'] + dy, trace_z(s_['X'], s_['Y'] + dy, pawn))
    for x, y in L['dm_spawn']:
        z, who = trace_z(x, y, pawn)
        log('死斗点', x, y, z, who, '被楼压着' if z is not None and z > 1.0 else '')
    for b in L['street']:
        z, who = trace_z(b['X'], b['Y'], pawn)
        log('楼顶', b['type'], round(b['X'], 1), round(b['Y'], 1), z, who)
    # 隐形墙：按对象类型查（Vehicle，只挡 Pawn）
    for nm, a_, b_ in (('右墙', (0, 0, 20), (0, 60, 20)), ('左墙', (20, 0, 20), (20, -60, 20)), ('封顶', (30, 0, 10), (30, 0, 60)), ('前墙', (100, 0, 20), (170, 0, 20)), ('后墙', (-100, 0, 20), (-170, 0, 20))):
        hit = unreal.SystemLibrary.line_trace_single_for_objects(world, unreal.Vector(*[v * 100 for v in a_]), unreal.Vector(*[v * 100 for v in b_]),
                                                                 [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY5], False, [], unreal.DrawDebugTrace.NONE, True)
        if isinstance(hit, tuple):
            hit = hit[1] if hit[0] else None
        tt = hit.to_tuple() if hit else None
        hv = next((v for v in tt if isinstance(v, unreal.Vector)), None) if tt and tt[0] else None
        log('隐形墙', nm, '停在', [round(hv.x / 100, 1), round(hv.y / 100, 1), round(hv.z / 100, 1)] if hv else None)


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = unreal.find_object(None, MAP)
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
            state['orig_q'] = {}
            q = os.environ.get('OS_SHOTS_QUALITY', 'epic')
            if q == 'epic':
                for k in QUAL:
                    state['orig_q'][k] = unreal.SystemLibrary.get_console_variable_float_value(k)
                    unreal.SystemLibrary.execute_console_command(world, f'{k} {100 if k == "sg.ResolutionQuality" else 3}')
                log('画质临时调到史诗，原值', state['orig_q'])
            for cmd in [c for c in os.environ.get('OS_SHOTS_EXEC', '').split(';') if c]:
                unreal.SystemLibrary.execute_console_command(world, cmd)
                log('执行', cmd)
            cams = [c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor) if 'ShotCam' in [str(t) for t in c.tags]]
            if not cams:
                log('地图里没有 ShotCam，退出')
                state['phase'] = 'quit'
                return
            state['cam'] = cams[0]
            state['pawn'] = pawn
            state['phase'] = 'shoot'
            log('暂停，开始拍')
            if TABLE == 'oldstreet':
                selfcheck(pawn)
        return
    if state['phase'] == 'shoot':
        cam = state['cam']
        todo = [s for s in SHOTS if not ONLY or s[0] in ONLY]
        if state['i'] >= 0 and state['t'] - state['shot_t'] > WAIT * 0.4:
            if state.get('real0') is None:
                state['real0'] = time.perf_counter()
                state['frames'] = 0
            state['frames'] += 1
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
            name = todo[state['i']][0]
            fps = state['frames'] / max(1e-3, time.perf_counter() - state['real0'])
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot {RES} filename={OUT}/{name}.png')
            log('截图', f'{OUT}/{name}.png', '后半段平均帧率', round(fps, 1))
            fps_log.append((name, round(fps, 1)))
            state['taken'] = True
            state['taken_t'] = state['t']
            return
        if state['i'] < 0 or (state.get('taken') and state['t'] - state['taken_t'] > 0.8):
            state['taken'] = False
            state['i'] += 1
            state['shot_t'] = state['t']
            state['frames'] = 0
            state['real0'] = None
            if state['i'] >= len(todo):
                state['phase'] = 'quit'
                return
            place(cam, state['pawn'], todo[state['i']])
            pc.set_view_target_with_blend(cam, 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        for k, v in state.get('orig_q', {}).items():
            unreal.SystemLibrary.execute_console_command(world, f'{k} {v:g}')
        if ORIG_RES:
            try:
                unreal.GameUserSettings.get_game_user_settings().set_screen_resolution(unreal.IntPoint(*ORIG_RES))
                log('窗口分辨率设回', ORIG_RES)
            except Exception as e:
                log('设不回窗口分辨率', e)
        state['done'] = True
        log('完成，帧率', fps_log)
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调，输出', OUT)
