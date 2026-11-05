"""只读核对现成街景模块的尺寸、材质与原点，不保存共享资源。"""
import unreal as u,json
from pathlib import Path
r=Path('D:/RobotSlice'); e=u.EditorAssetLibrary
names=['modular_urban_apartments_facade','rollershutter_door','exterior_aircon_unit','modular_electricity_poles','modular_electric_cables','modular_metal_gutter','utility_box_01','street_lamp_01','water_manhole_cover','metal_trash_can','old_tyre','covered_car']
life=['plastic_monobloc_chair_01','folding_wooden_stool','small_wooden_table_01','plastic_crate_02','potted_plant_02']
out={}
for name in names+life:
 rows={}
 folder=('/Game/Env/MillenniumStreetExisting/Life/' if name in life else '/Game/Env/PolyHaven/')+name
 for path in e.list_assets(folder,True,False):
  a=u.load_asset(path)
  if not isinstance(a,u.StaticMesh):continue
  b=a.get_bounding_box()
  rows[a.get_name()]={'path':path,'min':[b.min.x/100,b.min.y/100,b.min.z/100],'max':[b.max.x/100,b.max.y/100,b.max.z/100],'triangles':a.get_num_triangles(0),'materials':[s.material_interface.get_path_name() if s.material_interface else None for s in a.static_materials]}
 out[name]=rows
(r/'Saved/Automation/StreetExistingProbe.json').write_text(json.dumps(out,indent=2),encoding='utf8')
u.log('[street] source geometry inspected')
