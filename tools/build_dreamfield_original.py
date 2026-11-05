"""梦核旷野原创替换件：空场景起建，不读取旧墙、行星、植被或纹理。
Blender 后台运行。尺寸为米，导出 UE 时 Y 翻转；平台／出生点沿用开发图。
"""
import bpy, math, random, json, sys
from pathlib import Path
from mathutils import Vector
sys.path.insert(0, str(Path(__file__).parent))
import dreamfield_terrain as terrain

OUT=Path('D:/RobotSlice/SourceAssets/dreamfield_original')
OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
mats=[]
for name,color in [('Porcelain',(.55,.59,.60,1)),('Recess',(.055,.075,.10,1)),('Titanium',(.25,.31,.34,1)),('Grass',(.22,.32,.055,1)),('Petal',(.78,.77,.66,1)),('Pollen',(.55,.32,.04,1))]:
 m=bpy.data.materials.new(name);m.diffuse_color=color;mats.append(m)
report={'source':'原创程序建模，无外部模型或纹理输入','meshes':{}}

class Mesh:
 def __init__(self): self.v=[];self.f=[];self.mi=[]
 def face(self,pts,mat=0):
  start=len(self.v);self.v.extend(pts);self.f.append(tuple(range(start,len(self.v))));self.mi.append(mat)
 def box(self,c,s,mat=0):
  x,y,z=c;a,b,d=[t*.5 for t in s]
  vs=[(x+dx*a,y+dy*b,z+dz*d) for dx,dy,dz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
  for ids in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]: self.face([vs[i] for i in ids],mat)
 def profile(self,y,width,points,mat=0):
  # 厚截面沿墙长度方向拉伸：前缘折面有真正厚度，不用贴片堆砌。
  self.face([(x,y-width/2,z) for x,z in reversed(points)],mat)
  self.face([(x,y+width/2,z) for x,z in points],mat)
  for i,(x,z) in enumerate(points):
   x2,z2=points[(i+1)%len(points)]
   self.face([(x,y-width/2,z),(x2,y-width/2,z2),(x2,y+width/2,z2),(x,y+width/2,z)],mat)
 def finish(self,name,bevel=0,smooth=False):
  me=bpy.data.meshes.new(name);me.from_pydata(self.v,[],self.f);me.update()
  ob=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(ob)
  for m in mats: me.materials.append(m)
  for p,idx in zip(me.polygons,self.mi): p.material_index=idx;p.use_smooth=smooth
  if name.startswith('SM_DO_Grass') or name=='SM_DO_Daisies':
   uv=me.uv_layers.new(name='UVMap')
   for loop in me.loops:uv.data[loop.index].uv=(0.5,max(0,min(1,me.vertices[loop.vertex_index].co.z/.4)))
   # 草叶用朝上的柔和法线；低画质没有间接光时也不变成黑色铁丝。
   me.normals_split_custom_set([(0,0,1)]*len(me.loops))
  bpy.context.view_layer.objects.active=ob;ob.select_set(True)
  # 重新计算闭合部件法线，避免截面绕序引起背面翻转。
  if bevel:
   import bmesh
   bm=bmesh.new();bm.from_mesh(me);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
   mod=ob.modifiers.new('真实倒角','BEVEL');mod.width=bevel;mod.segments=2
   bpy.ops.object.modifier_apply(modifier=mod.name)
   for p in me.polygons:p.use_smooth=True
   mod=ob.modifiers.new('加权法线','WEIGHTED_NORMAL');mod.keep_sharp=True
   bpy.ops.object.modifier_apply(modifier=mod.name)
  bpy.ops.object.select_all(action='DESELECT');ob.select_set(True)
  bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',add_leaf_bones=False,bake_anim=False,axis_forward='-Z',axis_up='Y')
  report['meshes'][name]={'triangles':sum(len(p.vertices)-2 for p in me.polygons),'vertices':len(me.vertices),'materials':[m.name for m in me.materials]}
  ob.select_set(False)
  return ob

# 第二轮：粗粝巨构的主节奏来自不等宽、不等高的悬垂体和深槽。
# 上下两层错开，凹处露出更细的结构，避免第一轮等宽柱列像栅栏。
for variant in range(4):
 r=random.Random(8310+variant);m=Mesh();m.box((19,0,96),(12,60,208),1)
 for layer in range(2):
  cursor=-29.8
  while cursor<28:
   width=min(r.uniform(8,17),29.8-cursor);y=cursor+width/2
   top=r.uniform(187,195) if layer==0 else r.uniform(91,126)
   foot=r.uniform(48,99) if layer==0 else r.uniform(-12,10)
   front=r.uniform(-14,-6) if layer==0 else r.uniform(-9,1)
   # 贯穿数十米的厚立面，底部斜收；根部保持宽厚，悬垂末端不整齐。
   prof=[(14,foot-3),(front+5,foot),(front,foot+8),(front,top-12),(front-2,top-5),(14,top)]
   m.profile(y,width,prof,0)
   # 大块分段与纵向侧肋先定结构，随后才在局部放格栅、检修盖。
   for sign in (-1,1):
    m.box((front-.75,y+sign*(width/2-.65),(top+foot)/2),(1.5,.9,top-foot-8),2)
   z=foot+14
   while z<top-10:
    zh=min(r.uniform(10,24),top-9-z);pw=width*r.uniform(.48,.82)
    offset=r.uniform(-.09,.09)*width;face=front-r.uniform(.3,2.3)
    m.box((face+.7,y+offset,z+zh/2),(1.4,pw,zh-.8),0)
    # 每个主板块有不同的边框与收口，分块细节不会贯穿成整齐横线。
    m.box((face-.15,y+offset,z+.55),(.5,pw+1,.7),2)
    if r.random()<.7:
     gy=y+offset;gz=z+zh*.65;gw=pw*.68;gh=min(zh*.32,5.2)
     m.box((face-.12,gy,gz),(.25,gw,gh),1)
     for j in range(int(gh/.42)):
      m.box((face-.38,gy,gz-gh/2+.21+j*.42),(.6,gw,.17),2)
    for side in (-1,1):
     m.box((face-.05,y+offset+side*(pw/2-.32),z+zh*.46),(.2,.12,zh*.6),1)
    z+=zh+r.uniform(.4,1.5)
   gap=r.uniform(1.6,4.4);gy=cursor+width+gap/2
   if gy<29:
    # 深槽中的立管与接驳套筒，层层后退，形成像悬崖裂隙的暗部。
    for j in range(3):
     xx=front+5+j*2;yy=gy+(j-1)*.48
     m.box((xx,yy,(foot+top)/2),(.7,.45,top-foot+4),2)
     for zz in range(int(foot)+4,int(top),9):m.box((xx-.1,yy,zz),(1.05,.8,.55),0)
   cursor+=width+gap
 # 厚顶檐带不等宽的下挂檐块，整体边线连续，近看有层叠断面。
 m.profile(0,60,[(-17,196),(-10,189),(24,189),(24,200),(-17,200)],0)
 y=-30
 while y<29:
  w=min(r.uniform(4,10),30-y);h=r.uniform(1,4)
  m.box((-13.5,y+w/2,193-h/2),(7,w-.3,h),2)
  m.box((2,y+w/2,200.2),(23,w-.35,.4),0)
  y+=w
 m.finish(f'SM_DO_Wall{variant}',.065)

m=Mesh();m.profile(0,4.8,[(1,0),(-1.8,0),(-1.8,15),(-3.8,19),(1,22)],0)
for z in (4,9,14):m.box((-2.05,0,z),(.6,5.2,.6),2)
m.finish('SM_DO_Buttress',.09)

# 窄弯草叶实几何，无透明草片；叶根埋入地面，叶尖渐细，三种固定种子。
for variant in range(3):
 r=random.Random(6100+variant);m=Mesh()
 for j in range(52):
  angle=r.uniform(0,math.tau);rad=math.sqrt(r.random())*.87
  x,y=math.cos(angle)*rad,math.sin(angle)*rad
  yaw=r.uniform(0,math.tau);w=r.uniform(.022,.044);h=r.uniform(.24,.56)
  bend=r.uniform(.06,.22);side=Vector((math.cos(yaw),math.sin(yaw),0));lean=Vector((-math.sin(yaw),math.cos(yaw),0))
  pts=[]
  for t in (0,.45,.78):
   center=Vector((x,y,-.025+h*t))+lean*bend*t*t
   pts.extend([tuple(center-side*w*(1-t)*.5),tuple(center+side*w*(1-t)*.5)])
  pts.append(tuple(Vector((x,y,h))+lean*bend))
  for ids in [(0,1,3,2),(2,3,5,4),(4,5,6)]:m.face([pts[q] for q in ids],3)
 m.finish(f'SM_DO_Grass{variant}')

m=Mesh();r=random.Random(7103)
for j in range(3):
 x,y=r.uniform(-.35,.35),r.uniform(-.35,.35);h=r.uniform(.20,.34)
 m.box((x,y,h*.5),(.012,.012,h),3)
 for k in range(7):
  a=k*math.tau/7;dx,dy=math.cos(a),math.sin(a);sx,sy=-dy,dx
  m.face([(x+dx*.015,y+dy*.015,h),(x+dx*.065+sx*.025,y+dy*.065+sy*.025,h+.009),(x+dx*.10,y+dy*.10,h-.012),(x+dx*.065-sx*.025,y+dy*.065-sy*.025,h+.009)],4)
 m.box((x,y,h+.008),(.035,.035,.014),5)
m.finish('SM_DO_Daisies')

# 地形保留战斗高度函数，宽丘和平台出口的位置不变；去掉旧素材的地面贴图依赖。
for name,x0,x1,y0,y1,step,hole in [('SM_DO_GroundNear',-475,350,-625,625,5,None),('SM_DO_GroundFar',-6000,400,-4500,4500,25,(-475,350,-625,625))]:
 m=Mesh();nx=round((x1-x0)/step)+1;ny=round((y1-y0)/step)+1
 for ix in range(nx):
  x=x0+ix*step
  for iy in range(ny):
   y=y0+iy*step;z=terrain.height(x,y)
   if hole and hole[0]<x<hole[1] and hole[2]<y<hole[3]:z-=.3
   m.v.append((x,-y,z))
 for ix in range(nx-1):
  for iy in range(ny-1):
   x=x0+(ix+.5)*step;y=y0+(iy+.5)*step
   if hole and hole[0]+step<x<hole[1]-step and hole[2]+step<y<hole[3]-step:continue
   a=ix*ny+iy;m.f.append((a,a+1,a+ny+1,a+ny));m.mi.append(3)
 m.finish(name,smooth=True)

bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'DreamfieldOriginal.blend'))
(OUT/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('[dreamoriginal] 完成',json.dumps(report,ensure_ascii=False))
