import unreal as u,json
paths=['/Game/CampusV21/Lighting/Materials/MI_CampusClouds','/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst']
r={}
for path in paths:
 a=u.load_asset(path);m=a
 while isinstance(m,u.MaterialInstanceConstant):m=m.get_editor_property('parent')
 r[path]={'parent':m.get_path_name(),'scalar':{str(n):u.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(a,n) for n in u.MaterialEditingLibrary.get_scalar_parameter_names(m)},'vector':{str(n):str(u.MaterialEditingLibrary.get_material_instance_vector_parameter_value(a,n)) for n in u.MaterialEditingLibrary.get_vector_parameter_names(m)}}
open('D:/RobotSlice/Saved/Automation/CloudParameters.json','w',encoding='utf-8').write(json.dumps(r,ensure_ascii=False,indent=2))
