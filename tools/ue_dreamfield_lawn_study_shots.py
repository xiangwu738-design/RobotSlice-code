from pathlib import Path
import os
code=Path('D:/RobotSlice/tools/ue_dreamfield_free_final_shots.py').read_text(encoding='utf8')
if os.environ.get('DF_LAWN_FULL','0')!='1':code=code.replace('L_Dreamfield','L_Dreamfield_LawnStudy')
code=code.replace(' nosuffix',' -nosuffix')
if os.environ.get('DF_LAWN_AO_DIAG','0')=='1':
 code=code.replace("state['phase'] = 'shoot'", "state['phase'] = 'shoot'\n            unreal.SystemLibrary.execute_console_command(world,'r.AmbientOcclusionLevels 0')")
exec(compile(code,'lawn_study_shots','exec'))
