"""新建蓝穹月球基地和千禧老街；保留原关卡文件、地形和玩法标记。"""
import unreal as u,json,math,random,traceback,hashlib
from pathlib import Path
R=Path('D:/RobotSlice');SRC=R/'SourceAssets/original_developer_maps';DST='/Game/Env/OriginalDeveloperMaps'
E=u.EditorAssetLibrary;M=u.MaterialEditingLibrary;T=u.AssetToolsHelpers.get_asset_tools();S=u.get_editor_subsystem(u.EditorActorSubsystem)
REPORT={'passed':False,'materials':[],'meshes':{},'maps':{}}
def prop(o,k,v):o.set_editor_property(k,v)
def asset(p):
 a=u.load_asset(p);assert a,p;return a
def save(a):
 assert a.get_path_name().startswith('/Game/');assert E.save_loaded_asset(a,False)
def node(m,c,**kw):
 a=M.create_material_expression(m,c)
 for k,v in kw.items():prop(a,k,v)
 return a
def link(a,b,name='',out=''):assert M.connect_material_expressions(a,out,b,name)
def output(n,p):assert M.connect_material_property(n,'',p)
def val(m,x):return node(m,u.MaterialExpressionConstant,r=x)
def vec(m,v):return node(m,u.MaterialExpressionConstant3Vector,constant=u.LinearColor(*v,1))
def custom(m,code,inputs,kind=u.CustomMaterialOutputType.CMOT_FLOAT3):
 n=node(m,u.MaterialExpressionCustom,code=code,output_type=kind);ins=[]
 for name,a in inputs:
  i=u.CustomInput();prop(i,'input_name',name);ins.append(i)
 prop(n,'inputs',ins)
 for name,a in inputs:link(a,n,name)
 return n
def material(name):
 p=DST+'/Materials/M_'+name;m=u.load_asset(p) if E.does_asset_exist(p) else T.create_asset('M_'+name,DST+'/Materials',u.Material,u.MaterialFactoryNew())
 for _ in range(12):
  if not M.get_num_material_expressions(m):break
  M.delete_all_material_expressions(m)
 prop(m,'used_with_instanced_static_meshes',True);return m
def finish(m):
 errors=M.recompile_material(m);assert not errors,(m.get_name(),list(errors));save(m);REPORT['materials'].append(m.get_name());return m
COLORS={'Shell':(.48,.53,.57),'Dark':(.035,.055,.075),'Steel':(.19,.24,.29),'Blue':(.015,.16,.32),'Light':(.06,.55,1),'Glass':(.025,.08,.12),'Plaster':(.46,.40,.30),'Brick':(.21,.105,.06),'Tile':(.30,.32,.29),'Rust':(.20,.09,.045),'Road':(.07,.07,.065),'Concrete':(.27,.25,.21),'Leaf':(.11,.19,.045)}
CODE='''
struct Fn { float h(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);} float n(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(h(i),h(i+float2(1,0)),f.x),lerp(h(i+float2(0,1)),h(i+1),f.x),f.y);} }; Fn fn;
float3 p=P*.01,an=abs(N);float2 q=an.z>.7?p.xy:(an.x>an.y?p.yz:p.xz);
float stain=fn.n(q*float2(.8,.12))*fn.n(q*.3+3);float fine=fn.n(q*37.);float shade=.82+.23*stain+.08*fine;
if(Kind==1){float2 b=q/float2(.27,.09);b.x+=.5*fmod(floor(b.y),2);float2 f=frac(b);float edge=min(min(f.x,1-f.x),min(f.y,1-f.y));float mortar=1-smoothstep(.03,.075,edge);return lerp(Tint*(.65+.6*fn.h(floor(b))),float3(.20,.18,.14),mortar);}
if(Kind==2){float2 b=q/.12;float2 f=frac(b);float seam=1-smoothstep(.02,.05,min(min(f.x,1-f.x),min(f.y,1-f.y)));return Tint*shade*(1-.25*seam);}
if(Kind==3){shade=.72+.4*fn.n(q*1.8)+.12*fine;}
return Tint*shade;
'''
def V(x,y,z):return u.Vector(x*100,y*100,z*100)
def xf(x,y,z=0,yaw=0,scale=(1,1,1)):return u.Transform(V(x,y,z),u.Rotator(yaw=yaw),u.Vector(*scale))
def label(a,name,folder):a.set_actor_label(name);a.tags=['OriginalDeveloperMap'];a.set_folder_path('Original/'+folder);return a
def mesh_actor(name,mesh,x,y,z=0,yaw=0,scale=(1,1,1),collide=True):
 a=label(S.spawn_actor_from_class(u.StaticMeshActor,V(x,y,z),u.Rotator(yaw=yaw)),name,'Buildings');a.set_actor_scale3d(u.Vector(*scale));c=a.static_mesh_component
 c.set_static_mesh(mesh);prop(c,'mobility',u.ComponentMobility.STATIC);c.set_collision_profile_name('BlockAll' if collide else 'NoCollision')
 if not collide:c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
 return a
def instances(name,mesh,tfs,collide):
 a=label(S.spawn_actor_from_class(u.Actor,u.Vector()),name,'Street')
 sds=u.get_engine_subsystem(u.SubobjectDataSubsystem);root=sds.k2_gather_subobject_data_for_instance(a)[0]
 handle,fail=sds.add_new_subobject(u.AddNewSubobjectParams(parent_handle=root,new_class=u.HierarchicalInstancedStaticMeshComponent,blueprint_context=None));assert not str(fail),str(fail)
 c=u.SubobjectDataBlueprintFunctionLibrary.get_object(u.SubobjectDataBlueprintFunctionLibrary.get_data(handle));c.set_static_mesh(mesh);prop(c,'mobility',u.ComponentMobility.STATIC)
 c.set_collision_profile_name('BlockAll' if collide else 'NoCollision');c.add_instances(tfs,False,True);return a
def marks():
 return {a.get_actor_label():{'position':[round(v,4) for v in (a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z)],'tags':[str(t) for t in a.tags]} for a in S.get_all_level_actors() if isinstance(a,u.PlayerStart) or any(str(t) in ['DMSpawn','TeamSpawn0','TeamSpawn1','MechArena','OldStreetBounds','ArenaWall','OldStreetWall','OldStreetCeiling'] for t in a.tags)}
def visual_refs(a):
 refs=[]
 for c in a.get_components_by_class(u.MeshComponent):
  if isinstance(c,u.StaticMeshComponent) and c.static_mesh:refs.append(c.static_mesh.get_path_name())
  refs += [c.get_material(i).get_path_name() for i in range(c.get_num_materials()) if c.get_material(i)]
 return refs
try:
 # 所有改写目标都在新目录；重跑前自动留存本次生成的旧版本。
 import shutil,datetime
 stamp=datetime.datetime.now().strftime('%Y%m%d_%H%M%S');backup=R/'Saved/Backup'/('OriginalMapsRebuild_'+stamp)
 for rel in ['Content/Env/OriginalDeveloperMaps','Content/Maps/L_MoonDome.umap','Content/Maps/L_MillenniumStreet.umap']:
  p=R/rel
  if p.exists():
   dest=backup/rel;dest.parent.mkdir(parents=True,exist_ok=True)
   shutil.copytree(p,dest) if p.is_dir() else shutil.copy2(p,dest)
 originals={n:hashlib.sha256((R/f'Content/Maps/L_{n}.umap').read_bytes()).hexdigest() for n in ['Moon','OldStreet']}
 task=u.AssetImportTask();task.filename=str(SRC/'T_OS2_Signs.png');task.destination_path=DST+'/Textures';task.destination_name='T_OS2_Signs';task.automated=True;task.replace_existing=True;task.save=True;T.import_asset_tasks([task])
 mats={}
 for name,color in COLORS.items():
  m=material(name);p=node(m,u.MaterialExpressionWorldPosition);normal=node(m,u.MaterialExpressionVertexNormalWS)
  if name=='Light':output(vec(m,tuple(v*3 for v in color)),u.MaterialProperty.MP_EMISSIVE_COLOR)
  else:
   kind={'Brick':1,'Tile':2,'Road':3}.get(name,0)
   n=custom(m,CODE,[('P',p),('N',normal),('Tint',vec(m,color)),('Kind',val(m,kind))]);output(n,u.MaterialProperty.MP_BASE_COLOR)
   if name=='Leaf':prop(m,'two_sided',True);prop(m,'shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE);output(vec(m,color),u.MaterialProperty.MP_SUBSURFACE_COLOR)
  output(val(m,.24 if name=='Glass' else .48 if name in ('Steel','Shell') else .86),u.MaterialProperty.MP_ROUGHNESS)
  output(val(m,.6 if name=='Steel' else .1),u.MaterialProperty.MP_METALLIC);mats[name]=finish(m)
 m=material('Sign');tx=node(m,u.MaterialExpressionTextureSample,texture=asset(DST+'/Textures/T_OS2_Signs'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR);output(tx,u.MaterialProperty.MP_BASE_COLOR);output(val(m,.85),u.MaterialProperty.MP_ROUGHNESS);mats['Sign']=finish(m)
 m=material('Dome');prop(m,'blend_mode',u.BlendMode.BLEND_TRANSLUCENT);prop(m,'shading_model',u.MaterialShadingModel.MSM_UNLIT);prop(m,'two_sided',True)
 p=node(m,u.MaterialExpressionWorldPosition);cam=node(m,u.MaterialExpressionCameraPositionWS);normal=node(m,u.MaterialExpressionVertexNormalWS)
 rim=custom(m,'return pow(1-abs(dot(normalize(Camera-P),N)),3);',[('P',p),('Camera',cam),('N',normal)],u.CustomMaterialOutputType.CMOT_FLOAT1)
 line=custom(m,'float3 q=P*.002;float a=min(abs(frac(q.z)-.5),abs(frac(atan2(q.y,q.x)*24/6.283185)-.5));return 1-smoothstep(.009,.024,a);',[('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT1)
 glow=custom(m,'return float3(.025,.27,.9)*(1+Rim*2.5+Line*.45);',[('Rim',rim),('Line',line)]);opacity=custom(m,'return .065+Rim*.30+Line*.075;',[('Rim',rim),('Line',line)],u.CustomMaterialOutputType.CMOT_FLOAT1)
 output(glow,u.MaterialProperty.MP_EMISSIVE_COLOR);output(opacity,u.MaterialProperty.MP_OPACITY);mats['Dome']=finish(m)
 manifest=json.loads((SRC/'manifest.json').read_text(encoding='utf8'));meshes={}
 for name,info in manifest['meshes'].items():
  ui=u.FbxImportUI()
  for k,v in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),('import_textures',False),('import_animations',False),('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)]:prop(ui,k,v)
  data=ui.get_editor_property('static_mesh_import_data')
  for k,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('build_nanite',False),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)]:prop(data,k,v)
  task=u.AssetImportTask();task.filename=str(SRC/(name+'.fbx'));task.destination_path=DST+'/Meshes';task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=False;task.options=ui;T.import_asset_tasks([task]);mesh=asset(DST+'/Meshes/'+name)
  slots=list(mesh.get_editor_property('static_materials'))
  for i,s in enumerate(slots):
   slot=str(s.get_editor_property('imported_material_slot_name'));assert slot in mats,slot;prop(s,'material_interface',mats[slot]);slots[i]=s
  prop(mesh,'static_materials',slots);body=mesh.get_editor_property('body_setup')
  prop(body,'collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);prop(body,'double_sided_geometry',True)
  save(mesh);meshes[name]=mesh;REPORT['meshes'][name]=mesh.get_num_triangles(0)
 # 月球：只换基地建筑；月面、地球、灯光、岩石、出生点与重力沿用开发图。
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Moon');assert world;expected=marks();removed=[]
 for a in list(S.get_all_level_actors()):
  if any('SM_K65_' in p for p in visual_refs(a)) or a.actor_has_tag('MoonAir'):
   removed.append(a.get_actor_label());S.destroy_actor(a)
 L=json.loads((R/'Inbox/_moon_nasa/work/layout_final.json').read_text(encoding='utf8'));placed=[]
 for index,it in enumerate(L['items']):
  g=it['group'];x=it['cx'];y=it['cy'];z=it.get('pad_z',it['z']);yaw=it.get('yaw',0)
  if g=='XLG_C':continue
  if g.startswith(('MD_','LG_','XLG_')):typ='Hangar' if max(it['w'],it['d'])>85 else 'Habitat'
  elif g.startswith('SM_') or g.startswith('P_Building'):typ='Workshop'
  elif g.startswith('P_Tank'):typ='Tanks'
  elif g.startswith(('P_Antenna','P_Tower')):typ='Antenna'
  elif g.startswith(('P_Cooling','P_Pipes')):typ='Power'
  elif g.startswith(('P_Crate','P_BlastCover')):typ='Cargo'
  else:continue
  base={'Hangar':(67,45),'Habitat':(49,31),'Workshop':(29,23),'Tanks':(19,13),'Antenna':(8,6),'Power':(14,18),'Cargo':(5,3)}[typ]
  # 尺寸只做有限缩放，复用原平整场地；旋转后用对称包围盒限制避免越过道路。
  radians=math.radians(yaw);bw=abs(math.cos(radians))*base[0]+abs(math.sin(radians))*base[1];bd=abs(math.sin(radians))*base[0]+abs(math.cos(radians))*base[1]
  scale=min(1.35,max(.45,min(it['w']/bw,it['d']/bd)*.90))
  mesh_actor('OM_'+str(index)+'_'+typ,meshes['SM_OM_'+typ],x,y,z,yaw,(scale,scale,scale));placed.append(typ)
 z=L['items'][0]['pad_z']
 for part in ('DomeInterior','DomeRing','Dome'):
  a=mesh_actor('OM_'+part,meshes['SM_OM_'+part],0,0,z)
  if part=='Dome':
   c=a.static_mesh_component;c.set_collision_profile_name('Custom');c.set_collision_enabled(u.CollisionEnabled.QUERY_ONLY);c.set_collision_object_type(u.CollisionChannel.ECC_WORLD_STATIC)
   c.set_collision_response_to_all_channels(u.CollisionResponseType.ECR_IGNORE)
   c.set_collision_response_to_channel(u.CollisionChannel.ECC_VISIBILITY,u.CollisionResponseType.ECR_BLOCK)
   prop(c,'cast_shadow',False);prop(c,'affect_distance_field_lighting',False)
   a.tags=['OriginalDeveloperMap','MoonShield','MoonAir','Radius=6200','Height=3800']
 assert marks()==expected
 assert world.get_world_settings().get_editor_property('global_gravity_z')==-327.
 assert not any('SM_K65_' in p for a in S.get_all_level_actors() for p in visual_refs(a))
 assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/L_MoonDome')
 REPORT['maps']['L_MoonDome']={'removed_building_actors':len(removed),'new_buildings':len(placed),'preserved_markers':len(expected),'gravity':-327,'dome_radius_m':62,'dome_height_m':38}
 # 老街：保留世界光照、边界、彩蛋与循环点；所有旧街景网格及贴片全部换掉。
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_OldStreet');assert world;expected=marks();removed=[]
 for a in list(S.get_all_level_actors()):
  name=a.get_actor_label()
  if any('/Game/Env/OldStreet/' in p for p in visual_refs(a)) or name.startswith(('Full_','Low_','Town_','Backdrop_')):
   removed.append(name);S.destroy_actor(a)
 L=json.loads((R/'Inbox/_oldstreet/layout.json').read_text(encoding='utf8'));P=L['PERIOD'];full=[-1,0,1];far=[-5,-4,-3,-2,2,3,4,5]
 for scope,ks,collide in [('Full',full,True),('Far',far,False)]:
  instances('OS2_'+scope+'_Ground',meshes['SM_OS2_Ground'],[xf(k*P,0) for k in ks],collide)
  for typ in 'ABDEF':
   rows=[it for it in L['street'] if it['type']==typ]
   instances('OS2_'+scope+'_Building'+typ,meshes['SM_OS2_Building'+typ],[xf(it['X']+k*P,it['Y'],.12,it['yaw']) for k in ks for it in rows],collide)
  instances('OS2_'+scope+'_Furniture',meshes['SM_OS2_StreetFurniture'],[xf(k*P,0) for k in ks],False)
  trees=instances('OS2_'+scope+'_Trees',meshes['SM_OS2_Tree'],[xf(it['X']+k*P,it['Y'],.1) for k in ks for it in L['trees']],False)
  for c in trees.get_components_by_class(u.MeshComponent):prop(c,'affect_distance_field_lighting',False)
 for typ in 'ABDEF':
  rows=[it for it in L['town'] if it['type']==typ]
  if rows:instances('OS2_Town_'+typ,meshes['SM_OS2_Building'+typ],[xf(it['X']+k*P,it['Y'],.12,it['yaw']) for k in full+far for it in rows],False)
 # 树干用原生圆柱简化碰撞，植物叶片不挡炮弹。
 instances('OS2_TrunkCollisions',asset('/Engine/BasicShapes/Cylinder'),[xf(it['X']+k*P,it['Y'],1.5,scale=(.3,.3,3)) for k in full for it in L['trees']],True)
 for a in S.get_all_level_actors():
  if a.get_actor_label()=='OS2_TrunkCollisions':
   for c in a.get_components_by_class(u.MeshComponent):prop(c,'visible',False);prop(c,'hidden_in_game',True)
 assert marks()==expected
 assert not any('/Game/Env/OldStreet/' in p for a in S.get_all_level_actors() for p in visual_refs(a))
 assert u.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/L_MillenniumStreet')
 REPORT['maps']['L_MillenniumStreet']={'removed_visual_actors':len(removed),'building_types':5,'street_buildings':len(L['street'])*len(full+far),'preserved_markers':len(expected),'period_m':P,'full_copies':full,'far_copies':far}
 for n,h in originals.items():assert hashlib.sha256((R/f'Content/Maps/L_{n}.umap').read_bytes()).hexdigest()==h,n
 REPORT['original_maps_unchanged']=True;REPORT['passed']=True
except Exception:REPORT['error']=traceback.format_exc();u.log_error(REPORT['error']);raise
finally:
 (R/'Saved/Automation/OriginalDeveloperMaps.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
