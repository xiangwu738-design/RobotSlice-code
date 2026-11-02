"""从 USGS 3DEP ImageServer（公开、无需密钥）按 UTM 方框下载高程 GeoTIFF（float32，米），并出一张山体阴影预览。
用法：py -3.11 fetch_dem.py <名字> <纬度> <经度> <宽米> <高米> <分辨率米> <输出目录>"""
import math, sys, os, json, urllib.request
import numpy as np
from PIL import Image

def latlon_to_utm(lat, lon):
    zone = int((lon + 180) // 6) + 1
    a, f = 6378137.0, 1 / 298.257222101  # GRS80 (NAD83)
    e2 = f * (2 - f); ep2 = e2 / (1 - e2); k0 = 0.9996
    lam0 = math.radians((zone - 1) * 6 - 180 + 3)
    phi, lam = math.radians(lat), math.radians(lon)
    N = a / math.sqrt(1 - e2 * math.sin(phi) ** 2)
    T = math.tan(phi) ** 2; C = ep2 * math.cos(phi) ** 2; A = math.cos(phi) * (lam - lam0)
    M = a * ((1 - e2 / 4 - 3 * e2 ** 2 / 64 - 5 * e2 ** 3 / 256) * phi - (3 * e2 / 8 + 3 * e2 ** 2 / 32 + 45 * e2 ** 3 / 1024) * math.sin(2 * phi)
             + (15 * e2 ** 2 / 256 + 45 * e2 ** 3 / 1024) * math.sin(4 * phi) - (35 * e2 ** 3 / 3072) * math.sin(6 * phi))
    x = k0 * N * (A + (1 - T + C) * A ** 3 / 6 + (5 - 18 * T + T ** 2 + 72 * C - 58 * ep2) * A ** 5 / 120) + 500000
    y = k0 * (M + N * math.tan(phi) * (A ** 2 / 2 + (5 - T + 9 * C + 4 * C ** 2) * A ** 4 / 24 + (61 - 58 * T + T ** 2 + 600 * C - 330 * ep2) * A ** 6 / 720))
    return zone, x, y

def hillshade(z, res, az=315, alt=45):
    gy, gx = np.gradient(z, res)
    slope = np.arctan(np.hypot(gx, gy)); aspect = np.arctan2(-gx, gy)
    azr, altr = math.radians(360 - az + 90), math.radians(alt)
    hs = np.sin(altr) * np.cos(slope) + np.cos(altr) * np.sin(slope) * np.cos(azr - aspect)
    return np.clip(hs, 0, 1)

def main():
    name, lat, lon, w, h, res, out = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5]), float(sys.argv[6]), sys.argv[7]
    os.makedirs(out, exist_ok=True)
    zone, cx, cy = latlon_to_utm(lat, lon)
    epsg = 26900 + zone
    nx, ny = int(round(w / res)), int(round(h / res))
    bbox = f"{cx - w / 2:.2f},{cy - h / 2:.2f},{cx + w / 2:.2f},{cy + h / 2:.2f}"
    url = ("https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage?"
           f"bbox={bbox}&bboxSR={epsg}&imageSR={epsg}&size={nx},{ny}&format=tiff&pixelType=F32&noData=-9999"
           "&interpolation=RSP_BilinearInterpolation&compression=None&f=image")
    tif = os.path.join(out, f"{name}.tif")
    req = urllib.request.Request(url, headers={"User-Agent": "RobotSlice-canyon/1.0"})
    with urllib.request.urlopen(req, timeout=180) as r, open(tif, "wb") as f:
        f.write(r.read())
    z = np.array(Image.open(tif), dtype=np.float32)
    z[z < -1000] = np.nan
    meta = dict(name=name, lat=lat, lon=lon, epsg=epsg, utm_center=[cx, cy], width_m=w, height_m=h, res_m=res, shape=list(z.shape),
                zmin=float(np.nanmin(z)), zmax=float(np.nanmax(z)), source="USGS 3DEP ImageServer exportImage", url=url)
    json.dump(meta, open(os.path.join(out, f"{name}.json"), "w"), indent=1)
    zz = np.nan_to_num(z, nan=np.nanmin(z))
    hs = hillshade(zz, res)
    n = (zz - zz.min()) / max(1e-3, zz.max() - zz.min())
    rgb = np.stack([0.55 + 0.45 * n, 0.45 + 0.3 * n, 0.35 + 0.2 * n], -1) * (0.25 + 0.75 * hs[..., None])
    Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8)).save(os.path.join(out, f"{name}_hs.png"))
    print(json.dumps({k: meta[k] for k in ("name", "epsg", "shape", "zmin", "zmax")}, ensure_ascii=False))

main()
