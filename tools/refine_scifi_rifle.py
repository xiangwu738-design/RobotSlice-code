"""Second design pass: continuous receiver, fitted armor, compact layered assemblies.
Uses the same weapon interface dimensions, but replaces the first pass's disconnected blocks.
"""
import sys,math,json
from pathlib import Path
sys.path.insert(0,'D:/RobotSlice/tools')
import build_scifi_weapons as b
import bpy
from mathutils import Vector

d=b.GUNS[0]
b.setup('VHI')
P=b.profile;B=b.box;C=b.cylinder;R=b.ring;S=b.screws
# Uninterrupted load-bearing chassis. Shaped stock, receiver and handguard share datum lines.
P('Continuous receiver spine',[(-41,17),(-26,15),(-12,12),(20,12),(29,14),(66,14),(71,16),(71,21),(-35,23),(-41,21)],7.4,'Frame',b=.32)
P('Stock structural shell',[(-44,5),(-40,5),(-29,12),(-14,13),(-10,17),(-17,22),(-41,23),(-44,21)],6.6,'Secondary',b=.55)
# Deliberate negative space under the cheek rest: the window is assembled from enclosing spars.
P('Cheek rest',[(-42,20),(-15,19),(-11,22),(-15,25),(-38,25),(-42,23)],6.3,'Paint',b=.5)
P('Stock lower support',[(-41,5),(-37,4),(-21,9),(-16,13),(-22,14),(-27,10)],5.3,'Frame',b=.35)
P('Stock rear rubber plate',[(-45,3),(-43,2),(-41,5),(-41,23),(-43,25),(-45,23)],7.5,'Rubber',b=.55)
for side in (-1,1):
 P('Stock sliding collar',[(-21,14),(-13,13),(-10,17),(-14,21),(-22,21)],.5,'Paint',side*3.65,.22)
 B('Stock adjustment slot',(-30,side*3.42,17),(13,.15,1),'Black',.2)
 B('Stock adjustment catch',(-22,side*3.7,16.8),(2.5,.65,2),'Frame',.2)
 S([(-16,18)],3.98,.35)
# Slim upper receiver has a descending shoulder into the fore-end rather than a large faceplate.
P('Receiver upper shell',[(-13,14),(-8,11.4),(13,11.4),(24,14),(28,19),(24,23),(-10,24),(-14,21)],8.6,'Paint',b=.42)
P('Receiver top inset',[(-11,22.8),(23,21.8),(26,23.1),(22,24.3),(-10,25)],5.9,'Secondary',b=.24)
P('Receiver lower cradle',[(-7,10.5),(1,9.5),(9,10),(22,12),(25,15),(-9,15)],6.7,'Secondary',b=.28)
# Hand geometry remains exactly aligned with the existing animation solver.
b.grip(d['c'],d['up'],d['half'],d['along'])
b.bar('Integral grip neck',(-.4,0,6.3),(1,0,12),5.3,4.1,'Frame',.35)
# Trigger guard is closed at the receiver; bevelled, not a floating horizontal stick.
for a,e in [((2.3,0,4.4),(9.5,0,3.7)),((9.5,0,3.7),(12,0,10.9))]:b.bar('Trigger guard',a,e,.85,1.1,'Frame',.2)
b.bar('Trigger',(5.2,0,10.4),(4,0,6.8),.65,.8,'Steel',.16)
# Narrow, fitted magazine and beveled well, angled parallel to the grip.
P('Magazine well',[(10,12),(20,12),(19,6),(12,5),(9,7)],7.2,'Frame',b=.34)
P('Magazine body',[(12,6),(19,6),(17,-7),(13,-9),(9,-8)],6.5,'Secondary',b=.45)
P('Magazine toe',[(9,-7),(18,-6),(18,-9),(12,-11),(8,-10)],7,'Frame',b=.3)
for side in (-1,1):
 P('Magazine inlay',[(12,4),(17,4),(15.7,-5),(12,-6),(10.8,-5)],.18,'Paint',side*3.35,.16)
 for z in (-3,0,3):B('Magazine witness notch',(14,side*3.5,z),(3,.12,.45),'Black',.1)
 B('Magazine index stripe',(14,side*3.56,-7),(3.8,.10,.75),'Accent',.1)
# Foregrip nested below a full-length ventilated handguard. No broad open cage.
f=d['fore'];B('Contact foregrip',f,(15,3.2,5),'Rubber',.5)
for x in (25,28,31,34):B('Foregrip traction',(x,0,9.05),(.35,2.95,.15),'Secondary',.05)
B('Foreguard mounting saddle',(31,0,14.3),(18,4.8,2),'Frame',.25)
C('Internal barrel',(49,0,17),1.9,51,'Steel')
for side in (-1,1):
 # Three matching sections form a continuous housing, with purposeful panel breaks.
 P('Handguard shell',[(25,15.6),(28,13.8),(63,13.8),(69,16),(68,21.8),(28,22.7),(24,20)],.9,'Paint',side*3.95,.3)
 P('Handguard lower chamfer',[(27,14),(62,14),(65,15.6),(28,16)],.65,'Secondary',side*4.0,.18)
 P('Handguard upper lip',[(27,21.1),(67,20.4),(67,22.2),(28,23)],.6,'Secondary',side*3.95,.17)
 # Black inset sockets with real internal silver radiator edges at one end.
 for x in (31,39.5,48,56.5):
  P('Vent recessed aperture',[(x,17),(x+5.7,17),(x+6.9,19.6),(x+1.2,19.6)],.13,'Black',side*4.46,.17)
  b.bar('Vent internal edge',(x+1.3,side*4.55,17.5),(x+2.1,side*4.55,19.1),.23,.08,'Steel',.045)
 B('Handguard locking seam',(25.5,side*4.5,18),(.22,.1,3.4),'Black',.035)
 S([(28,20.6),(64,16.7)],4.55,.32)
# Longitudinal rail links the optic and front casing; tooth size is kept realistic.
P('Top accessory rail base',[(-14,24),(23,23.2),(27,22.6),(65,21.9),(65,23),(-14,25.3)],3.1,'Frame',b=.15)
for i in range(25):
 x=-12+i*3.1;z=25.5 if x<23 else 25.5-(x-23)*.044
 B('Rail lug',(x,0,z),(.8,3.6,.65),'Secondary',.12)
# Low enclosed reflex optic, mounted firmly to the receiver.
B('Sight foot',(-2,0,26),(12,4,1.5),'Frame',.24)
P('Optic left cheek',[(-6,26),(-5,31),(0,33),(4,30),(4,26)],.8,'Secondary',-2.2,.25)
P('Optic right cheek',[(-6,26),(-5,31),(0,33),(4,30),(4,26)],.8,'Secondary',2.2,.25)
B('Sight bridge',(-1,0,32),(6,5.1,1),'Paint',.23)
B('Optic window',(1,0,29.3),(.15,3.4,4),'Lens',.08)
for side in (-1,1):C('Sight windage',(-3,side*2.95,28.8),.7,.65,'Frame','Y',24)
# Fitted front collar and short suppressor. One connected axis to the exact gameplay muzzle.
P('Front barrel clamp',[(65,13),(70,13),(73,15),(73,20),(70,22),(65,22)],8.7,'Secondary',b=.4)
C('Muzzle extension',(73.4,0,17),2.8,4,'Frame')
R('Muzzle crown',76.2,17,3,1.65,1.6,'Steel')
C('Bore shadow',(74.9,0,17),1.58,.12,'Black')
for side in (-1,1):
 B('Clamp release',(67.5,side*4.45,17.5),(2.2,.35,4),'Frame',.23)
 B('Front serial mark',(69,side*4.66,17.5),(.6,.08,1.8),'Accent',.08)
# Asymmetric receiver controls; layered recesses rather than a uniformly embossed box.
P('Ejection well',[(7,16),(21,16),(23,19.7),(21,21),(8,21),(6,19)],.24,'Black',-4.42,.18)
P('Sliding bolt',[(8,16.8),(20,16.8),(21.7,19.4),(20.7,20.2),(8,20.2)],.15,'Steel',-4.58,.18)
B('Bolt channel',(14.5,-4.71,18.5),(10,.1,.6),'Frame',.07)
B('Charging handle',(10,-5.3,18.3),(2.2,1.2,1.25),'Secondary',.22)
P('Opposite electronics panel',[(-9,15),(4,15),(7,18),(4,21),(-9,21)],.22,'Secondary',4.45,.2)
for x in (-6,-3,0):B('Telemetry pin',(x,4.61,18),(.7,.1,.35),'Accent',.05)
C('Safety selector pivot',(-4,-4.55,14.7),.8,.38,'Frame','Y',24)
B('Safety selector',(-3.3,-4.88,14.9),(2.6,.35,.5),'Steel',.12,rot=(0,-15,0))
S([(-10,20.8),(2,13.5),(22,15)],4.52,.35)
b.label('VHI-R36',-10,-4.55,18.3,1.15)
b.label('LINE  /  SERIES 36',-9.8,-4.56,16.8,.48)
b.label('LOCK',-6,-4.58,13.1,.38)
B('Manufacturer marker',(-10.7,-4.6,18.5),(.4,.1,2.2),'Accent',.06)

o,meta=b.recalc_and_join(d)
# Keep v2 trials separate from the integrated model candidate until reviewed visually.
trial=Path('D:/RobotSlice/SourceAssets/scifi_weapons/rifle_v2');trial.mkdir(exist_ok=True)
b.OUT=trial
b.render(o,d)
print('[SF V2] rifle',meta['triangles'],flush=True)
