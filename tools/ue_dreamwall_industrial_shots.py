"""巨墙试拼的固定视角，保持当前画质；只在运行时移动镜头。"""
from pathlib import Path
import os
src=Path('D:/RobotSlice/tools/ue_dreamfield_free_final_shots.py').read_text(encoding='utf8')
if os.environ.get('DF_WALL_FULL','0')!='1':src=src.replace('L_Dreamfield','L_Dreamfield_WallStudy')
src=src.replace('filename={OUT}/{name}.png nosuffix','filename={OUT}/{name}.png -nosuffix')
exec(compile(src,'dreamwall_industrial_shots','exec'))
