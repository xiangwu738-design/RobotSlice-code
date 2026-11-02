// 梦核旷野图的地面（M_DF_Grass）。第二版（11-01，用户「旷野精细度太低」）：
// 大尺度：黄绿 / 嫩绿 / 深一点的草色按几层噪声慢慢变（MacroScale 厘米），再叠一层更大的明暗块（像云影、草长得有疏有密）；
// 近处：Kikuyu 草叶撒出来的草坪贴图（T_DF_Lawn_C / _N，tools/dreamfield_lawn_textures.py），两个尺度、一个转 37° 混着用，减少重复；
//       贴图（按线性读）只取「相对明暗」的灰度（平均亮度 0.5），颜色全由大尺度草色说了算（第一次连颜色一起乘，黄绿的草叶看着像干草）；离相机越远细节越淡（150 米外只剩草色，免得远处闪）。
// 输入：WP 世界坐标（厘米），N 世界法线，CamPos 相机位置，C1 / C2 两个尺度的草坪颜色，N1 / N2 两个尺度的草坪法线（已解码到 -1..1），
//       Mode 0 = 底色 / 1 = 世界空间法线（材质关了切线空间法线）
// 参数：ColorA 偏黄、ColorB 偏绿、ColorC 深一点的草，MacroScale，DetailAmount 细节强度，BumpScale 凹凸强度，FadeDist 细节淡出距离（厘米）
float dist = length(WP - CamPos);
float near = 1.0 - saturate((dist - FadeDist * 0.3) / FadeDist);
if (Mode > 0.5) {
    float a = radians(37.0);
    float s = sin(a);
    float c = cos(a);
    float3 duA = float3(1, 0, 0);
    float3 dvA = float3(0, -1, 0);
    float3 duB = float3(c, s, 0);
    float3 dvB = float3(s, -c, 0);
    float3 bump = (duA * N1.x + dvA * N1.y) * 0.6 + (duB * N2.x + dvB * N2.y) * 0.4;
    return normalize(N + bump * BumpScale * near);
}
float2 p = WP.xy / MacroScale;
float n = 0.0;
float amp = 0.5;
float2 pp = p;
for (int i = 0; i < 4; i++) {
    float2 c0 = floor(pp);
    float2 fr = pp - c0;
    float2 u = fr * fr * (3.0 - 2.0 * fr);
    float ha = frac(sin(dot(c0, float2(127.1, 311.7))) * 43758.5453);
    float hb = frac(sin(dot(c0 + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);
    float hc = frac(sin(dot(c0 + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    float hd = frac(sin(dot(c0 + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    n += amp * lerp(lerp(ha, hb, u.x), lerp(hc, hd, u.x), u.y);
    pp = pp * 2.13 + float2(17.3, 9.1);
    amp *= 0.5;
}
n = n / 0.9375;
float2 p2 = WP.xy / (MacroScale * 0.23) + float2(41.0, 7.0);
float2 c2 = floor(p2);
float2 f2 = p2 - c2;
float2 u2 = f2 * f2 * (3.0 - 2.0 * f2);
float a2 = frac(sin(dot(c2, float2(269.5, 183.3))) * 43758.5453);
float b2 = frac(sin(dot(c2 + float2(1.0, 0.0), float2(269.5, 183.3))) * 43758.5453);
float cc2 = frac(sin(dot(c2 + float2(0.0, 1.0), float2(269.5, 183.3))) * 43758.5453);
float d2 = frac(sin(dot(c2 + float2(1.0, 1.0), float2(269.5, 183.3))) * 43758.5453);
float m = lerp(lerp(a2, b2, u2.x), lerp(cc2, d2, u2.x), u2.y);
float2 p3 = WP.xy / (MacroScale * 4.0) + float2(3.7, 12.9);
float2 c3 = floor(p3);
float2 f3 = p3 - c3;
float2 u3 = f3 * f3 * (3.0 - 2.0 * f3);
float a3 = frac(sin(dot(c3, float2(91.7, 47.3))) * 43758.5453);
float b3 = frac(sin(dot(c3 + float2(1.0, 0.0), float2(91.7, 47.3))) * 43758.5453);
float cc3 = frac(sin(dot(c3 + float2(0.0, 1.0), float2(91.7, 47.3))) * 43758.5453);
float d3 = frac(sin(dot(c3 + float2(1.0, 1.0), float2(91.7, 47.3))) * 43758.5453);
float big = lerp(lerp(a3, b3, u3.x), lerp(cc3, d3, u3.x), u3.y);
float3 col = lerp(ColorB, ColorA, smoothstep(0.3, 0.7, n));
col = lerp(col, ColorC, smoothstep(0.55, 0.95, m) * 0.3);
col *= lerp(0.88, 1.1, big);
float det = dot(C1 * 0.6 + C2 * 0.4, float3(0.2126, 0.7152, 0.0722)) / 0.5;
col *= lerp(1.0, det, DetailAmount * near);
return col;
