"""已经导入全部资源后，只重建地图；公用实现仍以主脚本为准。"""
from pathlib import Path
script=Path('D:/RobotSlice/tools/ue_build_original_developer_maps.py').read_text(encoding='utf8')
prefix=script.split('\ntry:\n # 所有改写目标')[0]
exec(compile(prefix,'original_map_definitions','exec'))
originals={n:hashlib.sha256((R/f'Content/Maps/L_{n}.umap').read_bytes()).hexdigest() for n in ['Moon','OldStreet']}
manifest=json.loads((SRC/'manifest.json').read_text(encoding='utf8'))
meshes={name:asset(DST+'/Meshes/'+name) for name in manifest['meshes']}
REPORT['meshes']={name:mesh.get_num_triangles(0) for name,mesh in meshes.items()}
tail=script.split(' # 月球：只换基地建筑；')[1]
exec(compile('try:\n # 月球：只换基地建筑；'+tail,'original_map_scene','exec'))
