import bpy,json
from pathlib import Path
root=Path('D:/RobotSlice/SourceAssets/dreamfield_free/brutalist_buildings')
bpy.ops.wm.open_mainfile(filepath=str(root/'BrutalistBuildings_Renders.blend'),load_ui=False,use_scripts=False)
data={o.name:{'dimensions':list(o.dimensions),'rotation':list(o.rotation_euler),'polys':len(o.data.polygons)} for o in bpy.data.collections['Parts'].all_objects if o.type=='MESH'}
(root/'modules.json').write_text(json.dumps(data,indent=2))
