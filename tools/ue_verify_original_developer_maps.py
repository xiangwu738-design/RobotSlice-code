"""从磁盘重新加载两张新图，检查原图哈希、场景引用和穹顶碰撞持久化。"""
import unreal as u,json,hashlib,traceback
from pathlib import Path
R=Path('D:/RobotSlice');REPORT={'passed':False,'maps':{}}
E=u.EditorAssetLibrary;S=u.get_editor_subsystem(u.EditorActorSubsystem)
try:
 ar=u.AssetRegistryHelpers.get_asset_registry();ar.search_all_assets(True)
 opts=u.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True)
 for name in ['MoonDome','MillenniumStreet']:
  path='/Game/Maps/L_'+name;world=u.EditorLoadingAndSavingUtils.load_map(path);assert world
  actors=list(S.get_all_level_actors());seen=set();todo=[path]
  while todo:
   p=todo.pop()
   if p in seen:continue
   seen.add(p)
   if p.startswith('/Game/'):todo.extend(str(x) for x in ar.get_dependencies(p,opts) or [])
  forbidden=[p for p in seen if '/Game/Env/OldStreet/' in p or 'SM_K65_' in p or '/K65/' in p]
  assert not forbidden,(name,forbidden)
  starts=[a for a in actors if isinstance(a,u.PlayerStart)]
  if name=='MoonDome':
   dome=next(a for a in actors if a.actor_has_tag('MoonShield'));c=dome.static_mesh_component
   assert c.get_collision_enabled()==u.CollisionEnabled.QUERY_ONLY
   assert c.get_collision_object_type()==u.CollisionChannel.ECC_WORLD_STATIC
   assert c.get_collision_response_to_channel(u.CollisionChannel.ECC_PAWN)==u.CollisionResponseType.ECR_IGNORE
   bs=c.static_mesh.get_editor_property('body_setup')
   assert bs.get_editor_property('collision_trace_flag')==u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
   assert bs.get_editor_property('double_sided_geometry')
   assert len([a for a in actors if isinstance(a,u.Landscape)])==1
  else:
   bounds=next(a for a in actors if a.actor_has_tag('OldStreetBounds'));assert 'PeriodX=10000' in [str(t) for t in bounds.tags]
  REPORT['maps'][name]={'actors':len(actors),'player_starts':len(starts),'packages':len(seen),'old_building_dependencies':forbidden}
 # 全部九张参考图中只允许梦核图改动。
 original=json.loads((R/'Saved/Backup/DreamfieldOriginal_20261102/developer_maps.json').read_text())
 for n,h in original.items():
  if n!='Dreamfield':assert hashlib.sha256((R/f'Content/Maps/L_{n}.umap').read_bytes()).hexdigest()==h,n
 REPORT['unchanged_original_maps']=8;REPORT['passed']=True
except Exception:REPORT['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation/OriginalMapsVerify.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
