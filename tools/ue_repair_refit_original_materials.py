"""修复未落盘的原贴图材质副本；先备份，再保存材质和网格引用，不重导几何。"""
import unreal,json,shutil,datetime
from pathlib import Path
r=Path('D:/RobotSlice');src=r/'SourceAssets/weapon_refits'
dest='/Game/Weapons/Refits';eal=unreal.EditorAssetLibrary
backup=r/'Saved/Backup'/('RefitOriginalMaterials_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
shutil.copytree(r/'Content/Weapons/Refits',backup)
count=0
for p in sorted(src.glob('SM_RF_*.json')):
    d=json.loads(p.read_text(encoding='utf-8'))
    info=d['materials']['RF_Original'];path=dest+'/Materials/MI_RF_'+d['key']+'_RF_Original'
    if not eal.does_asset_exist(path):
        assert eal.duplicate_asset('/Game/Weapons/Materials/MI_'+info['original'],path),path
    material=eal.load_asset(path);assert material,path
    assert eal.save_loaded_asset(material,False),path
    assert (r/'Content'/path.removeprefix('/Game/')).with_suffix('.uasset').is_file(),path
    mesh=eal.load_asset(dest+'/Meshes/'+d['mesh']);assert mesh
    slots=list(mesh.static_materials);fixed=0
    for i,slot in enumerate(slots):
        if str(slot.material_slot_name)=='RF_Original':
            slot.set_editor_property('material_interface',material);slots[i]=slot;fixed+=1
    assert fixed==1,(d['key'],fixed)
    mesh.set_editor_property('static_materials',slots)
    assert all(s.material_interface for s in mesh.static_materials),d['key']
    assert eal.save_loaded_asset(mesh,False),d['key']
    count+=1
unreal.log('[REFIT_ORIGINAL_REPAIR] PASS '+str(count)+' saved materials and complete mesh material slots')
