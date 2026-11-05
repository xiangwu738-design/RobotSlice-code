"""用作者成套墙板替换细格线巨墙，保留弧面支撑与内部管线，不创建几何。"""
from pathlib import Path
import unreal as u,json,os,shutil,datetime,traceback
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_dreamwall_industrial.py').read_text(encoding='utf8').split('\ntry:\n backup=')[0],'bastion_helpers','exec'))
FULL=os.environ.get('DF_BASTION_FULL','0')=='1'
DST='/Game/Env/DreamfieldBastion' if FULL else '/Game/Env/DreamfieldBastionStudy'
MAP='/Game/Maps/L_Dreamfield' if FULL else '/Game/Maps/L_Dreamfield_BastionStudy'
REPORT={'passed':False,'map':MAP,'full':FULL,'materials':[]};own={};catalog['bastion']={}
oldmodel=model
def model(kit,name):
 if kit=='bastion':used.add(kit);return own[name],catalog[kit][name]
 return oldmodel(kit,name)

try:
 backup=R/'Saved/Backup'/('DreamwallBastion_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
 for rel in ['Content/Maps/'+MAP.split('/')[-1]+'.umap','Content/'+DST.removeprefix('/Game/')]:
  p=R/rel
  if p.exists():
   d=backup/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copytree(p,d) if p.is_dir() else shutil.copy2(p,d)
 concretes=[concrete('BastionConcrete'+str(i),t) for i,t in enumerate([(1.42,1.44,1.45),(1.14,1.17,1.19),(1.61,1.62,1.60)])]
 metal=material('BastionSteel');output(vec(metal,(.075,.085,.09)),u.MaterialProperty.MP_BASE_COLOR);output(val(metal,.8),u.MaterialProperty.MP_ROUGHNESS);output(val(metal,.1),u.MaterialProperty.MP_METALLIC);metal=finish(metal)
 decal=material('BastionHiddenDecal');prop(decal,'blend_mode',u.BlendMode.BLEND_MASKED);output(val(decal,0),u.MaterialProperty.MP_OPACITY_MASK);decal=finish(decal)
 src=R/'SourceAssets/moon_free/quaternius_scifi/Modular SciFi MegaKit[Standard]/FBX'
 for name in ['WallAstra_Straight','WallAstra_Straight_Broken','Column_Hollow','Platform_Metal2']:
  path=DST+'/Meshes/SM_'+name
  if not E.does_asset_exist(path):
   ui=u.FbxImportUI()
   for k,v in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),('import_textures',False),('import_animations',False),('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)]:prop(ui,k,v)
   data=ui.get_editor_property('static_mesh_import_data')
   for k,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('build_nanite',False),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)]:prop(data,k,v)
   task=u.AssetImportTask();task.filename=str(next(src.rglob(name+'.fbx')));task.destination_path=DST+'/Meshes';task.destination_name='SM_'+name;task.automated=True;task.replace_existing=False;task.save=False;task.options=ui;T.import_asset_tasks([task])
  mesh=asset(path);slots=list(mesh.static_materials)
  for i,s in enumerate(slots):
   slot=str(s.get_editor_property('imported_material_slot_name'))
   mat=decal if 'Decal' in slot else metal if 'Cables' in slot else concretes[1] if 'Dark' in slot else concretes[2] if 'Trim_02' in slot else concretes[1] if 'Trim_01' in slot else concretes[0]
   prop(s,'material_interface',mat);slots[i]=s
  prop(mesh,'static_materials',slots);body=mesh.get_editor_property('body_setup');prop(body,'collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);prop(body,'double_sided_geometry',True);save(mesh)
  b=mesh.get_bounding_box();catalog['bastion'][name]={'path':mesh.get_path_name(),'min':[b.min.x/100,b.min.y/100,b.min.z/100],'max':[b.max.x/100,b.max.y/100,b.max.z/100]};own[name]=mesh
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Dreamfield');expected=functional();expectedmarks=marks()
 for a in list(S.get_all_level_actors()):
  if a.get_actor_label().startswith(('Free_Wall_','DI_','DB_')):S.destroy_actor(a)
 # 作者闭合空心结构柱形成完整厚墙墩；墙片退入墩后，避免薄板支架当作巨墙。
 for bay in range(-45,46):
  y=bay*60
  for tier in (0,1):
   x=318 if tier==0 else (322,326,324,320)[bay%4];z=tier*100
   broken=tier==1 and bay in (-14,-8,-3,5,12,19)
   name='WallAstra_Straight_Broken' if broken else 'WallAstra_Straight'
   put('bastion',name,(x,y,z),0,(14,15,100/3),origin=(-2,0,0),collide=True)
  column_y=y+30
  start=90 if abs(column_y)<45 else 70 if min(abs(column_y-150),abs(column_y+150))<28 else 0
  put('bastion','Column_Hollow',(303,column_y,start),0,(20,20,(200-start)/5),origin=(0,0,0),collide=True,mat=concretes[0])
  put('bastion','Platform_Metal2',(309.5,y,200.2),0,(39/4,15,1),origin=(0,0,0),collide=True,mat=concretes[0])
  # 后墙封闭破损墙板背后，形成有厚度的凹槽，不暴露天空。
  for j in (-1,0,1):
   for tier in range(10):put('wall','SM_Free_Wall_Wall',(329,y+j*20,tier*20),90,(5,5,5),collide=False,mat=concretes[1])
 # 平台与门楼只做组件材质覆盖，不修改共享网格或功能位置。
 for a in S.get_all_level_actors():
  for c in a.get_components_by_class(u.MeshComponent):
   for i in range(c.get_num_materials()):
    m=c.get_material(i)
    if m and m.get_name() in ('M_FreeConcrete','M_FreeTrim'):c.set_material(i,concretes[0])
 counts={}
 for i,((path,collision,matpath),g) in enumerate(groups.items()):
  name='DB_'+str(i)+'_'+g['mesh'].get_name();a=instances(name,g['mesh'],g['transforms'],collision)
  if g['mat']:
   for c in a.get_components_by_class(u.MeshComponent):
    for k in range(c.get_num_materials()):c.set_material(k,g['mat'])
  counts[name]=len(g['transforms'])
 assert functional()==expected and marks()==expectedmarks
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 REPORT.update(passed=True,instances=counts,total_instances=sum(counts.values()),groups=len(groups),preserved_functional=expected,backup=str(backup),source='Quaternius Modular Sci-Fi MegaKit Standard + loafbrr Brutalist buildings + Poly Haven concrete_wall_006; CC0')
except Exception:REPORT['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation'/('DreamwallBastion.json' if FULL else 'DreamwallBastionStudy.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
