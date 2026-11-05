"""从用户领取的 Blender 文件取出三件完整设施和作者贴图。"""
import bpy
import hashlib
import json
from pathlib import Path
from mathutils import Vector

source = Path(bpy.data.filepath)
target = Path("D:/RobotSlice/SourceAssets/dreamfield_free/chuck_industrial")
target.mkdir(parents=True, exist_ok=True)
record = {"author": "Chuck_CG", "source": "https://chuckcg.gumroad.com/l/sqnwkv",
          "edition": "FREE (personal use and one commercial project)", "file": str(source),
          "sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "parts": {}}
for index in (1, 2, 4):
    obj = bpy.data.objects[f"structure {index}"]
    key = f"Structure{index}"
    images = {}
    for node in obj.material_slots[0].material.node_tree.nodes:
        if node.type != "TEX_IMAGE":
            continue
        image = node.image
        suffix = next(name for name in ("BaseColor", "Metallic", "Roughness", "Normal_GL") if name in image.name)
        path = target / f"T_{key}_{suffix}.jpg"
        path.write_bytes(image.packed_file.data)
        images[suffix] = path.name
    assert len(images) == 4
    corners = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    anchor = Vector([(max(p[i] for p in corners)+min(p[i] for p in corners))/2 for i in (0,1)] + [min(p.z for p in corners)])
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    obj.location -= anchor
    bpy.context.view_layer.update()
    bpy.ops.export_scene.fbx(filepath=str(target / (key+".fbx")), use_selection=True, object_types={"MESH"}, add_leaf_bones=False, mesh_smooth_type="FACE", path_mode="STRIP")
    record["parts"][key] = {"source_object": obj.name, "textures": images, "dimensions_m": list(obj.dimensions)}
(target/"source.json").write_text(json.dumps(record,ensure_ascii=False,indent=2), encoding="utf8")
