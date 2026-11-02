"""GeoTIFF（USGS 3DEP，UTM 12N）→ ESRI ASCII Grid，给 BlenderGIS 的 importgis.asc_file 用。
可选降采样和高斯平滑（3DEP 在纳瓦霍保留地一侧只有 10 米数据，有方块台阶）。
py -3.11 dem_to_asc.py in.tif in.json out.asc 步长 平滑米"""
import sys, json
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter
tif, meta, out, step, sigma = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), float(sys.argv[5])
m = json.load(open(meta))
z = np.array(Image.open(tif), dtype=np.float32)
z[z < -1000] = np.nan
z = np.where(np.isnan(z), np.nanmin(z), z)
res = m['res_m']
if sigma > 0:
    z = gaussian_filter(z, sigma / res)
z = z[::step, ::step]
cs = res * step
cx, cy = m['utm_center']
nrows, ncols = z.shape
xll = cx - m['width_m'] / 2
yll = cy + m['height_m'] / 2 - nrows * cs
with open(out, 'w') as f:
    f.write(f"ncols {ncols}\nnrows {nrows}\nxllcorner {xll:.3f}\nyllcorner {yll:.3f}\ncellsize {cs:.4f}\nNODATA_value -9999\n")
    np.savetxt(f, z, fmt='%.2f')
print(out, z.shape, cs, float(z.min()), float(z.max()))
