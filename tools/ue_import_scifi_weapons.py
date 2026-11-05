"""Import the original SF arsenal into separate assets; validate dimensions and slots.
Run UnrealEditor-Cmd project -run=pythonscript -script=... -unattended -nullrhi
No existing weapon mesh is overwritten. Code table switching is a separate operation.
"""
import json,math
from pathlib import Path
import unreal

ROOT=Path('D:/RobotSlice');SRC=ROOT/'SourceAssets/scifi_weapons'
DEST='/Game/Weapons';eal=unreal.EditorAssetLibrary;mel=unreal.MaterialEditingLibrary
at=unreal.AssetToolsHelpers.get_asset_tools()
palettes={
 'VHI':dict(Paint='#414c48',Secondary='#20292b',Accent='#ac3f35',Glow='#ff5646'),
 'PHASE':dict(Paint='#929ea3',Secondary='#303e49',Accent='#387d89',Glow='#69edee'),
 'NEC':dict(Paint='#707c83',Secondary='#30383e',Accent='#d18b34',Glow='#f6b34e'),
}
common=dict(Frame=('#222a30',.8,.33),Rubber=('#171c20',.05,.73),Steel=('#8d9ba5',.92,.25),Edge=('#b2bec5',.96,.21),Black=('#090e13',.35,.38),Mark=('#c1cbce',.3,.48),Lens=('#15495b',.78,.14))
def color(h):
 v=[int(h.strip('#')[i:i+2],16)/255 for i in (0,2,4)]
 return unreal.LinearColor(*[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in v],1)

masterpath=DEST+'/Materials/M_SFManufactured'
if not eal.does_asset_exist(masterpath):
 assert eal.duplicate_asset('/Game/Robot/Materials/M_RobotMaster',masterpath)
master=eal.load_asset(masterpath)
master.set_editor_property('used_with_nanite',True)
master.set_editor_property('used_with_instanced_static_meshes',True)
mel.recompile_material(master);eal.save_loaded_asset(master,False)
materials={}
def material(slot):
 if slot in materials:return materials[slot]
 fields=slot.split('_');role=fields[-1];maker=fields[1] if len(fields)==3 else None
 if maker:
  c=palettes[maker][role];metal=.1 if role=='Glow' else .48;rough=.36
 else:c,metal,rough=common[role]
 name='MI_'+slot;path=DEST+'/Materials/'+name
 mi=eal.load_asset(path) if eal.does_asset_exist(path) else at.create_asset(name,DEST+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
 mi.set_editor_property('parent',master)
 mel.set_material_instance_vector_parameter_value(mi,'BaseColor',color(c))
 mel.set_material_instance_scalar_parameter_value(mi,'Metallic',metal)
 mel.set_material_instance_scalar_parameter_value(mi,'Roughness',rough)
 mel.set_material_instance_scalar_parameter_value(mi,'RoughnessVariation',.025)
 mel.set_material_instance_vector_parameter_value(mi,'Emissive',color(c) if role=='Glow' else unreal.LinearColor(0,0,0,1))
 mel.set_material_instance_scalar_parameter_value(mi,'EmissiveStrength',1.6 if role=='Glow' else 0)
 eal.save_loaded_asset(mi,False);materials[slot]=mi;return mi

unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
results=[]
for meta in sorted(SRC.glob('SM_SF_*.json')):
 d=json.loads(meta.read_text(encoding='utf-8'));name=d['mesh']
 task=unreal.AssetImportTask();task.filename=str(SRC/(name+'.fbx'))
 task.destination_path=DEST+'/Meshes';task.destination_name=name
 task.automated=True;task.replace_existing=True;task.save=False
 ui=unreal.FbxImportUI();ui.import_mesh=True;ui.import_as_skeletal=False
 ui.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
 ui.import_materials=False;ui.import_textures=False
 ui.static_mesh_import_data.set_editor_property('combine_meshes',True)
 ui.static_mesh_import_data.set_editor_property('auto_generate_collision',False)
 ui.static_mesh_import_data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
 task.options=ui;at.import_asset_tasks([task])
 mesh=eal.load_asset(DEST+'/Meshes/'+name)
 assert isinstance(mesh,unreal.StaticMesh),name
 slots=list(mesh.get_editor_property('static_materials'))
 for i,s in enumerate(slots):
  slot=str(s.get_editor_property('imported_material_slot_name'))
  assert slot in d['materials'],(name,slot,d['materials'])
  s.set_editor_property('material_slot_name',slot)
  s.set_editor_property('material_interface',material(slot));slots[i]=s
 mesh.set_editor_property('static_materials',slots)
 ns=mesh.get_editor_property('nanite_settings');ns.set_editor_property('enabled',True)
 ns.set_editor_property('keep_percent_triangles',1.0);mesh.set_editor_property('nanite_settings',ns)
 eal.save_loaded_asset(mesh,False)
 bb=mesh.get_bounding_box();low=[bb.min.x,bb.min.y,bb.min.z];high=[bb.max.x,bb.max.y,bb.max.z]
 # Y is mirrored by the established FBX conversion; compare extents there, bounds for X/Z.
 assert all(abs(low[i]-d['min'][i])<.25 and abs(high[i]-d['max'][i])<.25 for i in (0,2)),(name,low,high,d['min'],d['max'])
 assert abs((high[1]-low[1])-(d['max'][1]-d['min'][1]))<.25,name
 assert all(s.material_interface is not None for s in mesh.static_materials),name
 result=dict(mesh=name,triangles=mesh.get_num_triangles(0),slots=len(slots),min=low,max=high)
 results.append(result);unreal.log('[SF IMPORT] '+json.dumps(result))
(SRC/'ue_validation.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
assert len(results)==13,len(results)
unreal.log('[SF IMPORT] PASS: 13 meshes, dimensions and all material slots verified.')
