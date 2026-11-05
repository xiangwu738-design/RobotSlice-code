"""从已有功能模型分离地板与地环，只删除旧掩体/发射器部件，不创建几何。"""
import bpy,bmesh,json
from pathlib import Path
R=Path('D:/RobotSlice');OUT=R/'SourceAssets/moon_free/retained';OUT.mkdir(parents=True,exist_ok=True)
report={}
for src,dst in [('SM_OM_DomeInterior','RetainedFloor'),('SM_OM_DomeRing','RetainedRing')]:
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 bpy.ops.import_scene.fbx(filepath=str(R/'SourceAssets/original_developer_maps'/(src+'.fbx')))
 ob=next(o for o in bpy.context.scene.objects if o.type=='MESH')
 bpy.context.view_layer.objects.active=ob;ob.select_set(True)
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 bm=bmesh.new();bm.from_mesh(ob.data);seen=set();remove=[];components=[]
 for v in bm.verts:
  if v in seen:continue
  todo=[v];seen.add(v);part=[]
  while todo:
   q=todo.pop();part.append(q)
   for e in q.link_edges:
    n=e.other_vert(q)
    if n not in seen:seen.add(n);todo.append(n)
  top=max(q.co.z for q in part);components.append({'vertices':len(part),'top':top,'kept':top<.5})
  if top>=.5:remove.extend(part)
 bmesh.ops.delete(bm,geom=remove,context='VERTS');bm.to_mesh(ob.data);bm.free()
 assert len(ob.data.polygons)>100
 for i,m in enumerate(ob.data.materials):
  if m:ob.data.materials[i]=bpy.data.materials.get(m.name.split('.')[0]) or m
 bpy.ops.export_scene.fbx(filepath=str(OUT/(dst+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',bake_anim=False,axis_forward='-Z',axis_up='Y')
 report[dst]={'source':src,'components':components,'triangles':sum(len(p.vertices)-2 for p in ob.data.polygons)}
(OUT/'manifest.json').write_text(json.dumps(report,indent=2))
print({k:v['triangles'] for k,v in report.items()})
