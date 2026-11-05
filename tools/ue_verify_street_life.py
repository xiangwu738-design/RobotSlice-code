"""读回老街：核对周期拼装、成套道具原点、路面材质和原地图，不保存关卡。"""
import unreal as u,json,hashlib,traceback
from collections import Counter
from pathlib import Path
R=Path('D:/RobotSlice');report={'passed':False}
try:
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_MillenniumStreet');assert world
 actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors();counts=Counter();parts={};instances=0;roads=0;cars=[]
 for a in actors:
  if a.get_actor_label().startswith('OS2_') and a.get_actor_label().endswith('_Ground'):
   for c in a.get_components_by_class(u.StaticMeshComponent):
    assert any(c.get_material(i) and c.get_material(i).get_name()=='M_StreetAsphalt' for i in range(c.get_num_materials()))
    roads+=1
  if not a.get_actor_label().startswith('OSE_'):continue
  for c in a.get_components_by_class(u.StaticMeshComponent):
   mesh=c.static_mesh
   if not mesh:continue
   mats=tuple(c.get_material(i).get_path_name() if c.get_material(i) else '' for i in range(c.get_num_materials()))
   if isinstance(c,u.InstancedStaticMeshComponent):
    tfs=[c.get_instance_transform(i,True) for i in range(c.get_instance_count())];instances+=len(tfs)
   else:tfs=[c.get_world_transform()]
   for tf in tfs:
    p=tf.translation;q=tf.rotation;s=tf.scale3d
    signature=(round((p.x+5000)%10000,1),round(p.y,1),round(p.z,1),round(q.x,4),round(q.y,4),round(q.z,4),round(q.w,4),round(s.x,4),round(s.y,4),round(s.z,4))
    counts[(mesh.get_path_name(),mats,signature)]+=1
    if 'potted_plant_02' in mesh.get_name():parts.setdefault(mesh.get_name(),[]).append(signature)
    if mesh.get_name()=='covered_car_covered_car':cars.append(abs(p.y)/100-.942)
 assert roads==2,roads
 bad=[(str(k),v) for k,v in counts.items() if v%11];assert not bad,bad[:3]
 assert len(parts)==3,parts.keys()
 series=[Counter(v) for v in parts.values()];assert all(v==series[0] for v in series),'盆土/叶片/花盆位置不一致'
 assert cars and min(cars)>=4.90,'停放车辆侵入中央通道'
 built=json.loads((R/'Saved/Automation/StreetExistingBuild.json').read_text());assert built['passed'] and instances==built['instances']
 original=json.loads((R/'Saved/Backup/DreamfieldOriginal_20261102/developer_maps.json').read_text())
 assert hashlib.sha256((R/'Content/Maps/L_OldStreet.umap').read_bytes()).hexdigest()==original['OldStreet']
 report.update(passed=True,instance_count=instances,period_copies=11,repeated_signatures=len(counts),potted_plants=len(next(iter(parts.values()))),plant_parts_aligned=True,dry_road_components=roads,min_central_half_width_m=min(cars),original_oldstreet_unchanged=True)
except Exception:report['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation/StreetLifeVerify.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
