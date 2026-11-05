"""只读盘点已导入工业模块，供巨墙试拼使用。"""
import unreal as u,json
from pathlib import Path
out={}
for kit in ['modular_factory_facade','modular_airduct_rectangular_01','modular_industrial_pipes_01','modular_fire_escape']:
 rows={}
 for path in u.EditorAssetLibrary.list_assets('/Game/Env/PolyHaven/'+kit,True,False):
  a=u.load_asset(path)
  if not isinstance(a,u.StaticMesh):continue
  b=a.get_bounding_box();rows[a.get_name()]={'path':path,'min':[b.min.x/100,b.min.y/100,b.min.z/100],'max':[b.max.x/100,b.max.y/100,b.max.z/100],'triangles':a.get_num_triangles(0),'materials':[s.material_interface.get_path_name() if s.material_interface else None for s in a.static_materials]}
 out[kit]=rows
Path('D:/RobotSlice/Saved/Automation/DreamwallExistingProbe.json').write_text(json.dumps(out,indent=2))
