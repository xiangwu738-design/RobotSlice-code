"""导入免费生活道具到老街专属目录；引擎材质仅复制，不保存其原包。"""
from pathlib import Path
import json,shutil,datetime
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_street_existing.py').read_text(encoding='utf8').split('\ntry:\n stamp=')[0],'street_life_helpers','exec'))
src=R/'SourceAssets/millennium_street_existing/life'
report={'passed':False,'models':{}}
backup=R/'Saved/Backup'/('StreetLifeImport_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
p=R/'Content/Env/MillenniumStreetExisting'
shutil.copytree(p,backup/p.name)
for entry in json.loads((src/'manifest.json').read_text()):
 name=entry['id'];dest=DST+'/Life/'+name
 if not E.does_directory_exist(dest):
  task=u.AssetImportTask();task.filename=str(next((src/name).glob('*.gltf')));task.destination_path=dest;task.automated=True;task.replace_existing=False;task.save=False
  T.import_asset_tasks([task])
 meshes=[]
 for path in E.list_assets(dest,True,False):
  a=asset(path)
  if isinstance(a,u.StaticMesh):
   slots=list(a.static_materials)
   for i,s in enumerate(slots):
    if s.material_interface:prop(s,'material_interface',local_material(s.material_interface));slots[i]=s
   prop(a,'static_materials',slots)
   settings=a.get_editor_property('nanite_settings');settings.enabled=True;prop(a,'nanite_settings',settings)
   body=a.get_editor_property('body_setup');prop(body,'collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
   meshes.append(a.get_name())
  save(a)
 assert meshes,name
 report['models'][name]=meshes
report.update(passed=True,materials_cloned=len(matcache),backup=str(backup))
(R/'Saved/Automation/StreetLifeImport.json').write_text(json.dumps(report,indent=2))
