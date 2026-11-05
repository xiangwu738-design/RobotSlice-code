"""只读拆分原武器，记录部件边界以便按真实结构改造；不改原 FBX。"""
import bpy,json,sys
from pathlib import Path
sys.path.insert(0,'D:/RobotSlice/tools')
import build_scifi_weapons as b
root=Path('D:/RobotSlice/SourceAssets');out=root/'scifi_weapons/refit_bases';out.mkdir(exist_ok=True)
report={}
for d in b.GUNS+b.BLADES:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    src=root/('guns' if 'muzzle' in d else 'swords')/(d['old']+'.fbx')
    bpy.ops.import_scene.fbx(filepath=str(src))
    for o in list(bpy.context.selected_objects):
        if o.type!='MESH':continue
        bpy.context.view_layer.objects.active=o
        bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.separate(type='LOOSE');bpy.ops.object.mode_set(mode='OBJECT')
    obs=sorted([o for o in bpy.context.scene.objects if o.type=='MESH'],key=lambda o:len(o.data.vertices),reverse=True)
    rows=[]
    for i,o in enumerate(obs):
        o.name=f'BasePart_{i:03}'
        vs=[o.matrix_world@v.co for v in o.data.vertices]
        rows.append(dict(id=i,verts=len(vs),lo=[round(min(v[j] for v in vs)*100,2) for j in range(3)],hi=[round(max(v[j] for v in vs)*100,2) for j in range(3)]))
    report[d['key']]=rows
    bpy.ops.wm.save_as_mainfile(filepath=str(out/(d['key']+'.blend')))
(out/'components.json').write_text(json.dumps(report,indent=2))
print('BASES',[(k,len(v)) for k,v in report.items()],flush=True)
