"""在开发版 L_Dreamfield 内替换墙、天空、星球及草地。原图已离线留档。
只保存 /Game/Env/DreamfieldOriginal 和目标地图；不保存引擎／插件资源。
复用校园项目 AdvancedLightingSystem 的 LightingEnvironmentRig，静态梦核预设不接全局天气。
"""
import unreal as u, json, math, random, sys, traceback, re
from pathlib import Path
sys.path.insert(0,'D:/RobotSlice/tools')
import dreamfield_terrain as terrain
ROOT=Path('D:/RobotSlice');SRC=ROOT/'SourceAssets/dreamfield_original'
DST='/Game/Env/DreamfieldOriginal';MAP='/Game/Maps/L_Dreamfield';TAG='DreamfieldOriginal'
E=u.EditorAssetLibrary;M=u.MaterialEditingLibrary;T=u.AssetToolsHelpers.get_asset_tools()
S=u.get_editor_subsystem(u.EditorActorSubsystem)
REPORT={'passed':False,'map':MAP,'materials':[],'meshes':{},'removed':[],'checks':{}}
REPORT_PATH=ROOT/'Saved/Automation/DreamfieldOriginal.json'

def log(msg):u.log('[dreamoriginal] '+str(msg))
def prop(o,k,v):o.set_editor_property(k,v)
def save(o):
 assert o.get_path_name().startswith('/Game/'),o.get_path_name()
 assert E.save_loaded_asset(o,False),o.get_path_name()
def asset(p):
 a=u.load_asset(p);assert a,p;return a
def node(m,c,**kw):
 n=M.create_material_expression(m,c)
 for k,v in kw.items():prop(n,k,v)
 return n
def link(a,b,name='',out=''):assert M.connect_material_expressions(a,out,b,name)
def output(n,p):assert M.connect_material_property(n,'',p)
def val(m,v):return node(m,u.MaterialExpressionConstant,r=v)
def vec(m,v):return node(m,u.MaterialExpressionConstant3Vector,constant=u.LinearColor(*v,1))
def custom(m,code,inputs,kind=u.CustomMaterialOutputType.CMOT_FLOAT3):
 n=node(m,u.MaterialExpressionCustom,code=code,output_type=kind)
 ins=[]
 for name,a in inputs:
  i=u.CustomInput();prop(i,'input_name',name);ins.append(i)
 prop(n,'inputs',ins)
 for name,a in inputs:link(a,n,name)
 return n
def mat(name):
 path=DST+'/Materials/'+name
 m=u.load_asset(path) if E.does_asset_exist(path) else T.create_asset(name,DST+'/Materials',u.Material,u.MaterialFactoryNew())
 for _ in range(12):
  if M.get_num_material_expressions(m)==0:break
  M.delete_all_material_expressions(m)
 assert M.get_num_material_expressions(m)==0
 prop(m,'used_with_instanced_static_meshes',True)
 return m
def finish(m):
 errors=M.recompile_material(m);assert not errors,(m.get_name(),list(errors))
 save(m);REPORT['materials'].append(m.get_path_name());return m
def surface(name,color,kind=2,foliage=False):
 m=mat(name);prop(m,'two_sided',foliage)
 p=node(m,u.MaterialExpressionWorldPosition);cam=node(m,u.MaterialExpressionCameraPositionWS)
 uv=node(m,u.MaterialExpressionTextureCoordinate)
 code=re.sub(r'//[^\n]*','',(ROOT/'tools/shaders/dreamfield_original_surface.hlsl').read_text(encoding='utf-8'))
 n=custom(m,code,[('P',p),('Camera',cam),('UV',uv),('N',node(m,u.MaterialExpressionVertexNormalWS)),('Tint',vec(m,color)),('Kind',val(m,kind))])
 if kind==0:
  tu=custom(m,'return P.xy*.01/2.4;', [('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT2)
  tx=node(m,u.MaterialExpressionTextureSample,texture=asset(DST+'/Textures/T_DO_TurfDetail'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
  link(tu,tx,'UVs')
  col=custom(m,'float fade=1-smoothstep(3500,16000,distance(P,Camera)); return Base*lerp(1.,.60+.80*Detail.r,fade);',[('Base',n),('P',p),('Camera',cam),('Detail',tx)])
  output(col,u.MaterialProperty.MP_BASE_COLOR)
  nm=node(m,u.MaterialExpressionTextureSample,texture=asset(DST+'/Textures/T_DO_TurfNormal'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
  link(tu,nm,'UVs');output(nm,u.MaterialProperty.MP_NORMAL)
 else:output(n,u.MaterialProperty.MP_BASE_COLOR)
 output(val(m,.84 if kind<2 else .64),u.MaterialProperty.MP_ROUGHNESS)
 output(val(m,.12),u.MaterialProperty.MP_SPECULAR)
 if foliage:
  prop(m,'shading_model',u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
  subs=custom(m,'return Base*.42;',[('Base',n)]);output(subs,u.MaterialProperty.MP_SUBSURFACE_COLOR)
  # 根部不动，顶端向同一个风向轻摆；植被亮暗由双面透光控制。
  wind=custom(m,'float h=saturate(1-UV.y); float w=h*h; float s=sin(Time*.85+P.x*.003+P.y*.0014); return float3(s*4.2*w,w*1.8*s,0);',
    [('P',p),('Camera',cam),('UV',uv),('Time',node(m,u.MaterialExpressionTime))])
  output(wind,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 return finish(m)
def solid(name,color,emit=False):
 m=mat(name)
 if emit:prop(m,'shading_model',u.MaterialShadingModel.MSM_UNLIT)
 output(vec(m,color),u.MaterialProperty.MP_EMISSIVE_COLOR if emit else u.MaterialProperty.MP_BASE_COLOR)
 output(val(m,.8),u.MaterialProperty.MP_ROUGHNESS)
 return finish(m)
def v(x,y,z):return u.Vector(x*100,y*100,z*100)
def xf(x,y,z,yaw=0,scale=1):return u.Transform(v(x,y,z),u.Rotator(yaw=yaw),u.Vector(scale,scale,scale))
def label(a,name,folder):
 a.tags=['Dreamfield',TAG];a.set_actor_label(name);a.set_folder_path('Dreamfield/Original/'+folder);return a
def mesh_actor(name,mesh,loc,collide=False):
 a=label(S.spawn_actor_from_class(u.StaticMeshActor,loc),name,'Geometry');c=a.static_mesh_component
 c.set_static_mesh(mesh);c.set_collision_profile_name('BlockAll' if collide else 'NoCollision')
 if not collide:c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
 return a
def instances(name,mesh,transforms,cull=None):
 a=label(S.spawn_actor_from_class(u.Actor,u.Vector()),name,'Vegetation' if cull else 'Wall')
 sds=u.get_engine_subsystem(u.SubobjectDataSubsystem);parent=sds.k2_gather_subobject_data_for_instance(a)[0]
 h,fail=sds.add_new_subobject(u.AddNewSubobjectParams(parent_handle=parent,new_class=u.HierarchicalInstancedStaticMeshComponent,blueprint_context=None))
 assert not str(fail),str(fail)
 c=u.SubobjectDataBlueprintFunctionLibrary.get_object(u.SubobjectDataBlueprintFunctionLibrary.get_data(h))
 c.set_static_mesh(mesh);c.set_collision_profile_name('NoCollision');c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
 prop(c,'mobility',u.ComponentMobility.STATIC)
 if cull:
  prop(c,'cast_shadow',False);prop(c,'affect_distance_field_lighting',False)
  prop(c,'instance_start_cull_distance',int(cull[0]*100));prop(c,'instance_end_cull_distance',int(cull[1]*100))
 c.add_instances(transforms,False,True)
 assert c.get_instance_count()==len(transforms)
 return a

def on_structure(x,y):
 if x>186 and abs(y)<228:return True
 if x>272 and 223<abs(y)<348:return True
 if x>167 and any(abs(y-ey)<13 for ey in (-75,75)):return True
 return x>294
def patch(x,y):return .5+.5*math.sin(x/63+.5)*math.sin(y/91+1.1)*math.cos((x+y)/117)

try:
 for name in ('T_DO_TurfDetail','T_DO_TurfNormal'):
  task=u.AssetImportTask();task.filename=str(SRC/(name+'.png'));task.destination_path=DST+'/Textures';task.destination_name=name
  task.automated=True;task.replace_existing=True;task.save=False;T.import_asset_tasks([task])
  tex=asset(DST+'/Textures/'+name);prop(tex,'srgb',False)
  prop(tex,'compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if name.endswith('Normal') else u.TextureCompressionSettings.TC_GRAYSCALE)
  save(tex)
 # 新目录独立创建，旧开发图的材质和网格不动。
 mats={
  'Porcelain':surface('M_DO_Porcelain',(.56,.62,.65)),
  'Recess':surface('M_DO_Recess',(.035,.055,.085)),
  'Titanium':surface('M_DO_Titanium',(.21,.27,.32)),
  'Grass':surface('M_DO_Grass',(1,1,1),1,True),
  'Petal':solid('M_DO_Petal',(.72,.72,.65)),
  'Pollen':solid('M_DO_Pollen',(.5,.28,.035)),
 }
 ground=surface('M_DO_Ground',(1,1,1),0)
 platform=surface('M_DO_Platform',(.29,.32,.34))
 # 天空在已有校园项目的原生大气＋天空球架构里运行；月球纹理改为解析球面。
 sky=mat('M_DO_Sky');prop(sky,'shading_model',u.MaterialShadingModel.MSM_UNLIT);prop(sky,'two_sided',True);prop(sky,'is_sky',True)
 code=re.sub(r'//[^\n]*','',(ROOT/'tools/shaders/dreamfield_original_sky.hlsl').read_text(encoding='utf-8'))
 sk=custom(sky,code,[('P',node(sky,u.MaterialExpressionWorldPosition)),('Camera',node(sky,u.MaterialExpressionCameraPositionWS)),
  ('Atmosphere',node(sky,u.MaterialExpressionSkyAtmosphereViewLuminance)),('Time',node(sky,u.MaterialExpressionTime))])
 output(sk,u.MaterialProperty.MP_EMISSIVE_COLOR);finish(sky)
 manifest=json.loads((SRC/'manifest.json').read_text(encoding='utf-8'))
 meshes={};sms=u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
 for name in manifest['meshes']:
  ui=u.FbxImportUI()
  for k,value in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),('import_textures',False),('import_animations',False),('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)]:prop(ui,k,value)
  d=ui.get_editor_property('static_mesh_import_data')
  for k,value in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False)]:prop(d,k,value)
  fol='Grass' in name or 'Daisies' in name
  prop(d,'build_nanite',not fol and 'Ground' not in name)
  prop(d,'normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
  task=u.AssetImportTask();task.filename=str(SRC/(name+'.fbx'));task.destination_path=DST+'/Meshes';task.destination_name=name
  task.automated=True;task.replace_existing=True;task.save=False;task.options=ui
  T.import_asset_tasks([task]);mesh=asset(DST+'/Meshes/'+name)
  slots=list(mesh.get_editor_property('static_materials'))
  for i,s in enumerate(slots):
   slot=str(s.get_editor_property('imported_material_slot_name'))
   assert slot in mats,(name,slot)
   prop(s,'material_interface',ground if 'Ground' in name else mats[slot]);slots[i]=s
  prop(mesh,'static_materials',slots)
  # 低画质关闭 Nanite 时保留真实地形和建筑轮廓；自动简化曾让草根埋入地面。
  ns=mesh.get_editor_property('nanite_settings')
  prop(ns,'enabled',not fol and 'Ground' not in name)
  prop(ns,'fallback_relative_error',0.)
  prop(ns,'fallback_percent_triangles',1.)
  prop(mesh,'nanite_settings',ns)
  bs=mesh.get_editor_property('body_setup')
  if bs:prop(bs,'collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE if name.endswith('GroundNear') else u.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
  save(mesh);meshes[name]=mesh
  REPORT['meshes'][name]={'triangles':mesh.get_num_triangles(0),'slots':[str(s.get_editor_property('imported_material_slot_name')) for s in slots]}
 log('原创资源导入完成')
 # 资源全部就绪以后才修改关卡，避免导入半途失败保存出半张地图。
 world=u.EditorLoadingAndSavingUtils.load_map(MAP);assert world
 before=list(S.get_all_level_actors())
 def functional(actors):
  return {a.get_actor_label():[round(x,2) for x in (a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z)] for a in actors if
   isinstance(a,u.PlayerStart) or a.get_class().get_name()=='MechElevator' or any(str(t) in ('TeamSpawn0','TeamSpawn1','MechArena','ArenaWall') for t in a.tags) or a.get_actor_label() in ('Platform','RampN','RampS','WallCollision')}
 expected=functional(before)
 for a in before:
  name=a.get_actor_label();folder=str(a.get_folder_path())
  remove=(TAG in [str(t) for t in a.tags] or name in ('SkyDome','Planet','Moon','Sun','SkyLight','HeightFog','GroundNear','GroundFar','DeckTiles') or
   name.startswith(('Wall0_','Wall1_','DeckUnderlay_','PlatBit','Cloud')) or '/Ground/Grass' in folder)
  if remove:REPORT['removed'].append(name);S.destroy_actor(a);continue
  # 平台、大门和井架保留形状与碰撞，仅把引用旧墙面纹理的材质换掉。
  for c in a.get_components_by_class(u.MeshComponent):
   for i in range(c.get_num_materials()):
    old=c.get_material(i)
    if old:
     repl={'MI_DW_Wall':mats['Porcelain'],'MI_DW_Platform':platform,'MI_DW_Deck':platform,'MI_DW_Backing':mats['Recess'],'MI_DW_Steel':mats['Titanium'],'MI_DW_GateLeaf':mats['Porcelain']}.get(old.get_name())
     if repl:c.set_material(i,repl)
  if a.get_class().get_name()=='MechElevator':prop(a,'platform_material',mats['Titanium'])
 # 墙段被同一个实例组批处理；从地面到顶檐都有真实截面。
 for variant in range(4):
  ts=[xf(300,-2670+i*60,0) for i in range(90) if i%4==variant]
  instances('DO_Wall'+str(variant),meshes['SM_DO_Wall'+str(variant)],ts)
 ts=[xf(189,y,0) for y in range(-214,215,13) if all(abs(y-ey)>15 for ey in (-75,75))]
 instances('DO_PlatformButtresses',meshes['SM_DO_Buttress'],ts)
 mesh_actor('DO_GroundNear',meshes['SM_DO_GroundNear'],u.Vector(),True)
 far=mesh_actor('DO_GroundFar',meshes['SM_DO_GroundFar'],u.Vector());prop(far.static_mesh_component,'cast_shadow',False)
 r=random.Random(1102);grass=[[],[],[]];flowers=[]
 # 短草与疏密过渡取代高草毯，平台前保留开阔的连续草面。
 for ix in range(375):
  for iy in range(583):
   x=-360+(ix+r.random())*1.75;y=-510+(iy+r.random())*1.75
   if on_structure(x,y):continue
   p=patch(x,y)
   if r.random()>.58+.28*p:continue
   z=terrain.height(x,y)-.012
   grass[r.randrange(3)].append(xf(x,y,z,r.uniform(0,360),r.uniform(.85,1.2)))
   # 白花极稀，按大块地势聚集；不铺满每个草丛。
   if p>.72 and r.random()<.045:flowers.append(xf(x,y,z, r.uniform(0,360),r.uniform(.85,1.3)))
 for i,ts in enumerate(grass):instances('DO_FineGrass'+str(i),meshes['SM_DO_Grass'+str(i)],ts,(65,110))
 instances('DO_WhiteDaisies',meshes['SM_DO_Daisies'],flowers,(65,95))
 REPORT['vegetation']={'grass':sum(map(len,grass)),'flowers':len(flowers),'old_grass_and_plants_removed':sum('Grass' in n or 'Plants' in n or 'Flowers' in n for n in REPORT['removed'])}
 # 复用本机校园天空项目的 C++ 环境载体，不引入第二套天空控制器。
 cls=u.load_class(None,'/Script/AdvancedLightingRuntime.LightingEnvironmentRig');assert cls
 rig=label(S.spawn_actor_from_class(cls,u.Vector()),'DO_DreamSky_ALS','Sky')
 prop(rig,'sky_sphere_mesh',asset('/Engine/BasicShapes/Sphere'));prop(rig,'star_material',sky);prop(rig,'rain_drop_count',0)
 # 使用此前校园天空工程的体积云母材质，实例只在本图目录保存。
 cp=DST+'/Materials/MI_DO_VolumetricClouds'
 cloud=u.load_asset(cp) if E.does_asset_exist(cp) else E.duplicate_asset('/Game/CampusV21/Lighting/Materials/MI_CampusClouds',cp)
 assert cloud
 for k,value in [('Cloud_GlobalCoverage',.05),('Cloud_GlobalDensity',.008),('Layout_CloudGlobalScale',256.),('StormClouds',0.),('CloudLightningLuminance',0.)]:
  # UE 5.8 此 setter 写入成功也固定返回 false；以读回值验证。
  M.set_material_instance_scalar_parameter_value(cloud,k,value)
  assert abs(M.get_material_instance_scalar_parameter_value(cloud,k)-value)<1e-5,k
 for k,value in [('Layout_CloudTypeMask',(.6,.4,.15,0)),('Noise_Strength',(.75,.24,.07,2.5)),('Layout_WindControls',(.7,.7,.5,.035))]:
  M.set_material_instance_vector_parameter_value(cloud,k,u.LinearColor(*value))
  actual=M.get_material_instance_vector_parameter_value(cloud,k)
  assert all(abs(a-b)<1e-5 for a,b in zip((actual.r,actual.g,actual.b,actual.a),value)),k
 M.update_material_instance(cloud);save(cloud);prop(rig,'cloud_material',cloud)
 rig.prepare_resources()
 rig.stars.set_relative_scale3d(u.Vector(120000,120000,120000))
 rig.sun.set_world_rotation(u.Rotator(pitch=-36,yaw=52),False,False)
 rig.sun.set_intensity(11);rig.sun.set_light_color(u.LinearColor(.95,.97,1,1))
 prop(rig.sun,'light_source_angle',1.1);prop(rig.sun,'cast_cloud_shadows',True);prop(rig.sun,'cloud_shadow_strength',.25)
 rig.moon.set_intensity(0);rig.lightning.set_intensity(0)
 rig.clouds.set_visibility(True,True)
 prop(rig.clouds,'layer_bottom_altitude',1.2);prop(rig.clouds,'layer_height',1.7)
 rig.sky_light.set_intensity(.24);rig.sky_light.set_light_color(u.LinearColor(.58,.73,1,1))
 prop(rig.sky_light,'source_type',u.SkyLightSourceType.SLS_CAPTURED_SCENE);prop(rig.sky_light,'real_time_capture',True)
 prop(rig.sky_light,'lower_hemisphere_is_black',False);prop(rig.sky_light,'lower_hemisphere_color',u.LinearColor(.12,.16,.055,1))
 rig.post_process.set_visibility(False,True);prop(rig.post_process,'enabled',False)
 for k,value in [('fog_density',.006),('fog_height_falloff',.12),('start_distance',55000.),('fog_cutoff_distance',1400000.),('enable_volumetric_fog',False),('fog_inscattering_luminance',u.LinearColor(1.15,.9,1.08,1))]:prop(rig.fog,k,value)
 after=list(S.get_all_level_actors())
 REPORT['checks']['functional_actors_unchanged']=functional(after)==expected
 REPORT['checks']['functional_actor_count']=len(expected)
 assert REPORT['checks']['functional_actors_unchanged'],(expected,functional(after))
 # 保存前排查关卡组件上直接残留的旧墙、天空与植物引用。
 forbidden=[]
 for a in after:
  for c in a.get_components_by_class(u.MeshComponent):
   if isinstance(c,u.StaticMeshComponent) and c.static_mesh:
    p=c.static_mesh.get_path_name()
    if '/StyleHex_Studio/' in p or '/Dreamfield/Meshes/SM_DW_' in p or '/Dreamfield/Meshes/SM_DF_Cloud' in p:forbidden.append(p)
   for i in range(c.get_num_materials()):
    m=c.get_material(i)
    if m and m.get_name() in ('MI_DF_Sky','MI_DF_Planet','MI_DF_Moon','MI_DF_Grass','MI_DF_GrassBlades','MI_DF_Flowers','MI_DW_Wall','MI_DW_Deck','MI_DW_Platform','MI_DW_Backing','MI_DW_Steel','MI_DW_GateLeaf'):forbidden.append(m.get_path_name())
 assert not forbidden,forbidden
 REPORT['checks']['old_visual_component_references']=forbidden
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 REPORT['passed']=True;log('关卡已保存 '+str(REPORT['vegetation']))
except Exception:
 REPORT['error']=traceback.format_exc();u.log_error(REPORT['error']);raise
finally:
 REPORT_PATH.parent.mkdir(parents=True,exist_ok=True);REPORT_PATH.write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
