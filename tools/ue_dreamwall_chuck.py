"""在梦核巨墙屋面放置现成设备。以网格包围盒底面中心定位，保留作者材质。"""
from pathlib import Path
from datetime import datetime
import json
import math
import shutil
import unreal as u

R = Path("D:/RobotSlice")
helper = (R/"tools/ue_build_original_developer_maps.py").read_text(encoding="utf8").split("\ntry:\n # 所有改写目标")[0]
exec(compile(helper, "original_map_helpers", "exec"))
DST = "/Game/Env/DreamwallChuck"
SRC = R/"SourceAssets/dreamfield_free/chuck_industrial"
MAP = "/Game/Maps/L_Dreamfield"
REPORT = {"materials": [], "placements": []}
channels = {
    "BaseColor": (u.TextureCompressionSettings.TC_DEFAULT, u.MaterialSamplerType.SAMPLERTYPE_COLOR, u.MaterialProperty.MP_BASE_COLOR),
    "Normal_GL": (u.TextureCompressionSettings.TC_NORMALMAP, u.MaterialSamplerType.SAMPLERTYPE_NORMAL, u.MaterialProperty.MP_NORMAL),
    "Roughness": (u.TextureCompressionSettings.TC_MASKS, u.MaterialSamplerType.SAMPLERTYPE_MASKS, u.MaterialProperty.MP_ROUGHNESS),
    "Metallic": (u.TextureCompressionSettings.TC_MASKS, u.MaterialSamplerType.SAMPLERTYPE_MASKS, u.MaterialProperty.MP_METALLIC),
}

def imported(file, folder, name, options=None):
    dest = DST+"/"+folder
    if not E.does_asset_exist(dest+"/"+name):
        task = u.AssetImportTask()
        task.filename = str(file)
        task.destination_path = dest
        task.destination_name = name
        task.automated = True
        task.save = False
        if options:
            task.options = options
        T.import_asset_tasks([task])
    return asset(dest+"/"+name)

def prepare(key, info):
    m = material(key)
    for channel, file in info["textures"].items():
        path = SRC/file
        tex = imported(path, "Textures", path.stem)
        compression, sampler, field = channels[channel]
        prop(tex,"srgb",channel=="BaseColor")
        prop(tex,"compression_settings",compression)
        if channel=="Normal_GL":
            prop(tex,"flip_green_channel",True)
        save(tex)
        sample = node(m,u.MaterialExpressionTextureSample,texture=tex,sampler_type=sampler)
        output(sample,field)
    finish(m)
    ui = u.FbxImportUI()
    ui.import_materials = False
    ui.import_textures = False
    ui.import_as_skeletal = False
    data = ui.static_mesh_import_data
    data.auto_generate_collision = False
    data.build_nanite = False
    data.combine_meshes = True
    data.normal_import_method = u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    mesh = imported(SRC/(key+".fbx"),"Meshes","SM_"+key,ui)
    assert len(mesh.static_materials)==1
    mesh.set_material(0,m)
    save(mesh)
    bounds = mesh.get_bounding_box()
    height = (bounds.max.z-bounds.min.z)/100
    assert abs(height-info["dimensions_m"][2]) < .2, (key,height)
    return mesh

backup = R/"Saved/Backup"/("DreamwallChuck_"+datetime.now().strftime("%Y%m%d_%H%M%S"))
backup.mkdir(parents=True)
shutil.copy2(R/"Content/Maps/L_Dreamfield.umap",backup)
if (R/"Content/Env/DreamwallChuck").exists():
    shutil.copytree(R/"Content/Env/DreamwallChuck",backup/"DreamwallChuck")
manifest = json.loads((SRC/"source.json").read_text(encoding="utf8"))
meshes = {key:prepare(key, info) for key,info in manifest["parts"].items()}
world = u.EditorLoadingAndSavingUtils.load_map(MAP)
assert world
actors = S.get_all_level_actors()
assert any(a.get_actor_label().startswith("DB_") for a in actors)
for a in actors:
    if a.get_actor_label().startswith("ChuckRoof_"):
        S.destroy_actor(a)
# 主塔、辅塔与单机错开，屋面下方保留原封锁门和战斗平台。
layout = [(1,-455,0),(4,-425,90),(2,-68,90),(4,157,0),(1,365,90),(2,402,0),(4,872,90)]
for i,(part,y,yaw) in enumerate(layout):
    mesh = meshes["Structure"+str(part)]
    b = mesh.get_bounding_box()
    origin = ((b.min.x+b.max.x)/200,(b.min.y+b.max.y)/200,b.min.z/100)
    ang = math.radians(yaw)
    ox = origin[0]*math.cos(ang)-origin[1]*math.sin(ang)
    oy = origin[0]*math.sin(ang)+origin[1]*math.cos(ang)
    actor = mesh_actor("ChuckRoof_"+str(i),mesh,309.5-ox,y-oy,200.3-origin[2],yaw,collide=False)
    actor.set_folder_path("Dreamfield/Wall/RoofPlant")
    REPORT["placements"].append({"label":actor.get_actor_label(),"mesh":mesh.get_path_name(),"roof_y_m":y})
assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
reloaded = u.EditorLoadingAndSavingUtils.load_map(MAP)
checks = {a.get_actor_label():a for a in S.get_all_level_actors() if a.get_actor_label().startswith("ChuckRoof_")}
assert len(checks)==len(layout)
for placement in REPORT["placements"]:
    c = checks[placement["label"]].static_mesh_component
    assert c.static_mesh.get_path_name()==placement["mesh"]
    assert c.get_material(0).get_path_name().startswith(DST+"/Materials/")
    assert c.get_collision_enabled()==u.CollisionEnabled.NO_COLLISION
REPORT.update(passed=True,map=MAP,backup=str(backup))
(R/"Saved/Automation/DreamwallChuck.json").write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding="utf8")
