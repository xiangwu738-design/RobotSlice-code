"""从可编辑文件整理运行时 UV；保留原贴图坐标与几何，重新输出 FBX。"""
import bpy,sys,json,shutil,datetime
from pathlib import Path
sys.path.insert(0,'D:/RobotSlice/tools')
import weapon_refit_common as c

backup=c.ROOT/'Saved/Backup'/('RefitUV_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
backup.mkdir(parents=True)
for meta in sorted(c.OUT.glob('SM_RF_*.json')):
    d=json.loads(meta.read_text(encoding='utf-8'));name=d['mesh']
    for ext in ('.blend','.fbx','.json'):
        shutil.copy2(c.OUT/(name+ext),backup/(name+ext))
    bpy.ops.wm.open_mainfile(filepath=str(c.OUT/(d['key']+'_editable.blend')))
    parts=[o for o in bpy.context.scene.objects if o.type=='MESH']
    for part in parts:c.runtime_uv(part)
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:part.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
    o=bpy.context.object;o.name=name
    bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    assert len(o.data.uv_layers)==1
    o.data.calc_loop_triangles();assert len(o.data.loop_triangles)==d['triangles']
    bpy.ops.export_scene.fbx(filepath=str(c.OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',add_leaf_bones=False)
    bpy.ops.wm.save_as_mainfile(filepath=str(c.OUT/(name+'.blend')))
    d['runtime_uv_channels']=1;d['active_uv_coordinates_preserved']=True
    meta.write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8')
    print('[UV_NORMALIZED]',name,flush=True)
