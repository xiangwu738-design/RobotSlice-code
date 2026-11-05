"""只修正现有店招平面的 UV 方向，不重建关卡或修改几何。"""
from pathlib import Path
import textwrap
source=Path('D:/RobotSlice/tools/ue_build_street_existing.py').read_text(encoding='utf8')
exec(compile(source.split('\ntry:\n')[0],'street_sign_helpers','exec'))
backup=R/'Saved/Backup'/('StreetSignOrientation_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
for i in range(8):
 p=R/('Content/Env/MillenniumStreetExisting/Materials/M_ShopSign'+str(i)+'.uasset');shutil.copy2(p,backup/p.name)
block=source[source.index(' signs=[]\n'):source.index(' world=u.EditorLoadingAndSavingUtils.load_map(MAP)')]
exec(compile(textwrap.dedent(block),'street_sign_materials','exec'))
assert len(signs)==8
u.log('[street] corrected 8 sign UV orientations; map unchanged')
