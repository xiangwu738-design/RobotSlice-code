"""HX-07 original hand-held arsenal. Blender, metres internally; design inputs in cm.
Run Blender -b --factory-startup -P tools/build_scifi_weapons.py -- [name|all] [--render]
Exports editable .blend, FBX, and geometry/attachment manifest. Original assets are untouched.
"""
import bpy, math, json, sys
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/RobotSlice')
OUT=ROOT/'SourceAssets/scifi_weapons'
OUT.mkdir(parents=True,exist_ok=True)
ARGS=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['all']
ONLY=ARGS[0] if ARGS and not ARGS[0].startswith('--') else 'all'
RENDER='--render' in ARGS
PARTS=[]
MATS={}
PALETTES={
 'VHI':dict(Paint='#414c48',Secondary='#20292b',Accent='#ac3f35',Glow='#ff5646'),
 'PHASE':dict(Paint='#929ea3',Secondary='#303e49',Accent='#387d89',Glow='#69edee'),
 'NEC':dict(Paint='#707c83',Secondary='#30383e',Accent='#d18b34',Glow='#f6b34e'),
}
COMMON=dict(Frame=('#222a30',.8,.33),Rubber=('#171c20',.05,.73),Steel=('#8d9ba5',.92,.25),Edge=('#b2bec5',.96,.21),Black=('#090e13',.35,.38),Mark=('#c1cbce',.3,.48),Lens=('#15495b',.78,.14))

def lin(h):
 vals=[int(h.strip('#')[i:i+2],16)/255 for i in (0,2,4)]
 return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in vals)

def material(name,color,metal,rough,glow=0):
 m=bpy.data.materials.new(name); m.use_nodes=True
 b=m.node_tree.nodes.get('Principled BSDF')
 c=lin(color); b.inputs['Base Color'].default_value=(*c,1)
 b.inputs['Metallic'].default_value=metal; b.inputs['Roughness'].default_value=rough
 if glow:
  b.inputs['Emission Color'].default_value=(*c,1); b.inputs['Emission Strength'].default_value=glow
 else:
  noise=m.node_tree.nodes.new('ShaderNodeTexNoise'); noise.inputs['Scale'].default_value=260; noise.inputs['Detail'].default_value=2
  ramp=m.node_tree.nodes.new('ShaderNodeValToRGB')
  ramp.color_ramp.elements[0].color=(rough*.9,rough*.9,rough*.9,1)
  ramp.color_ramp.elements[1].color=(min(.95,rough*1.1),)*3+(1,)
  m.node_tree.links.new(noise.outputs['Fac'],ramp.inputs[0]); m.node_tree.links.new(ramp.outputs[0],b.inputs['Roughness'])
 m.diffuse_color=(*c,1)
 return m

def setup(maker):
 global PARTS,MATS
 bpy.ops.wm.read_factory_settings(use_empty=True); PARTS=[]; MATS={}
 for n,(c,me,ro) in COMMON.items():MATS[n]=material('SF_'+n,c,me,ro)
 for n,c in PALETTES[maker].items():
  MATS[n]=material('SF_'+maker+'_'+n,c,.48 if n!='Glow' else .1,.36,1.6 if n=='Glow' else 0)

def finish(o,name,mat='Paint',bevel=0):
 o.name=name; o.data.materials.clear(); o.data.materials.append(MATS[mat]); PARTS.append(o)
 if bevel:
  mod=o.modifiers.new('Machined edge radii','BEVEL');mod.width=bevel/100;mod.segments=3
  mod.affect='EDGES'
  bpy.context.view_layer.objects.active=o
  bpy.ops.object.modifier_apply(modifier=mod.name)
 if o.type=='MESH':
  for p in o.data.polygons:p.use_smooth=True
  w=o.modifiers.new('Weighted surface normals','WEIGHTED_NORMAL');w.keep_sharp=True;w.weight=35
 return o

def box(name,loc,size,mat='Paint',b=.18,rot=None):
 bpy.ops.mesh.primitive_cube_add(size=1,location=Vector(loc)/100)
 o=bpy.context.object;o.scale=Vector(size)/100
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if rot:o.rotation_euler=tuple(math.radians(x) for x in rot)
 return finish(o,name,mat,b)

def cylinder(name,loc,r,length,mat='Frame',axis='X',verts=32,b=.08):
 bpy.ops.mesh.primitive_cylinder_add(vertices=verts,radius=r/100,depth=length/100,location=Vector(loc)/100)
 o=bpy.context.object
 o.rotation_euler=(0,math.pi/2,0) if axis=='X' else ((math.pi/2,0,0) if axis=='Y' else (0,0,0))
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 return finish(o,name,mat,b)

def profile(name,points,width,mat='Paint',y=0,b=.2):
 # Polygon in XZ, extruded in Y; recalculate normals after constructing.
 n=len(points); vs=[(x/100,(y+s*width/2)/100,z/100) for s in (-1,1) for x,z in points]
 fs=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
 me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update()
 o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o)
 bpy.context.view_layer.objects.active=o;o.select_set(True)
 return finish(o,name,mat,b)

def bar(name,a,b,width,depth,mat='Frame',bevel=.12):
 d=Vector(b)-Vector(a);o=box(name,(Vector(a)+Vector(b))/2,(width,depth,d.length),mat,bevel)
 o.rotation_euler=d.to_track_quat('Z','Y').to_euler();return o

def ring(name,x,z,outer,inner,length,mat='Frame',y=0,axis='X',n=40):
 vs=[]
 for a,r in [(-length/2,outer),(length/2,outer),(-length/2,inner),(length/2,inner)]:
  for j in range(n):
   t=j*2*math.pi/n; v=(x+a,y+r*math.cos(t),z+r*math.sin(t)) if axis=='X' else (x+r*math.cos(t),y+a,z+r*math.sin(t))
   vs.append(tuple(q/100 for q in v))
 fs=[]
 for j in range(n):
  k=(j+1)%n
  fs.extend([(j,k,n+k,n+j),(2*n+j,3*n+j,3*n+k,2*n+k),(j,2*n+j,2*n+k,k),(n+j,n+k,3*n+k,3*n+j)])
 me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.update()
 o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);return finish(o,name,mat,.035)

def screws(points,y,r=.45):
 for i,(x,z) in enumerate(points):
  for side in (-1,1):
   cylinder('Recessed fastener', (x,side*y,z),r,.16,'Steel','Y',16,.035)
   box('Fastener slot',(x,side*(y+.095),z),(r*1.2,.035,.10),'Black',.015)

def label(text,x,y,z,size=1.3):
 bpy.ops.object.text_add(location=(x/100,y/100,z/100))
 o=bpy.context.object;o.data.body=text;o.data.size=size/100;o.data.extrude=.00012
 o.data.space_character=1.12;o.rotation_euler=(math.pi/2,0,0)
 o.data.materials.append(MATS['Mark']);bpy.ops.object.convert(target='MESH');PARTS.append(o);o.name='Laser marking '+text

def vents(x0,z,y,n=6,step=2.2,h=3):
 for side in (-1,1):
  for i in range(n):
   box('Inset thermal slot',(x0+i*step,side*y,z),(.75,.18,h),'Black',.14)
   box('Fin lip',(x0+i*step+.18,side*(y+.10),z),(.16,.12,h*.8),'Frame',.04)

def grip(center,up,half,along):
 c=Vector(center);u=Vector(up).normalized()
 length=along[1]-along[0];mid=c+u*((along[0]+along[1])/2)
 o=box('Ergonomic grip',mid,(half[0]*2,half[1]*2,length),'Rubber',min(half[2],.65))
 o.rotation_euler=u.to_track_quat('Z','Y').to_euler()
 # Subtle ribs keep the grasp envelope, using the same angled frame as the grip.
 for i in range(7):
  p=c+u*(along[0]+1+(length-2)*i/6)
  o=box('Grip inset band',p,(half[0]*2+.04,half[1]*2+.04,.16),'Secondary',.055)
  o.rotation_euler=u.to_track_quat('Z','Y').to_euler()

GUNS=[
 dict(key='Rifle',old='SM_Gun3',maker='VHI',code='VHI-R36',name='LINE / 36',muzzle=[77,0,17],c=[-1.8,0,.7],up=[.2,0,.98],half=[4.4,2.2,1.6],along=[-6.2,7],fore=[30,0,11.5],fh=[1.6,2.5]),
 dict(key='SMG',old='SM_Gun6',maker='VHI',code='VHI-C22',name='SWEEP / 22',muzzle=[62,0,8],c=[.64,0,2.3],up=[.5,0,.86],half=[1.9,1.6,1.2],along=[-3.5,4.5],fore=[22,0,4.2],fh=[2.2,1.9]),
 dict(key='Shotgun',old='SM_Gun11',maker='NEC',code='NEC-12',name='BREACH / 12',muzzle=[77,0,7],c=[-6.45,0,2],up=[.91,0,.41],half=[3.2,3.4,2.2],along=[-12,6],fore=[34,0,7.5],fh=[3.3,2.5]),
 dict(key='Marksman',old='SM_Gun2',maker='PHASE',code='PHASE-L1',name='NEEDLE / L1',muzzle=[87,0,11],c=[-8,0,2.6],up=[-.5,0,.87],half=[1.6,2.1,1.2],along=[-3.5,3.5],fore=[40,0,-.3],fh=[2.7,3]),
 dict(key='Sniper',old='SM_Gun9',maker='PHASE',code='PHASE-S3',name='DATUM / S3',muzzle=[102,0,9],c=[1.1,0,1.45],up=[.2,0,.98],half=[2.1,1.5,1],along=[-3,3.5],fore=[44,0,4.1],fh=[2.3,3]),
 dict(key='DualPistol',old='SM_Pistol7',maker='NEC',code='NEC-09',name='PAIR / 09',muzzle=[32,0,14],c=[-.6,0,-2.5],up=[.2,0,.98],half=[3,2.1,1.5],along=[-4,5.5],fore=None),
]
BLADES=[
 dict(key='Katana',old='SM_Sword07',maker='PHASE',code='PHASE-K1',tip=115,r=1.5,gr=[-9,9.5],z=0,w=6),
 dict(key='QuickBlade',old='SM_Sword11',maker='VHI',code='VHI-B08',tip=110,r=1.5,gr=[-12,7],z=-1.84,w=6),
 dict(key='OneHand',old='SM_Sword13',maker='PHASE',code='PHASE-K3',tip=100,r=1.6,gr=[-15,3],z=.4,w=5),
 dict(key='GreatSword',old='SM_Sword14',maker='NEC',code='NEC-40',tip=145,r=2.6,gr=[-22.5,10],z=.9,w=12),
 dict(key='TwinBlades',old='SM_Sword16',maker='VHI',code='VHI-B14',tip=90,r=2,gr=[-17,15],z=.42,w=3),
 dict(key='Blunt',old='SM_Sword20',maker='NEC',code='NEC-25',tip=135,r=2,gr=[-34,5.5],z=.93,w=10),
 dict(key='Dagger',old='SM_Sword03',maker='VHI',code='VHI-D06',tip=45,r=.9,gr=[-7.5,3],z=.4,w=4),
]

def gun(d):
 k=d['key'];mx,_,z=d['muzzle'];maker=d['maker']
 grip(d['c'],d['up'],d['half'],d['along'])
 if d.get('fore'):
  f=d['fore'];box('Support-hand guard',f,(15,d['fh'][0]*2,d['fh'][1]*2),'Rubber',.55)
  for j in range(5):box('Foreguard traction',(f[0]-5+j*2.5,0,f[2]-d['fh'][1]),(.45,d['fh'][0]*2-.25,.18),'Secondary',.04)
 if k=='Rifle':
  profile('Upper receiver',[(-13,11),(14,10),(28,14),(27,23),(-12,24),(-17,19)],9.6)
  profile('Stock upper spar',[(-43,18),(-13,16),(-13,23),(-41,25)],6,'Secondary')
  bar('Stock lower brace',(-41,0,4),(-15,0,15),2.5,5,'Frame')
  box('Butt pad',(-44,0,13),(3,7,24),'Rubber',1)
  profile('Magazine well',[(10,11),(20,11),(18,-7),(11,-9),(7,-5)],7,'Frame')
  box('Magazine base',(13,0,-8),(11,7.7,3),'Accent',.4)
  # Open side radiator assembly between the receiver and front collar.
  cylinder('Barrel', (49,0,z),2.4,49,'Steel')
  for side in (-1,1):
   box('Floating upper handguard',(48,side*4.3,22),(42,1.1,3.8),'Paint',.45)
   box('Lower handguard spine',(50,side*4.3,15),(38,1.1,2),'Secondary',.3)
   for i in range(7):box('Thermal bridge',(34+i*5,side*4.3,18.4),(1.2,1.3,5.5),'Frame',.2,rot=(0,15,0))
  box('Front armor collar',(69,0,z),(7,10.6,11),'Secondary',.6)
  vents(-10,19,4.9,6,2.2,3)
  box('Weapon ID stripe',(12,-4.9,20),(11,.25,1.2),'Accent',.1)
  label(d['code'],-10,-5.05,13,1.3)
 elif k=='SMG':
  profile('Compact receiver',[(-15,8),(7,8),(15,12),(36,12),(38,20),(-12,20),(-18,16)],7.2)
  profile('Rear energy cassette',[(-19,5),(-8,4),(-9,16),(-19,18)],7.8,'Secondary')
  box('Rear cassette latch',(-19,-4.1,13),(3,.6,6),'Accent',.3)
  cylinder('Heat jacket',(44,0,z),3.3,28,'Frame')
  for x in range(36,56,3):ring('Exposed cooling annulus',x,z,3.8,2.9,.7,'Steel',n=24)
  box('Barrel shield',(46,0,12.8),(24,6,2.7),'Paint',.45)
  bar('Stock spine',(-14,0,14),(-24,0,12),2.3,4,'Frame')
  box('Compact butt',(-24,0,8),(2.5,5.8,14),'Rubber',.65)
  box('Front thermal collar',(58,0,z),(4,8.5,8.5),'Secondary',.5)
  vents(9,16,3.7,9,2.8,3)
  label(d['code'],-12,-3.8,9.8,1.15)
 elif k=='Shotgun':
  profile('Breech block',[(3,2),(24,2),(30,6),(28,14),(3,14)],9.4)
  profile('Angled stock',[(-42,-10),(-38,-12),(-12,0),(-10,5),(-40,1)],6.4,'Secondary')
  box('Impact butt',(-42,0,-5),(3,7.4,15),'Rubber',.7)
  # Massive central bore; feed cylinder under forward receiver.
  cylinder('Main bore',(53,0,z),2.8,46,'Steel')
  cylinder('Tubular cell feed',(53,0,1.3),2.3,33,'Frame')
  for side in (-1,1):
   profile('Separated heat shield',[(30,10),(73,10),(75,13),(69,15),(31,15)],1.2,'Paint',y=side*4.2)
   for x in (40,48,56,64):box('Shield slot',(x,side*4.85,12.4),(4,.18,1.1),'Black',.25)
  box('Breech power indicator',(15,-4.85,8),(12,.3,2.7),'Secondary',.35)
  for x in (11,14,17,20):box('Charge segment',(x,-5.05,8),(1.1,.08,1.5),'Accent',.13)
  ring('Breech lock',8,8,3.3,2.25,.5,'Steel',y=-5,axis='Y',n=32)
  label(d['code'],12,-4.86,11.4,1)
  # Open trigger loop, positioned below the legacy angled grip.
  for a,b in [((-14,0,-5),(0,0,-9)),((0,0,-9),(5,0,1))]:bar('Trigger-loop frame',a,b,.7,.8,'Frame')
 elif k=='Marksman':
  profile('Split induction receiver',[(-15,9),(5,5),(25,5),(30,12),(24,22),(-12,22)],9.4)
  profile('Floating cheek stock',[(-31,15),(-14,13),(-14,20),(-32,23)],5.8,'Secondary')
  bar('Rear return strut',(-30,0,14),(-21,0,-8),2.3,3.4,'Frame')
  box('Rear actuator',(-24,0,-7),(12,5,5),'Secondary',.6)
  # Two open rails with inset luminous conductors; central bore stays on the original muzzle axis.
  for side in (-1,1):
   profile('Induction rail',[(25,7),(79,7),(85,10),(80,15),(25,15)],2.1,'Secondary',y=side*3.7,b=.35)
   box('Ceramic rail armor',(51,side*5,13),(38,.9,3.5),'Paint',.3)
   box('Recessed conductor',(53,side*2.6,11),(49,.22,.65),'Glow',.08)
   for x in (30,43,56,69):box('Rail clamp',(x,side*3.7,11),(2,3.6,9),'Frame',.25)
  cylinder('Beam aperture',(84,0,z),1.2,4,'Lens')
  box('Power cell spine',(15,0,1),(20,7,4),'Secondary',.5)
  ring('Inductor side socket',12,13,4,2.9,.7,'Frame',y=-5,axis='Y')
  cylinder('Inductor glass',(12,-5.35,13),2.6,.16,'Lens','Y')
  ring('Inductor status',12,13,2.7,2.4,.1,'Glow',y=-5.46,axis='Y')
  label(d['code'],-10,-4.9,18.5,1.25)
 elif k=='Sniper':
  profile('Monocoque receiver',[(-9,6),(31,5),(39,10),(30,17),(-10,18),(-17,13)],8)
  profile('Stock comb',[(-39,12),(-13,10),(-12,17),(-39,20)],5.6,'Paint')
  bar('Stock bottom spar',(-38,0,-1),(-13,0,9),1.9,3.7,'Frame')
  box('Butt plate',(-40,0,9),(2.7,6.4,22),'Rubber',.8)
  cylinder('Precision barrel',(71,0,z),1.9,61,'Steel')
  for side in (-1,1):
   profile('Tapered barrel shroud',[(38,10),(92,10),(95,13),(79,15),(39,15)],1.2,'Paint',y=side*3.4,b=.3)
   box('Underslung rail',(65,side*3.4,6),(49,1.2,1.6),'Frame',.2)
  for x in (54,67,80):ring('Barrel isolator',x,z,2.8,2.0,1,'Secondary')
  box('Muzzle housing',(97,0,z),(7,6.4,6.4),'Secondary',.6)
  box('Scope mount',(7,0,19),(23,3,2.5),'Frame',.3)
  cylinder('Optical housing',(8,0,23),3.5,29,'Secondary')
  ring('Optic front bezel',23,23,3.7,2.8,2,'Paint')
  cylinder('Front optic lens',(23.5,0,23),2.75,.2,'Lens')
  ring('Optic rear bezel',-7,23,2.9,2.25,1.8,'Frame')
  box('Range finder',(10,-3.8,22),(10,2.5,3.5),'Paint',.4)
  label(d['code'],-7,-4.15,11.8,1.15)
  box('Calibration stripe',(26,-4.15,10),(5,.2,.6),'Glow',.08)
 else:
  profile('Pistol slide',[(-9,8),(24,8),(31,11),(30,17),(-8,18),(-11,14)],6.2)
  profile('Lower frame',[(1,4),(23,5),(25,10),(0,10)],5.2,'Secondary')
  cylinder('Short barrel',(26,0,z),2,10,'Steel')
  box('Front compensator',(28,0,14),(7,7,8),'Secondary',.5)
  for x in (21,24,27):box('Compensator port',(x,0,18.1),(1.2,3,.16),'Black',.22)
  box('Rear sight',(-7,0,19),(3,4,2),'Frame',.2)
  box('Front sight',(27,0,18.7),(1.2,1.1,1.3),'Accent',.15)
  for a,b in [((3,0,-2),(12,0,-2)),((12,0,-2),(15,0,6))]:bar('Open trigger guard',a,b,.8,.8,'Frame')
  vents(-7,13,3.2,5,2,4)
  label(d['code'],5,-3.25,12.5,1.1)
  box('Pistol cell foot',(-2,0,-7.5),(7.4,4.7,2.3),'Secondary',.45)
 # Final open muzzle ends exactly at the existing muzzle point.
 radius=1.45 if k in ('Marksman','Sniper') else 2.25 if k=='DualPistol' else 3.15
 ring('Muzzle crown',mx-.75,z,radius,radius*.63,1.5,'Steel',n=40)
 cylinder('Recessed bore darkness',(mx-2.2,0,z),radius*.60,.12,'Black')
 if k not in ('DualPistol','Sniper'):
  pedestal=25 if k=='Rifle' else 15 if k=='Shotgun' else 22 if k=='SMG' else 23
  box('Sight pedestal',(3,0,pedestal),(10,3,1.5),'Frame',.3)
  ztop=pedestal+3
  # Open reflex sight housing.
  for sy in (-1,1):bar('Reflex sight side',(-1,sy*2,ztop-2),(1,sy*2,ztop+3),.8,.8,'Frame')
  box('Reflex sight top',(1,0,ztop+3),(2,4.8,.8),'Frame',.18)
  box('Reflex optic',(1,0,ztop+1),(.18,3.2,2),'Lens',.05)
 if k not in ('Shotgun','Marksman','DualPistol'):
  for a,b in [((2,0,4),(10,0,3)),((10,0,3),(12,0,z-5))]:bar('Open trigger guard',a,b,.75,.9,'Frame')
 # Restrained manufacturing details, with large clean panels between groups.
 yy=4.9 if k in ('Rifle','Marksman','Shotgun') else 3.8 if k=='SMG' else 4.1 if k=='Sniper' else 3.3
 screws([(0,z+1),(24 if k!='DualPistol' else 17,z+1)],yy,.42)
 if k!='Shotgun':label('HX  /  '+d['name'],8,-yy-.12,z+3,.65)

def refine_gun(d):
 k=d['key'];mx,_,z=d['muzzle'];phase=d['maker']=='PHASE'
 # Refinement replaces these base-pass details, so no overlapping labels or hubs remain.
 for o in list(PARTS):
  if o.name.startswith(('Laser marking','Inductor','Weapon ID stripe')):
   PARTS.remove(o);bpy.data.objects.remove(o,do_unlink=True)
 # Structural continuity at immutable animation contact points.
 c=Vector(d['c']);u=Vector(d['up']).normalized();top=c+u*d['along'][1]
 body_z={'Rifle':13,'SMG':10,'Shotgun':6,'Marksman':12,'Sniper':8,'DualPistol':10}[k]
 bar('Grip tang into chassis',top,(top.x+1,0,body_z),d['half'][0]*1.5,d['half'][1]*1.7,'Frame',.25)
 if d.get('fore'):
  f=d['fore'];topz=f[2]+d['fh'][1]
  for dx in (-5,5):
   bar('Foreguard mounting lug',(f[0]+dx,0,topz),(f[0]+dx,0,z),1.8,2.6,'Frame',.23)
 if k!='Marksman':ring('Muzzle adapter sleeve',mx-2.25,z,2.5 if k!='Sniper' else 2.15,1.5,3,'Frame',n=32)
 # Recessed, separated access panels: negative seams and fastened removable covers.
 if k in ('Rifle','SMG','Sniper','Marksman'):
  y={'Rifle':4.9,'SMG':3.7,'Sniper':4.1,'Marksman':4.8}[k]
  low={'Rifle':12,'SMG':10,'Sniper':7,'Marksman':7}[k]
  high={'Rifle':22.5,'SMG':18.7,'Sniper':16,'Marksman':20.5}[k]
  x0=-11 if k!='Sniper' else -7;x1=25 if k!='SMG' else 33
  outer=[(x0,low+2),(x0+3,low),(x1-3,low),(x1,low+3),(x1-3,high),(x0+1,high)]
  inner=[(x0+.8,low+2.3),(x0+3.4,low+.65),(x1-3.4,low+.65),(x1-.9,low+3.2),(x1-3.6,high-.65),(x0+1.7,high-.65)]
  for side in (-1,1):
   profile('Gasket perimeter',outer,.30,'Black',side*y,.1)
   profile('Removable armor panel',inner,.26,'Paint',side*(y+.23),.16)
   # Segmented lower backbone separates the weapon body from the hand interfaces.
   profile('Lower chassis shoulder',[(x0+2,low-.35),(x1-3,low-.35),(x1-4,low+1.8),(x0+2,low+1.8)],.6,'Secondary',side*(y+.25),.14)
   box('Raised machining rib',((x0+x1)/2,side*(y+.4),high-1.5),(x1-x0-6,.28,.55),'Steel',.12)
  # Keep identification readable on the final outside skin.
  label(d['code'],x0+3,-y-.43,high-4.2,1.08)
  label('SERVICE  /  07',x0+3,-y-.44,low+2.5,.5)
  screws([(x0+2,high-1.5),(x1-4,low+2.8)],y+.48,.38)
  if phase:
   # Ceramic cover broken by a slim status window and sealed diagnostic plug.
   box('Diagnostic recess',(x1-9,-y-.4,high-4),(8,.4,2.4),'Secondary',.28)
   for j in range(3):box('Power status',(x1-11+j*2.2,-y-.64,high-4),(1.3,.07,.35),'Glow',.05)
   cylinder('Diagnostic plug',(x1-7,y+.5,low+4),1.5,.8,'Frame','Y')
   ring('Diagnostic plug ring',x1-7,low+4,1.5,.95,.15,'Steel',y=y+.95,axis='Y',n=24)
  else:
   # Charging-slide recess with a captured handle (it never floats off the shell).
   box('Charging slide recess',(x1-8,-y-.45,high-4.4),(10,.4,2.3),'Black',.3)
   box('Charging slide rail',(x1-8,-y-.72,high-4.4),(8.7,.25,.45),'Steel',.08)
   box('Charging handle',(x1-10,-y-1.3,high-4.4),(2.7,1.4,1.6),'Secondary',.27)
   cylinder('Selector pivot',(x0+6,-y-.6,low+4),.9,.35,'Frame','Y',24)
   box('Safety selector lever',(x0+6.6,-y-.9,low+4.3),(2.7,.45,.55),'Steel',.13,rot=(0,-20,0))
  # A top equipment rail is localized around the optic, leaving a clean barrel silhouette.
  top={'Rifle':24,'SMG':20,'Sniper':17,'Marksman':22}[k]
  box('Optic rail foundation',(4,0,top+.8),(22,3.7,1),'Frame',.18)
  for j in range(9):box('Optic rail tooth',(-5+j*2.2,0,top+1.4),(.95,4.3,.6),'Secondary',.12)
 if k=='Rifle':
  for side in (-1,1):
   profile('Handguard removable shell',[(29,20.5),(64,20.5),(66,23.2),(31,23.2)],.45,'Paint',side*4.95,.17)
   for x in (33,45,57):box('Handguard elongated port',(x,side*5.22,21.75),(7,.12,.65),'Black',.2)
   box('Magazine armor',(13,side*3.6,-1),(6,.35,11),'Paint',.25)
   for x in (11.5,14.5):box('Magazine embossed flute',(x,side*3.85,-1),(.32,.18,8),'Secondary',.07)
  screws([(30,22),(64,22)],5.25,.3)
  profile('Stock cheek pad',[(-40,23),(-20,22),(-20,25),(-39,27)],5.2,'Rubber',b=.5)
 elif k=='SMG':
  box('Heat sink lower retainer',(45,0,4.2),(23,4,1.3),'Secondary',.25)
  for side in (-1,1):
   box('Cassette inset',(-14,side*4.02,10.5),(6,.28,6),'Frame',.3)
   box('Cassette status',(-14,side*4.2,10.5),(2.5,.08,.55),'Accent',.05)
 elif k=='Shotgun':
  for side in (-1,1):
   profile('Breech access gasket',[(4,3),(23,3),(27,6),(24,12.8),(4,12.8)],.24,'Black',side*4.82,.12)
   profile('Breech access plate',[(5,3.7),(22.7,3.7),(26,6),(23.3,12),(5,12)],.22,'Paint',side*5.05,.18)
   box('Breech inspection slit',(17,side*5.2,7.8),(8,.18,1.7),'Black',.22)
   for x in (15,18,21):box('Cell inspection brass',(x,side*5.32,7.8),(.6,.1,1.1),'Accent',.08)
  screws([(6,11),(24,6)],5.3,.45)
  label(d['code'],10,-5.26,10,1)
  box('Breech unlock',(3,-5.6,8),(2.1,1.4,4),'Frame',.3)
  for side in (-1,1):bar('Feed retaining strap',(65,side*2.7,0),(65,side*2.7,7),1,1.5,'Secondary')
 elif k=='Marksman':
  # A circular core in a much larger ceramic face is a functional focus, with recessed radial keys.
  for side in (-1,1):
   ring('Power hub cover',11,12.5,3.1,2.4,.65,'Frame',y=side*5.45,axis='Y')
   cylinder('Power hub', (11,side*5.8,12.5),2.25,.15,'Lens','Y')
   ring('Power hub filament',11,12.5,2.2,1.96,.08,'Glow',y=side*5.91,axis='Y')
   for x in (36,49,62):
    profile('Rail access seam',[(x,12),(x+8,12),(x+8,14),(x,14)],.12,'Secondary',side*5.51,.05)
  box('Rear stock connector',(-14,0,17),(5,4,4),'Frame',.4)
  box('Power cell lock',(23,-4,1),(3,1,2),'Accent',.2)
 elif k=='Sniper':
  for x in (48,60,72,84):
   for side in (-1,1):box('Shroud cooling recess',(x,side*4.05,12.7),(7,.15,.55),'Black',.12)
  cylinder('Scope elevation',(5,0,27),1.7,2.5,'Frame','Z')
  ring('Scope focus collar',-1,23,3.8,3.45,2.5,'Frame')
  for i in range(16):
   a=2*math.pi*i/16
   box('Scope collar knurl',(-1,3.8*math.cos(a),23+3.8*math.sin(a)),(1.9,.3,.3),'Secondary',.06)
 elif k=='DualPistol':
  for side in (-1,1):
   profile('Slide seam',[(1,9),(22,9),(25,12),(22,16),(1,16)],.18,'Black',side*3.21,.1)
   profile('Slide cover',[(2,9.6),(21.5,9.6),(24,12),(21.5,15.4),(2,15.4)],.25,'Paint',side*3.43,.15)
   box('Slide energy channel',(12,side*3.6,10.8),(12,.15,.55),'Accent',.1)
   box('Slide release',(2,side*3.8,7),(3,.6,.8),'Frame',.17)
  label(d['code'],5,-3.6,13.5,1)
  screws([(2.7,14.3),(21.5,12.2)],3.7,.3)
  box('Trigger',(7,0,2.5),(.8,.8,4.5),'Steel',.25,rot=(0,-15,0))

def blade(d):
 lo,hi=d['gr'];z=d['z'];r=d['r'];tip=d['tip'];w=d['w'];k=d['key']
 cylinder('Grip core',((lo+hi)/2,0,z),r,hi-lo,'Rubber','X',32,.10)
 for i in range(12):
  x=lo+.7+(hi-lo-1.4)*i/11
  ring('Handle traction',x,z,r+.04,r-.12,.24,'Secondary',n=24)
 for x in (lo+.1,hi-.3):ring('Grip ferrule',x,z,r+.25,r*.72,1.1,'Steel')
 box('Pommel',(lo-.6,0,z),(1.4,r*2.5,r*2.5),'Secondary',.4)
 start=hi+3;end=tip
 cylinder('Internal blade tang',(hi+1,0,z),r*.7,6,'Frame','X',24,.08)
 if k=='Blunt':
  # Powered impact cleaver: long counterbalanced spine, three compact piston housings.
  profile('Impact chassis',[(start,z-6),(end-5,z-9),(end,z-4),(end-2,z+9),(start,z+7)],5.7,'Secondary',b=.65)
  profile('Striking shoe',[(start+9,z-6),(end-5,z-9),(end,z-4),(end-1,z-1),(start+9,z-2)],6.5,'Steel',b=.5)
  for side in (-1,1):
   profile('Hydraulic armor',[(start+2,z+1),(end-11,z+1),(end-8,z+7),(start+2,z+6)],.9,'Paint',side*3.25,.35)
   for x in (start+20,start+52,start+84):
    box('Piston recess',(x,side*3.8,z+3),(12,.3,3.2),'Black',.4)
    cylinder('Impact piston',(x,side*4,z+3),.8,9,'Steel')
   box('Impact power bus',((start+end)/2,side*3.4,z-4),(end-start-20,.2,.55),'Accent',.08)
  label(d['code'],start+5,-3.85,z+7.4,1.3)
  screws([(start+4,z+3),(end-7,z+3)],3.85,.6)
  return
 # Separate central spine and actual bevelled cutting faces, giving a readable cross-section.
 sweep=4 if k in ('Katana','OneHand') else 0
 pts=[(start,z-w*.6),(end-15,z-w*.8+sweep),(end,z+w*.4+sweep),(end-19,z+w*.65),(start,z+w*.65)]
 profile('Tempered blade body',pts,1.3 if k not in ('GreatSword','Dagger') else 2.4 if k=='GreatSword' else .8,'Steel',b=.09)
 # Cutting bevel is a wedge in depth, not a flat glowing strip.
 a=start+1;b=end-15
 vs=[(a,-.6,z-w*.35),(b,-.6,z-w*.55+sweep),(end,0,z+w*.4+sweep),(b,0,z-w*.8+sweep),(a,0,z-w*.6),
     (a,.6,z-w*.35),(b,.6,z-w*.55+sweep)]
 fs=[(0,1,2,3,4),(4,3,2,6,5),(0,5,6,1),(1,6,2),(0,4,5)]
 me=bpy.data.meshes.new('Honed cutting bevel');me.from_pydata([tuple(c/100 for c in v) for v in vs],[],fs);me.update()
 ob=bpy.data.objects.new('Honed cutting bevel',me);bpy.context.collection.objects.link(ob);finish(ob,'Honed cutting bevel','Edge')
 guard=3.2 if k=='Dagger' else 9 if k=='GreatSword' else 5
 box('Guard core',(hi+1.3,0,z),(2.5,4 if k!='Dagger' else 2.3,guard*2),'Secondary',.5)
 for side in (-1,1):
  length=(end-start)*(.44 if k not in ('GreatSword','TwinBlades') else .74)
  y=side*(1.0 if k!='GreatSword' else 1.6)
  profile('Power spine armor',[(start,z),(start+length-3,z+.2),(start+length,z+w*.5),(start+3,z+w*.65),(start,z+w*.45)],.65,'Paint',y,.18)
  profile('Inset power conduit',[(start+4,z+w*.17),(start+length-5,z+w*.17),(start+length-3,z+w*.29),(start+4,z+w*.29)],.08,'Glow' if d['maker']=='PHASE' else 'Accent',y+side*.36,.025)
  for j in range(3 if k=='Dagger' else 5):
   x=start+5+j*(length-10)/(2 if k=='Dagger' else 4)
   box('Blade spine separator',(x,y+side*.35,z+w*.47),(.24,.08,w*.23),'Frame',.03)
 if k=='GreatSword':
  for side in (-1,1):
   box('Power accumulator',(start+13,side*2.4,z+4),(18,2,7),'Secondary',.45)
   vents(start+7,z+4,3.5,6,2,3)
  box('Counterweight collar',(hi,0,z),(6,7,11),'Frame',.7)
 elif k=='Katana':
  ring('Phase resonator',start+7,z+1.8,3.4,2.4,.8,'Frame',y=-1.6,axis='Y')
  cylinder('Resonator face',(start+7,-2.05,z+1.8),2.1,.1,'Secondary','Y')
  ring('Resonator indicator',start+7,z+1.8,2.1,1.8,.08,'Glow',y=-2.12,axis='Y')
 elif k=='QuickBlade':
  for x in (start+8,start+16,start+24):
   box('Spine ventilation',(x,-1.4,z+2),(.7,.2,2.4),'Black',.1,rot=(0,25,0))
 elif k=='TwinBlades':
  box('Balance weight',(start+6,0,z+.8),(9,3.8,4),'Secondary',.4)
 elif k=='Dagger':
  # Small retrieval fin and keyed pommel read clearly even when thrown.
  profile('Retrieval fin',[(lo,z+1),(hi-1,z+1),(hi-2,z+4),(lo+2,z+3)],.9,'Frame',b=.12)
 label(d['code'],start+3,-1.5 if k!='GreatSword' else -3.55,z+w*.4, .72 if k=='Dagger' else 1)
 screws([(start+2,z+w*.23),(start+(end-start)*.35,z+w*.23)],1.5 if k!='GreatSword' else 2,.3)

def recalc_and_join(d):
 bpy.ops.object.select_all(action='DESELECT')
 for o in PARTS:
  o.select_set(True);bpy.context.view_layer.objects.active=o
  for mod in list(o.modifiers):
   try:bpy.ops.object.modifier_apply(modifier=mod.name)
   except RuntimeError:pass
 bpy.context.view_layer.objects.active=PARTS[0]
 bpy.ops.object.join();o=bpy.context.object;o.name='SM_SF_'+d['key']
 bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.normals_make_consistent(inside=False);bpy.ops.object.mode_set(mode='OBJECT')
 # Object-space manufacturing texture in UE; UVs still provided for portable editing.
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.uv.smart_project(angle_limit=math.radians(70),island_margin=.012);bpy.ops.object.mode_set(mode='OBJECT')
 o.data.calc_loop_triangles();vs=[v.co for v in o.data.vertices]
 meta=dict(d,mesh=o.name,triangles=len(o.data.loop_triangles),min=[min(v[i] for v in vs)*100 for i in range(3)],max=[max(v[i] for v in vs)*100 for i in range(3)],materials=[m.name for m in o.data.materials])
 # FBX follows the project's existing normalized weapon exporter.
 bpy.ops.export_scene.fbx(filepath=str(OUT/(o.name+'.fbx')),use_selection=True,object_types={'MESH'},apply_unit_scale=True,mesh_smooth_type='FACE',add_leaf_bones=False)
 bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(o.name+'.blend')))
 (OUT/(o.name+'.json')).write_text(json.dumps(meta,indent=2),encoding='utf-8')
 return o,meta

def render(o,d):
 sc=bpy.context.scene;sc.render.engine='CYCLES';sc.cycles.samples=32;sc.cycles.use_denoising=True
 sc.render.resolution_x=1600;sc.render.resolution_y=760;sc.render.resolution_percentage=100
 sc.world=bpy.data.worlds.new('Studio');sc.world.use_nodes=True;sc.world.node_tree.nodes['Background'].inputs[0].default_value=(.16,.19,.23,1);sc.world.node_tree.nodes['Background'].inputs[1].default_value=.35
 sc.view_settings.view_transform='AgX'
 corners=[o.matrix_world@Vector(v) for v in o.bound_box];center=sum(corners,Vector())/8
 length=max(v.x for v in corners)-min(v.x for v in corners)
 def area(name,pos,power,size,color):
  a=bpy.data.lights.new(name,'AREA');a.energy=power;a.shape='DISK';a.size=size;a.color=color
  ob=bpy.data.objects.new(name,a);sc.collection.objects.link(ob);ob.location=center+Vector(pos);ob.rotation_euler=(center-ob.location).to_track_quat('-Z','Y').to_euler()
 area('Large cool key',(-.3,-1.4,1.7),170,2.2,(.82,.91,1))
 area('Warm edge',(.4,.9,1),230,1.5,(1,.86,.68))
 area('Front softbox',(.8,-1,.1),55,1.1,(.8,.9,1))
 ca=bpy.data.cameras.new('Product lens');cam=bpy.data.objects.new('Product lens',ca);sc.collection.objects.link(cam)
 cam.location=center+Vector((length*.18,-length*1.6,length*.65));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
 ca.type='ORTHO';sc.camera=cam
 q=cam.rotation_euler.to_quaternion();right=q@Vector((1,0,0));up=q@Vector((0,1,0))
 pw=max(v.dot(right) for v in corners)-min(v.dot(right) for v in corners)
 ph=max(v.dot(up) for v in corners)-min(v.dot(up) for v in corners)
 ca.ortho_scale=max(pw,ph*sc.render.resolution_x/sc.render.resolution_y)*1.15
 sc.render.image_settings.file_format='PNG';sc.render.film_transparent=False
 sc.render.filepath=str(OUT/(o.name+'_studio.png'));bpy.ops.render.render(write_still=True)

if __name__=='__main__':
 results=[]
 for d in GUNS+BLADES:
  if ONLY!='all' and d['key']!=ONLY:continue
  setup(d['maker'])
  if 'muzzle' in d:gun(d);refine_gun(d)
  else:blade(d)
  o,m=recalc_and_join(d);results.append(m)
  print('[SF]',d['key'],m['triangles'],'triangles',flush=True)
  if RENDER:render(o,d)
 if ONLY=='all':(OUT/'manifest.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
