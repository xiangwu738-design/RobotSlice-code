"""用 BlenderGIS 导入 GeoTIFF 高程（只在本次会话里从 Inbox/_tools 加载插件，不改用户的 Blender 配置）"""
import os, sys, traceback
os.environ["IMAGEIO_NO_INTERNET"] = "1"
import bpy, addon_utils
SRC = r"D:\RobotSlice\Inbox\_tools\BlenderGIS\src"
TIF = sys.argv[sys.argv.index("--") + 1]
sd = bpy.context.preferences.filepaths.script_directories.new()
sd.directory = SRC
sd.name = "bgis_tools"
bpy.utils.refresh_script_paths()
addon_utils.modules_refresh()
mod = addon_utils.enable("BlenderGIS", default_set=True, handle_error=lambda e: traceback.print_exc())
print("ENABLED:", mod is not None, [a.module for a in bpy.context.preferences.addons])
print("ops:", [o for o in dir(bpy.ops.importgis) if not o.startswith('_')])
bpy.ops.wm.read_factory_settings(use_empty=True) if False else None
scn = bpy.context.scene
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
from BlenderGIS.geoscene import GeoScene
geo = GeoScene(scn)
geo.crs = "EPSG:26912"
try:
    r = bpy.ops.importgis.georaster('EXEC_DEFAULT', filepath=TIF, importMode='DEM_RAW', rastCRS='EPSG:26912', reprojection=False)
    print("DEM_RAW:", r)
except Exception:
    traceback.print_exc()
    try:
        r = bpy.ops.importgis.georaster('EXEC_DEFAULT', filepath=TIF, importMode='DEM', rastCRS='EPSG:26912', reprojection=False, subdivision='mesh', demInterpolation=True)
        print("DEM:", r)
    except Exception:
        traceback.print_exc()
for o in bpy.data.objects:
    print("OBJ", o.name, o.type, tuple(round(v, 1) for v in o.dimensions), len(o.data.vertices) if o.type == 'MESH' else '', [m.type for m in o.modifiers])
print("georef:", geo.isGeoref, geo.crs, geo.getOriginPrj() if geo.isGeoref else None)
out = sys.argv[sys.argv.index("--") + 2]
bpy.ops.wm.save_as_mainfile(filepath=out)
