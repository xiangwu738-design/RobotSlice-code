"""月球基地：照 bat 的启动方式（?Range -MechInfiniteEnergy）进图走一下：记机体落地位置、世界重力，拍一张第三人称画面，然后退出。"""
import unreal

st = {'w': None, 't': 0.0, 'shot': False}


def tick(dt):
    w = st['w']
    if w is None:
        st['w'] = unreal.find_object(None, '/Game/Maps/L_Moon.L_Moon')
        return
    st['t'] += dt
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    p = pc.get_controlled_pawn() if pc else None
    if st['t'] > 9.0 and not st['shot']:
        st['shot'] = True
        l = p.get_actor_location() if p else None
        ws = w.get_world_settings()
        n = len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Pawn))
        unreal.log(f'[rangecheck] 机体位置 {l} 世界重力 global_gravity_z={ws.get_editor_property("global_gravity_z")} 机体+木桩数 {n}')
        unreal.SystemLibrary.execute_console_command(w, 'HighResShot 1920x1080 filename=D:/RobotSlice/Saved/Screenshots/Moon/range_check.png')
    if st['t'] > 11.0:
        st['t'] = -1e9
        unreal.log('[rangecheck] 退出')
        unreal.SystemLibrary.execute_console_command(w, 'quit')


unreal.register_slate_post_tick_callback(tick)
