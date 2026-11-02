"""探测 BlenderGIS 能否在当前 Blender 里注册（只在本次会话内从 Inbox/_tools 加载，不装进用户配置）"""
import sys, traceback
import bpy
ADDONS = r"D:\RobotSlice\Inbox\_tools\BlenderGIS\src\addons"
sys.path.insert(0, ADDONS)
print("BLENDER", bpy.app.version_string, "PY", sys.version)
try:
    import BlenderGIS
    BlenderGIS.register()
    print("REGISTER OK")
    ops = [o for o in dir(bpy.ops.importgis)]
    print("importgis ops:", ops)
    print("view3d ops w/ map:", [o for o in dir(bpy.ops.view3d) if 'map' in o.lower()])
except Exception:
    traceback.print_exc()
    print("REGISTER FAIL")
