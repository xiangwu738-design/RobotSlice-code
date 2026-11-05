"""资源拼装后的两处固定视角；保持用户画质，普通截图避免高分辨率重采样。"""
from pathlib import Path
src=Path('D:/RobotSlice/tools/ue_dreamfield_shots.py').read_text(encoding='utf8')
start=src.index('SHOTS = [');end=src.index('\nQUAL =',start)
shots=[('street_eye',(-43,-2,3.5,'a'),(9,2,6),76,None),('street_overview',(-37,-37,32,'a'),(8,4,6),68,None)]
src=src[:start]+'SHOTS = '+repr(shots)+'\n'+src[end:]
src=src.replace('L_Dreamfield','L_MillenniumStreet')
start=src.index('            # 碰撞自检（');end=src.index('\n        return',start);src=src[:start]+src[end:]
src=src.replace("f'HighResShot {RES} filename={OUT}/{name}.png'","f'Shot filename={OUT}/{name}.png -nosuffix'")
exec(compile(src,'street_existing_shots','exec'))
