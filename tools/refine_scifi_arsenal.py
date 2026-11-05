"""Cohesive second pass for the remaining five firearms. Same gameplay attachment coordinates."""
import sys,math,json
from pathlib import Path
sys.path.insert(0,'D:/RobotSlice/tools')
import build_scifi_weapons as b
from mathutils import Vector
P=b.profile;B=b.box;C=b.cylinder;R=b.ring;S=b.screws

def interfaces(d):
 b.grip(d['c'],d['up'],d['half'],d['along'])
 c=Vector(d['c']);u=Vector(d['up']).normalized();top=c+u*d['along'][1]
 body={'SMG':10,'Shotgun':7,'Marksman':12,'Sniper':10,'DualPistol':10}[d['key']]
 b.bar('Receiver grip socket',top,(top.x,0,body),d['half'][0]*1.55,d['half'][1]*1.8,'Frame',.22)
 if d.get('fore'):
  f=d['fore'];B('Ergonomic support contact',f,(15,d['fh'][0]*2,d['fh'][1]*2),'Rubber',.48)
  for i in range(5):B('Traction insert',(f[0]-5+i*2.5,0,f[2]-d['fh'][1]),(.32,d['fh'][0]*2-.18,.13),'Secondary',.035)
  for dx in (-5,5):b.bar('Support integrated bracket',(f[0]+dx,0,f[2]+d['fh'][1]),(f[0]+dx,0,d['muzzle'][2]),2.4,2.8,'Frame',.22)

def muzzle(d,r=2.3):
 x,_,z=d['muzzle'];R('Barrel adapter',x-2.5,z,r,.62*r,4,'Frame',n=32)
 R('Machined muzzle crown',x-.65,z,r*1.05,.65*r,1.3,'Steel',n=32)
 C('Bore darkness',(x-2,0,z),r*.6,.1,'Black')

def optic(x,z,phase=False):
 if phase:
  P('Integrated optical sensor',[(x-6,z),(x+11,z),(x+13,z+2),(x+9,z+4),(x-5,z+3.5)],4.6,'Secondary',b=.3)
  B('Rangefinder lens',(x+11.5,0,z+2),(.18,3.5,1.6),'Lens',.06)
  B('Optic fiducial',(x,-2.4,z+2),(6,.1,.4),'Glow',.04)
 else:
  B('Sight saddle',(x,0,z),(10,3.8,1.4),'Frame',.22)
  for side in (-1,1):P('Sight protective hoop',[(x-4,z),(x-4,z+4),(x,z+5.5),(x+3,z+3),(x+3,z)],.7,'Secondary',side*2,.22)
  B('Sight roof',(x-1,0,z+5),(4,4.5,.7),'Paint',.18)
  B('Sight lens',(x+1,0,z+2.7),(.1,3.1,3),'Lens',.04)

def slot(x,z,y,w=7,h=1.6,mat='Black'):
 for side in (-1,1):P('Recessed vent',[(x,z),(x+w-1,z),(x+w,z+h),(x+1,z+h)],.13,mat,side*y,.12)

def smg(d):
 # Compact self-contained thermal weapon: a single low chassis and a captured rear cell.
 P('Compact backbone',[(-19,9),(-7,7),(22,7),(27,9),(57,9),(59,13),(53,17),(-17,18)],6.4,'Frame',b=.35)
 P('Receiver shell',[(-17,10),(-9,8),(10,8),(23,10),(27,14),(23,18),(-14,19),(-18,17)],7.5,'Paint',b=.4)
 P('Rear thermal cassette',[(-20,4),(-11,3),(-9,8),(-10,17),(-19,18)],6.8,'Secondary',b=.5)
 P('Cassette armor',[(-19,5),(-12,4),(-12,8),(-14,15),(-19,15)],.3,'Paint',-3.55,.2)
 B('Cassette release',(-16,-3.8,11),(4,.55,1),'Accent',.18)
 for x in (-23,-19):B('Retractable stock rail',(x,0,12),(7,3,2),'Steel',.18)
 P('Compact butt pad',[(-27,3),(-24,3),(-23,5),(-23,17),(-25,19),(-27,18)],5.8,'Rubber',b=.5)
 # Barrel cooling jacket forms the front half of the gun and meets the receiver on a keyed seam.
 C('Heat core',(43,0,8),2.1,33,'Steel')
 for side in (-1,1):
  P('Radiator shroud',[(24,7),(53,6.5),(59,9),(57,15),(52,17),(27,17.5),(23,14)],.8,'Paint',side*3.8,.32)
  P('Radiator bottom return',[(26,6.5),(53,6),(55,7.5),(26,9)],.5,'Secondary',side*3.85,.18)
  B('Shroud release seam',(25,side*4.25,12),(.22,.12,6),'Black',.04)
 for x in (29,35,41,47):slot(x,10,4.25,4,3.6)
 B('Forward barrel collar',(57,0,9),(5,8.5,9),'Secondary',.7)
 P('Receiver rail base',[(-13,18),(20,17),(27,17),(27,19),(-13,20)],3.2,'Frame',b=.17)
 for i in range(11):B('Rail lug',(-11+i*3.3,0,20-(i*3.3)*.025),(1,3.7,.55),'Secondary',.1)
 optic(-2,20.4)
 P('Charging port',[(3,11),(18,11),(21,14),(18,16),(4,16)],.2,'Black',-3.85,.15)
 B('Charging shuttle',(11,-4.05,13.6),(10,.2,.6),'Steel',.1)
 B('Charging handle',(7,-4.6,13.6),(2,1,1.4),'Secondary',.25)
 b.label(d['code'],-14,-3.88,15.5,1)
 B('Brand slash',(-15,-3.95,15.7),(.35,.1,1.8),'Accent',.06)
 S([(-14,11),(21,16),(55,11)],4.3,.32)
 for a,e in [((3,0,4),(10,0,3.5)),((10,0,3.5),(12,0,9))]:b.bar('Trigger guard',a,e,.7,.9,'Frame',.15)
 b.bar('Trigger',(6,0,8),(5,0,5),.6,.7,'Steel',.12)
 muzzle(d,2.6)

def shotgun(d):
 # Sloped shoulder stock follows the original grip axis; broad breach and covered tubular fore-end.
 P('Continuous stock core',[(-42,-9),(-36,-9),(-14,1),(-8,6),(-14,7),(-40,1)],6.6,'Frame',b=.45)
 P('Cheek stock shell',[(-40,-2),(-33,-1),(-13,5),(-11,8),(-17,9),(-40,3)],6.9,'Paint',b=.45)
 P('Stock butt',[(-44,-12),(-41,-12),(-39,-9),(-39,2),(-41,4),(-44,3)],7.5,'Rubber',b=.55)
 P('Breech frame',[(-2,3),(24,2),(31,5),(32,10),(27,13),(0,13)],8.4,'Frame',b=.4)
 P('Breech upper casing',[(0,8),(26,7),(30,10),(27,15),(3,15),(-1,12)],9.5,'Paint',b=.4)
 for side in (-1,1):
  P('Breech sliding door',[(4,5),(23,5),(27,8),(24,12.7),(4,12.7)],.35,'Secondary',side*4.8,.23)
  P('Bolt inspection opening',[(9,7),(21,7),(23,9.5),(21,11),(9,11)],.15,'Black',side*5.05,.13)
  for x in (12,16,20):C('Cell feed visible lug',(x,side*5.16,9),.55,1.8,'Steel','X',16,.05)
  B('Breech unlock',(3,side*5,9),(1.7,1.2,4),'Frame',.25)
 C('Upper barrel',(52,0,7),2.7,48,'Steel')
 C('Feed tube',(51,0,1),2.15,42,'Secondary')
 # The fixed left-hand contact sits inside the lower cradle, not below a disconnected tube.
 P('Forward top spine',[(28,10),(70,10),(74,12),(70,14),(29,14)],5.6,'Secondary',b=.3)
 for side in (-1,1):
  P('Front shell',[(42,5),(69,5),(74,8),(72,12),(43,12)],.8,'Paint',side*3.8,.3)
  for x in (46,54,62):slot(x,7.3,4.25,5,2)
  B('Feed tube collar',(68,side*2.2,2),(2,1.3,5),'Frame',.22)
 B('Muzzle junction',(73,0,7),(4,8.8,8),'Secondary',.65)
 for a,e in [((-14,0,-5),(0,0,-9)),((0,0,-9),(5,0,3))]:b.bar('Lever guard',a,e,.8,1,'Frame',.18)
 b.bar('Trigger',(1,0,5),(-1,0,-1),.7,.8,'Steel',.15)
 optic(8,16)
 b.label(d['code'],5,-5.05,12.7,1)
 B('Manufacturer index',(25,-4.95,11.5),(.5,.13,2),'Accent',.07)
 S([(4,5.7),(25,10.7),(71,9)],5.12,.38)
 muzzle(d,3)

def phase_stock(d,z):
 P('Stock frame',[(-36,z-5),(-31,z-6),(-15,z-1),(-10,z+4),(-15,z+8),(-35,z+9)],5.7,'Secondary',b=.5)
 P('Cheek shell',[(-35,z+5),(-13,z+4),(-10,z+7),(-15,z+10),(-33,z+11),(-36,z+9)],5.4,'Paint',b=.45)
 P('Butt pad',[(-38,z-7),(-35,z-7),(-33,z-4),(-33,z+9),(-35,z+11),(-38,z+10)],6.6,'Rubber',b=.5)
 for side in (-1,1):
  P('Stock recessed service panel',[(-33,z-2),(-28,z-2),(-17,z+1),(-20,z+4),(-33,z+3)],.18,'Black',side*2.95,.18)
  B('Stock datum',(-28,side*3.08,z+2),(5,.08,.45),'Accent',.05)

def marksman(d):
 phase_stock(d,10)
 P('Induction structural core',[(-15,8),(19,5),(28,8),(30,15),(24,19),(-12,20)],7.5,'Frame',b=.4)
 P('Ceramic receiver shell',[(-14,13),(-8,9),(14,8),(26,10),(28,15),(22,21),(-10,22),(-15,19)],8.7,'Paint',b=.5)
 P('Lower power module',[(3,7),(23,7),(25,2),(22,-1),(6,-1),(2,2)],6.8,'Secondary',b=.5)
 for side in (-1,1):
  P('Power module inlay',[(6,2),(21,2),(22,4),(20,6),(6,6)],.22,'Paint',side*3.5,.2)
  B('Cell latch',(20,side*3.7,1.5),(2.2,.4,1),'Accent',.15)
  P('Receiver diagonal seam',[(-5,10),(-3,10),(0,20),(-1,21)],.08,'Secondary',side*4.4,.025)
  P('Sealed power window',[(4,12),(19,12),(22,15),(20,18),(5,18),(3,16)],.15,'Secondary',side*4.48,.2)
  for x in (7,11,15):B('Inset coil indicator',(x,side*4.6,15),(.55,.08,2.3),'Glow',.07)
 # Two continuous ceramic emitter arms, joined to a central saddle above the support hand.
 B('Emitter lower saddle',(38,0,6),(22,5.2,5),'Frame',.5)
 for side in (-1,1):
  P('Emitter rail core',[(26,8),(78,8),(85,10),(85,12),(77,15),(27,17),(24,14)],2.1,'Secondary',side*2.9,.32)
  P('Emitter ceramic shell',[(28,10),(73,10),(82,12),(74,15),(29,17),(26,14)],1,'Paint',side*4.1,.35)
  P('Rail inner conductor',[(29,10.6),(82,10.6),(84,11.3),(80,11.8),(29,11.8)],.08,'Glow',side*1.8,.025)
  for x in (39,56,70):
   P('Ceramic joint seam',[(x,10.1),(x+.45,10.1),(x+1.6,15.7),(x+1.1,15.7)],.10,'Frame',side*4.65,.025)
  S([(29,14),(75,12.5)],4.72,.3)
 # Connecting cradle has a real aperture, avoiding isolated floating emitters.
 R('Emitter throat',85.7,11,2.35,1.1,2.6,'Frame')
 C('Aperture lens',(85.9,0,11),1.05,.1,'Lens')
 optic(1,22.7,True)
 b.label(d['code'],-11,-4.51,17.8,.9)
 b.label('INDUCTION  /  01',-10.8,-4.52,16.3,.4)
 S([(-10,13),(21,19)],4.52,.32)
 # The grip is rear-angled by design; the trigger guard follows it rather than imposing an AR grip.
 for a,e in [((-8,0,-1),(0,0,0)),((0,0,0),(2,0,9))]:b.bar('Trigger guard',a,e,.65,.85,'Frame',.14)
 b.bar('Trigger',(-3,0,10),(-4,0,5),.6,.8,'Steel',.12)
 muzzle(d,1.35)

def sniper(d):
 phase_stock(d,10)
 P('Precision chassis',[(-15,7),(31,6),(40,8),(92,8),(98,10),(95,13),(35,16),(-12,18)],6.4,'Frame',b=.32)
 P('Receiver ceramic shell',[(-13,10),(-7,7),(24,7),(34,10),(35,13),(29,17),(-10,19),(-15,16)],7.7,'Paint',b=.4)
 for side in (-1,1):
  P('Receiver access seam',[(-7,10),(24,9.5),(29,12),(26,14),(0,16),(-8,14.5)],.14,'Secondary',side*3.95,.18)
  P('Ceramic side insert',[(-6,10.6),(23,10.1),(27.8,12),(25.5,13.4),(0,15.3),(-7,14)],.18,'Paint',side*4.10,.16)
  B('Receiver status',(22,side*4.25,12),(5,.08,.4),'Glow',.04)
 C('Inner precision barrel',(70,0,9),1.6,60,'Steel')
 for side in (-1,1):
  P('Tapered barrel guard',[(37,8),(90,7.6),(98,9.5),(94,13),(40,16),(35,13)],.8,'Paint',side*3.15,.3)
  P('Barrel guard top return',[(39,14),(93,11.9),(94,13),(40,16)],.7,'Secondary',side*3.12,.17)
  for x in (50,63,76):slot(x,10.2,3.61,8,1.1)
  P('Fore-end joint',[(37,8),(37.4,8),(40.8,15.8),(40.4,15.8)],.1,'Frame',side*3.65,.025)
 B('Support saddle',(44,0,8.8),(18,4.4,2.8),'Frame',.28)
 P('Muzzle casing',[(94,6),(99,6),(101,8),(101,10),(98,12),(94,12)],6.1,'Secondary',b=.4)
 # Scope is low and clamped into the receiver spine at two spaced load points.
 B('Optic rail',(6,0,18.8),(28,3.5,1.7),'Frame',.24)
 for x in (-5,16):B('Scope saddle',(x,0,20.5),(3.5,4.5,2.8),'Secondary',.3)
 C('Scope tube',(7,0,23),2.5,29,'Secondary')
 C('Objective housing',(21,0,23),3.4,7,'Paint')
 R('Objective rim',25,23,3.5,2.7,1.5,'Frame')
 C('Objective glass',(25.4,0,23),2.65,.1,'Lens')
 R('Ocular focus',-7,23,3,2.4,4,'Frame')
 for x in (-8,-6.5):R('Focus grip rib',x,23,3.1,2.9,.25,'Rubber',n=32)
 C('Elevation turret',(5,0,26.2),1.6,2.3,'Frame','Z',32)
 B('Rangefinder module',(9,-3.1,23),(10,2,3),'Paint',.3)
 B('Rangefinder status',(9,-4.14,23),(4,.08,.4),'Glow',.05)
 b.label(d['code'],-6,-4.25,13.2,.95)
 S([(-8,15),(28,12),(93,10)],4.25,.3)
 for a,e in [((3,0,2),(11,0,2.5)),((11,0,2.5),(13,0,8))]:b.bar('Trigger guard',a,e,.7,.9,'Frame',.15)
 b.bar('Trigger',(6,0,8),(5.3,0,4.5),.65,.8,'Steel',.12)
 muzzle(d,1.9)

def pistol(d):
 # Consistent industrial tooling, one slide silhouette and a compact under-barrel module.
 P('Pistol lower frame',[(-5,5),(1,2),(10,3),(17,7),(27,8),(27,12),(-7,12)],5.1,'Frame',b=.4)
 P('Pistol upper slide',[(-10,10),(-7,7),(24,8),(31,11),(30,17),(-7,18),(-11,15)],6.2,'Paint',b=.4)
 P('Slide top inset',[(-8,16),(27,15.5),(29,17),(26,18.6),(-7,19)],4.5,'Secondary',b=.25)
 for side in (-1,1):
  for x in (-7,-5,-3):B('Slide rear serration',(x,side*3.2,13),(1,.16,4.1),'Secondary',.12)
  P('Slide inspection port',[(3,12),(16,12),(18,14),(16,16),(3,16)],.15,'Black',side*3.21,.15)
  B('Slide bolt inner',(10,side*3.34,14),(10,.12,1.5),'Steel',.18)
  B('Slide release',(1,side*2.8,7.4),(3,.7,.8),'Secondary',.15)
  B('Magazine release',(-1,side*2.5,2),(1.1,.35,1.1),'Frame',.18)
  B('Muzzle vent',(25,side*3.23,13.2),(4,.13,1.1),'Black',.16)
 B('Rear sight',(-8,0,19.4),(2.5,3.6,1.5),'Frame',.18)
 B('Front sight',(27,0,18.5),(1.1,1,1.3),'Accent',.13)
 P('Underbarrel module',[(15,5),(27,6),(29,9),(27,11),(17,10)],4.9,'Secondary',b=.4)
 for a,e in [((2,0,-1),(11,0,-1)),((11,0,-1),(15,0,7))]:b.bar('Trigger guard',a,e,.85,1,'Frame',.17)
 b.bar('Trigger',(7,0,7),(5,0,2.4),.7,.9,'Steel',.15)
 B('Magazine base',(-2,0,-7.6),(7.2,4.6,2),'Secondary',.4)
 b.label(d['code'],3,-3.24,10.1,.8)
 B('Frame maker stripe',(18,-3.25,10.3),(3,.12,.55),'Accent',.07)
 S([(-9,13),(20,14)],3.32,.3)
 C('Barrel',(27,0,14),1.8,8,'Steel')
 muzzle(d,2.1)

functions={'SMG':smg,'Shotgun':shotgun,'Marksman':marksman,'Sniper':sniper,'DualPistol':pistol}
for d in b.GUNS[1:]:
 b.OUT=Path('D:/RobotSlice/SourceAssets/scifi_weapons');b.setup(d['maker'])
 interfaces(d);functions[d['key']](d)
 o,m=b.recalc_and_join(d);b.render(o,d)
 print('[SF V2]',d['key'],m['triangles'],flush=True)
root=Path('D:/RobotSlice/SourceAssets/scifi_weapons')
import shutil
shutil.copy2(root/'rifle_v2/SM_SF_Rifle_studio.png',root/'SM_SF_Rifle_studio.png')
manifest=[json.loads((root/('SM_SF_'+d['key']+'.json')).read_text()) for d in b.GUNS+b.BLADES]
(root/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
