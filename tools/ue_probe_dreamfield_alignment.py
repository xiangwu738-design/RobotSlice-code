import unreal as u,json,sys,math
sys.path.insert(0,'D:/RobotSlice/tools');import dreamfield_terrain as terrain
u.EditorLoadingAndSavingUtils.load_map('/Game/Maps/L_Dreamfield')
E=u.EditorAssetLibrary;P=u.ProceduralMeshLibrary;r={}
for name in ('SM_DO_GroundNear','SM_DO_Grass0'):
 mesh=u.load_asset('/Game/Env/DreamfieldOriginal/Meshes/'+name)
 vs,ts,ns,uv,t=P.get_section_from_static_mesh(mesh,0,0)
 r[name]={'bounds':str(mesh.get_bounding_box()),'sample':[{'p':str(vs[i]),'uv':str(uv[i]),'n':str(ns[i])} for i in range(min(8,len(vs)))]}
 if 'Ground' in name:
  errors=[abs(v.z/100-terrain.height(v.x/100,v.y/100)) for v in vs]
  r[name]['height_error_max']=max(errors);r[name]['height_error_mean']=sum(errors)/len(errors)
rig=next(a for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='DO_DreamSky_ALS')
r['cloud']={k:str(rig.clouds.get_editor_property(k)) for k in ['material','visible','layer_bottom_altitude','layer_height']}
r['cloud_parent']=str(rig.cloud_material.parent)
open('D:/RobotSlice/Saved/Automation/DreamfieldAlignment.json','w').write(json.dumps(r,indent=2))
