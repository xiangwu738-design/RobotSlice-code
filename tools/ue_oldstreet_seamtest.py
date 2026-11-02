"""老旧街道：瞬移一个整周期的实测（不改 C++，临时 Python）。-game ?Range（不暂停、没有成图模式的计时退出）里把 ShotCam 放到接缝前，反复瞬移 ±100 米（看到的东西完全一样）。
每次瞬移后等 k 帧截一张（k = 0, 1, 2, 3, 5, 8, 16），每张之间留足时间让画面回稳：连着每帧 Shot 截出来的图绿通道是坏的（品红色），暂停时运动模糊又一直拖着，都不能当数据。
不截图的那些帧记帧时间（毫秒），看瞬移那几帧卡不卡。结果用 tools/oldstreet_seam_compare.py 比。
UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_OldStreet?Range" -game -MechHour=16.3 -MechWeather=Haze -RenderOffscreen -unattended -nosound -nosplash -ForceRes -ResX=1920 -ResY=1080 -windowed "-ExecCmds=py D:/RobotSlice/tools/ue_oldstreet_seamtest.py"
环境变量：OS_SEAM_CUT=1 瞬移那帧调 SetGameCameraCutThisFrame；OS_SEAM_EXEC 开测前的控制台命令（分号隔开，比如换 GI）；OS_SEAM_TAG 结果目录名
"""
import json
import math
import os
import time

import unreal

TAG = os.environ.get('OS_SEAM_TAG', 'lumen')
OUT = f'D:/RobotSlice/Saved/Screenshots/OldStreet/seam_{TAG}'
KS = [0, 1, 2, 3, 5, 8, 16]
SETTLE = 2.5
CUT = os.environ.get('OS_SEAM_CUT') == '1'
GUS_INI = 'D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini'
EYE = 3.2
# 名字, 瞬移前机位 (X, Y, Z), 视线方向, FOV, 瞬移量 dX（下一次再跳回来）
CASES = [
    ('fwd_street', (49.5, 0.0, EYE), (100.0, 0.0, -2.0), 80, -100.0),       # 顺街往前，穿 X=+50 → -50
    ('back_street', (-49.5, 0.0, EYE), (-100.0, 0.0, -2.0), 80, 100.0),     # 顺街往后（迎着太阳），穿 X=-50 → +50
    ('shade', (49.5, -5.0, 2.0), (60.0, 30.0, 8.0), 75, -100.0),            # 看树冠和立面（阴影、GI 都多的地方）
    ('high', (49.5, 0.0, 30.0), (100.0, 0.0, -22.0), 75, -100.0),           # 高处
]
QUAL = ['sg.ResolutionQuality', 'sg.ViewDistanceQuality', 'sg.AntiAliasingQuality', 'sg.ShadowQuality', 'sg.GlobalIlluminationQuality',
        'sg.ReflectionQuality', 'sg.PostProcessQuality', 'sg.TextureQuality', 'sg.EffectsQuality', 'sg.FoliageQuality', 'sg.ShadingQuality']
log = lambda *a: unreal.log('[osseam] ' + ' '.join(str(x) for x in a))
world = None
st = {'t': 0.0, 'phase': 'wait', 'case': 0, 'ki': 0, 'seq': [], 'done': False, 'last': time.perf_counter(), 'away': False, 'dts': [], 't0': 0.0}


def ini_res():
    try:
        kv = dict(l.strip().split('=', 1) for l in open(GUS_INI, encoding='utf-8-sig') if l.startswith('ResolutionSize'))
        return int(kv['ResolutionSizeX']), int(kv['ResolutionSizeY'])
    except Exception:
        return None


ORIG_RES = ini_res()
os.makedirs(OUT, exist_ok=True)


def place(cam, c, d, fov):
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    cam.set_actor_location_and_rotation(unreal.Vector(c[0] * 100, c[1] * 100, c[2] * 100), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)


def shot():
    unreal.SystemLibrary.execute_console_command(world, 'Shot')


def current_pos(c):
    return (c[1][0] + c[4], c[1][1], c[1][2]) if st['away'] else c[1]


def tick(dt):
    global world
    if st['done']:
        return
    now = time.perf_counter()
    real_dt = now - st['last']
    st['last'] = now
    st['t'] += dt
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_OldStreet.L_OldStreet')
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if st['phase'] == 'wait':
        if pawn and st['t'] > 4.0:
            st['orig_q'] = {}
            for k in QUAL:
                st['orig_q'][k] = unreal.SystemLibrary.get_console_variable_float_value(k)
                unreal.SystemLibrary.execute_console_command(world, f'{k} {100 if k == "sg.ResolutionQuality" else 3}')
            for cmd in [c for c in os.environ.get('OS_SEAM_EXEC', '').split(';') if c]:
                unreal.SystemLibrary.execute_console_command(world, cmd)
                log('执行', cmd)
            # 机体和木桩都藏起来，不挡镜头
            for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn):
                a.set_actor_hidden_in_game(True)
            st['cam'] = [c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor) if 'ShotCam' in [str(t) for t in c.tags]][0]
            pc.set_view_target_with_blend(st['cam'], 0.0)
            st['pc'] = pc
            c = CASES[0]
            place(st['cam'], c[1], c[2], c[3])
            st['phase'] = 'settle0'
            st['t0'] = st['t']
            log('开始；瞬移那帧镜头切换标记', CUT)
        return
    if st['phase'] == 'quit':
        if st['t'] - st['t0'] > 3.0:
            json.dump({'seq': st['seq']}, open(f'{OUT}/seq.json', 'w', encoding='utf-8'), ensure_ascii=False)
            for k, v in st.get('orig_q', {}).items():
                unreal.SystemLibrary.execute_console_command(world, f'{k} {v:g}')
            if ORIG_RES:
                try:
                    unreal.GameUserSettings.get_game_user_settings().set_screen_resolution(unreal.IntPoint(*ORIG_RES))
                except Exception as e:
                    log('设不回窗口分辨率', e)
            st['done'] = True
            log('完成', len(st['seq']), '张')
            unreal.SystemLibrary.execute_console_command(world, 'quit')
        return
    c = CASES[st['case']]
    if st['phase'] == 'settle0':     # 新机位：等稳定，截基准
        if st['t'] - st['t0'] > SETTLE + 2.5:
            shot()
            st['seq'].append({'case': c[0], 'kind': 'pre', 'k': -1})
            st['phase'] = 'settle'
            st['t0'] = st['t']
        return
    if st['phase'] == 'settle':      # 等够了再跳
        if st['ki'] == 0 and not st.get('pre2') and st['t'] - st['t0'] > SETTLE / 2:
            # 第二张基准：两张基准之差 = 什么都不动时的噪声（胶片颗粒、时域噪声），后面的差要减去它才是瞬移造成的
            shot()
            st['seq'].append({'case': c[0], 'kind': 'pre', 'k': -2})
            st['pre2'] = True
            return
        if st['t'] - st['t0'] > SETTLE:
            st['pre2'] = False
            st['away'] = not st['away']
            place(st['cam'], current_pos(c), c[2], c[3])
            if CUT:
                st['pc'].player_camera_manager.set_game_camera_cut_this_frame()
            st['frame'] = 0
            st['dts'] = []
            st['phase'] = 'after'
            if KS[st['ki']] == 0:
                shot()
                st['seq'].append({'case': c[0], 'kind': 'post', 'k': 0, 'away': st['away']})
        return
    if st['phase'] == 'after':
        st['frame'] += 1
        st['dts'].append(round(real_dt * 1000, 1))
        k = KS[st['ki']]
        if st['frame'] == k:
            shot()
            st['seq'].append({'case': c[0], 'kind': 'post', 'k': k, 'away': st['away']})
        if st['frame'] >= max(k, 0) + 12:
            log('帧时间毫秒', c[0], 'k', k, st['dts'])
            st['ki'] += 1
            if st['ki'] >= len(KS):
                st['ki'] = 0
                st['case'] += 1
                st['away'] = False
                if st['case'] >= len(CASES):
                    st['phase'] = 'quit'
                    st['t0'] = st['t']
                    return
                c2 = CASES[st['case']]
                place(st['cam'], c2[1], c2[2], c2[3])
                st['phase'] = 'settle0'
                st['t0'] = st['t']
                return
            st['phase'] = 'settle'
            st['t0'] = st['t']
        return


unreal.register_slate_post_tick_callback(tick)
log('已挂上逐帧回调')
