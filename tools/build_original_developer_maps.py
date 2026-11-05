"""原创开发地图建筑：空场景建模，米制；不读取旧建筑模型或材质。"""
import bpy, bmesh, math, json, random
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/RobotSlice');OUT=ROOT/'SourceAssets/original_developer_maps';OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
COLORS={'Shell':(.48,.53,.57,1),'Dark':(.035,.055,.075,1),'Steel':(.19,.24,.29,1),'Blue':(.015,.16,.32,1),'Light':(.06,.55,1,1),'Glass':(.025,.08,.12,1),'Plaster':(.46,.40,.30,1),'Brick':(.21,.105,.06,1),'Tile':(.30,.32,.29,1),'Rust':(.20,.09,.045,1),'Road':(.07,.07,.065,1),'Concrete':(.27,.25,.21,1),'Sign':(1,1,1,1),'Leaf':(.11,.19,.045,1),'Dome':(.02,.25,.8,1)}
MATS=[]
for name,c in COLORS.items():
 m=bpy.data.materials.new(name);m.diffuse_color=c;MATS.append(m)
NAMES=list(COLORS);REPORT={'source':'原创程序建筑、手绘程序招牌；未读取旧建筑资产','meshes':{}}
class Mesh:
 def __init__(self):self.v=[];self.f=[];self.mi=[];self.uv=[]
 def face(self,pts,mat='Shell',uv=None):
  start=len(self.v);self.v.extend(pts);self.f.append(tuple(range(start,len(self.v))));self.mi.append(NAMES.index(mat));self.uv.append(uv)
 def box(self,c,s,mat='Shell'):
  x,y,z=c;a,b,d=[v*.5 for v in s];vs=[(x+dx*a,y+dy*b,z+dz*d) for dx,dy,dz in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
  for ids in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]:self.face([vs[i] for i in ids],mat)
 def cylinder(self,c,r,h,mat='Steel',n=32):
  x,y,z=c;lo=[(x+r*math.cos(i*math.tau/n),y+r*math.sin(i*math.tau/n),z-h/2) for i in range(n)];hi=[(a,b,z+h/2) for a,b,_ in lo]
  self.face(list(reversed(lo)),mat);self.face(hi,mat)
  for i in range(n):j=(i+1)%n;self.face([lo[i],lo[j],hi[j],hi[i]],mat)
 def tube(self,a,b,r,mat='Steel',n=8):
  a=Vector(a);b=Vector(b);axis=(b-a).normalized();t=axis.cross(Vector((0,0,1)))
  if t.length<.01:t=axis.cross(Vector((0,1,0)))
  t.normalize();v=axis.cross(t);lo=[a+r*(t*math.cos(i*math.tau/n)+v*math.sin(i*math.tau/n)) for i in range(n)];hi=[p+b-a for p in lo]
  self.face(list(reversed(lo)),mat);self.face(hi,mat)
  for i in range(n):j=(i+1)%n;self.face([lo[i],lo[j],hi[j],hi[i]],mat)
 def ring(self,r,z,width,height,mat='Steel',n=96):
  for i in range(n):
   a=i*math.tau/n;b=(i+1)*math.tau/n;vs=[(rr*math.cos(t),rr*math.sin(t),zz) for zz in (z-height/2,z+height/2) for rr,t in [(r-width/2,a),(r+width/2,a),(r+width/2,b),(r-width/2,b)]]
   for ids in [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]:self.face([vs[j] for j in ids],mat)
 def export(self,name,bevel=.035,collision=True):
  me=bpy.data.meshes.new(name);me.from_pydata(self.v,[],self.f);me.update();ob=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(ob)
  for m in MATS:me.materials.append(m)
  uv=me.uv_layers.new(name='UVMap')
  for p,mi,coords in zip(me.polygons,self.mi,self.uv):
   p.material_index=mi
   # 普通面使用按主轴投影的米制 UV，招牌覆盖为图集坐标。
   normal=p.normal;drop=max(range(3),key=lambda i:abs(normal[i]));axes=[i for i in range(3) if i!=drop]
   for k,li in enumerate(p.loop_indices):
    co=me.vertices[me.loops[li].vertex_index].co;uv.data[li].uv=coords[k] if coords else (co[axes[0]],co[axes[1]])
  bpy.context.view_layer.objects.active=ob;ob.select_set(True)
  if bevel:
   bm=bmesh.new();bm.from_mesh(me);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00001);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
   mod=ob.modifiers.new('边缘真实厚度','BEVEL');mod.width=bevel;mod.segments=2;bpy.ops.object.modifier_apply(modifier=mod.name)
   for p in me.polygons:p.use_smooth=True
   mod=ob.modifiers.new('加权法线','WEIGHTED_NORMAL');mod.keep_sharp=True;bpy.ops.object.modifier_apply(modifier=mod.name)
  bpy.ops.object.select_all(action='DESELECT');ob.select_set(True)
  bpy.ops.export_scene.fbx(filepath=str(OUT/(name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',bake_anim=False,axis_forward='-Z',axis_up='Y')
  REPORT['meshes'][name]={'triangles':sum(len(p.vertices)-2 for p in me.polygons),'collision':collision}
  ob.select_set(False)

# 月球基地：可穿行机库、带窗的生活舱、能源设施和成组散热板。
for variant in range(3):
 m=Mesh();w,d,h=[(64,42,20),(46,28,14),(26,20,11)][variant]
 m.box((0,0,-.45),(w+3,d+3,.9),'Dark')
 m.box((0,0,h-1.1),(w,d,2.2));m.box((0,d/2-1,h/2),(w,2,h))
 for side in (-1,1):
  m.box((side*(w/2-1),0,h/2),(2,d,h));m.box((side*(w/2+1.2),0,h*.35),(2.4,d*.72,h*.7),'Steel')
  # 门框之间留 18 米以上净空，机体能进入，内部上方有横梁。
  m.box((side*(w*.34),-d/2+1,h*.42),(w*.30,2,h*.84))
  for y in range(-int(d/2)+3,int(d/2),6):
   m.box((side*(w/2+1.1),y,h*.69),(2.8,.65,h*.6),'Shell')
   m.box((side*(w/2+1.52),y+1.5,h*.56),(.18,2.4,2.1),'Glass')
   m.box((side*(w/2+1.65),y+1.5,h*.52),(.12,2.35,.08),'Light')
  m.box((side*w*.32,-d/2-.16,h*.82),(w*.28,.3,.5),'Blue')
 for x in range(-int(w/2)+4,int(w/2),7):
  m.box((x,0,h+.15),(.7,d+1,.65),'Steel')
  m.box((x,-d/2-.2,h-1.2),(2.5,.25,.18),'Light')
 for side in (-1,1):
  m.box((side*w*.32,0,h+1),(w*.2,d*.35,1.5),'Dark')
  for j in range(10):m.box((side*w*.32,-d*.17+j*d*.034,h+1.8),(w*.19,.17,.12),'Steel')
 # 机库的后排检修道和舱内低矮掩体。
 for x in (-w*.29,w*.29):
  m.box((x,d*.25,2),(6,5,4),'Steel');m.box((x,d*.25,4.15),(6.2,5.2,.3))
 m.export('SM_OM_'+['Hangar','Habitat','Workshop'][variant],.08)
m=Mesh()
for x in (-5,5):
 m.cylinder((x,0,7),3.6,14,'Shell');m.cylinder((x,0,.6),4.25,1.2,'Dark');m.cylinder((x,0,14.4),3.1,.8,'Steel')
 for z in (2,6,10,13):m.cylinder((x,0,z),3.72,.28,'Steel')
 m.tube((x,0,15),(x,0,17),.35,'Steel');m.tube((x,0,17),(0,0,17),.35,'Steel')
m.box((0,0,.1),(19,13,.5),'Concrete');m.export('SM_OM_Tanks',.045)
m=Mesh();m.box((0,0,2),(12,7,4),'Shell')
for x in (-4,4):
 m.tube((x,0,4),(x,0,12),.32);m.box((x,0,12),(6,17,.38),'Dark')
 for y in range(-8,9):m.box((x,y,12.24),(5.6,.84,.08),'Blue')
m.export('SM_OM_Power',.035)
m=Mesh();m.box((0,0,2.5),(5,3,5),'Steel');m.box((0,-1.6,2.5),(4.3,.25,4.1),'Shell');m.box((0,-1.8,4),(3,.12,.25),'Light');m.export('SM_OM_Cargo',.08)
m=Mesh();m.cylinder((0,0,2),2.8,4,'Steel');m.tube((0,0,4),(0,0,24),.48)
for z in (8,14,20):
 m.tube((-4,0,z),(4,0,z),.18);m.tube((0,-3,z),(0,3,z),.18)
m.cylinder((0,0,24.3),.7,.6,'Light');m.export('SM_OM_Antenna',.03)

# 蓝色穹顶：半径 62 m，顶部 38 m，周边 9 m 高的可穿越膜，精确对齐 MoonAir。
m=Mesh();N=128;R=62;H=38;rows=32;edge=math.acos(9/H)
for j in range(rows):
 a=edge*j/rows;b=edge*(j+1)/rows
 for i in range(N):
  p=i*math.tau/N;q=(i+1)*math.tau/N
  pts=[(R*math.sin(t)*math.cos(s),R*math.sin(t)*math.sin(s),H*math.cos(t)) for t,s in [(a,p),(b,p),(b,q),(a,q)]]
  if j==0:pts=[pts[0],pts[1],pts[2]]
  m.face(pts,'Dome')
# Air 判定的半径一直到 62 m：小段平顶收边 + 竖直膜裙。
rr=R*math.sin(edge)
for i in range(N):
 p=i*math.tau/N;q=(i+1)*math.tau/N
 m.face([(rr*math.cos(p),rr*math.sin(p),9),(R*math.cos(p),R*math.sin(p),9),(R*math.cos(q),R*math.sin(q),9),(rr*math.cos(q),rr*math.sin(q),9)],'Dome')
 m.face([(R*math.cos(p),R*math.sin(p),9),(R*math.cos(p),R*math.sin(p),-1),(R*math.cos(q),R*math.sin(q),-1),(R*math.cos(q),R*math.sin(q),9)],'Dome')
m.export('SM_OM_Dome',0)
m=Mesh();m.ring(62,-.15,2.4,.3,'Steel');m.ring(62,.08,.3,.14,'Light')
# 发射器位于膜外，八个大间隙可自由通过；没有封闭的高墙。
for i in range(8):
 a=(i+.5)*math.tau/8;x,y=65*math.cos(a),65*math.sin(a)
 m.box((x,y,4),(4,4,8),'Dark');m.box((x,y,8),(5,5,.8),'Shell');m.cylinder((x,y,8.9),1.1,1,'Light',16)
m.export('SM_OM_DomeRing',.05)
m=Mesh();m.cylinder((0,0,-.3),60,.6,'Concrete',128);m.ring(50,.015,.3,.02,'Blue')
for x in (-24,24):
 for y in (-24,24):m.box((x,y,1.6),(9,5,3.2),'Shell');m.box((x,y,3.3),(9.4,5.4,.2),'Steel')
m.export('SM_OM_DomeInterior',.04)

# 千禧年街区：几种尺度贴合原图足印。招牌、窗框、雨棚、空调、阳台都是独立结构。
def sign(m,x,y,z,w,h,index,reverse=False):
 # 图集八行，Blender UV 下原点；UE 导入时会转换 V。
 v0=1-(index+1)/8+.008;v1=1-index/8-.008
 pts=[(x-w/2,y,z-h/2),(x+w/2,y,z-h/2),(x+w/2,y,z+h/2),(x-w/2,y,z+h/2)]
 uv=[(.01,v0),(.99,v0),(.99,v1),(.01,v1)]
 if reverse:pts.reverse();uv.reverse()
 m.face(pts,'Sign',uv)
dimensions={'A':(14.5,12.4,18),'B':(9.7,6.0,16.7),'D':(14.9,14.3,14.2),'E':(11.1,13.2,10.5),'F':(25.1,21.8,7.1)}
for k,(w,d,h) in dimensions.items():
 r=random.Random(ord(k));m=Mesh();body='Brick' if k=='B' else 'Plaster'
 m.box((0,0,h/2),(w,d,h),body);m.box((0,0,.25),(w+.2,d+.2,.5),'Concrete')
 for z in [3.6]+list(range(7,int(h),3)):
  m.box((0,0,z),(w+.22,d+.22,.18),'Concrete')
 m.box((0,0,h+.12),(w+.65,d+.65,.24),'Concrete')
 for x in (-w/2,w/2):m.box((x,0,h+.65),(.2,d,1.1),'Brick')
 for y in (-d/2,d/2):m.box((0,y,h+.65),(w,.2,1.1),'Brick')
 # 两侧临街立面，避免建筑转向后出现空白背面。
 for side in (-1,1):
  front=side*(d/2+.06);cols=max(2,int(w/3));step=w/cols
  for col in range(cols):
   x=-w/2+step*(col+.5);ww=step*.72
   m.box((x,front,1.55),(ww,.12,2.8),'Dark')
   if (col+ord(k))%3==0:
    for j in range(18):m.box((x,front+side*.1,.22+j*.15),(ww,.10,.085),'Steel')
   else:
    m.box((x,front+side*.08,1.55),(ww-.16,.08,2.6),'Glass')
    m.box((x,front+side*.18,1.5),(.055,.06,2.8),'Steel')
   for z in [5.1+j*3 for j in range(max(0,int((h-4)/3)))]:
    m.box((x,front,z),(1.65,.18,1.95),'Dark');m.box((x,front+side*.11,z),(1.45,.08,1.75),'Glass')
    for dx in (-.77,0,.77):m.box((x+dx,front+side*.19,z),(.07,.12,1.9),'Concrete')
    for dz in (-.92,.23,.92):m.box((x,front+side*.19,z+dz),(1.64,.12,.06),'Concrete')
    m.box((x,front+side*.31,z-1),(1.9,.60,.12),'Concrete')
    if (col+int(z))%3==0:
     m.box((x+1.05,front+side*.4,z-.5),(.72,.6,.53),'Tile')
     for j in range(6):m.box((x+.8+j*.08,front+side*.72,z-.5),(.024,.025,.4),'Dark')
    if k=='A' and col%2==0:
     m.box((x,front+side*.9,z-1.1),(2.4,1.8,.18),'Concrete')
     for j in range(8):m.tube((x-1.1+j*.31,front+side*1.7,z-1),(x-1.1+j*.31,front+side*1.7,z-.1),.025,'Rust',6)
     m.tube((x-1.15,front+side*1.7,z-.1),(x+1.15,front+side*1.7,z-.1),.04,'Rust')
  # 凹进的招牌边框，单块宽招牌与小招牌混用，避免统一商业街。
  idx=(ord(k)-65+(0 if side==-1 else 3))%8
  m.box((0,front+side*.23,3.2),(w-.4,.32,.96),'Rust');sign(m,0,front+side*.41,3.2,w-.65,.78,idx,side==1)
  if k in ('B','E'):
   m.box((0,front+side*.95,2.6),(w*.85,1.8,.13),'Blue' if k=='E' else 'Rust')
  m.tube((-w/2+.3,front+side*.25,.3),(-w/2+.3,front+side*.25,h),.07,'Rust')
 # 侧墙百叶窗与煤气管，屋顶水箱、杂物和天线。
 for side in (-1,1):
  for yy in (-d*.25,d*.25):
   for z in range(5,int(h),3):
    m.box((side*(w/2+.035),yy,z),(.10,1.4,1.7),'Dark')
    for j in range(6):m.box((side*(w/2+.10),yy-.6+j*.24,z),(.10,.04,1.65),'Steel')
 m.cylinder((w*.27,d*.18,h+1),.85,1.7,'Steel',20)
 m.tube((-w*.27,0,h),(-w*.27,0,h+3),.035,'Rust',6)
 for j in range(4):m.tube((-w*.27-.6,0,h+1.8+j*.3),(-w*.27+.6,0,h+1.8+j*.3),.025,'Steel',6)
 m.export('SM_OS2_Building'+k,.025)
m=Mesh();m.box((0,0,-.3),(100,110,.6),'Road')
# 步道不跨过十字路口，左右两块和周期端点对齐。
for x,w in [(-28,44),(28,44)]:
 for side in (-1,1):
  m.box((x,side*24,.08),(w,34,.16),'Concrete')
  for xx in range(int(x-w/2),int(x+w/2),2):m.box((xx+.98,side*7.1,.13),(1.94,.26,.26),'Tile')
# 道路旧黄线和路口斑马线均为原创几何。
for x in range(-48,50,6):
 if abs(x)>8:m.box((x,0,.012),(2.8,.09,.015),'Tile')
for side in (-1,1):
 for yy in range(-5,6,2):m.box((side*6.4,yy,.014),(2.2,.64,.02),'Tile')
m.export('SM_OS2_Ground',0)
m=Mesh()
for x in (-32,25):
 for side in (-1,1):
  y=side*6.25;m.cylinder((x,y,3.8),.16,7.6,'Concrete',12);m.tube((x,y,6.8),(x,y-side*1.8,7.2),.06,'Rust');m.box((x,y-side*1.8,7.15),(.55,1.3,.14),'Tile')
  # 多根下垂架空线，跨周期端点前保持相同高度。
for y in (-6.3,6.3):
 for lane in range(3):
  points=[(-50+i*2,y+lane*.17,6.8-1.0*math.sin(math.pi*i/50)**2) for i in range(51)]
  for a,b in zip(points,points[1:]):m.tube(a,b,.018,'Dark',5)
for x,y in [(-19,6.3),(36,-6.2),(-8,17),(8,-27)]:
 m.box((x,y,.5),(.75,.65,1),'Steel');m.box((x,y,1.03),(.82,.7,.08),'Rust')
m.export('SM_OS2_StreetFurniture',.015,False)
# 原创小树：分叉树干和真实叶片簇，无外部透明树卡。
m=Mesh();r=random.Random(401)
m.tube((0,0,0),(0,0,4.6),.17,'Rust',10)
for i in range(14):
 a=r.random()*math.tau;rr=r.uniform(.4,2.1);end=(math.cos(a)*rr,math.sin(a)*rr,r.uniform(3.5,6.2));m.tube((0,0,2.6),end,.045,'Rust')
 for j in range(90):
  cx,cy,cz=(end[0]+r.gauss(0,.55),end[1]+r.gauss(0,.55),end[2]+r.gauss(0,.4));aa=r.random()*math.tau;v=Vector((math.cos(aa),math.sin(aa),r.uniform(-.2,.5)))*.18;t=Vector((-math.sin(aa),math.cos(aa),0))*.055;c=Vector((cx,cy,cz));m.face([c-v,c+t,c+v,c-t],'Leaf')
m.export('SM_OS2_Tree',0,False)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'OriginalDeveloperMaps.blend'))
(OUT/'manifest.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print('ORIGINAL_MAP_MODELS',json.dumps(REPORT,ensure_ascii=False))
