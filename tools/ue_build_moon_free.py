"""用作者现成的科幻模块拼装月球建筑，月面、蓝穹和玩法标记保持原状。"""
import unreal as u,json,math,datetime,shutil,hashlib,traceback
from pathlib import Path
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_original_developer_maps.py').read_text(encoding='utf8').split('\ntry:\n # 所有改写目标')[0],'moon_scene_helpers','exec'))
DST='/Game/Env/MoonFree';MAP='/Game/Maps/L_MoonDome'
REPORT={'passed':False,'buildings':[]};groups={};meshcache={}
source=json.loads((R/'Saved/Automation/MoonFreeImport.json').read_text(encoding='utf8'));assert source['passed']
layout=json.loads((R/'Inbox/_moon_nasa/work/layout_final.json').read_text(encoding='utf8'))

def rot(v,yaw):
 a=math.radians(yaw);return(v[0]*math.cos(a)-v[1]*math.sin(a),v[0]*math.sin(a)+v[1]*math.cos(a),v[2])

def place(name,x,y,z,yaw=0,scale=1,origin=(0,0,0),collision=True):
 sc=(scale,scale,scale) if isinstance(scale,(int,float)) else scale
 off=rot(tuple(origin[i]*sc[i] for i in range(3)),yaw)
 tf=u.Transform(V(x-off[0],y-off[1],z-off[2]),u.Rotator(yaw=yaw),u.Vector(*sc))
 groups.setdefault((name,collision),[]).append(tf)

def device(name,x,y,z,yaw=0,scale=1,collision=True):
 b=source['meshes'][name];lo,hi=b['min'],b['max'];origin=((lo[0]+hi[0])/2,(lo[1]+hi[1])/2,lo[2])
 place(name,x,y,z,yaw,scale,origin,collision)

def room(cx,cy,z,nx,ny,s=1,round_corners=True,tower=False,kind='habitat'):
 step=4*s;w=nx*step;d=ny*step;h=5*s;rounded=round_corners and min(nx,ny)>=3;cut=1 if rounded else 0
 axis='x' if abs(cx)>=abs(cy) else 'y'
 # 四侧立面在同一网格上拼接。两端留真实门洞，机器人可从基地通道穿过建筑。
 for side in ('w','e','s','n'):
  count=ny if side in ('w','e') else nx
  yaw={'w':0,'e':180,'s':90,'n':270}[side]
  gate_side=(side in ('w','e')) if axis=='x' else (side in ('s','n'))
  gate=int((count-1)/2);gate_tiles=(gate,gate+1) if count%2==0 else (gate,)
  for i in range(cut,count-cut):
   q=(i+.5-count/2)*step
   x,y=(cx+(-w/2 if side=='w' else w/2),cy+q) if side in ('w','e') else (cx+q,cy+(-d/2 if side=='s' else d/2))
   if gate_side and i in gate_tiles:
    if i!=gate:continue
    # 偶数格房间把两格合成居中的宽门，避免通道轴线撞在门边或中柱上。
    if len(gate_tiles)==2:
     if side in ('w','e'):y=cy
     else:x=cx
    # 底沿埋入地板 6 厘米/尺度，竖向补足相同高度，使顶部仍与屋面齐平。
    place('Door_Frame_SquareTall',x,y,z-.06*s,90 if side in ('w','e') else 0,(s*len(gate_tiles),s,s*1.012))
    device('Prop_Light_Wide',x,y,z+h+.07,yaw+90,s,False)
   else:
    module='WallAstra_Straight' if kind=='hangar' or i%3==0 else 'WallAstra_Straight_Window'
    place(module,x,y,z,yaw,s,(-2,0,0))
    place('TopAstra_Straight',x,y,z,yaw,s,(-2,0,0))
    place('BottomMetal_Straight',x,y,z+.02,yaw,s,(-2,0,0))
 if rounded:
  for sx,sy,yaw in [(-1,-1,0),(1,-1,90),(1,1,180),(-1,1,270)]:
   x=cx+sx*(w/2-step);y=cy+sy*(d/2-step)
   place('WallAstra_Corner_Round_Outer',x,y,z,yaw,s)
   place('TopAstra_Curve_Round_Outer',x,y,z,yaw,s)
   for zz in (z+.015,z+h):place('Platform_Metal2_Curve',x,y,zz,yaw,s,(2,2,0))
 # 作者的屋面与地板都有对应的弧形片；四角留给弧片，避免方形楼板穿出圆角。
 for ix in range(nx):
  for iy in range(ny):
   if rounded and ix in (0,nx-1) and iy in (0,ny-1):continue
   x=cx+(ix+.5-nx/2)*step;y=cy+(iy+.5-ny/2)*step
   for zz in (z+.015,z+h):place('Platform_Metal2',x,y,zz,0,s)
 for dx,dy in [(-w*.20,-d*.18),(w*.20,d*.18)]:
  device('Prop_Fan_Small',cx+dx,cy+dy,z+h+.03,0,s*2)
  device('Prop_Vent_Wide',cx+dx+2*s,cy+dy,z+h+.03,90,s*2)
 device('Prop_Computer',cx+w*.26,cy-d*.25,z+.03,0,s,False)
 device('Prop_Crate4',cx-w*.24,cy+d*.25,z+.03,0,s*1.3)
 # 大机库增加低矮控制室，轮廓有主次，避免全场成为等高盒子。
 if tower and nx>=6 and ny>=5:room(cx+w*.18,cy,z+h,3,3,1,True,False,'control')
 REPORT['buildings'].append({'x':cx,'y':cy,'z':z,'width':w,'depth':d,'height':h,'kind':kind,'rounded':rounded})

def shield_state():
 out={}
 for a in S.get_all_level_actors():
  if a.actor_has_tag('MoonShield') or a.actor_has_tag('MoonAir'):
   c=a.get_component_by_class(u.StaticMeshComponent)
   pos=a.get_actor_location();sc=a.get_actor_scale3d()
   out[a.get_actor_label()]={'position':[pos.x,pos.y,pos.z],'scale':[sc.x,sc.y,sc.z],'tags':[str(t) for t in a.tags],'mesh':c.static_mesh.get_path_name(),'enabled':str(c.get_collision_enabled()),'profile':str(c.get_collision_profile_name())}
 return out

def walkways():
 plan=json.loads((R/'SourceAssets/moon_free/walkways.json').read_text())
 for route in plan['routes']:
  pts=route['points'];width=route['width']
  for i,(a,b) in enumerate(zip(pts,pts[1:])):
   dx=b[0]-a[0];dy=b[1]-a[1];dz=b[2]-a[2];length=math.hypot(dx,dy);yaw=math.degrees(math.atan2(dy,dx));pitch=math.degrees(math.atan2(dz,length))
   mid=[(a[k]+b[k])*.5 for k in range(3)];slope_length=math.hypot(length,dz)
   tf=u.Transform(V(*mid),u.Rotator(yaw=yaw,pitch=pitch),u.Vector(slope_length/4,width/4,1))
   groups.setdefault(('Platform_Squares',True),[]).append(tf)
   # 灯带识别通道边缘；靠膜处不放栏杆，四周仍可自由穿越。
   if i%3==1:
    for side in (-1,1):
     x=mid[0]-dy/length*side*(width/2-.2);y=mid[1]+dx/length*side*(width/2-.2)
     device('Prop_Light_Wide',x,y,mid[2]+.025,yaw,(.8,.45,.7),False)
   if math.hypot(mid[0],mid[1])>76 and i%3!=0:
    for side in (-1,1):
     x=mid[0]-dy/length*side*(width/2);y=mid[1]+dx/length*side*(width/2)
     device('Prop_Rail_4',x,y,mid[2],yaw-90,(1,length/3.9257,1))
 REPORT['walkways']=[{k:r[k] for k in ('name','length','width','max_grade')} for r in plan['routes']]
 return plan['retained_floor_top']

try:
 stamp=datetime.datetime.now().strftime('%Y%m%d_%H%M%S');backup=R/'Saved/Backup'/('MoonFreeScene_'+stamp);backup.mkdir(parents=True)
 p=R/'Content/Maps/L_MoonDome.umap';shutil.copy2(p,backup/p.name)
 original=R/'Content/Maps/L_Moon.umap';original_hash=hashlib.sha256(original.read_bytes()).hexdigest()
 world=u.EditorLoadingAndSavingUtils.load_map(MAP);assert world;expected=marks();shield=shield_state();assert shield
 # 移除上一轮自行搭建的场地建筑；穹顶及其功能地板、月面、岩石、灯光不动。
 removed=[]
 for a in list(S.get_all_level_actors()):
  n=a.get_actor_label()
  if (n.startswith('OM_') and n not in ('OM_Dome','OM_DomeRing','OM_DomeInterior')) or n.startswith('MF_'):
   removed.append(n);S.destroy_actor(a)
 # 旧掩体、发射器与功能地面原先合在一个模型里：换成只保留地板/地环的副本。
 for a in S.get_all_level_actors():
  n=a.get_actor_label()
  if n in ('OM_DomeInterior','OM_DomeRing'):
   a.static_mesh_component.set_static_mesh(asset(DST+'/Meshes/SM_'+('RetainedFloor' if n=='OM_DomeInterior' else 'RetainedRing')))
   if n=='OM_DomeInterior':
    p=a.get_actor_location();p.z=next(iter(shield.values()))['position'][2]+6;a.set_actor_location(p,False,True)
   else:a.static_mesh_component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
 for index,it in enumerate(layout['items']):
  g=it['group'];x=it['cx'];y=it['cy'];z=it.get('pad_z',it['z'])+.06;w=it['w'];d=it['d']
  if g=='XLG_C':continue
  if g.startswith(('MD_','LG_','XLG_','SM_','P_Building')):
   large=g.startswith(('MD_','LG_','XLG_')) or min(w,d)>28
   s=2 if large else 1;step=4*s
   nx=max(1,int(min(w*.78,72)/step));ny=max(1,int(min(d*.78,56)/step))
   room(x,y,z,nx,ny,s,min(nx,ny)>=3,large,'hangar' if large and index%2==0 else 'habitat')
  elif g.startswith('P_Tank'):
   sc=min(6,max(2.2,min(w,d)*.45));span=sc*.85
   for dx,dy in [(0,0),(-span/2,span/2),(span/2,span/2)]:device('Prop_Barrel_Large',x+dx,y+dy,z,0,sc)
  elif g.startswith(('P_Crate','P_BlastCover')):
   count=3 if 'Blast' in g else 1;sc=2.2 if count==3 else 1.6
   for j in range(count):device('Prop_Crate3',x+(j-(count-1)/2)*2.6,y,z,it.get('yaw',0),sc)
  elif g.startswith(('P_Cooling','P_Pipes')):
   room(x,y,z,2,2,1,False,False,'plant')
  elif g.startswith(('P_Antenna','P_Tower')):
   device('Column_Round',x,y,z,0,(1.3,1.3,2))
   device('Prop_Light_Wide',x,y,z+10,0,1.8,False)
  elif g.startswith('P_Lamp'):
   device('Column_Round',x,y,z,0,(.45,.45,.85),False)
   device('Prop_Light_Wide',x,y,z+4.25,it.get('yaw',0),1,False)
 floor=walkways()
 # 发射器使用作者的完整结构柱，四个入口方向留空，不改变蓝色膜的碰撞。
 for angle in [22.5+i*45 for i in range(8)]:
  a=math.radians(angle);x=65*math.cos(a);y=65*math.sin(a)
  device('Column_Large_Straight',x,y,-.18,angle+90,(2,2,.8))
  device('Prop_Light_Wide',x,y,7.85,angle+90,(1.3,2,1),False)
  device('Prop_AccessPoint',x-2*math.cos(a),y-2*math.sin(a),3.5,angle,2,False)
 # 四个服务区代替原始方块掩体，保留中央及四向通路。
 for x in (-24,24):
  for y in (-24,24):
   for dx,sc in [(-3,2.8),(0,3.2),(3,2.6)]:device('Prop_Crate3',x+dx,y,floor,0,sc)
   device('Prop_Chest',x,y+3,floor,0,2)
   device('Prop_Computer',x-3,y-2.8,floor,0,1.7,False)
 REPORT['retained_floor_lift_cm']=6;REPORT['new_emitters']=8;REPORT['interior_service_clusters']=4
 count=0
 for (name,collision),tfs in groups.items():
  mesh=asset(DST+'/Meshes/SM_'+name);instances('MF_'+name+('_Solid' if collision else '_Detail'),mesh,tfs,collision);count+=len(tfs)
 assert marks()==expected
 REPORT['shield_before']=shield;REPORT['shield_after']=shield_state()
 assert REPORT['shield_after']==shield,'穹顶状态变化'
 assert world.get_world_settings().get_editor_property('global_gravity_z')==-327.
 assert not any('/Meshes/SM_OM_' in p and not any(k in p for k in ['Dome','Interior']) for a in S.get_all_level_actors() for p in visual_refs(a))
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 assert hashlib.sha256(original.read_bytes()).hexdigest()==original_hash
 REPORT.update(passed=True,instances=count,groups=len(groups),preserved_markers=len(expected),shield_unchanged=True,original_map_unchanged=True,removed_actors=len(removed),backup=str(backup))
except Exception:
 REPORT['error']=traceback.format_exc();u.log_error(REPORT['error']);raise
finally:
 (R/'Saved/Automation/MoonFreeScene.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
