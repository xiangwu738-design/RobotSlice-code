"""石柱山水 L_Pillars 的成图截图（照 ue_megacanyon_shots.py 改的；坐标按原作写，单位，放大 S 倍）。
游戏里（-game -MechBeauty）第 3.5 秒暂停，把地图里的 ShotCam 挪到各机位拍 HighResShot；第一张和原包相机完全对齐（拿来和 Renders/1–3.png 对比）。
UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Pillars -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed
  "-ExecCmds=py D:/RobotSlice/tools/ue_megacanyon_shots.py"
环境变量：PL_SHOTS_DIR 输出目录（默认 Saved/Screenshots/Pillars/now），PL_SHOTS_ONLY 只拍某几个机位，PL_SHOTS_RES 分辨率
"""
import json
import math
import os
import time

import unreal

OUT = os.environ.get('PL_SHOTS_DIR', 'D:/RobotSlice/Saved/Screenshots/Pillars/now').replace('\\', '/')
WAIT = float(os.environ.get('PL_SHOTS_WAIT', '5.0'))
GUS_INI = 'D:/RobotSlice/Saved/Config/WindowsEditor/GameUserSettings.ini'


def ini_res():
    """用户自己设的窗口分辨率（编辑器 -game 的 GameUserSettings.ini 里的 ResolutionSizeX/Y）。截图命令带 -ForceRes 1920×1080，
    引擎退出时会把 1920×1080 写回这个文件、盖掉用户的设置（11-01 发现）→ 退出前把分辨率设回原值，引擎退出时写回去的就是原值"""
    try:
        kv = dict(l.strip().split('=', 1) for l in open(GUS_INI, encoding='utf-8-sig') if l.startswith('ResolutionSize'))
        return int(kv['ResolutionSizeX']), int(kv['ResolutionSizeY'])
    except Exception:
        return None


ORIG_RES = ini_res()
RES = os.environ.get('PL_SHOTS_RES', '1920x1080')
ONLY = [s for s in os.environ.get('PL_SHOTS_ONLY', '').split(',') if s]
os.makedirs(OUT, exist_ok=True)
META = json.load(open('D:/RobotSlice/Inbox/_f25/export/pillars.json', encoding='utf-8'))
CAM = META['camera']
WZ = META['water_z']
S = 6.0
c0, f0 = CAM['loc'], CAM['fwd']
FOV0 = math.degrees(2 * math.atan(CAM['sensor'] / 2 / CAM['lens']))     # 27 毫米、36 毫米宽底片 → 水平 67.4°

# 名字, 机位 (原作 x, y, z), 看向 (原作 x, y, z), 水平 FOV, 机体 (x, y, z, UE 朝向) 或 None
SHOTS = [
    ('1_ref_camera', tuple(c0), (c0[0] + f0[0] * 100, c0[1] + f0[1] * 100, c0[2] + f0[2] * 100), FOV0, None),   # 和原作相机对齐
    ('2_overview', (-70.0, -55.0, 55.0), (10.0, 15.0, 2.0), 62, None),                                           # 远：全场斜俯视
    ('3_flight', (26.0, -30.0, 15.0), (-6.0, 8.0, 6.0), 78, None),                                               # 飞行视角（机体高度，穿石柱）
    ('4_mid', (-1.0, -24.0, 7.0), (11.0, -12.0, 7.0), 70, None),                                                 # 中距离看一根石柱
    ('5_close', (17.6, -13.5, 7.2), (14.0, -13.5, 7.0), 75, None),                                               # 贴近岩壁（约 12 米）
    ('6_low_water', (-6.0, -34.0, 2.2), (5.0, 2.0, 3.5), 72, None),                                              # 贴水面看过去
    ('7_island', (-20.0, 24.0, 4.2), (-30.0, 34.0, 1.5), 70, None),                                              # 站在岛上的高度
    ('8_mech_spawn', (-30.4, 34.2, 2.5), (-5.0, 5.0, 5.0), 72, (-28.2, 31.64, 1.62, -45.0)),                     # 机体站在出生岛上，第三人称看石林
    ('9_dive', (4.0, -6.0, 26.0), (7.0, -11.0, 1.0), 74, None),                                                  # 俯冲时往下看
    ('10_between', (22.0, -24.0, 6.0), (4.0, -2.0, 6.5), 75, None),                                              # 贴着水面在石柱之间飞
    ('11_east', (28.0, 14.0, 12.0), (50.0, 40.0, 8.0), 70, None),                                                # 东边那几根高的
]

# 调水面用（PL_WATER_SWEEP="名字|向量参数 Deep:1 0 0/Mid:0 1 0||控制台命令|标量参数 Spec=0.2;..."）：同一个机位按不同参数各拍一张，不用反复编材质
SWEEP = {}
for item in [x for x in os.environ.get('PL_WATER_SWEEP', '').split(';') if x]:
    parts = item.split('|')
    nm = 'wt_' + parts[0]
    SWEEP[nm] = parts
    base = next((b for b in SHOTS if b[0] == os.environ.get('PL_WATER_SWEEP_CAM', '1_ref_camera')), SHOTS[0])
    SHOTS.append((nm, base[1], base[2], base[3], None))

QUAL = ['sg.ResolutionQuality', 'sg.ViewDistanceQuality', 'sg.AntiAliasingQuality', 'sg.ShadowQuality', 'sg.GlobalIlluminationQuality',
        'sg.ReflectionQuality', 'sg.PostProcessQuality', 'sg.TextureQuality', 'sg.EffectsQuality', 'sg.FoliageQuality', 'sg.ShadingQuality']
world = None
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'cam': None, 'done': False}
fps_log = []
log = lambda *a: unreal.log('[plshots] ' + ' '.join(str(x) for x in a))
B = lambda x, y, z: unreal.Vector(x * 100.0 * S, -y * 100.0 * S, (z - WZ) * 100.0 * S)


def place(cam, pawn, shot):
    name, c, look, fov, mech = shot
    if name in SWEEP:
        parts = SWEEP[name]
        for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
            cc0 = a.static_mesh_component
            m0 = cc0.get_material(0)
            if m0 and 'PL_Water' in m0.get_name():
                mid = m0 if isinstance(m0, unreal.MaterialInstanceDynamic) else cc0.create_dynamic_material_instance(0)
                if len(parts) > 1 and parts[1]:
                    for kv in parts[1].split('/'):        # 向量参数：Deep:1 0 0/Mid:0 1 0
                        k, vv = kv.split(':')
                        v = [float(x) for x in vv.split()]
                        mid.set_vector_parameter_value(k, unreal.LinearColor(v[0], v[1], v[2], 1))
        if len(parts) > 4 and parts[4]:
            for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
                cc0 = a.static_mesh_component
                m0 = cc0.get_material(0)
                if m0 and 'PL_Water' in m0.get_name():
                    mid = m0 if isinstance(m0, unreal.MaterialInstanceDynamic) else cc0.create_dynamic_material_instance(0)
                    for kv in parts[4].split(','):
                        k, v = kv.split('=')
                        mid.set_scalar_parameter_value(k, float(v))
        if len(parts) > 3 and parts[3]:
            for cmd in parts[3].split(','):
                cmd = cmd.strip()
                if cmd.startswith('@'):      # 排查：直接改水面那块平面的属性（@scale 8000、@movable、@shadow、@lumen）
                    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
                        cc0 = a.static_mesh_component
                        m0 = cc0.get_material(0)
                        if m0 and 'PL_Water' in m0.get_name():
                            if cmd.startswith('@scale'):
                                v = float(cmd.split()[1])
                                a.set_actor_scale3d(unreal.Vector(v, v, 1))
                            elif cmd == '@movable':
                                cc0.set_mobility(unreal.ComponentMobility.MOVABLE)
                            elif cmd == '@shadow':
                                cc0.set_cast_shadow(True)
                            elif cmd == '@lumen':
                                cc0.set_affect_dynamic_indirect_lighting(True)
                                cc0.set_affect_distance_field_lighting(True)
                else:
                    unreal.SystemLibrary.execute_console_command(world, cmd)
        log('参数', parts)
    loc = B(*c)
    tgt = B(*look)
    d = tgt - loc
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)
    if pawn:
        if mech:
            try:
                hh = pawn.capsule_component.get_scaled_capsule_half_height()
            except Exception:
                hh = 130.0
            pawn.set_actor_hidden_in_game(False)
            pawn.set_actor_location_and_rotation(B(mech[0], mech[1], mech[2]) + unreal.Vector(0, 0, hh + 2), unreal.Rotator(yaw=mech[3]), False, True)
        else:
            pawn.set_actor_hidden_in_game(True)
    log(name, '机位', [round(v, 1) for v in c], 'yaw', round(yaw, 1), 'pitch', round(pitch, 1), 'fov', round(fov, 1))


def tick(dt):
    global world
    if state['done']:
        return
    state['t'] += dt
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_Pillars.L_Pillars')
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
            cams = [c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor) if 'ShotCam' in [str(t) for t in c.tags]]
            state['cam'] = cams[0] if cams else None
            state['pawn'] = pawn
            state['phase'] = 'shoot'
            # 画质：编辑器 -game 读 Saved/Config/WindowsEditor/GameUserSettings.ini，11-01 那里是全最低档（阴影关、分辨率 50%），
            # 截出来没有投影、发糊。拍照时临时调到「史诗」，退出前改回原值（引擎退出时会把当前档位写回这个文件，不能改掉用户的设置）
            state['orig_q'] = {}
            if os.environ.get('PL_SHOTS_QUALITY', 'epic') == 'epic':
                for k in QUAL:
                    state['orig_q'][k] = unreal.SystemLibrary.get_console_variable_float_value(k)
                    unreal.SystemLibrary.execute_console_command(world, f'{k} {100 if k == "sg.ResolutionQuality" else 3}')
                log('画质临时调到史诗，原值', state['orig_q'])
            dbg = os.environ.get('PL_DEBUG', '')     # 排查用：nosun = 关太阳、nosky = 关天光，看某块亮面是直射光还是环境光照的
            if dbg in ('nosun', 'nosky'):
                cls = unreal.DirectionalLight if dbg == 'nosun' else unreal.SkyLight
                for l in unreal.GameplayStatics.get_all_actors_of_class(world, cls):
                    l.light_component.set_intensity(0.0)
                log('调试：关掉', cls.__name__)
            wm = os.environ.get('PL_WATER_MAT', '')      # 排查用：临时换水面材质
            if wm:
                mat = unreal.load_asset(wm)
                for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor):
                    c = a.static_mesh_component
                    m0 = c.get_material(0)
                    if m0 and 'MI_PL_Water' in m0.get_name():
                        c.set_material(0, mat)
                        log('水面材质换成', wm, mat)
            p = pawn.get_actor_location()
            log('暂停；出生后机体位置（米，水面 0）', [round(p.x / 100, 1), round(-p.y / 100, 1), round(p.z / 100, 1)])
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
        if state.get('orig_q'):
            log('画质改回原值')
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
