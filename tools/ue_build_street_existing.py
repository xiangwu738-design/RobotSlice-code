"""千禧老街：复用 Poly Haven 作者模块与道具；只拼装，不生成建筑几何。"""
import unreal as u,json,math,shutil,datetime,hashlib,traceback
from pathlib import Path
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_original_developer_maps.py').read_text(encoding='utf8').split('\ntry:\n # 所有改写目标')[0],'street_helpers','exec'))
DST='/Game/Env/MillenniumStreetExisting'; MAP='/Game/Maps/L_MillenniumStreet'
REPORT={'passed':False,'materials':[],'sources':[]}
catalog=json.loads((R/'Saved/Automation/StreetExistingProbe.json').read_text())
APT='modular_urban_apartments_facade'; cache={}; groups={}; used=set(); matcache={}

def local_material(source):
 path=source.get_path_name()
 if path in matcache:return matcache[path]
 name=source.get_name()+'_'+hashlib.sha1(path.encode()).hexdigest()[:8];dest=DST+'/Materials/'+name
 m=u.load_asset(dest) if E.does_asset_exist(dest) else E.duplicate_asset(path.split('.')[0],dest)
 assert m
 if isinstance(m,u.MaterialInstanceConstant):
  prop(m,'parent',local_material(source.get_editor_property('parent')))
 else:
  prop(m,'used_with_instanced_static_meshes',True);prop(m,'used_with_nanite',True)
  if 'glass' in name.lower():prop(m,'blend_mode',u.BlendMode.BLEND_OPAQUE)
  errors=M.recompile_material(m);assert not errors,(name,list(errors))
 save(m);matcache[path]=m;return m

def model(kit,name):
 info=catalog[kit][name];path=info['path']
 if path not in cache:
  # 原包材质没有实例化/Nanite 标记：复制到新关卡目录修复，不覆盖小巷等地图共享版本。
  dest=DST+'/Meshes/'+kit+'_'+name
  mesh=u.load_asset(dest) if E.does_asset_exist(dest) else E.duplicate_asset(path.split('.')[0],dest)
  assert mesh;slots=list(mesh.get_editor_property('static_materials'))
  source=asset(path)
  for i,s in enumerate(slots):
   original=source.get_editor_property('static_materials')[i].material_interface
   if original:prop(s,'material_interface',local_material(original));slots[i]=s
  prop(mesh,'static_materials',slots)
  body=mesh.get_editor_property('body_setup');prop(body,'double_sided_geometry',True)
  save(mesh);cache[path]=mesh
 used.add(kit);return cache[path],info

def anchor(info):
 lo,hi=info['min'],info['max'];return ((lo[0]+hi[0])/2,0 if abs(lo[1])<.001 else (lo[1]+hi[1])/2,lo[2])

def rotate(p,yaw,roll=0):
 # UE 的正 Roll 把局部 Z 转向 +Y；统一变换原作者摆在展示网格上的偏移。
 a,b=math.radians(yaw),math.radians(roll);x,y,z=p;y,z=y*math.cos(b)+z*math.sin(b),-y*math.sin(b)+z*math.cos(b)
 return (x*math.cos(a)-y*math.sin(a),x*math.sin(a)+y*math.cos(a),z)

def put(kit,name,position,yaw=0,scale=(1,1,1),origin=None,roll=0,collide=True,mat=None):
 mesh,info=model(kit,name);origin=origin if origin is not None else anchor(info)
 offset=rotate(tuple(origin[i]*scale[i] for i in range(3)),yaw,roll)
 loc=[position[i]-offset[i] for i in range(3)]
 key=(info['path'],collide,mat.get_path_name() if mat else '')
 if key not in groups:groups[key]={'mesh':mesh,'mat':mat,'transforms':[]}
 groups[key]['transforms'].append(u.Transform(V(*loc),u.Rotator(yaw=yaw,roll=roll),u.Vector(*scale)))

def apt(name,pos,yaw=0,scale=(1,1,1),origin=None,roll=0,collide=True,mat=None):
 put(APT,name,pos,yaw,scale,origin,roll,collide,mat)

def prop_set(kit,pos,yaw=0,scale=(1,1,1),collide=False):
 # 多部件成品共用作者原点，盆土、叶片、车轮不能分别落地归零。
 for name in catalog[kit]:put(kit,name,pos,yaw,scale,origin=(0,0,0),collide=collide)

def facade(cx,cy,yaw,bays,floors,shops=False,variant=0,collide=True):
 wallmat=wallmats[variant%len(wallmats)]
 def world(x,y,z):
  q=rotate((x,y,z),yaw);return(cx+q[0],cy+q[1],.16+q[2])
 for j in range(bays):
  x=(j+.5-bays/2)*3
  for f in range(floors):
   if f==0 and shops:
    shopstyle=(variant+j//2)%3
    wall='wall_door_centered_large_01' if shopstyle==0 else 'wall_door_window_small_01';origin=anchor(catalog[APT][wall])
    apt(wall,world(x,0,0),yaw,origin=origin,collide=collide,mat=wallmat)
    # 三种店面交错：关门卷帘、作者玻璃门、卷帘升起后的玻璃门；避免整街像停业。
    if shopstyle==0:
     put('rollershutter_door','rollershutter_door',world(x,-.06,0),yaw+180,scale=(1.88,1,1.16),collide=collide)
    else:
     apt('door_window_small_01',world(x,0,0),yaw,origin=origin,collide=collide)
     if shopstyle==2:put('rollershutter_door','rollershutter_door',world(x,-.08,2.18),yaw+180,scale=(2.45,1,.23),collide=collide)
   else:
    win=('window_centered_large_03','window_centered_double_03','window_centered_small_03')[(variant+j//3)%3]
    wall='wall_'+win;origin=anchor(catalog[APT][wall])
    apt(wall,world(x,0,f*3),yaw,origin=origin,collide=collide,mat=wallmat)
    apt(win,world(x,0,f*3),yaw,origin=origin,collide=collide)
  # 平直檐口与矮女儿墙，不使用带明显欧式装饰的冠饰。
  apt('cornice_standard_standard_01',world(x,0,3*floors-.2),yaw,collide=collide)
  apt('wall_standard_standard_01',world(x,0,3*floors),yaw,scale=(1,1,.18),collide=collide,mat=wallmat)

def wall_material(i,tint):
 m=material('WeatheredWall'+str(i));prop(m,'used_with_nanite',True);prop(m,'two_sided',True)
 root='/Game/Env/PolyHaven/modular_urban_apartments_facade/modular_urban_apartments_facade_2k/Textures/modular_urban_apartments_facade_plaster_'
 c=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'diff'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
 c=custom(m,'return lerp(C,dot(C,float3(.299,.587,.114)).xxx,.82)*Tint;',[('C',c),('Tint',vec(m,tint))]);output(c,u.MaterialProperty.MP_BASE_COLOR)
 n=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'nor_gl'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL);output(n,u.MaterialProperty.MP_NORMAL)
 output(val(m,.87),u.MaterialProperty.MP_ROUGHNESS);return finish(m)

def dry_material(name,source):
 p=DST+'/Materials/'+name
 m=u.load_asset(p) if E.does_asset_exist(p) else E.duplicate_asset('/Game/Env/Materials/'+source,p)
 assert m
 prop(m,'parent',local_material(asset('/Game/Env/Materials/'+source).get_editor_property('parent')))
 M.set_material_instance_scalar_parameter_value(m,'Wetness',0)
 M.set_material_instance_scalar_parameter_value(m,'PuddleAmount',0)
 M.set_material_instance_scalar_parameter_value(m,'TileSize',400)
 M.set_material_instance_vector_parameter_value(m,'Tint',u.LinearColor(.9,.88,.82,1));save(m);return m

def road_material():
 # 单独使用现成沥青贴图；干燥街面不继承其它场景的水洼分支。
 m=material('StreetAsphalt');prop(m,'used_with_nanite',True);prop(m,'tangent_space_normal',False)
 p=node(m,u.MaterialExpressionWorldPosition);uv=custom(m,'return P.xy/350.;',[('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT2)
 root='/Game/Env/Textures/asphalt_02/T_asphalt_02_'
 c=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'diff_2k'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR);link(uv,c,'UVs')
 c=custom(m,'return lerp(C,dot(C,float3(.2126,.7152,.0722)).xxx,.3)*.85;',[('C',c)]);output(c,u.MaterialProperty.MP_BASE_COLOR)
 n=node(m,u.MaterialExpressionTextureSample,texture=asset(root+'nor_gl_2k'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL);link(uv,n,'UVs')
 output(custom(m,'return normalize(float3(N.xy*.3,N.z));',[('N',n)]),u.MaterialProperty.MP_NORMAL)
 output(val(m,.93),u.MaterialProperty.MP_ROUGHNESS);output(val(m,.12),u.MaterialProperty.MP_SPECULAR)
 return finish(m)

try:
 stamp=datetime.datetime.now().strftime('%Y%m%d_%H%M%S');backup=R/'Saved/Backup'/('StreetExisting_'+stamp)
 for rel in ['Content/Maps/L_MillenniumStreet.umap','Content/Env/MillenniumStreetExisting']:
  p=R/rel
  if p.exists():
   dest=backup/rel;dest.parent.mkdir(parents=True,exist_ok=True)
   shutil.copytree(p,dest) if p.is_dir() else shutil.copy2(p,dest)
 old=R/'Content/Maps/L_OldStreet.umap';original_hash=hashlib.sha256(old.read_bytes()).hexdigest()
 REPORT['backup']=str(backup)
 road=road_material();paving=dry_material('MI_DryPaving','MI_ConcreteFloor')
 wallmats=[wall_material(i,t) for i,t in enumerate([(1.15,1.17,1.15),(1.45,1.37,1.18),(.95,1.12,1.04),(1.22,1.05,.92)])]
 signs=[]
 for i in range(8):
  m=material('ShopSign'+str(i));prop(m,'two_sided',True)
  uv=node(m,u.MaterialExpressionTextureCoordinate)
  uv=custom(m,'return float2(1-U.x,(1-U.y)*.125+Row*.125);',[('U',uv),('Row',val(m,i))],u.CustomMaterialOutputType.CMOT_FLOAT2)
  tx=node(m,u.MaterialExpressionTextureSample,texture=asset('/Game/Env/OriginalDeveloperMaps/Textures/T_OS2_Signs'),sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR);link(uv,tx,'UVs')
  output(tx,u.MaterialProperty.MP_BASE_COLOR);output(val(m,.85),u.MaterialProperty.MP_ROUGHNESS);signs.append(finish(m))
 world=u.EditorLoadingAndSavingUtils.load_map(MAP);assert world;expected=marks()
 removed=[]
 for a in list(S.get_all_level_actors()):
  n=a.get_actor_label()
  if n.startswith('OSE_') or (n.startswith('OS2_') and not n.endswith('_Ground')):
   removed.append(n);S.destroy_actor(a)
  elif n.startswith('OS2_') and n.endswith('_Ground'):
   for c in a.get_components_by_class(u.StaticMeshComponent):
    for j,s in enumerate(c.static_mesh.static_materials):
     slot=str(s.get_editor_property('imported_material_slot_name'))
     if slot in ('Road','Concrete'):c.set_material(j,road if slot=='Road' else paving)
 # 每个周期的布局完全相同，避免瞬移后窗户、店招或设施发生变化。
 buildings=[];segs=[(-41,6,3),(-20,8,4),(16,6,3),(38,8,5)]
 for period in range(-5,6):
  collide=abs(period)<=1
  for side in (1,-1):
   yaw=0 if side==1 else 180
   for bi,(cx,bays,floors) in enumerate(segs):
    cx+=period*100;floors=max(2,floors-(1 if side==-1 and bi%2==0 else 0));width=bays*3;depth=9
    front=side*8.5;back=side*(8.5+depth);cy=(front+back)/2
    facade(cx,front,yaw,bays,floors,True,bi+(side<0),collide)
    facade(cx,back,yaw+180,bays,floors,False,bi,collide)
    facade(cx-width/2,cy,-90,3,floors,False,bi,collide)
    facade(cx+width/2,cy,90,3,floors,False,bi,collide)
    # 用作者的完整墙模块横放作屋面，保留 UV；不新建顶点或拉出程序楼体。
    for ix in range(bays):
     for iy in range(3):
      apt('wall_standard_standard_01',(cx-width/2+(ix+.5)*3,cy-depth/2+iy*3,3*floors+.16),0,roll=90,collide=collide,mat=wallmats[0])
    buildings.append({'x':cx,'y':cy,'floors':floors,'period':period})
    # 落水管、空调按楼层挂设，所有装饰留在原有可走区域外。
    for z in range(3*floors):
     put('modular_metal_gutter','modular_metal_gutter_section',(cx-width/2+.25,front-side*.16,z+.16),yaw,collide=False)
    for j in (1,4):
     for f in range(1,floors):
      put('exterior_aircon_unit','exterior_aircon_unit_rusted' if (bi+j+f)%3==0 else 'exterior_aircon_unit',(cx-width/2+1.5+j*3+.9,front-side*.36,3*f+.5),yaw+180,collide=False)
    # 店招沿用已有自拟中文图集，基础平面仅承载平面招牌，不作建筑替身。
    plane=asset('/Engine/BasicShapes/Plane')
    shopcount=bays//2;shopwidth=width/shopcount
    for sj in range(shopcount):
     si=(bi*3+sj+(4 if side<0 else 0))%8
     a=mesh_actor('OSE_Sign_'+str(period)+'_'+str(side)+'_'+str(bi)+'_'+str(sj),plane,cx-width/2+shopwidth*(sj+.5),front-side*.08,2.93,yaw,(shopwidth-.3,.65,1),False)
     a.set_actor_rotation(u.Rotator(yaw=yaw,roll=-90),False);a.static_mesh_component.set_material(0,signs[si])
     sx=cx-width/2+shopwidth*(sj+.5)
     if si==6:
      # 面馆门口的矮桌和板凳沿人行道横排，主路中央留空。
      prop_set('small_wooden_table_01',(sx,side*7.88,.16),yaw,scale=(1.15,1.1,1.2))
      prop_set('folding_wooden_stool',(sx-.87,side*7.91,.16),yaw+12)
      prop_set('plastic_monobloc_chair_01',(sx+.95,side*7.94,.16),yaw+90)
     elif si in (0,4):
      for q in range(3 if si==4 else 2):prop_set('plastic_crate_02',(sx-.65+q*.56,side*8.06,.16),yaw+(q%2)*5)
      prop_set('plastic_crate_02',(sx-.65,side*8.06,.414),yaw+3)
     elif si in (1,3,7):
      prop_set('potted_plant_02',(sx-1.85,side*8.05,.16),yaw+si*17)
      if si==3:prop_set('plastic_monobloc_chair_01',(sx+1.7,side*7.97,.16),yaw+10)
   for x in (-24,24):
    px=x+period*100;py=side*7.9
    # 预设电杆的所有子件使用同一原点，不能逐件居中，否则会把横担和绝缘子散开。
    for n in catalog['modular_electricity_poles']:
     if n.startswith('preset_02_'):put('modular_electricity_poles',n,(px,py,.16),yaw,origin=(-4.5,0,0),collide=False)
    put('utility_box_01','utility_box_01_2k',(px+1.1,side*8.05,.16),yaw,collide=False)
    put('street_lamp_01','street_lamp_01_2k',(px-4.5,py,.16),yaw,collide=False)
   # 后排住楼同样使用现成模块；保留中间横街，补足街口侧望时的空间层次。
   for bx in (-29,29):
    bx+=period*100;front=side*25;back=side*34;by=(front+back)/2;floors=3 if side==1 else 4
    facade(bx,front,yaw,8,floors,False,1,False)
    facade(bx,back,yaw+180,8,floors,False,0,False)
    facade(bx-12,by,-90,3,floors,False,1,False)
    facade(bx+12,by,90,3,floors,False,1,False)
    for ix in range(8):
     for iy in range(3):apt('wall_standard_standard_01',(bx-12+(ix+.5)*3,by-4.5+iy*3,floors*3+.16),roll=90,collide=False,mat=wallmats[0])
    buildings.append({'x':bx,'y':by,'floors':floors,'period':period,'background':True})
  for x in (-32,17):
   for n in catalog['water_manhole_cover']:put('water_manhole_cover',n,(x+period*100,1.2,0),origin=(0,0,0),collide=False)
  # 少量停放车辆和修理杂物形成街道尺度；两侧最内缘约 ±4.9 米，中央通道不收窄。
  for x,side in ((-12,1),(32,-1)):
   prop_set('covered_car',(x+period*100,side*5.85,0),90,collide=collide)
   put('old_tyre','old_tyre_2k',(x+period*100+2.7,side*7.85,.16),15,collide=False)
  # 复用工程已有悬垂电缆成品，不重新生成曲线；跨街线的端点落在电杆横担上。
  cable=asset('/Game/Env/Cables/cable_single_thin');cablemat=asset('/Game/Env/Cables/MI_Cable')
  for x in (-24,24):
   for dx in (-.45,.45):
    a=mesh_actor('OSE_Cable_'+str(period)+'_'+str(x)+'_'+str(dx),cable,x+period*100+dx,-7.9,5.76,90,(15.8/18,1,.35),False)
    a.static_mesh_component.set_material(0,cablemat)
 # 共享模型和材质不写回，只在新关卡内存放 HISM 实例。
 total=0
 for i,((path,collide,matpath),g) in enumerate(groups.items()):
  a=instances('OSE_Instances_'+str(i)+'_'+g['mesh'].get_name(),g['mesh'],g['transforms'],collide)
  for c in a.get_components_by_class(u.MeshComponent):
   if g['mat']:
    for j in range(c.get_num_materials()):
     if c.get_material(j) and 'plaster' in c.get_material(j).get_name():c.set_material(j,g['mat'])
  total+=len(g['transforms'])
 assert marks()==expected,'功能标记变化'
 assert not any('/Game/Env/OldStreet/' in p or '/Meshes/SM_OS2_Building' in p for a in S.get_all_level_actors() for p in visual_refs(a))
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 assert hashlib.sha256(old.read_bytes()).hexdigest()==original_hash
 source_instances={kit:sum(len(g['transforms']) for key,g in groups.items() if key[0] in {v['path'] for v in catalog[kit].values()}) for kit in used}
 REPORT.update(passed=True,buildings=len(buildings),background_buildings=sum(b.get('background',False) for b in buildings),instances=total,source_instances=source_instances,groups=len(groups),preserved_markers=len(expected),original_map_unchanged=True,sources=sorted(used),removed_actors=len(removed),period_m=100,shared_assets_written=False,shopfront_styles=3,life_props=True,central_clear_width_m=9.8)
except Exception:
 REPORT['error']=traceback.format_exc();u.log_error(REPORT['error']);raise
finally:
 (R/'Saved/Automation/StreetExistingBuild.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
