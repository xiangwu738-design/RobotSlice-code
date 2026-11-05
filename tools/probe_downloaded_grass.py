import bpy,json
from pathlib import Path
from mathutils import Vector
root=Path('D:/RobotSlice/SourceAssets/dreamfield_free');result={}
for name in ('grass_medium_01','grass_bermuda_01'):
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 bpy.ops.import_scene.fbx(filepath=str(root/name/(name+'.fbx')))
 result[name]=[]
 for ob in bpy.context.scene.objects:
  if ob.type!='MESH':continue
  verts=[ob.matrix_world@Vector(v) for v in ob.bound_box]
  result[name].append({'name':ob.name,'triangles':sum(len(p.vertices)-2 for p in ob.data.polygons),'loc':list(ob.location),'bounds_min':[min(v[i] for v in verts) for i in range(3)],'bounds_max':[max(v[i] for v in verts) for i in range(3)],'materials':[s.name for s in ob.data.materials]})
(root/'grass_probe.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
