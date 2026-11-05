"""重新加载免费资源拼装版，核对持久化、玩法位置与其他开发图；不保存资源。"""
import unreal as u,json,hashlib,traceback
from pathlib import Path
R=Path('D:/RobotSlice');MAP='/Game/Maps/L_Dreamfield';report={'passed':False,'map':MAP}
try:
 world=u.EditorLoadingAndSavingUtils.load_map(MAP);assert world
 actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors();labels={a.get_actor_label():a for a in actors}
 built=json.loads((R/'Saved/Automation/DreamfieldFree.json').read_text(encoding='utf8'))
 assert built['passed'] and not built['study']
 for name,position in built['preserved_functional'].items():
  a=labels[name];assert [round(v,2) for v in (a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z)]==position,name
 assert sum(a.get_class().get_name()=='MechElevator' for a in actors)==2
 assert not any(n.startswith(('DO_Wall','DO_FineGrass','DO_WhiteDaisies')) for n in labels)
 counts={a.get_actor_label():sum(c.get_instance_count() for c in a.get_components_by_class(u.HierarchicalInstancedStaticMeshComponent)) for a in actors if a.get_actor_label().startswith('Free_')}
 assert sum(v for n,v in counts.items() if n.startswith('Free_SM_Free_BermudaPatch'))==built['grass_instances']
 bastion=any(n.startswith('DB_') for n in labels)
 if bastion:
  wall_report=json.loads((R/'Saved/Automation/DreamwallBastion.json').read_text(encoding='utf8'))
  assert wall_report['passed'] and wall_report['full']
  wall_counts={a.get_actor_label():sum(c.get_instance_count() for c in a.get_components_by_class(u.HierarchicalInstancedStaticMeshComponent)) for a in actors if a.get_actor_label().startswith('DB_')}
  assert wall_counts==wall_report['instances']
  assert not any(n.startswith(('Free_Wall','DI_')) for n in labels)
  for a in actors:
   if a.get_actor_label().startswith('DB_'):
    for c in a.get_components_by_class(u.MeshComponent):
     assert c.static_mesh.get_path_name().startswith('/Game/Env/DreamfieldBastion/')
     for i in range(c.get_num_materials()):assert c.get_material(i).get_path_name().startswith('/Game/Env/DreamfieldBastion/')
  counts.update(wall_counts)
  report['bastion_verified']={'groups':len(wall_counts),'instances':sum(wall_counts.values())}
 else:
  assert sum(v for n,v in counts.items() if n.startswith('Free_Wall'))==built['wall_instances']
 rig=labels['DO_DreamSky_ALS'];assert '/DreamfieldFree/' in rig.star_material.get_path_name();assert not rig.clouds.is_visible()
 near=labels['DO_GroundNear'].static_mesh_component.static_mesh
 assert not near.get_editor_property('nanite_settings').get_editor_property('enabled')
 assert near.get_editor_property('body_setup').get_editor_property('collision_trace_flag')==u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
 mats=[]
 if bastion:
  for p in u.EditorAssetLibrary.list_assets('/Game/Env/DreamfieldBastion/Materials',recursive=False,include_folder=False):
   m=u.load_asset(p)
   if isinstance(m,u.Material):
    errors=u.MaterialEditingLibrary.recompile_material(m);assert not errors,(p,list(errors));mats.append(m.get_name())
 for p in u.EditorAssetLibrary.list_assets('/Game/Env/DreamfieldFree/Materials',recursive=False,include_folder=False):
  m=u.load_asset(p)
  if isinstance(m,u.Material):
   errors=u.MaterialEditingLibrary.recompile_material(m);assert not errors,(p,list(errors));mats.append(m.get_name())
 lawn=any(c.get_material(0) and '/DreamfieldLawn/' in c.get_material(0).get_path_name() for a in actors for c in a.get_components_by_class(u.HierarchicalInstancedStaticMeshComponent) if a.get_actor_label().startswith('Free_SM_Free_BermudaPatch'))
 if lawn:
  for a in actors:
   name=a.get_actor_label()
   expected='M_LawnBlades' if name.startswith('Free_SM_Free_BermudaPatch') else 'M_LawnGround' if name in ('DO_GroundNear','DO_GroundFar') else None
   if expected:
    for c in a.get_components_by_class(u.MeshComponent):
     for i in range(c.get_num_materials()):assert c.get_material(i).get_name()==expected,(name,i)
  for name in ('M_LawnBlades','M_LawnGround'):
   m=u.load_asset('/Game/Env/DreamfieldLawn/Materials/'+name);assert m
   errors=u.MaterialEditingLibrary.recompile_material(m);assert not errors,(name,list(errors));mats.append(name)
  report['lawn_material_bindings_verified']=True
 if 'NASA_DreamPlanet' in labels:
  planet=labels['NASA_DreamPlanet'];c=planet.static_mesh_component;mesh=c.static_mesh
  assert len([a for a in actors if a.actor_has_tag('NASA_VTAD_Planet')])==1
  assert '/DreamfieldPlanet/AuthorMesh/' in mesh.get_path_name()
  assert mesh.get_num_triangles(0)==3072 and not mesh.get_editor_property('nanite_settings').enabled
  assert c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION and not c.cast_shadow
  m=c.get_material(0);assert m.get_path_name().startswith('/Game/Env/DreamfieldPlanet/')
  errors=u.MaterialEditingLibrary.recompile_material(m);assert not errors,list(errors);mats.append(m.get_name())
  planet_report=json.loads((R/'Saved/Automation/DreamfieldPlanet.json').read_text(encoding='utf8'));assert planet_report['passed'] and planet_report['full']
  pos=planet.get_actor_location();assert all(abs(x-y)<.1 for x,y in zip((pos.x,pos.y,pos.z),planet_report['position_cm']))
  report['planet_verified']={'triangles':3072,'collision':'NoCollision','cast_shadow':False,'source':'NASA VTAD'}
 original=json.loads((R/'Saved/Backup/DreamfieldOriginal_20261102/developer_maps.json').read_text());unchanged=[]
 for name,h in original.items():
  if name=='Dreamfield':continue
  assert hashlib.sha256((R/f'Content/Maps/L_{name}.umap').read_bytes()).hexdigest()==h,name
  unchanged.append(name)
 assert hashlib.sha256((R/'Saved/Backup/DreamfieldOriginal_20261102/L_Dreamfield.umap').read_bytes()).hexdigest()==original['Dreamfield']
 report.update(passed=True,preserved_functional=len(built['preserved_functional']),elevators=2,compiled_materials=mats,instances=counts,unchanged_other_maps=unchanged,original_backup_verified=True,near_ground_triangles=near.get_num_triangles(0))
except Exception:report['error']=traceback.format_exc();raise
finally:
 (R/'Saved/Automation/DreamfieldFreeVerify.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
 u.log('[free_verify] '+json.dumps(report,ensure_ascii=False))
