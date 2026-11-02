"""峡谷材质用的程序贴图：可平铺噪声（R 大块 / G 中块 / B 竖向流痕）、岩层色带（按世界高度查）"""
import numpy as np
from PIL import Image
OUT = r"D:\RobotSlice\SourceAssets\canyon\textures\_gen"
N = 1024
rng = np.random.default_rng(5)


def periodic_noise(n, cells_x, cells_y, seed):
    r = np.random.default_rng(seed)
    g = r.random((cells_y, cells_x)).astype(np.float32)
    y = np.arange(n) / n * cells_y; x = np.arange(n) / n * cells_x
    yi = np.floor(y).astype(int); xi = np.floor(x).astype(int)
    fy = (y - yi); fx = (x - xi)
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    y0 = yi % cells_y; y1 = (yi + 1) % cells_y; x0 = xi % cells_x; x1 = (xi + 1) % cells_x
    a = g[y0][:, x0]; b = g[y0][:, x1]; c = g[y1][:, x0]; d = g[y1][:, x1]
    FX = fx[None, :]; FY = fy[:, None]
    return a + (b - a) * FX + (c - a) * FY + (a - b - c + d) * FX * FY


def fbm(n, base_x, base_y, octaves, seed):
    s = np.zeros((n, n), np.float32); amp = 1.0; tot = 0
    for o in range(octaves):
        s += amp * periodic_noise(n, base_x * 2 ** o, base_y * 2 ** o, seed + o)
        tot += amp; amp *= 0.5
    s /= tot
    return (s - s.min()) / (s.max() - s.min())


R = fbm(N, 4, 4, 6, 10)
G = fbm(N, 16, 16, 5, 20)
B = fbm(N, 48, 3, 5, 30)          # 横向细、竖向长：流痕
B = np.clip((B - 0.35) * 1.8, 0, 1)
img = np.stack([R, G, B], -1)
Image.fromarray((img * 255).astype(np.uint8)).save(OUT + r"\T_Canyon_Noise.png")

# 岩层色带：v=0 底（Z=-500 厘米）→ v=1 顶（Z=6500）
H = 512
z = np.linspace(-500, 6500, H)
key = [(-500, (0.62, 0.40, 0.30)), (300, (0.66, 0.42, 0.31)), (850, (0.72, 0.52, 0.38)), (950, (0.80, 0.66, 0.50)),
       (1400, (0.74, 0.55, 0.40)), (1800, (0.86, 0.78, 0.64)), (2300, (0.78, 0.70, 0.58)), (2700, (0.70, 0.56, 0.44)),
       (3300, (0.64, 0.42, 0.32)), (4000, (0.70, 0.50, 0.38)), (4700, (0.80, 0.70, 0.56)), (6500, (0.76, 0.66, 0.54))]
kz = np.array([k[0] for k in key]); kc = np.array([k[1] for k in key])
col = np.stack([np.interp(z, kz, kc[:, i]) for i in range(3)], -1)
bands = np.cumsum(rng.normal(0, 1, H)); bands = bands - np.convolve(bands, np.ones(31) / 31, mode="same")
thin = rng.choice([1.0, 0.92, 1.06, 0.85], size=H, p=[0.55, 0.2, 0.17, 0.08])
thin = np.repeat(thin[::3], 3)[:H]
col = col * (1 + 0.025 * bands[:, None]) * thin[:, None]
col = col / col.mean(0, keepdims=True).mean() * 0.75
strip = np.repeat(col[::-1][:, None, :], 16, 1)   # 行 0 = 顶
Image.fromarray((np.clip(strip, 0, 1) * 255).astype(np.uint8)).save(OUT + r"\T_Canyon_Strata.png")
print("ok", img.shape, strip.shape)

# 水面细波纹法线（可平铺）
h = fbm(512, 24, 24, 4, 40) * 0.6 + fbm(512, 60, 60, 3, 50) * 0.4
gy, gx = np.gradient(h)
k = 18.0
nx, ny, nz = -gx * k, -gy * k, np.ones_like(h)
l = np.sqrt(nx ** 2 + ny ** 2 + nz ** 2)
nrm = np.stack([nx / l, ny / l, nz / l], -1) * 0.5 + 0.5
Image.fromarray((nrm * 255).astype(np.uint8)).save(OUT + r"\T_Canyon_WaterN.png")
print("water normal ok")
