"""11-02 测音乐包（kind=gamepy，地图 /Game/Maps/L_Range?Range）：在 MusicPack 里临时写一个 6 秒的测试 wav（range_Range.wav，440 Hz），
关开一次音乐让它重新选曲，看日志有没有「[music] 音乐包：…」，然后删掉测试文件、退出。只为测读取，不是游戏资源。"""
import math
import os
import struct
import wave

import unreal

ROOT = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).replace('\\', '/').rstrip('/')
DIR = f'{ROOT}/MusicPack'
PATH = f'{DIR}/range_Range.wav'
log = lambda *a: unreal.log_warning('[musictest] ' + ' '.join(str(x) for x in a))
made_dir = not os.path.isdir(DIR)
os.makedirs(DIR, exist_ok=True)
with wave.open(PATH, 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(44100)
    w.writeframes(b''.join(struct.pack('<hh', v, v) for v in (int(6000 * math.sin(2 * math.pi * 440 * i / 44100)) for i in range(44100 * 6))))
log('写了', PATH)
world = None
step = {'n': 0}


def tick(dt):
    global world
    if world is None:
        world = unreal.find_object(None, '/Game/Maps/L_Range.L_Range')
        if world is None:
            return
    step['n'] += 1
    n = step['n']
    if n == 30:
        unreal.SystemLibrary.execute_console_command(world, 'mech.Music 0')
    elif n == 150:
        unreal.SystemLibrary.execute_console_command(world, 'mech.Music 1')
    elif n == 400:
        try:
            os.remove(PATH)
            if made_dir:
                os.rmdir(DIR)
        except OSError as e:
            log('删测试文件失败', e)
        log('结束')
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(world, 'quit')


handle = unreal.register_slate_post_tick_callback(tick)
