"""用户要看新地图：每图固定视角预览，复用会还原画质和分辨率的截图脚本。"""
import os
from pathlib import Path
kind=os.environ['ORIGINAL_SHOT_MAP']
shots=[('moon_overview',(-185,-230,110,'a'),(0,0,12),66,None),('moon_dome',(-95,-80,16,'a'),(0,0,18),73,None)] if kind=='moon' else [('street_eye',(-43,-2,3.5,'a'),(9,2,7),76,None),('street_overview',(-37,-37,32,'a'),(8,4,6),68,None)]
src=Path('D:/RobotSlice/tools/ue_dreamfield_shots.py').read_text(encoding='utf8')
start=src.index('SHOTS = [');end=src.index('\nQUAL =',start)
src=src[:start]+'SHOTS = '+repr(shots)+'\n'+src[end:]
src=src.replace('L_Dreamfield','L_MoonDome' if kind=='moon' else 'L_MillenniumStreet')
start=src.index('            # 碰撞自检（');end=src.index('\n        return',start)
src=src[:start]+src[end:]
exec(compile(src,'original_map_shots','exec'))
