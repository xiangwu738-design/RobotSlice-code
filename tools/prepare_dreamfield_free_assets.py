"""仅整理下载资源：分离现成草丛／合并同一建筑、归零原点、转 FBX。没有新建几何。"""
import bpy,json,re,random,math
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/RobotSlice/SourceAssets/dreamfield_free');OUT=ROOT/'prepared';OUT.mkdir(exist_ok=True)
report={'meshes':{}}
def export(ob,name,source,center=True):
 bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 vs=[ob.matrix_world@Vector(v) for v in ob.bound_box];lo=[min(v[i] for v in vs) for i in range(3)];hi=[max(v[i] for v in vs) for i in range(3)]
 if center:
  anchor=Vector(((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,lo[2]));ob.location-=anchor
 else:ob.location=Vector((0,0,0))
 bpy.ops.object.transform_apply(location=True,rotation=False,scale=False)
 bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',bake_anim=False,axis_forward='-Z',axis_up='Y')
 report['meshes'][name]={'source':source,'dimensions':[hi[i]-lo[i] for i in range(3)],'triangles':sum(len(p.vertices)-2 for p in ob.data.polygons),'slots':[s.name for s in ob.data.materials]}
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ROOT/'grass_medium_01/grass_medium_01.fbx'))
for suffix in ('large_a','large_b','mid_a','mid_b','mid_c','small_a'):
 ob=bpy.data.objects['grass_medium_01_'+suffix+'_LOD0'];export(ob,'SM_Free_Grass_'+suffix,'https://polyhaven.com/a/grass_medium_01',False)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ROOT/'grass_bermuda_01/grass_bermuda_01.fbx'))
stems=[bpy.data.objects['grass_bermuda_01_'+suffix] for suffix in ('small_a','small_b','small_c','medium_a','medium_c','medium_e')]
# 仅散布并合批作者现有草株，降低关卡实例数量；不生成叶片或修改原网格、UV。
for variant in range(2):
 r=random.Random(3411+variant);copies=[]
 bpy.ops.object.select_all(action='DESELECT')
 for ix in range(12):
  for iy in range(12):
   source=r.choice(stems);ob=source.copy();ob.data=source.data.copy();ob.parent=None
   bpy.context.scene.collection.objects.link(ob)
   ob.location=((ix+r.random())/12-.5,(iy+r.random())/12-.5,0)
   ob.rotation_euler=(0,0,r.random()*math.tau);sc=r.uniform(1.5,2.1);ob.scale=(sc,sc,r.uniform(1.15,1.65))
   ob.select_set(True);copies.append(ob)
 bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join()
 export(bpy.context.object,'SM_Free_BermudaPatch_'+str(variant),'https://polyhaven.com/a/grass_bermuda_01')
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'brutalist_buildings/BrutalistBuildings_Renders.blend'),load_ui=False,use_scripts=False)
groups={}
for ob in list(bpy.data.collections['BUild_Optimized'].all_objects):
 if ob.type!='MESH' or 'colonly' in ob.name.lower():continue
 match=re.match(r'^Build_?0*(\d+)',ob.name,re.I)
 if match:groups.setdefault(int(match[1]),[]).append(ob)
for i,obs in sorted(groups.items()):
 bpy.ops.object.select_all(action='DESELECT')
 copies=[]
 for source in obs:
  ob=source.copy();ob.data=source.data.copy();world=source.matrix_world.copy();ob.parent=None;ob.matrix_world=world
  bpy.context.scene.collection.objects.link(ob);ob.hide_set(False);ob.hide_viewport=False;ob.hide_select=False;ob.select_set(True);copies.append(ob)
 bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();ob=bpy.context.object
 export(ob,'SM_Free_Brutalist_'+str(i),'https://loafbrr.itch.io/brutalist-themed-buildings')
for part in ('Pillar_Side_D','Pillar_Side_A_2','Floor_Platform_2_Edge','Wall_Wall','Wall_Window_Slit_Center_1_Vertical_2','Pillar_Wide_3','Pillar_Wide_Cap'):
 source=bpy.data.objects.get(part)
 if not source:continue
 ob=source.copy();ob.data=source.data.copy();world=source.matrix_world.copy();ob.parent=None;ob.matrix_world=world
 bpy.context.scene.collection.objects.link(ob);ob.hide_set(False);ob.hide_viewport=False;ob.hide_select=False
 export(ob,'SM_Free_'+part,'https://loafbrr.itch.io/brutalist-themed-buildings')
(OUT/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
(ROOT/'brutalist_buildings/provenance.json').write_text(json.dumps({'author':'loafbrr','source':'https://loafbrr.itch.io/brutalist-themed-buildings','license':'CC0 1.0 Universal','license_evidence':'作者页面 Asset license 一栏','archive':'C:/Users/ASUS/Downloads/BrutalismBuildings_blend_glb_textures.zip','processing':'仅按建筑分组并合并导出，保留现成模型和 UV'},ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
