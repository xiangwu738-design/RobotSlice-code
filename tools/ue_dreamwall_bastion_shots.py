from pathlib import Path
import os
code=Path('D:/RobotSlice/tools/ue_dreamfield_free_final_shots.py').read_text(encoding='utf8')
if os.environ.get('DF_BASTION_FULL','0')!='1':code=code.replace('L_Dreamfield','L_Dreamfield_BastionStudy')
code=code.replace(' nosuffix',' -nosuffix')
code=code.replace('SHOTS = [',"SHOTS = [\n    ('15_dream_vista', (-380,-650,5,'g'),(300,40,145),84,None),")
exec(compile(code,'bastion_shots','exec'))
