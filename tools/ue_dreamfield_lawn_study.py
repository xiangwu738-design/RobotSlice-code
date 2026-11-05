"""保留下载草叶几何，独立验证接光、地表配色及沉降淡出，不写开发图。"""
from pathlib import Path
import unreal as u,os,json,shutil,datetime,traceback
R=Path('D:/RobotSlice')
exec(compile((R/'tools/ue_dreamfield_free_assets.py').read_text(encoding='utf8').split('\nbackup=R/')[0],'lawn_helpers','exec'))
FULL=os.environ.get('DF_LAWN_FULL','0')=='1'
DST='/Game/Env/DreamfieldLawn' if FULL else '/Game/Env/DreamfieldLawnStudy';MAP='/Game/Maps/L_Dreamfield' if FULL else '/Game/Maps/L_Dreamfield_LawnStudy'
REPORT={'passed':False,'map':MAP,'full':FULL,'materials':[]}
TEX='/Game/Env/DreamfieldFree/Textures/'

def lawn(name,blades=False):
 m=material(name);prop(m,'tangent_space_normal',False);prop(m,'used_with_nanite',True)
 p=node(m,u.MaterialExpressionWorldPosition)
 uv=custom(m,'return P.xy/400.;',[('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT2)
 g=sample(m,asset(TEX+'T_Free_Ground_Diffuse'),uv)
 macro_uv=custom(m,'return P.xy/2700.+float2(.31,.67);',[('P',p)],u.CustomMaterialOutputType.CMOT_FLOAT2)
 macro=sample(m,asset(TEX+'T_Free_Ground_Diffuse'),macro_uv)
 # 统一底色；现成草坪照片只提供小幅细节，避免地表和叶片各自一套明暗。
 base=custom(m,'float detail=clamp(dot(G,float3(.2126,.7152,.0722))/.12,.6,1.4);float broad=clamp(dot(B,float3(.2126,.7152,.0722))/.12,.6,1.4);return float3(.12,.205,.055)*(1+(detail-1)*.22+(broad-1)*.25);',[('G',g),('B',macro)])
 if blades:
  uvleaf=node(m,u.MaterialExpressionTextureCoordinate)
  atlas=sample(m,asset(TEX+'T_Free_Grass_Diffuse'),uvleaf)
  base=custom(m,'float leaf=clamp(dot(A,float3(.2126,.7152,.0722))/.124,.6,1.4);return C*(1+(leaf-1)*.12);',[('C',base),('A',atlas)])
  prop(m,'two_sided',True);prop(m,'blend_mode',u.BlendMode.BLEND_MASKED);prop(m,'opacity_mask_clip_value',.35)
  prop(m,'shading_model',u.MaterialShadingModel.MSM_DEFAULT_LIT)
  output(sample(m,asset(TEX+'T_Free_Grass_Alpha'),uvleaf,linear=True),u.MaterialProperty.MP_OPACITY_MASK)
  # 世界空间法线直接朝上；再乘双面符号会把一半草叶变成朝下的黑面。
  normal=vec(m,(0,0,1))
  camera=node(m,u.MaterialExpressionCameraPositionWS)
  wind=custom(m,'float d=distance(P,Camera);float f=smoothstep(3800.,7600.,d);return float3(sin(T*.8+P.x*.005+P.y*.007)*.4*(1-f),0,-28*f);',[('P',p),('Camera',camera),('T',node(m,u.MaterialExpressionTime))])
  output(wind,u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 else:
  n=sample(m,asset(TEX+'T_Free_Ground_nor_dx'),uv,normal=True)
  normal=custom(m,'return normalize(float3(N.xy*.12,max(N.z,.7)));',[('N',n)])
 output(base,u.MaterialProperty.MP_BASE_COLOR);output(normal,u.MaterialProperty.MP_NORMAL)
 output(val(m,.93),u.MaterialProperty.MP_ROUGHNESS);output(val(m,.02),u.MaterialProperty.MP_SPECULAR)
 output(custom(m,'return C*.08;',[('C',base)]),u.MaterialProperty.MP_EMISSIVE_COLOR)
 return finish(m)

try:
 backup=R/'Saved/Backup'/('DreamfieldLawn_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S'));backup.mkdir(parents=True)
 for rel in ['Content/Maps/'+MAP.split('/')[-1]+'.umap','Content/'+DST.removeprefix('/Game/')]:
  src=R/rel
  if src.exists():
   dest=backup/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copytree(src,dest) if src.is_dir() else shutil.copy2(src,dest)
 grass=lawn('LawnBlades',True);ground=lawn('LawnGround')
 world=u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Dreamfield');expected=functional();count=0;ground_count=0
 for a in S.get_all_level_actors():
  if a.get_actor_label().startswith('Free_SM_Free_BermudaPatch'):
   for c in a.get_components_by_class(u.HierarchicalInstancedStaticMeshComponent):
    for i in range(c.get_num_materials()):c.set_material(i,grass)
    count+=c.get_instance_count()
  elif a.get_actor_label() in ('DO_GroundNear','DO_GroundFar'):
   for c in a.get_components_by_class(u.MeshComponent):
    for i in range(c.get_num_materials()):c.set_material(i,ground)
    ground_count+=1
 assert count==611564 and ground_count==2,(count,ground_count)
 assert functional()==expected
 assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
 REPORT.update(passed=True,grass_instances=count,ground_components=ground_count,preserved_functional=expected,backup=str(backup))
except Exception:REPORT['error']=traceback.format_exc();raise
finally:(R/'Saved/Automation'/('DreamfieldLawn.json' if FULL else 'DreamfieldLawnStudy.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
