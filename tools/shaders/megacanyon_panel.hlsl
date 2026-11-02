// 原版峡谷图（L_MegaCanyon）面板材质 M_MC_Panel 第二步：从 dreamfield_panel.hlsl 抄来，多一个 PlanarXZ（见 megacanyon_panel_uv.hlsl）。
// 平铺模式下 u 沿世界 X、v 沿 -Z，不翻转：法线的 du = +X、dv = -Z。
// 梦核旷野图巨构墙 / 甲板 / 平台的面板材质（M_DW_Panel）第二步。10-31 第二版：用户「墙面先补一轮明暗和细节，尽量接近原图」。
// 第一版是程序画的大方块接缝，近看是平的灰白方块；现在用峡谷包原材质的那张面板图（Texture.jpg：格栅、小方块、刻线），
// 和原包一样同时当底色和凹凸，再加：每格明暗随机、缝隙（图里暗的地方）压暗、墙脚渐暗、零星几个发光小窗。
// 输入：WP 世界坐标（厘米），N 世界法线，C 面板图颜色，NT 面板图法线（已解码到 -1..1，x = u 方向的坡，y = v 方向（往下）的坡），
//       Mode 0 = 底色 / 1 = 自发光 / 2 = 世界空间法线（材质关了切线空间法线）
// 参数：Tile 一格多大（厘米，要和 UV 那一步一样），Tint 底色乘数，Var 每格明暗差，CavityDark 缝隙压暗，FadeH 墙脚渐暗高度（厘米），
//       BumpScale 凹凸强度，GlowColor / GlowDensity（发光小格的比例），
//       UnderDark 朝下 / 朝侧面的面压暗（第三轮：用户「亮面更白、阴影更蓝更深」；甲板单元底下密密麻麻的小面在阴影里还会漏光，
//       一排白点，甲板压到 0.2；墙压到 0.7，楼柱底面和悬挑下面更深）
float3 an = abs(N);
float2 p;
float axisId;
float3 du;
float3 dv;
float3 nf;
if (an.z >= an.x && an.z >= an.y) { p = float2(WP.x, -WP.y); axisId = 0.0; du = float3(1, 0, 0); dv = float3(0, -1, 0); nf = float3(0, 0, sign(N.z)); }
else if (an.x >= an.y) { p = float2(WP.y * sign(N.x), -WP.z); axisId = 1.0; du = float3(0, sign(N.x), 0); dv = float3(0, 0, -1); nf = float3(sign(N.x), 0, 0); }
else { p = float2(-WP.x * sign(N.y), -WP.z); axisId = 2.0; du = float3(-sign(N.y), 0, 0); dv = float3(0, 0, -1); nf = float3(0, sign(N.y), 0); }
if (PlanarXZ > 0.5) { p = float2(WP.x, -WP.z); du = float3(1, 0, 0); dv = float3(0, 0, -1); }
float2 uv = p / Tile;
float2 cell = floor(uv);
float h = frac(sin(dot(cell + axisId * 7.0, float2(12.9898, 78.233))) * 43758.5453);
float flip = (h < 0.5 && PlanarXZ < 0.5) ? -1.0 : 1.0;
if (Mode > 1.5) {
    float3 n = normalize(nf * max(NT.z, 0.2) + (du * flip * NT.x + dv * NT.y) * BumpScale);
    return n;
}
float lum = dot(C, float3(0.2126, 0.7152, 0.0722));
if (Mode > 0.5) {
    if (axisId < 0.5) return float3(0, 0, 0);
    float2 uvt = float2(uv.x * flip, uv.y) * 40.0;
    float2 gc = floor(uvt);
    float g = frac(sin(dot(gc, float2(41.37, 289.11))) * 15731.743);
    float dark = 1.0 - smoothstep(0.42, 0.55, lum);
    return GlowColor * step(g, GlowDensity) * dark;
}
float var = 1.0 + (frac(h * 13.7) - 0.5) * Var;
float cav = lerp(1.0 - CavityDark, 1.0, smoothstep(0.38, 0.75, lum));
float fade = saturate(WP.z / max(FadeH, 1.0));
float3 col = C * Tint * var * cav;
col *= lerp(float3(0.8, 0.83, 0.9), float3(1.0, 1.0, 1.0), fade);
col *= lerp(UnderDark, 1.0, saturate(N.z * 3.0 + 0.5));
return col;
