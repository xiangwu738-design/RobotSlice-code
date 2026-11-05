"""仅将 NASA 原始 GLB 转成 FBX 并提取原贴图；不创建或简化球体几何。"""
from pathlib import Path
import bpy,json,struct,hashlib
r=Path('D:/RobotSlice/SourceAssets/dreamfield_free/nasa_jupiter');p=r/'Jupiter_1_142984.glb'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(p))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(objects)==1
o=objects[0];o.data.calc_loop_triangles();assert len(o.data.loop_triangles)==3072
bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
bpy.ops.export_scene.fbx(filepath=str(r/'Jupiter_author.fbx'),use_selection=True,object_types={'MESH'},add_leaf_bones=False,bake_anim=False,path_mode='AUTO')
data=p.read_bytes();length=struct.unpack_from('<I',data,12)[0];g=json.loads(data[20:20+length]);bin_start=20+length+8
v=g['bufferViews'][g['images'][0]['bufferView']];image=data[bin_start+v.get('byteOffset',0):bin_start+v.get('byteOffset',0)+v['byteLength']]
assert image[:8]==b'\x89PNG\r\n\x1a\n';(r/'Jupiter_author.png').write_bytes(image)
(r/'prepared.json').write_text(json.dumps({'triangles':3072,'operation':'Author mesh export only, no new geometry or decimation','source_sha256':hashlib.sha256(data).hexdigest(),'texture_sha256':hashlib.sha256(image).hexdigest()},indent=2))
