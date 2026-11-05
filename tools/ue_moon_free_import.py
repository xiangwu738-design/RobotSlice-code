"""导入 Quaternius CC0 标准版的现成模块；保留原几何和 UV，材质统一为白灰蓝。"""
import unreal as u,json,traceback,datetime,shutil
from pathlib import Path
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_build_original_developer_maps.py').read_text(encoding='utf8').split('\ntry:\n # 所有改写目标')[0],'moon_free_helpers','exec'))
SRC=R/'SourceAssets/moon_free/quaternius_scifi/Modular SciFi MegaKit[Standard]';DST='/Game/Env/MoonFree'
REPORT={'passed':False,'materials':[],'meshes':{}}
NAMES=['WallAstra_Straight','WallAstra_Straight_Flat','WallAstra_Straight_Window','WallAstra_Corner_Round_Outer','WallWindow_Straight','TopAstra_Straight','TopAstra_Curve_Round_Outer','BottomMetal_Straight','Platform_Metal2','Platform_Squares','Platform_Stairs_4Wide','Platform_Ramp_4Wide','Door_Frame_SquareTall','Door_Frame_Square','Column_Astra','Column_Round','Prop_Barrel_Large','Prop_Vent_Wide','Prop_Vent_Big','Prop_Fan_Small','Prop_Light_Wide','Prop_Light_Corner','Prop_Computer','Prop_AccessPoint','Prop_Crate3','Prop_Crate4','Prop_Rail_4','Prop_Rail_2','Prop_Cable_3','ShortWall_WhitePlate2_Straight']
NAMES+=['Platform_Metal2_Curve']
NAMES+=['Column_Large_Straight','Column_Pipes','Column_Hollow','Prop_Light_Floor','Prop_Chest']

def texture(p):
 dest=DST+'/Textures/'+p.stem
 if not E.does_asset_exist(dest):
  task=u.AssetImportTask();task.filename=str(p);task.destination_path=DST+'/Textures';task.automated=True;task.save=False;T.import_asset_tasks([task])
 a=asset(dest);normal='_Normal' in p.stem;linear=normal or '_ORM' in p.stem or 'Mask' in p.stem
 prop(a,'srgb',not linear);prop(a,'compression_settings',u.TextureCompressionSettings.TC_NORMALMAP if normal else u.TextureCompressionSettings.TC_MASKS if linear else u.TextureCompressionSettings.TC_DEFAULT)
 if normal:prop(a,'flip_green_channel',True)
 save(a);return a

def sample(m,t,normal=False,linear=False):
 return node(m,u.MaterialExpressionTextureSample,texture=t,sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL if normal else u.MaterialSamplerType.SAMPLERTYPE_MASKS if linear else u.MaterialSamplerType.SAMPLERTYPE_COLOR)

try:
 dest=R/'Content/Env/MoonFree'
 if dest.exists():
  backup=R/'Saved/Backup'/('MoonFreeImport_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));shutil.copytree(dest,backup/'MoonFree')
 textures={p.stem:texture(p) for p in (SRC/'Textures').rglob('*.png')}
 sources={name:next((SRC/'FBX').rglob(name+'.fbx')) for name in NAMES};material_names=set()
 for name in NAMES:
  data=json.loads(next((SRC/'glTF').rglob(name+'.gltf')).read_text(encoding='utf8'));material_names.update(m['name'] for m in data['materials'])
 mats={}
 for name in sorted(material_names):
  m=material(name);prop(m,'used_with_nanite',True);prop(m,'two_sided',True)
  if name in ('M_Light','M_LightFade_Red'):
   output(vec(m,(.035,.65,2.4)),u.MaterialProperty.MP_EMISSIVE_COLOR);output(vec(m,(.08,.35,.5)),u.MaterialProperty.MP_BASE_COLOR);output(val(m,.4),u.MaterialProperty.MP_ROUGHNESS)
  elif name in ('M_Black','M_Glass'):
   output(vec(m,(.015,.045,.065) if name=='M_Glass' else (.012,.015,.018)),u.MaterialProperty.MP_BASE_COLOR);output(val(m,.26 if name=='M_Glass' else .65),u.MaterialProperty.MP_ROUGHNESS);output(val(m,.45),u.MaterialProperty.MP_METALLIC)
  elif 'Decal' in name:
   prop(m,'blend_mode',u.BlendMode.BLEND_MASKED);tx=sample(m,textures['T_Decals']);output(tx,u.MaterialProperty.MP_BASE_COLOR);assert M.connect_material_property(tx,'A',u.MaterialProperty.MP_OPACITY_MASK);output(val(m,.7),u.MaterialProperty.MP_ROUGHNESS)
  else:
   prefix='T_PaddedWall' if 'Padded' in name else 'T_Trim_01' if 'Trim_01' in name else 'T_Trim_02' if 'Trim_02' in name else 'T_Trim_03'
   color=prefix+'_Cables' if prefix=='T_Trim_03' and ('Cables' in name or 'Dark' in name) else prefix+'_BaseColor'
   assert color in textures,(name,color)
   c=sample(m,textures[color]);c=custom(m,'return saturate(C*1.2);',[('C',c)]);output(c,u.MaterialProperty.MP_BASE_COLOR)
   n=sample(m,textures[prefix+'_Normal'],True);output(n,u.MaterialProperty.MP_NORMAL)
   orm=sample(m,textures[prefix+'_ORM'],linear=True)
   # 作者默认用于室内裸金属；月球黑色天空几乎没有环境反射，改为浅色隔热漆而非黑镜面。
   assert M.connect_material_property(orm,'R',u.MaterialProperty.MP_AMBIENT_OCCLUSION)
   rough=custom(m,'return max(O.g,.62);',[('O',orm)],u.CustomMaterialOutputType.CMOT_FLOAT1);output(rough,u.MaterialProperty.MP_ROUGHNESS)
   metal=custom(m,'return O.b*.15;',[('O',orm)],u.CustomMaterialOutputType.CMOT_FLOAT1);output(metal,u.MaterialProperty.MP_METALLIC)
   if prefix=='T_Trim_01':
    em=sample(m,textures['T_Trim_01_Emissive']);em=custom(m,'return max(E.r,max(E.g,E.b))*float3(.03,.65,2.4);',[('E',em)]);output(em,u.MaterialProperty.MP_EMISSIVE_COLOR)
  mats[name]=finish(m)
 for name,path in sources.items():
  ui=u.FbxImportUI()
  for k,v in [('import_mesh',True),('import_as_skeletal',False),('import_materials',False),('import_textures',False),('import_animations',False),('mesh_type_to_import',u.FBXImportType.FBXIT_STATIC_MESH)]:prop(ui,k,v)
  imp=ui.get_editor_property('static_mesh_import_data')
  for k,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('build_nanite',True),('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)]:prop(imp,k,v)
  task=u.AssetImportTask();task.filename=str(path);task.destination_path=DST+'/Meshes';task.destination_name='SM_'+name;task.automated=True;task.replace_existing=True;task.save=False;task.options=ui;T.import_asset_tasks([task]);mesh=asset(DST+'/Meshes/SM_'+name)
  slots=list(mesh.get_editor_property('static_materials'))
  for i,s in enumerate(slots):
   slot=str(s.get_editor_property('imported_material_slot_name'));assert slot in mats,(name,slot);prop(s,'material_interface',mats[slot]);slots[i]=s
  prop(mesh,'static_materials',slots);body=mesh.get_editor_property('body_setup');prop(body,'collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);prop(body,'double_sided_geometry',True);save(mesh)
  b=mesh.get_bounding_box();REPORT['meshes'][name]={'path':mesh.get_path_name(),'min':[b.min.x/100,b.min.y/100,b.min.z/100],'max':[b.max.x/100,b.max.y/100,b.max.z/100],'triangles':mesh.get_num_triangles(0)}
 # 已有地板和地环仅去掉旧设备部件，其材质与 UV 保留；不重建功能地面几何。
 for name in ('RetainedFloor','RetainedRing'):
  task=u.AssetImportTask();task.filename=str(R/'SourceAssets/moon_free/retained'/(name+'.fbx'));task.destination_path=DST+'/Meshes';task.destination_name='SM_'+name;task.automated=True;task.replace_existing=True;task.save=False;task.options=ui;T.import_asset_tasks([task]);mesh=asset(DST+'/Meshes/SM_'+name)
  slots=list(mesh.static_materials)
  for i,s in enumerate(slots):
   slot=str(s.get_editor_property('imported_material_slot_name'));dest=DST+'/Materials/M_Retained_'+slot
   m=asset(dest) if E.does_asset_exist(dest) else E.duplicate_asset('/Game/Env/OriginalDeveloperMaps/Materials/M_'+slot,dest)
   prop(m,'used_with_nanite',True);prop(m,'used_with_instanced_static_meshes',True);M.recompile_material(m);save(m)
   prop(s,'material_interface',m);slots[i]=s
  prop(mesh,'static_materials',slots);body=mesh.get_editor_property('body_setup');prop(body,'collision_trace_flag',u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);prop(body,'double_sided_geometry',True);save(mesh)
  b=mesh.get_bounding_box();REPORT['meshes'][name]={'path':mesh.get_path_name(),'min':[b.min.x/100,b.min.y/100,b.min.z/100],'max':[b.max.x/100,b.max.y/100,b.max.z/100],'triangles':mesh.get_num_triangles(0)}
 REPORT['passed']=True
except Exception:
 REPORT['error']=traceback.format_exc();u.log_error(REPORT['error']);raise
finally:
 (R/'Saved/Automation/MoonFreeImport.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
