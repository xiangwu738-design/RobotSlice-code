"""同步更新草叶与地面材质，避免为远近配色微调重建全图实例。"""
from pathlib import Path
import shutil,datetime,json
R=Path('D:/RobotSlice')
source=(R/'tools/ue_dreamfield_free_assets.py').read_text(encoding='utf8').split('\nbackup=R/')[0]
exec(compile(source,'free_grass_material_helpers','exec'))
b=R/'Saved/Backup'/('DreamfieldGrassMaterial_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
b.mkdir(parents=True)
for name in ('FreeGrass','FreeGround'):
 p=R/('Content/'+DST.removeprefix('/Game/')+'/Materials/M_'+name+'.uasset')
 shutil.copy2(p,b/p.name)
g=[asset(DST+'/Textures/T_Free_Grass_'+role) for role in ('Diffuse','nor_dx','Rough','Alpha')]
texmat('FreeGrass',g[0],g[1],g[2],foliage=True,alpha=g[3])
ground=[asset(DST+'/Textures/T_Free_Ground_'+role) for role in ('Diffuse','nor_dx','Rough')]
texmat('FreeGround',*ground,world=True)
(R/'Saved/Automation/DreamfieldGrassMaterial.json').write_text(json.dumps({'passed':True,'world_space_upward_normals':True,'shared_world_ground_color':True,'materials':[DST+'/Materials/M_FreeGrass',DST+'/Materials/M_FreeGround'],'backup':str(b)},indent=2))
