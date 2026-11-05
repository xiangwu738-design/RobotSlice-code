"""月球现成模块版的固定视角检查，保持用户画质和配置。"""
from pathlib import Path
src=Path('D:/RobotSlice/tools/ue_dreamfield_shots.py').read_text(encoding='utf8')
start=src.index('SHOTS = [');end=src.index('\nQUAL =',start)
shots=[('moon_overview',(-185,-230,110,'a'),(0,0,12),66,None),('moon_dome',(-95,-80,16,'a'),(0,0,18),73,None)]
src=src[:start]+'SHOTS = '+repr(shots)+'\n'+src[end:];src=src.replace('L_Dreamfield','L_MoonDome')
start=src.index('            # 碰撞自检（');end=src.index('\n        return',start);src=src[:start]+src[end:]
src=src.replace("f'HighResShot {RES} filename={OUT}/{name}.png'","f'Shot filename={OUT}/{name}.png -nosuffix'")
exec(compile(src,'moon_free_shots','exec'))
