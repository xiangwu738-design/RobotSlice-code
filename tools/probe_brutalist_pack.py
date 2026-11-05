import bpy,json
from pathlib import Path
from mathutils import Vector
root=Path('D:/RobotSlice/SourceAssets/dreamfield_free/brutalist_buildings')
bpy.ops.wm.open_mainfile(filepath=str(root/'BrutalistBuildings_Renders.blend'),load_ui=False,use_scripts=False)
r={'collections':{},'materials':{},'cameras':[]}
for c in bpy.data.collections:
 obs=[o for o in c.all_objects if o.type=='MESH'];vs=[o.matrix_world@Vector(v) for o in obs for v in o.bound_box]
 r['collections'][c.name]={'direct':len(c.objects),'meshes':len(obs),'children':[s.name for s in c.children],'sample':[o.name for o in obs[:6]],'bounds':[ [min(v[i] for v in vs) for i in range(3)], [max(v[i] for v in vs) for i in range(3)]] if vs else []}
for m in bpy.data.materials:r['materials'][m.name]=[(n.name,n.image.filepath) for n in m.node_tree.nodes if n.type=='TEX_IMAGE' and n.image] if m.node_tree else []
for ob in bpy.context.scene.objects:
 if ob.type=='CAMERA':r['cameras'].append({'name':ob.name,'position':list(ob.location)})
(root/'probe.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
