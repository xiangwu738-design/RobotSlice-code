"""为保留地板/地环建立本图材质副本，修复 Nanite 灰色回退，不改变碰撞或几何。"""
from pathlib import Path
import shutil,datetime,json
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_moon_free_import.py').read_text(encoding='utf8').split('\ntry:\n dest=')[0],'moon_retained_helpers','exec'))
b=R/'Saved/Backup'/('MoonRetainedMaterials_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
shutil.copytree(R/'Content/Env/MoonFree',b/'MoonFree');names=set()
for name in ('RetainedFloor','RetainedRing'):
 mesh=asset(DST+'/Meshes/SM_'+name);slots=list(mesh.static_materials)
 for i,s in enumerate(slots):
  slot=str(s.get_editor_property('imported_material_slot_name'));dest=DST+'/Materials/M_Retained_'+slot
  m=asset(dest) if E.does_asset_exist(dest) else E.duplicate_asset('/Game/Env/OriginalDeveloperMaps/Materials/M_'+slot,dest)
  prop(m,'used_with_nanite',True);prop(m,'used_with_instanced_static_meshes',True);errors=M.recompile_material(m);assert not errors;save(m)
  prop(s,'material_interface',m);slots[i]=s;names.add(m.get_path_name())
 prop(mesh,'static_materials',slots);save(mesh)
(R/'Saved/Automation/MoonRetainedMaterials.json').write_text(json.dumps({'passed':True,'materials':sorted(names),'backup':str(b)},indent=2))
