"""重新加载开发图，检查持久化、旧视觉资源引用与关键玩法结构，不渲染、不存资源。"""
import unreal as u,json,traceback
from pathlib import Path
R=Path('D:/RobotSlice');MAP='/Game/Maps/L_Dreamfield';DST='/Game/Env/DreamfieldOriginal'
report={'passed':False,'map':MAP}
try:
 world=u.EditorLoadingAndSavingUtils.load_map(MAP);assert world
 actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
 labels={a.get_actor_label():a for a in actors}
 for name in ['DO_DreamSky_ALS','DO_Wall0','DO_Wall1','DO_Wall2','DO_Wall3','DO_GroundNear','DO_GroundFar','Platform','RampN','RampS','WallCollision','MechArena','TeamSpawn0','TeamSpawn1']:
  assert name in labels,name
 elevators=[a for a in actors if a.get_class().get_name()=='MechElevator'];assert len(elevators)==2
 rigs=[a for a in actors if a.get_class().get_name()=='LightingEnvironmentRig'];assert len(rigs)==1
 rig=rigs[0];assert rig.star_material.get_path_name().startswith(DST)
 sky=u.load_asset(DST+'/Materials/M_DO_Sky')
 assert sky.get_editor_property('shading_model')==u.MaterialShadingModel.MSM_UNLIT
 assert sky.get_editor_property('is_sky')
 assert sky.get_editor_property('blend_mode')==u.BlendMode.BLEND_OPAQUE
 compiled=[]
 for p in u.EditorAssetLibrary.list_assets(DST+'/Materials',recursive=False,include_folder=False):
  material=u.load_asset(p)
  if isinstance(material,u.Material):
   errors=u.MaterialEditingLibrary.recompile_material(material)
   assert not errors,(p,list(errors))
   compiled.append(material.get_name())
 report['compiled_materials']=compiled
 assert rig.sky_sphere_mesh and rig.stars.static_mesh
 assert rig.clouds.is_visible()
 assert rig.cloud_material and rig.cloud_material.get_path_name().startswith(DST)
 near=labels['DO_GroundNear'].static_mesh_component
 assert near.static_mesh.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
 count={}
 for a in actors:
  for c in a.get_components_by_class(u.HierarchicalInstancedStaticMeshComponent):
   count[a.get_actor_label()]=c.get_instance_count()
 assert sum(count['DO_Wall'+str(i)] for i in range(4))==90
 assert sum(count['DO_FineGrass'+str(i)] for i in range(3))>10000
 ar=u.AssetRegistryHelpers.get_asset_registry();ar.search_all_assets(True)
 opts=u.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True)
 seen=set();todo=[MAP]
 while todo:
  p=todo.pop()
  if p in seen:continue
  seen.add(p)
  if p.startswith('/Game/'):
   todo.extend(str(d) for d in ar.get_dependencies(p,opts) or [])
 forbidden=[p for p in seen if p.startswith('/Game/StyleHex_Studio/') or p.startswith('/Game/Env/Dreamfield/Textures/') or p.startswith('/Game/Env/Dreamfield/Meshes/')]
 assert not forbidden,forbidden
 # 对照建图前记录，检查八张其他开发图未发生变化。
 import hashlib
 original=json.loads((R/'Saved/Backup/DreamfieldOriginal_20261102/developer_maps.json').read_text())
 unchanged=[]
 for n,h in original.items():
  if n=='Dreamfield':continue
  assert hashlib.sha256((R/f'Content/Maps/L_{n}.umap').read_bytes()).hexdigest()==h,n
  unchanged.append(n)
 backup=R/'Saved/Backup/DreamfieldOriginal_20261102/L_Dreamfield.umap'
 assert hashlib.sha256(backup.read_bytes()).hexdigest()==original['Dreamfield']
 report.update(passed=True,actor_count=len(actors),elevators=len(elevators),sky_rigs=len(rigs),instances=count,old_visual_dependencies=forbidden,
  packages=len(seen),unchanged_other_maps=unchanged,original_map_backup_verified=True,remaining_old_environment_packages=sorted(p for p in seen if p.startswith('/Game/Env/Dreamfield/')))
except Exception:report['error']=traceback.format_exc();raise
finally:
 (R/'Saved/Automation/DreamfieldOriginalVerify.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 u.log('[dreamoriginal_verify] '+json.dumps(report,ensure_ascii=False))
