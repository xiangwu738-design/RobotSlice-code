"""NASA 现成木星模型与贴图：独立天空试拼，确认后才写开发图。"""
from pathlib import Path
import unreal as u,json,os,shutil,datetime,traceback,math
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_dreamfield_free_assets.py').read_text(encoding='utf8').split('\nbackup=R/')[0],'planet_helpers','exec'))
FULL=os.environ.get('DF_PLANET_FULL','0')=='1'
DST='/Game/Env/DreamfieldPlanet' if FULL else '/Game/Env/DreamfieldPlanetStudy'
MAP='/Game/Maps/L_Dreamfield' if FULL else '/Game/Maps/L_Dreamfield_PlanetStudy'
REPORT={'passed':False,'map':MAP,'full':FULL,'materials':[]}
try:
 backup=R/'Saved/Backup'/('DreamfieldPlanet_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
 for rel in ['Content/Maps/'+MAP.split('/')[-1]+'.umap','Content/'+DST.removeprefix('/Game/')]:
  src=R/rel
  if src.exists():
   d=backup/rel;d.parent.mkdir(parents=True,exist_ok=True);shutil.copytree(src,d) if src.is_dir() else shutil.copy2(src,d)
 imported=DST+'/AuthorMesh';meshpath=imported+'/SM_JupiterAuthor'
 if not E.does_asset_exist(meshpath):
  # 命令行没有 StaticMeshEditorSubsystem；用作者原几何的 FBX 导入时关闭 Nanite。
  ui=u.FbxImportUI()
  for k,v in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),('import_textures',False),('import_animations',False),('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)]:prop(ui,k,v)
  data=ui.get_editor_property('static_mesh_import_data')
  for k,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('build_nanite',False),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)]:prop(data,k,v)
  task=u.AssetImportTask();task.filename=str(R/'SourceAssets/dreamfield_free/nasa_jupiter/Jupiter_author.fbx');task.destination_path=imported;task.destination_name='SM_JupiterAuthor';task.automated=True;task.replace_existing=False;task.save=False;task.options=ui;T.import_asset_tasks([task])
 mesh=asset(meshpath)
 tex=asset(DST+'/Textures/T_JupiterAuthor') if E.does_asset_exist(DST+'/Textures/T_JupiterAuthor') else texture('T_JupiterAuthor',R/'SourceAssets/dreamfield_free/nasa_jupiter/Jupiter_author.png')
 # 继续使用作者 UV 与球体几何，着色只做梦核配色、球面光照和空气感。
 m=material('DreamPlanet');prop(m,'shading_model',u.MaterialShadingModel.MSM_UNLIT);prop(m,'blend_mode',u.BlendMode.BLEND_TRANSLUCENT);prop(m,'two_sided',False);prop(m,'use_translucency_vertex_fog',False)
 c=sample(m,tex);n=node(m,u.MaterialExpressionVertexNormalWS);p=node(m,u.MaterialExpressionWorldPosition);cam=node(m,u.MaterialExpressionCameraPositionWS)
 color=custom(m,'float3 nn=normalize(N);float diff=saturate(dot(nn,normalize(float3(-.56,-.72,.407))));float3 rose=lerp(C,dot(C,float3(.2126,.7152,.0722)).xxx,.45)*float3(1.05,.68,.83);float rim=pow(1-saturate(dot(nn,normalize(Camera-P))),3);return rose*(.4+.7*diff)+float3(.20,.28,.40)*rim*.35;',[('C',c),('N',n),('P',p),('Camera',cam)])
 output(color,u.MaterialProperty.MP_EMISSIVE_COLOR);output(val(m,.72),u.MaterialProperty.MP_OPACITY);m=finish(m)
 slots=list(mesh.static_materials)
 for i,s in enumerate(slots):prop(s,'material_interface',m);slots[i]=s
 prop(mesh,'static_materials',slots)
 save(mesh)
 assert mesh.get_num_triangles(0)==3072,mesh.get_num_triangles(0)
 assert not mesh.get_editor_property('nanite_settings').enabled
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Dreamfield');expected=functional()
 for a in list(S.get_all_level_actors()):
  if a.get_actor_label()=='NASA_DreamPlanet':S.destroy_actor(a)
 distance=18000.;yaw=math.radians(35);alt=math.radians(24);pos=V(distance*math.cos(alt)*math.cos(yaw),distance*math.cos(alt)*math.sin(yaw),distance*math.sin(alt))
 a=S.spawn_actor_from_class(u.StaticMeshActor,pos,u.Rotator(pitch=12,yaw=28,roll=10));a.set_actor_label('NASA_DreamPlanet');a.set_folder_path('Dreamfield/Sky');a.tags=['Dreamfield','NASA_VTAD_Planet']
 c=a.static_mesh_component;c.set_static_mesh(mesh);c.set_collision_profile_name('NoCollision');c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
 prop(c,'cast_shadow',False);prop(c,'affect_distance_field_lighting',False);prop(c,'mobility',u.ComponentMobility.STATIC)
 b=mesh.get_bounding_box();radius=max(b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z)*.5;scale=380000./radius;a.set_actor_scale3d(u.Vector(scale,scale,scale))
 assert functional()==expected
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 if FULL:
  provenance=R/'SourceAssets/dreamfield_free/nasa_jupiter/provenance.json';shutil.copy2(provenance,backup/'provenance.json')
  source_info=json.loads(provenance.read_text(encoding='utf8'));source_info.update(status='Integrated in developer L_Dreamfield; author geometry and texture retained, material recolored for fictional sky.',asset=mesh.get_path_name(),triangles=3072)
  provenance.write_text(json.dumps(source_info,ensure_ascii=False,indent=2),encoding='utf8')
 REPORT.update(passed=True,mesh=mesh.get_path_name(),triangles=mesh.get_num_triangles(0),texture=tex.get_path_name(),radius_m=3800,distance_m=distance,position_cm=[pos.x,pos.y,pos.z],scale=scale,preserved_functional=expected,backup=str(backup))
except Exception:REPORT['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation'/('DreamfieldPlanet.json' if FULL else 'DreamfieldPlanetStudy.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
