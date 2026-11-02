// 蜂窝（尖顶六边形铺在 UV0 上，格子约 0.3 米）
float2 p = UV0 / 0.3;
const float2 r = float2(1.0, 1.7320508);
const float2 h = r * 0.5;
float2 a = (p - r * floor(p / r)) - h;
float2 q = p - h;
float2 b = (q - r * floor(q / r)) - h;
float2 gv = dot(a, a) < dot(b, b) ? a : b;
float2 id = p - gv;
float2 ga = abs(gv);
float edge = 0.5 - max(dot(ga, normalize(r)), ga.x);
float rnd = frac(sin(dot(id, float2(12.9898, 78.233))) * 43758.5453);
float rr = UV1.x;
// 边框外裁掉；展开时从中心往外一圈圈亮起来，波前最亮
float inside = saturate((1.0 - rr) * 80.0);
float reveal = saturate((Deploy * 1.1 - rr) * 18.0);
float fr = (rr - Deploy * 1.1) * 12.0;
float front = exp(-fr * fr) * (1.0 - step(0.999, Deploy));
// 格子线；每格各自明暗起伏；一道往上扫的光带
float lineMask = 1.0 - smoothstep(0.0, 0.045, edge);
float cell = 0.2 + 0.8 * pow(saturate(0.5 + 0.5 * sin(T * (1.2 + rnd * 2.5) + rnd * 40.0)), 5.0);
float sc = (frac(UV0.y * 0.22 + T * 0.5) - 0.5) * 8.0;  // 导入时 V 翻过，+T 是往上扫
float scan = exp(-sc * sc);
float rim = smoothstep(0.88, 0.995, rr) * inside;
// 受击：打中处一圈波纹往外扩，打中那一下附近整片闪亮
float3 Hs[4] = {H0.xyz, H1.xyz, H2.xyz, H3.xyz};
float hits = 0.0;
for (int i = 0; i < 4; i++)
{
    float age = T - Hs[i].z;
    if (age >= 0.0 && age < 0.9)
    {
        float d = distance(UV0, Hs[i].xy);
        float w = (d - age * 4.0) * 4.5;
        hits += exp(-w * w) * (1.0 - age / 0.9);
        hits += exp(-d * 2.5) * saturate(1.0 - age / 0.2) * 1.5;
    }
}
// 和地面、墙相交的地方一道亮线
float inter = saturate(1.0 - DepthDiff.x / 35.0);
// 亮度按「屏幕上多亮」写（0–1），再乘曝光倒数：白天夜里看起来一样亮，不会白天一团白
// （Day 输入是 max(1, 曝光倒数×1.3)^0.85，这里反算回曝光倒数）
float lineGlow = exp(-max(edge, 0.0) * 14.0);
float body = lineMask * (0.22 + 0.3 * cell) + lineGlow * 0.06 + (0.012 + 0.025 * cell) * (0.4 + Fres * 2.2) + scan * (0.03 + lineMask * 0.18);
float glow = (body * (1.0 + hits * 0.6) + rim * 0.45 + hits * (0.2 + lineMask * 0.7) + front * 0.7 + inter * 0.0) * inside * reveal;  // 相交亮线先关：读场景深度不稳，整片会发亮
float eai = pow(max(Day, 1.0), 1.17647) / 1.3;
// 11-01 白天补偿的指数修好后（ue_shield_fix.py），亮度终于和曝光无关了：×1 在白天几乎看不见，试了 ×2.25（自己视角）偏奶白，定 ×3（自己视角再 ×0.45）
return Color.rgb * glow * Strength * eai * 3.0;
