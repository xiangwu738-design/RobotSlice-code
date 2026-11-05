"""独立导入新武器，核验三路涂装和尺寸；不在此脚本里切换装备表。"""
import unreal,json,sys,shutil,datetime
from pathlib import Path
ROOT=Path('D:/RobotSlice');SRC=ROOT/'SourceAssets/weapon_refits';DEST='/Game/Weapons/Refits'
eal=unreal.EditorAssetLibrary;mel=unreal.MaterialEditingLibrary;at=unreal.AssetToolsHelpers.get_asset_tools()
existing=ROOT/'Content/Weapons/Refits'
if existing.exists():
    backup=ROOT/'Saved/Backup'/('WeaponRefits_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
    shutil.copytree(existing,backup)
masterpath=DEST+'/Materials/M_RefitCoating'
if not eal.does_asset_exist(masterpath):assert eal.duplicate_asset('/Game/Robot/Materials/M_RobotMaster',masterpath)
master=eal.load_asset(masterpath)
master.set_editor_property('used_with_nanite',True);master.set_editor_property('used_with_instanced_static_meshes',True)
mel.recompile_material(master);eal.save_loaded_asset(master,False)
assert 'BaseColor' in [str(n) for n in mel.get_vector_parameter_names(master)]

def color(h):
    vals=[int(h.strip('#')[i:i+2],16)/255 for i in (0,2,4)]
    return unreal.LinearColor(*[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in vals],1)

def material(d,info):
    name='MI_RF_'+d['key']+'_'+info['slot']
    path=DEST+'/Materials/'+name
    if 'original' in info:
        if not eal.does_asset_exist(path):assert eal.duplicate_asset('/Game/Weapons/Materials/MI_'+info['original'],path)
        original=eal.load_asset(path)
        assert original and eal.save_loaded_asset(original,False),path
        return original
    mi=eal.load_asset(path) if eal.does_asset_exist(path) else at.create_asset(name,DEST+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent',master)
    mel.set_material_instance_vector_parameter_value(mi,'BaseColor',color(info['color']))
    for key,value in [('Metallic',info['metal']),('Roughness',info['rough']),('RoughnessVariation',.025),('EmissiveStrength',info.get('glow',0))]:
        mel.set_material_instance_scalar_parameter_value(mi,key,value)
    mel.set_material_instance_vector_parameter_value(mi,'Emissive',color(info['color']) if info.get('glow',0) else unreal.LinearColor(0,0,0,1))
    eal.save_loaded_asset(mi,False);return mi

unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
selected=globals().get('REFIT_KEYS')  # 调整单把外形时可局部重导，其他验证记录保留。
results=[]
if selected and (SRC/'ue_validation.json').exists():
    results=[d for d in json.loads((SRC/'ue_validation.json').read_text(encoding='utf-8')) if d['key'] not in selected]
imported_count=0
for path in sorted(SRC.glob('SM_RF_*.json')):
    d=json.loads(path.read_text(encoding='utf-8'));name=d['mesh']
    if selected and d['key'] not in selected:continue
    task=unreal.AssetImportTask();task.filename=str(SRC/(name+'.fbx'));task.destination_path=DEST+'/Meshes';task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=False
    ui=unreal.FbxImportUI();ui.import_mesh=True;ui.import_as_skeletal=False;ui.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
    ui.import_materials=False;ui.import_textures=False
    ui.static_mesh_import_data.set_editor_property('combine_meshes',True)
    ui.static_mesh_import_data.set_editor_property('auto_generate_collision',False)
    ui.static_mesh_import_data.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    task.options=ui;at.import_asset_tasks([task])
    mesh=eal.load_asset(DEST+'/Meshes/'+name);assert isinstance(mesh,unreal.StaticMesh),name
    slots=list(mesh.static_materials)
    for i,s in enumerate(slots):
        imported=str(s.get_editor_property('imported_material_slot_name'));assert imported in d['materials'],(name,imported)
        info=d['materials'][imported]
        s.set_editor_property('material_slot_name',info['slot']);s.set_editor_property('material_interface',material(d,info));slots[i]=s
    mesh.set_editor_property('static_materials',slots)
    names=[str(s.material_slot_name) for s in slots]
    for role in ('MI_Robot_Paint','MI_Robot_PaintDark','MI_Robot_Accent'):assert names.count(role)==1,(name,role,names)
    ns=mesh.get_editor_property('nanite_settings');ns.set_editor_property('enabled',True)
    target=25000 if 'muzzle' in d and d['key']!='DualPistol' else 12000
    ns.set_editor_property('keep_percent_triangles',min(1.,target/d['triangles']));mesh.set_editor_property('nanite_settings',ns)
    eal.save_loaded_asset(mesh,False)
    bb=mesh.get_bounding_box();lo=[bb.min.x,bb.min.y,bb.min.z];hi=[bb.max.x,bb.max.y,bb.max.z]
    assert all(abs(lo[i]-d['min'][i])<.25 and abs(hi[i]-d['max'][i])<.25 for i in (0,2)),(name,lo,hi)
    assert abs((hi[1]-lo[1])-(d['max'][1]-d['min'][1]))<.25,name
    results.append(dict(key=d['key'],mesh=DEST+'/Meshes/'+name,slots=names,source_triangles=d['triangles'],nanite_keep=min(1.,target/d['triangles']),bounds_ok=True))
    imported_count+=1
(SRC/'ue_validation.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('[REFIT_IMPORT] PASS '+str(imported_count)+' models: dimensions, materials, three paint roles. Inventory: '+str(len(results)))
