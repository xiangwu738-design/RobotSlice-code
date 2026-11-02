"""按 UTM 中心直接下载（USGS 3DEP，公开无密钥）：py -3.11 fetch_dem_utm.py 名字 epsg cx cy 宽 高 分辨率 输出目录"""
import sys, os, json, urllib.request
import numpy as np
from PIL import Image
name, epsg, cx, cy, w, h, res, out = sys.argv[1], int(sys.argv[2]), *map(float, sys.argv[3:8]), sys.argv[8]
os.makedirs(out, exist_ok=True)
nx, ny = int(round(w / res)), int(round(h / res))
bbox = f"{cx - w / 2:.2f},{cy - h / 2:.2f},{cx + w / 2:.2f},{cy + h / 2:.2f}"
url = ("https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage?"
       f"bbox={bbox}&bboxSR={epsg}&imageSR={epsg}&size={nx},{ny}&format=tiff&pixelType=F32&noData=-9999"
       "&interpolation=RSP_BilinearInterpolation&compression=None&f=image")
tif = os.path.join(out, f"{name}.tif")
for attempt in range(4):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "RobotSlice-canyon/1.0"})
        with urllib.request.urlopen(req, timeout=240) as r:
            data = r.read()
        open(tif, "wb").write(data)
        break
    except Exception as e:
        print("retry", attempt, e)
z = np.array(Image.open(tif), dtype=np.float32)
meta = dict(name=name, epsg=epsg, utm_center=[cx, cy], width_m=w, height_m=h, res_m=res, shape=list(z.shape),
            zmin=float(z[z > -1000].min()), zmax=float(z.max()), source="USGS 3DEP ImageServer exportImage (public, no key)", url=url)
json.dump(meta, open(os.path.join(out, f"{name}.json"), "w"), indent=1)
print(json.dumps({k: meta[k] for k in ("name", "shape", "zmin", "zmax")}))
