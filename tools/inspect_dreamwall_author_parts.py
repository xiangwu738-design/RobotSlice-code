"""只读检查现成结构部件：导入作者 FBX，输出尺寸与灰模缩略图，不导出新模型。"""
from pathlib import Path
import bpy,json,math
from mathutils import Vector
root=Path('D:/RobotSlice/SourceAssets/moon_free/quaternius_scifi/Modular SciFi MegaKit[Standard]/FBX')
out=Path('D:/RobotSlice/Saved/Automation/DreamwallAuthorParts');out.mkdir(parents=True,exist_ok=True)
names=['Column_Astra','Column_Hollow','Column_MetalSupport','Column_MetalSupport_Curve','WallAstra_Straight','WallAstra_Straight_Broken','WallAstra_Straight_Divided','WallBand_Straight_Broken']
report={}
for name in names:
 bpy.ops.wm.read_factory_settings(use_empty=True)
 file=next(root.rglob(name+'.fbx'));bpy.ops.import_scene.fbx(filepath=str(file))
 objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];points=[o.matrix_world@Vector(c) for o in objects for c in o.bound_box]
 lo=Vector(tuple(min(p[i] for p in points) for i in range(3)));hi=Vector(tuple(max(p[i] for p in points) for i in range(3)));center=(lo+hi)/2
 report[name]={'file':str(file),'min':list(lo),'max':list(hi),'objects':len(objects),'polygons':sum(len(o.data.polygons) for o in objects)}
 for o in objects:o.color=(.58,.62,.65,1)
 scene=bpy.context.scene;scene.world=bpy.data.worlds.new('PreviewWorld');scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=480;scene.render.resolution_y=480;scene.render.resolution_percentage=100
 scene.display.shading.light='STUDIO';scene.display.shading.color_type='OBJECT';scene.display.shading.show_shadows=False;scene.display.shading.show_cavity=True;scene.display.shading.cavity_type='BOTH';scene.display.shading.background_type='WORLD';scene.world.color=(.12,.12,.12)
 bpy.ops.object.camera_add(location=center+Vector((1.5,-2.8,1.1))*max(hi-lo));cam=bpy.context.object;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=max(hi-lo)*1.55;scene.camera=cam
 scene.render.filepath=str(out/(name+'.png'));bpy.ops.render.render(write_still=True)
(out/'parts.json').write_text(json.dumps(report,indent=2))
