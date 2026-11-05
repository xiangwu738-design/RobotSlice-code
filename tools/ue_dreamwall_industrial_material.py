"""只更新巨墙试拼的混凝土材质，保留备份；不重建关卡。"""
from pathlib import Path
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_dreamwall_industrial.py').read_text(encoding='utf8').split('\ntry:\n backup=')[0],'dreamwall_material_helpers','exec'))
backup=R/'Saved/Backup'/('DreamwallMaterial_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir(parents=True)
source=R/'Content'/DST.removeprefix('/Game/')/'Materials'
shutil.copytree(source,backup/'Materials')
for i,t in enumerate([(1.5,1.53,1.55),(1.25,1.29,1.31),(1.7,1.72,1.73)]):concrete('MassConcrete'+str(i),t)
REPORT.update(passed=True,backup=str(backup))
(R/'Saved/Automation/DreamwallMaterial.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
