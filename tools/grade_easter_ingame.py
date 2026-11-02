"""11-02 彩蛋模型进游戏的样子（本地测试流程 kind=gamepy，地图 "/Game/Maps/L_Soviet?Range"，参数 -MechInfiniteEnergy）：
用 C++ 的控制台命令在机体面前放 4 只友好的机械狗、3 架无人机（真的游戏里的 actor：随机涂装、灯、旋翼），
再从机体的镜头平视 / 抬头各拍一张、往前挪近再拍一张，然后退出。截图在 Saved/Screenshots/EasterIngame。
（ue_easter_shots.py 用 Python 生成 actor，游戏里 Python 调不到 BeginDeferredActorSpawnFromClass，用不了）"""
import os
import time

import unreal

OUT = 'D:/RobotSlice/Saved/Screenshots/EasterIngame'
os.makedirs(OUT, exist_ok=True)
log = lambda *a: unreal.log_warning('[easteringame] ' + ' '.join(str(x) for x in a))
st = {'t0': time.time(), 'phase': 0, 'next': 0.0, 'world': None}
SHOTS = [('front', -6.0, 0.0), ('up', 12.0, 0.0), ('left', -4.0, -25.0)]


def world():
    if st['world'] is None:
        st['world'] = unreal.find_object(None, '/Game/Maps/L_Soviet.L_Soviet')
    return st['world']


def tick(dt):
    w = world()
    if not w:
        return
    t = time.time() - st['t0']
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    if st['phase'] == 0 and pawn and t > 8.0:
        unreal.SystemLibrary.execute_console_command(w, 'mech.Easter.Dogs 4')
        unreal.SystemLibrary.execute_console_command(w, 'mech.Easter.Drones 3 gun')
        unreal.SystemLibrary.execute_console_command(w, 'mech.Easter.Info')
        log('放了狗和无人机，机体在', pawn.get_actor_location())
        st['phase'], st['next'] = 1, t + 4.0
    elif 1 <= st['phase'] <= len(SHOTS) and t > st['next'] and pawn:
        name, pitch, dyaw = SHOTS[st['phase'] - 1]
        if name == 'left':
            # 往前挪近一点再拍（狗在面前 8 米）
            loc = pawn.get_actor_location()
            fwd = pawn.get_actor_forward_vector()
            pawn.set_actor_location(unreal.Vector(loc.x + fwd.x * 400, loc.y + fwd.y * 400, loc.z), False, True)
        yaw = pawn.get_actor_rotation().yaw + dyaw
        pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw))
        unreal.SystemLibrary.execute_console_command(w, f'HighResShot 1920x1080 filename={OUT}/{name}.png')
        log('拍', name)
        st['phase'] += 1
        st['next'] = t + 2.5
    elif st['phase'] > len(SHOTS) and t > st['next']:
        unreal.unregister_slate_post_tick_callback(h)
        unreal.SystemLibrary.execute_console_command(w, 'quit')


h = unreal.register_slate_post_tick_callback(tick)
log('挂上逐帧回调')
