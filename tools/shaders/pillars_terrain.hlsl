// 石柱山水 M_PL_Terrain：地形原包只有 1024 的颜色图（一张贴图铺 300 多米），颜色当大块色调（绿 / 青 / 橙），贴近靠世界投影的草（工程里现成的 T_DF_Lawn）和岩石细节，坡上露岩。
// 输入：WP、N 世界法线（几何）、BaseN（UV 法线已转世界）、C 原包颜色、GC/GN 草色和法线（朝上投影）、RC_T/RC_S、RN_T/RN_S 岩石颜色和法线、Noise
float3 base = saturate(C * 2.0) * BaseScale;   // 原作 HSV 明度 ×2，太亮压一点
float slope = 1.0 - N.z;
float rockMask = smoothstep(RockFrom, RockTo, slope + (Noise.r - 0.5) * 0.22);
float dist = length(WP - CamPos);
float nearFade = 1.0 - smoothstep(12000.0, 30000.0, dist);
float lg = dot(GC, float3(0.3, 0.59, 0.11));
float3 grass = base * lerp(1.0, lg / max(LawnMean, 0.05), GrassMix * nearFade) * lerp(0.85, 1.15, Noise.r);
float top = smoothstep(0.35, 0.8, abs(N.z));
float3 rc = lerp(RC_S, RC_T, top);
float3 rock = lerp(base * 0.5, dot(rc, float3(0.3, 0.59, 0.11)).xxx * RockTint * 2.0, 0.5);
float wet = 1.0 - smoothstep(0.0, 160.0, WP.z + (Noise.r - 0.5) * 80.0);
if (Mode > 1.5) {
    return lerp(lerp(0.92, 0.75, rockMask), 0.3, wet).xxx;
}
if (Mode > 0.5) {
    float3 n = normalize(lerp(N, BaseN, 0.4));   // 原包法线在陡坡上是一道道梳子纹，压轻一点
    float3 side = abs(N.x) > abs(N.y) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tA = lerp(side, float3(1, 0, 0), top);
    float3 tB = lerp(float3(0, 0, 1), float3(0, 1, 0), top);
    float3 rn = lerp(RN_S, RN_T, top);
    float3 gd = float3(GN.x, GN.y, 0.0) * GrassN * nearFade;
    float3 rd = (tA * rn.x + tB * rn.y) * RockN * nearFade;
    return normalize(n + lerp(gd, rd, rockMask));
}
float3 col = lerp(grass, rock, rockMask);
col *= lerp(1.0, 0.55, wet);
return col;
