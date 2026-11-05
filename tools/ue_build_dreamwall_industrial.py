"""巨墙试拼：复用现成结构柱、墙片和工业设备，不创建几何。默认写独立试拼图。"""
from pathlib import Path
import json,unreal as u,math,os,shutil,datetime,hashlib,traceback
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_street_existing.py').read_text(encoding='utf8').split('\ntry:\n stamp=')[0],'industrial_helpers','exec'))
STUDY=os.environ.get('DF_WALL_FULL','0')!='1'
DST='/Game/Env/DreamfieldIndustrialStudy' if STUDY else '/Game/Env/DreamfieldIndustrial'
MAP='/Game/Maps/L_Dreamfield_WallStudy' if STUDY else '/Game/Maps/L_Dreamfield'
REPORT={'passed':False,'map':MAP,'study':STUDY,'materials':[]};catalog=json.loads((R/'Saved/Automation/DreamwallExistingProbe.json').read_text())
for kit,folder,names in [('mega','/Game/Env/MoonFree/Meshes',['SM_Column_Large_Straight']),('wall','/Game/Env/DreamfieldFree/Meshes',['SM_Free_Wall_Wall','SM_Free_Floor_Platform_2_Edge','SM_Free_Pillar_Side_D'])]:
 catalog[kit]={}
 for n in names:
  a=asset(folder+'/'+n);b=a.get_bounding_box()
  catalog[kit][n]={'path':a.get_path_name(),'min':[b.min.x/100,b.min.y/100,b.min.z/100],'max':[b.max.x/100,b.max.y/100,b.max.z/100]}

def concrete(name,tint):
 m=material(name);prop(m,'used_with_nanite',True);prop(m,'two_sided',True);prop(m,'tangent_space_normal',False)
 p=node(m,u.MaterialExpressionWorldPosition);n=node(m,u.MaterialExpressionVertexNormalWS)
 uv=custom(m,'float3 a=abs(N);return (a.x>a.y&&a.x>a.z?P.yz:(a.y>a.z?P.xz:P.xy))/300.;',[('P',p),('N',n)],u.CustomMaterialOutputType.CMOT_FLOAT2)
 root='/Game/Env/DreamfieldFree/Textures/T_Free_Weathered_'
 c=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'Diffuse'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR);link(uv,c,'UVs')
 # 错开同一现成贴图的方向与尺度，压低反差，避免远处成为规则竖条。
 uv2=custom(m,'return float2(-UV.y,UV.x)*.731+float2(.317,.683);',[('UV',uv)],u.CustomMaterialOutputType.CMOT_FLOAT2)
 c2=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'Diffuse'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR);link(uv2,c2,'UVs')
 c=custom(m,'float3 mixC=(C+C2)*.5;float l=dot(mixC,float3(.2126,.7152,.0722));return lerp(float3(.18,.18,.18),lerp(mixC,l.xxx,.9),.45)*Tint;',[('C',c),('C2',c2),('Tint',vec(m,tint))]);output(c,u.MaterialProperty.MP_BASE_COLOR)
 bump=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'nor_dx'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL);link(uv,bump,'UVs')
 normal=custom(m,'float3 a=abs(N),T,B;if(a.x>a.y&&a.x>a.z){T=float3(0,sign(N.x),0);B=float3(0,0,1);}else if(a.y>a.z){T=float3(-sign(N.y),0,0);B=float3(0,0,1);}else{T=float3(1,0,0);B=float3(0,sign(N.z),0);}return normalize(T*Bump.x*.12+B*Bump.y*.12+normalize(N)*max(Bump.z,.5));',[('N',n),('Bump',bump)])
 output(normal,u.MaterialProperty.MP_NORMAL);output(val(m,.94),u.MaterialProperty.MP_ROUGHNESS);output(val(m,.10),u.MaterialProperty.MP_SPECULAR)
 return finish(m)

def functional():
 return {a.get_actor_label():[round(v,3) for v in (a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z)] for a in S.get_all_level_actors() if isinstance(a,u.PlayerStart) or a.get_class().get_name()=='MechElevator' or any(str(t) in ('TeamSpawn0','TeamSpawn1','MechArena','ArenaWall') for t in a.tags) or a.get_actor_label() in ('Platform','RampN','RampS','WallCollision')}

try:
 backup=R/'Saved/Backup'/('DreamwallIndustrial_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
 for rel in ['Content/Maps/'+MAP.split('/')[-1]+'.umap','Content/'+DST.removeprefix('/Game/')]:
  p=R/rel
  if p.exists():
   d=backup/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copytree(p,d) if p.is_dir() else shutil.copy2(p,d)
 concretes=[concrete('MassConcrete'+str(i),t) for i,t in enumerate([(1.5,1.53,1.55),(1.25,1.29,1.31),(1.7,1.72,1.73)])]
 metal=material('DarkSteel');output(vec(metal,(.085,.095,.10)),u.MaterialProperty.MP_BASE_COLOR);output(val(metal,.65),u.MaterialProperty.MP_ROUGHNESS);output(val(metal,.35),u.MaterialProperty.MP_METALLIC);prop(metal,'used_with_nanite',True);metal=finish(metal)
 column,_=model('mega','SM_Column_Large_Straight');slots=list(column.static_materials)
 for i,s in enumerate(slots):
  name=str(s.get_editor_property('imported_material_slot_name'));prop(s,'material_interface',concretes[2] if name=='MI_Trim_02' else metal if 'Dark' in name else concretes[1]);slots[i]=s
 prop(column,'static_materials',slots);save(column)
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Dreamfield');assert world;expected=functional();expectedmarks=marks()
 for a in list(S.get_all_level_actors()):
  if a.get_actor_label().startswith(('Free_Wall_','DI_')):S.destroy_actor(a)
 # 大块墙面后退，结构柱与深挑檐形成体量；减少原来像仓库网格的白色细框。
 for bay in range(-45,46):
  y=bay*60
  for j in range(3):
   yy=y+(j-1)*20
   for tier in range(10):
    x=306 if tier<3 else 316 if tier<7 else 309
    put('wall','SM_Free_Wall_Wall',(x,yy,tier*20),90,(5,5,5),collide=False,mat=concretes[(bay//3)%2])
   for z in (70,146,198):
    put('wall','SM_Free_Floor_Platform_2_Edge',(306,yy,z),90,(5,5,2),collide=False,mat=concretes[0])
  column_y=y+30
  # 封锁门正上方留出门楼高度，柱脚避开三个既有大门。
  first=2 if any(abs(column_y-g)<24 for g in (-150,0,150)) else 0
  for tier in range(first,5):put('mega','SM_Column_Large_Straight',(300,column_y,tier*40),90,(14,10,4),collide=True)
  if bay%3!=1:
   for dz in (84,104):put('wall','SM_Free_Pillar_Side_D',(309,y-10,dz),90,(4,4,5),collide=False,mat=concretes[1])
  # 管线和检修设备集中到局部竖井，大片混凝土留白；远景不堆满细小零件。
  if abs(bay)<=20 and bay%3==0:
   for dy in (-4,0,4):
    for k in range(11):put('modular_industrial_pipes_01','modular_industrial_pipes_01_pipe02',(302,y+dy,42+k*11.7),0,(7,7,6),collide=False)
   for yy in (y-15,y+15):
    for z in (53,121,174):put('modular_airduct_rectangular_01','modular_airduct_rectangular_01_vent_02',(304,yy,z),90,(16,16,16),collide=False)
   for z in (39,101):
    put('modular_fire_escape','modular_fire_escape_platform_middle',(304,y,z),90,(2,2,2),collide=False)
    put('modular_fire_escape','modular_fire_escape_railing_middle',(304,y,z+1.54),90,(2,2,2),collide=False)
 # 现有平台、坡道、门楼使用同一粗糙混凝土色系；只改本图组件覆盖，不写共享资源。
 for a in S.get_all_level_actors():
  for c in a.get_components_by_class(u.MeshComponent):
   for i in range(c.get_num_materials()):
    m=c.get_material(i)
    if m and m.get_name() in ('M_FreeConcrete','M_FreeTrim'):c.set_material(i,concretes[0])
 total=0
 for i,((path,collision,matpath),g) in enumerate(groups.items()):
  a=instances('DI_'+str(i)+'_'+g['mesh'].get_name(),g['mesh'],g['transforms'],collision)
  if g['mat']:
   for c in a.get_components_by_class(u.MeshComponent):
    for k in range(c.get_num_materials()):c.set_material(k,g['mat'])
  total+=len(g['transforms'])
 assert functional()==expected and marks()==expectedmarks
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 REPORT.update(passed=True,instances=total,groups=len(groups),preserved_functional=expected,backup=str(backup),sources=sorted(used))
except Exception:REPORT['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation'/('DreamwallIndustrialStudy.json' if STUDY else 'DreamwallIndustrial.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
