// 原版峡谷图（L_MegaCanyon）面板材质 M_MC_Panel 第一步：从 dreamfield_panel_uv.hlsl 抄来，多一个 PlanarXZ：
// PlanarXZ = 1 时照原包（Scene_03 的 Material.001 / Glossy）的做法：所有面都往世界 XZ 平面平铺、不随机翻转
// （朝沟的正面只剩横条纹、侧面是完整的格栅，和渲染图一样）；= 0 时和梦核旷野一样按法线三向投影。
// 梦核旷野图巨构墙面板材质（M_DW_Panel）第一步：世界坐标三向投影的 UV（按法线主方向选一个投影面，不混合，只采一次贴图）。
// 原包的做法是物体空间往 XZ 平面平铺（朝沟的正面只剩横条纹）；这里三个方向都铺，从旷野正面看墙也有格栅和小方块。
// 每一格（Tile 厘米见方）按格子编号随机左右翻转、错开半格，减少重复；格子边界的贴图跳变正好像面板接缝。
// 输入：WP 世界坐标（厘米），N 世界法线；参数 Tile 一格多大（厘米）。立面上 v 朝下（图片上方朝上）。
float3 an = abs(N);
float2 p;
float axisId;
if (an.z >= an.x && an.z >= an.y) { p = float2(WP.x, -WP.y); axisId = 0.0; }
else if (an.x >= an.y) { p = float2(WP.y * sign(N.x), -WP.z); axisId = 1.0; }
else { p = float2(-WP.x * sign(N.y), -WP.z); axisId = 2.0; }
if (PlanarXZ > 0.5) { p = float2(WP.x, -WP.z); axisId = 1.0; }
float2 uv = p / Tile;
if (PlanarXZ > 0.5) return uv;
float2 cell = floor(uv);
float h = frac(sin(dot(cell + axisId * 7.0, float2(12.9898, 78.233))) * 43758.5453);
float flip = h < 0.5 ? -1.0 : 1.0;
float2 off = float2(frac(h * 7.31) < 0.5 ? 0.0 : 0.5, frac(h * 3.17) < 0.5 ? 0.0 : 0.5);
return float2(uv.x * flip, uv.y) + off;
