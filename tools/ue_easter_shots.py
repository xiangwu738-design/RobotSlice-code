"""游戏里给彩蛋模型拍照（11-02）：在测试场机体前面摆 4 只狗、4 架无人机（各一套涂装：设 PaintA / PaintB / Accent / AccentGlow，和代码以后的做法一样），
旋翼在插槽上转、狗有 A_PDog_Walk 就播。拍 远景（机体在旁边比大小）/ 狗近景 / 狗侧面 / 无人机近景 / 无人机仰视，再退出。不改任何资源。
  UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechBeauty -RenderOffscreen -unattended -nosound -ForceRes -ResX=1920 -ResY=1080 -windowed -MechHour=13
     "-ExecCmds=py D:/RobotSlice/tools/ue_easter_shots.py"
  或 runner_request.json：{"kind":"gamepy","name":"easter","map":"/Game/Maps/L_Range?Range","script":"tools/ue_easter_shots.py","args":["-MechBeauty"],"seconds":120}
输出 Saved/Screenshots/Easter/*.png（环境变量 EASTER_SHOTS_DIR 改），日志 [eastershots]。
"""
import json
import math
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip('/')
OUT = os.environ.get('EASTER_SHOTS_DIR', f'{PROJECT}/Saved/Screenshots/Easter').replace(chr(92), '/')
RES = os.environ.get('EASTER_SHOTS_RES', '1920x1080')
WAIT = 2.5
os.makedirs(OUT, exist_ok=True)
log = lambda *a: unreal.log('[eastershots] ' + ' '.join(str(x) for x in a))
eas = unreal.EditorAssetLibrary if hasattr(unreal, 'EditorAssetLibrary') else None


def load(p):
    try:
        return unreal.load_asset(p)
    except Exception:
        return None


def srgb(h):
    h = h.lstrip('#')

    def f(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    return unreal.LinearColor(*[f(int(h[i:i + 2], 16)) for i in (0, 2, 4)], 1.0)


def liveries(name):
    p = f'{PROJECT}/tools/{name}'
    try:
        return json.load(open(p, encoding='utf-8')).get('liveries', [])
    except Exception as e:
        log('读不到涂装表', p, e)
        return []


DOG = load('/Game/Easter/Dog/SK_PDog')
WALK = load('/Game/Easter/Dog/A_PDog_Walk')
BODY = load('/Game/Easter/Drone/SM_VDrone_Body')
ROTOR = load('/Game/Easter/Drone/SM_VDrone_Rotor')
log('资源', 'SK_PDog', bool(DOG), 'A_PDog_Walk', bool(WALK), 'SM_VDrone_Body', bool(BODY), 'SM_VDrone_Rotor', bool(ROTOR))
state = {'t': 0.0, 'phase': 'wait', 'i': -1, 'shot_t': 0.0, 'done': False, 'rotors': [], 'world': None}


def spawn(world, cls, loc, yaw):
    tf = unreal.Transform(location=loc, rotation=unreal.Rotator(roll=0, pitch=0, yaw=yaw), scale=unreal.Vector(1, 1, 1))
    a = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(world, cls, tf)
    return unreal.GameplayStatics.finish_spawning_actor(a, tf)


def ground_z(world, x, y, z0):
    hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, z0 + 2000), unreal.Vector(x, y, z0 - 5000),
                                                 unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
    if isinstance(hit, tuple):
        hit = hit[1] if hit[0] else None
    if hit is None:
        return z0
    t = hit.to_tuple()
    return t[4].z if t[0] else z0


def paint(comp, lv, n):
    for i in range(n):
        mid = comp.create_dynamic_material_instance(i)
        if not mid:
            continue
        for k in ('PaintA', 'PaintB', 'Accent'):
            if lv.get(k):
                mid.set_vector_parameter_value(k, srgb(lv[k]))
        mid.set_scalar_parameter_value('AccentGlow', 8.0)


def setup(world, pawn):
    p = pawn.get_actor_location()
    yaw = pawn.get_actor_rotation().yaw
    f = unreal.Vector(math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0)
    r = unreal.Vector(-f.y, f.x, 0)
    state['base'], state['f'], state['r'], state['yaw'] = p, f, r, yaw
    gz = ground_z(world, p.x, p.y, p.z)
    state['gz'] = gz
    log('机体', p, 'yaw', round(yaw), '地面', round(gz))
    if DOG:
        lv = liveries('easter_dog.json')
        for k, side in enumerate((-300, -100, 100, 300)):
            loc = p + f * 700 + r * side
            loc.z = ground_z(world, loc.x, loc.y, p.z)
            a = spawn(world, unreal.SkeletalMeshActor, loc, yaw + 180 - 25 + k * 15)
            c = a.get_editor_property('skeletal_mesh_component')
            c.set_mobility(unreal.ComponentMobility.MOVABLE)
            try:
                c.set_skeletal_mesh_asset(DOG)
            except Exception:
                c.set_skeletal_mesh(DOG)
            if lv:
                paint(c, lv[k % len(lv)], c.get_num_materials())
            if WALK and k % 2 == 0:
                c.play_animation(WALK, True)
            log('狗', k, loc, '涂装', lv[k % len(lv)]['name'] if lv else '-')
    if BODY:
        lv = liveries('easter_drone.json')
        for k, side in enumerate((-350, -120, 120, 350)):
            loc = p + f * 1100 + r * side
            loc.z = ground_z(world, loc.x, loc.y, p.z) + 250 + 60 * (k % 2)
            a = spawn(world, unreal.StaticMeshActor, loc, yaw + 180 + (k - 1.5) * 12)
            c = a.get_editor_property('static_mesh_component')
            c.set_mobility(unreal.ComponentMobility.MOVABLE)
            c.set_static_mesh(BODY)
            if lv:
                paint(c, lv[k % len(lv)], c.get_num_materials())
            names = [str(n) for n in c.get_all_socket_names()]
            for sn in ('Rotor_FL', 'Rotor_FR', 'Rotor_BL', 'Rotor_BR'):
                if ROTOR and sn in names:
                    rc = a.add_component_by_class(unreal.StaticMeshComponent, False, unreal.Transform(), False)
                    rc.set_mobility(unreal.ComponentMobility.MOVABLE)
                    rc.set_static_mesh(ROTOR)
                    rc.attach_to_component(c, sn, unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET,
                                           unreal.AttachmentRule.KEEP_WORLD, False)
                    rc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
                    if lv:
                        paint(rc, lv[k % len(lv)], rc.get_num_materials())
                    state['rotors'].append((rc, 1 if sn in ('Rotor_FL', 'Rotor_BR') else -1))
            log('无人机', k, loc, '插槽', names, '涂装', lv[k % len(lv)]['name'] if lv else '-')
            if k == 0:
                m = c.get_socket_location('Muzzle')
                log('  Muzzle 世界位置', m, '机身位置', loc)


def shots():
    p, f, r, gz = state['base'], state['f'], state['r'], state['gz']

    def at(fw, side, h):
        v = p + f * fw + r * side
        return unreal.Vector(v.x, v.y, gz + h)
    return [
        ('e0_wide', at(-250, 450, 380), at(900, 0, 180), 70),
        ('e1_dogs', at(380, -60, 160), at(700, 0, 70), 60),
        ('e2_dog_side', at(700, -480, 110), at(700, -300, 60), 45),
        ('e3_drones', at(700, 80, 330), at(1100, 0, 270), 60),
        ('e4_drone_under', at(950, -380, 140), at(1100, -350, 260), 55),
    ]


def place(cam, shot):
    name, loc, look, fov = shot
    d = look - loc
    yaw = math.degrees(math.atan2(d.y, d.x))
    pitch = math.degrees(math.atan2(d.z, math.hypot(d.x, d.y)))
    root = cam.get_editor_property('root_component')
    root.set_mobility(unreal.ComponentMobility.MOVABLE)
    cam.set_actor_location_and_rotation(loc, unreal.Rotator(roll=0, pitch=pitch, yaw=yaw), False, True)
    cc = cam.camera_component
    cc.set_field_of_view(fov)
    cc.set_editor_property('constrain_aspect_ratio', False)


def tick(dt):
    if state['done']:
        return
    state['t'] += dt
    for rc, sgn in state['rotors']:
        rc.add_local_rotation(unreal.Rotator(roll=0, pitch=0, yaw=sgn * 1440.0 * dt))
    world = state['world']
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_Range.L_Range')  # 同 ue_techpark_shots.py 的找法
        if world is None:
            try:
                world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            except Exception:
                world = None
        state['world'] = world
        return
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if state['phase'] == 'wait':
        if pawn and state['t'] > 3.0:
            try:
                setup(world, pawn)
            except Exception as e:
                import traceback
                log('摆放出错', traceback.format_exc())
            state['cam'] = spawn(world, unreal.CameraActor, pawn.get_actor_location(), 0)
            state['phase'] = 'shoot'
            state['list'] = shots()
        return
    if state['phase'] == 'shoot':
        todo = state['list']
        if state['i'] >= 0 and not state.get('taken') and state['t'] - state['shot_t'] > WAIT:
            name = todo[state['i']][0]
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot {RES} filename={OUT}/{name}.png')
            log('截图', f'{OUT}/{name}.png')
            state['taken'] = True
            state['taken_t'] = state['t']
            return
        if state['i'] < 0 or (state.get('taken') and state['t'] - state['taken_t'] > 0.8):
            state['taken'] = False
            state['i'] += 1
            state['shot_t'] = state['t']
            if state['i'] >= len(todo):
                state['phase'] = 'quit'
                return
            place(state['cam'], todo[state['i']])
            pc.set_view_target_with_blend(state['cam'], 0.0)
        return
    if state['phase'] == 'quit' and state['t'] - state['shot_t'] > 2.0:
        state['done'] = True
        log('完成，退出')
        unreal.SystemLibrary.execute_console_command(world, 'quit')


unreal.register_slate_post_tick_callback(tick)
