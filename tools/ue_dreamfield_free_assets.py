"""免费模型拼装方案：先在 L_Dreamfield_FreeStudy 小范围验证，原关卡保存前完整备份。"""
import unreal as u,json,sys,math,random,shutil,datetime,os
from pathlib import Path
root=Path('D:/RobotSlice')
helpers=(root/'tools/ue_build_original_developer_maps.py').read_text(encoding='utf8').split('\ntry:\n # 所有改写目标')[0]
exec(compile(helpers,'free_asset_helpers','exec'))
SRC=R/'SourceAssets/dreamfield_free';DST='/Game/Env/DreamfieldFree';REPORT={'passed':False,'materials':[],'meshes':{}}
sys.path.insert(0,str(R/'tools'));import dreamfield_terrain as terrain
STUDY=os.environ.get('DF_FREE_FULL','0')!='1';MAP='/Game/Maps/L_Dreamfield_FreeStudy' if STUDY else '/Game/Maps/L_Dreamfield'
DST='/Game/Env/DreamfieldFreeStudy' if STUDY else '/Game/Env/DreamfieldFree'
# 先检查坡面旋转接口，防止改写材质后才发现 API 不可用。
u.MathLibrary.make_rot_from_zx(u.Vector(0,0,1),u.Vector(1,0,0))

def functional():
 return {a.get_actor_label():[round(v,2) for v in (a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z)] for a in S.get_all_level_actors() if isinstance(a,u.PlayerStart) or a.get_class().get_name()=='MechElevator' or any(str(t) in ('TeamSpawn0','TeamSpawn1','MechArena','ArenaWall') for t in a.tags) or a.get_actor_label() in ('Platform','RampN','RampS','WallCollision')}
def texture(name,file,normal=False,linear=False,flip=False):
 task=u.AssetImportTask();task.filename=str(file);task.destination_path=DST+'/Textures';task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=False;T.import_asset_tasks([task])
 t=asset(DST+'/Textures/'+name)
 if isinstance(t,u.Texture2D):
  prop(t,'srgb',not(normal or linear));prop(t,'compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if normal else u.TextureCompressionSettings.TC_DEFAULT)
  if normal:prop(t,'flip_green_channel',flip)
 save(t);return t
def sample(m,t,uv=None,normal=False,linear=False):
 n=node(m,u.MaterialExpressionTextureSample,texture=t,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if linear else u.MaterialSamplerType.SAMPLERTYPE_COLOR)
 if uv:link(uv,n,'UVs')
 return n
def texmat(name,d,n,r,uvscale=1,world=False,foliage=False,alpha=None,trim=False):
 m=material(name);uv=node(m,u.MaterialExpressionTextureCoordinate,u_tiling=uvscale,v_tiling=uvscale)
 if world:
  p=node(m,u.MaterialExpressionWorldPosition);uv=custom(m,'return P.xy*.01/4.;',[('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT2)
 base=sample(m,d,uv)
 if world:base=custom(m,'return lerp(C,float3(.1593,.1198,.0498),.35)*float3(.52,.80,.33);',[('C',base)])
 if foliage:
  # 草叶与地面共用世界坐标底色，压低叶片图集明暗反差，避免近处像撒了一层碎片。
  p=node(m,u.MaterialExpressionWorldPosition)
  ground_uv=custom(m,'return P.xy*.01/4.;',[('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT2)
  ground_color=sample(m,asset(DST+'/Textures/T_Free_Ground_Diffuse'),ground_uv)
  base=custom(m,'float shade=clamp(pow(max(dot(C,float3(.2126,.7152,.0722)),.001)/.124,.35),.8,1.2);return lerp(G,float3(.1593,.1198,.0498),.35)*float3(.60,.80,1.0)*1.6*shade;',[('C',base),('G',ground_color)])
 output(base,u.MaterialProperty.MP_BASE_COLOR)
 normalmap=sample(m,n,uv,normal=True)
 if foliage:
  # 草坪用偏向上方的世界空间法线接光，避免薄叶朝向把整片近草压成黑色。
  prop(m,'tangent_space_normal',False)
  normalmap=custom(m,'return normalize(lerp(N,float3(0,0,1),.85));',[('N',node(m,u.MaterialExpressionVertexNormalWS))])
 output(normalmap,u.MaterialProperty.MP_NORMAL)
 rough=sample(m,r,uv,linear=True)
 if foliage:rough=custom(m,'return max(.85,R.r);',[('R',rough)],u.CustomMaterialOutputType.CMOT_FLOAT1)
 output(rough,u.MaterialProperty.MP_ROUGHNESS)
 output(val(m,.04 if foliage else .18),u.MaterialProperty.MP_SPECULAR)
 if foliage:
  prop(m,'two_sided',True);prop(m,'blend_mode',u.BlendMode.BLEND_MASKED);prop(m,'opacity_mask_clip_value',.35);prop(m,'shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
  mask=sample(m,alpha,uv,linear=True);fade=node(m,u.MaterialExpressionPerInstanceFadeAmount)
  # 时序抖动淡出保留完整草叶轮廓，避免直接乘透明度把叶片削成远处的小黑点。
  dither=node(m,u.MaterialExpressionMaterialFunctionCall)
  dither.set_material_function(asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'))
  link(fade,dither,'Alpha Threshold')
  output(custom(m,'return A.r*Fade;',[('A',mask),('Fade',dither)],u.CustomMaterialOutputType.CMOT_FLOAT1),u.MaterialProperty.MP_OPACITY_MASK)
  output(custom(m,'return C*.65;',[('C',base)]),u.MaterialProperty.MP_SUBSURFACE_COLOR)
  output(custom(m,'return C*.12;',[('C',base)]),u.MaterialProperty.MP_EMISSIVE_COLOR)
  # 保留作者草叶与 UV，只加小幅整体风摆；叶根依据局部高度固定。
  pos=node(m,u.MaterialExpressionWorldPosition);obj=node(m,u.MaterialExpressionObjectPositionWS)
  wind=custom(m,'float h=saturate((P.z-Object.z+15)/45);return float3(sin(T*.8+P.x*.005+P.y*.007)*2*h,0,0);',[('P',pos),('Object',obj),('T',node(m,u.MaterialExpressionTime))])
  output(wind,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 return finish(m)
backup=R/'Saved/Backup'/('DreamfieldFree_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
for rel in ['Content/Maps/'+MAP.split('/')[-1]+'.umap','Content/'+DST.removeprefix('/Game/')]:
 p=R/rel
 if p.exists():
  dest=backup/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copytree(p,dest) if p.is_dir() else shutil.copy2(p,dest)
try:
 grassroot=SRC/'grass_bermuda_01';g=[]
 for role in ('Diffuse','nor_dx','Rough','Alpha'):g.append(texture('T_Free_Grass_'+role,grassroot/('grass_bermuda_01_'+role+'.jpg'),normal=role=='nor_dx',linear=role in ('Rough','Alpha')))
 ground=[]
 for role in ('Diffuse','nor_dx','Rough'):ground.append(texture('T_Free_Ground_'+role,SRC/'grass_ground'/('grass_ground_'+role+'.jpg'),normal=role=='nor_dx',linear=role=='Rough'))
 gm=texmat('FreeGrass',g[0],g[1],g[2],foliage=True,alpha=g[3])
 groundmat=texmat('FreeGround',*ground,world=True)
 concrete=[];trim=[]
 for folder,target in [('Concrete',concrete),('ConcreteTrim',trim)]:
  for label_,role in [('BASE COLOR','Diffuse'),('NORMAL','Normal'),('ROUGHNESS','Rough')]:target.append(texture('T_Free_'+folder+'_'+role,SRC/'brutalist_buildings/Textures'/folder/(label_+'_'+folder+'.png'),normal=role=='Normal',linear=role=='Rough',flip=role=='Normal'))
 weathered=[]
 for role in ('Diffuse','nor_dx','Rough'):weathered.append(texture('T_Free_Weathered_'+role,SRC/'concrete_wall_006'/('concrete_wall_006_'+role+'.jpg'),normal=role=='nor_dx',linear=role=='Rough'))
 cm=texmat('FreeConcrete',*weathered,uvscale=2);tm=texmat('FreeTrim',*trim)
 glass=material('FreeGlass');output(vec(glass,(.055,.075,.078)),u.MaterialProperty.MP_BASE_COLOR);output(val(glass,.24),u.MaterialProperty.MP_ROUGHNESS);glass=finish(glass)
 skytex=texture('T_Free_RealSky',SRC/'kloppenheim_06_puresky/kloppenheim_06_puresky.hdr')
 sky=material('FreeSky');prop(sky,'shading_model',u.MaterialShadingModel.MSM_UNLIT);prop(sky,'two_sided',True);prop(sky,'is_sky',True)
 p=node(sky,u.MaterialExpressionWorldPosition);cam=node(sky,u.MaterialExpressionCameraPositionWS)
 direction=custom(sky,'float3 d=normalize(P-Camera);return float3(-d.y,d.x,d.z);',[('P',p),('Camera',cam)])
 tx=node(sky,u.MaterialExpressionTextureSample,texture=skytex,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR);link(direction,tx,'UVs')
 output(custom(sky,'return C*.65;',[('C',tx)]),u.MaterialProperty.MP_EMISSIVE_COLOR);sky=finish(sky)
 manifest=json.loads((SRC/'prepared/manifest.json').read_text(encoding='utf8'));meshes={}
 for name,info in manifest['meshes'].items():
  if name.startswith(('SM_Free_Grass','SM_Free_Brutalist_')):continue
  if E.does_asset_exist(DST+'/Meshes/'+name):
   meshes[name]=asset(DST+'/Meshes/'+name);REPORT['meshes'][name]=meshes[name].get_num_triangles(0);continue
  ui=u.FbxImportUI()
  for k,v in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),('import_textures',False),('import_animations',False),('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)]:prop(ui,k,v)
  data=ui.get_editor_property('static_mesh_import_data')
  for k,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('build_nanite',False),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)]:prop(data,k,v)
  task=u.AssetImportTask();task.filename=str(SRC/'prepared'/(name+'.fbx'));task.destination_path=DST+'/Meshes';task.destination_name=name;task.automated=True;task.replace_existing=True;task.save=False;task.options=ui;T.import_asset_tasks([task]);mesh=asset(DST+'/Meshes/'+name)
  slots=list(mesh.get_editor_property('static_materials'))
  for i,s in enumerate(slots):
   slot=str(s.get_editor_property('imported_material_slot_name'));mat=gm if 'Grass' in name or 'BermudaPatch' in name else glass if slot=='Glass' else tm if 'Trim' in slot else cm
   prop(s,'material_interface',mat);slots[i]=s
  prop(mesh,'static_materials',slots);save(mesh);meshes[name]=mesh;REPORT['meshes'][name]=mesh.get_num_triangles(0)
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Dreamfield');assert world;expected=marks();expected_functional=functional()
 for a in list(S.get_all_level_actors()):
  if a.get_actor_label().startswith(('DO_Wall','DO_PlatformButtresses','DO_FineGrass','DO_WhiteDaisies','Free_')):S.destroy_actor(a)
 for a in S.get_all_level_actors():
  for c in a.get_components_by_class(u.MeshComponent):
   for i in range(c.get_num_materials()):
    m=c.get_material(i)
    if m and m.get_name() in ('M_DO_Ground','M_FreeGround'):c.set_material(i,groundmat)
    elif m and m.get_name() in ('M_DO_Porcelain','M_DO_Platform','M_DO_Titanium','M_DO_Recess','M_FreeConcrete','M_FreeTrim'):c.set_material(i,cm)
 rig=next(a for a in S.get_all_level_actors() if a.get_actor_label()=='DO_DreamSky_ALS')
 prop(rig,'star_material',sky);rig.prepare_resources();rig.clouds.set_visibility(False,True);rig.atmosphere.set_visibility(False,True)
 rig.sun.set_intensity(5);rig.sun.set_world_rotation(u.Rotator(pitch=-24,yaw=52),False,False);prop(rig.sun,'light_source_angle',.5357)
 rig.sky_light.set_intensity(1.2);rig.sky_light.set_light_color(u.LinearColor(1,1,1,1));prop(rig.sky_light,'source_type',u.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP);prop(rig.sky_light,'cubemap',skytex);prop(rig.sky_light,'real_time_capture',False)
 prop(rig.fog,'fog_density',.0015);prop(rig.fog,'start_distance',50000.);prop(rig.fog,'fog_inscattering_luminance',u.LinearColor(.65,.72,.82,1))
 # 使用作者现成墙片、凹槽、承重柱和平台边缘拼装，不改变模型几何。
 wallgroups={n:[] for n in ('Wall_Wall','Wall_Window_Slit_Center_1_Vertical_2','Pillar_Wide_3','Pillar_Wide_Cap','Floor_Platform_2_Edge')}
 for j in range(-35,36) if STUDY else range(-135,136):
  for tier in range(10):
   recessed=j%6 in (2,3);x=308 if recessed else 302
   n='Wall_Window_Slit_Center_1_Vertical_2' if recessed and tier in (4,5,8) else 'Wall_Wall'
   wallgroups[n].append(xf(x,j*20,tier*20,90,(5,5,5)))
   if n!='Wall_Wall':wallgroups['Wall_Wall'].append(xf(316,j*20,tier*20,90,(5,5,5)))
   if j%3==0:wallgroups['Pillar_Wide_3'].append(xf(300,j*20,tier*20,0,(5,5,5)))
  for z in (62,142,198):wallgroups['Floor_Platform_2_Edge'].append(xf(308,j*20,z,90,(5,5,1)))
 for n,tfs in wallgroups.items():
  if tfs:instances('Free_Wall_'+n,meshes['SM_Free_'+n],tfs,False)
 # 草丛保留作者真实叶片，密铺但用距离剔除控制近景负担；地表由实拍贴图接续。
 r=random.Random(31103);names=[n for n in meshes if n.startswith('SM_Free_BermudaPatch')];groups={n:[] for n in names}
 xmin,xmax,ymin,ymax=(-180,180,-405,145) if STUDY else (-360,294,-510,510)
 step=1.
 for ix in range(int((xmax-xmin)/step)):
  for iy in range(int((ymax-ymin)/step)):
   x=xmin+(ix+.5+r.uniform(-.08,.08))*step;y=ymin+(iy+.5+r.uniform(-.08,.08))*step
   if x>186 and abs(y)<228 or x>272 and 223<abs(y)<348 or x>167 and any(abs(y-ey)<13 for ey in (-75,75)):continue
   n=r.choice(names);s=r.uniform(1.15,1.25);z=terrain.height(x,y)+.012
   yaw=math.radians(r.choice((0,90,180,270))+r.uniform(-4,4))
   dx=(terrain.height(x+.25,y)-terrain.height(x-.25,y))/.5;dy=(terrain.height(x,y+.25)-terrain.height(x,y-.25))/.5
   rotation=u.MathLibrary.make_rot_from_zx(u.Vector(-dx,-dy,1),u.Vector(math.cos(yaw),math.sin(yaw),0))
   groups[n].append(u.Transform(V(x,y,z),rotation,u.Vector(s,s,1)))
 for name,tfs in groups.items():
  a=instances('Free_'+name,meshes[name],tfs,False)
  for c in a.get_components_by_class(u.HierarchicalInstancedStaticMeshComponent):
   prop(c,'instance_start_cull_distance',4000);prop(c,'instance_end_cull_distance',9500);prop(c,'cast_shadow',False);prop(c,'affect_distance_field_lighting',False)
 assert marks()==expected
 assert functional()==expected_functional
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 REPORT.update(passed=True,map=MAP,study=STUDY,grass_instances=sum(map(len,groups.values())),wall_instances=sum(map(len,wallgroups.values())),preserved_markers=len(expected),preserved_functional=expected_functional)
except Exception:
 import traceback;REPORT['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation'/('DreamfieldFreeStudy.json' if STUDY else 'DreamfieldFree.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
