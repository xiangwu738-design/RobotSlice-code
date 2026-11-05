"""等待游戏场景完成物理注册后运行通道检查；NullRHI，无截图。"""
import unreal as u,time
from pathlib import Path
started=time.monotonic();done=False
def tick(dt):
 global done
 if done:return
 world=u.find_object(None,'/Game/Maps/L_MoonDome.L_MoonDome')
 if not world or time.monotonic()-started<3:return
 pc=u.GameplayStatics.get_player_controller(world,0)
 if not pc or not pc.get_controlled_pawn():
  if time.monotonic()-started>60:
   done=True;u.log_error('Moon walkway game world unavailable');u.SystemLibrary.execute_console_command(world,'quit')
  return
 done=True
 try:
  exec(compile(Path('D:/RobotSlice/tools/ue_verify_moon_walkways.py').read_text(encoding='utf8'),'moon_walkways_verify','exec'),{'MOON_RUNTIME_WORLD':world})
 finally:u.SystemLibrary.execute_console_command(world,'quit')
handle=u.register_slate_post_tick_callback(tick)
