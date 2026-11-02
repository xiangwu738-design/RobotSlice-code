// 石柱山水 M_PL_Water：不透明水面。颜色按离最近地面的距离（DistanceToNearestSurface，引擎的全局距离场）：水下有地形的地方浅、亮，远了变深；岸边一圈白沫（Water 插件的贴图）。
// 输入：WP、CamPos、Time、Dist 距离（厘米）、N1/N2/N3 三层滚动法线、Noise 大尺度噪声、Foam 白沫图
float dist = length(WP - CamPos);
float far = smoothstep(4000.0, 60000.0, dist);
float2 w = lerp(N1.xy * W1 + N2.xy * W2, N3.xy * W3 + N2.xy * W2 * 0.4, far);
float d = max(Dist, 0.0);
float s1 = saturate(d / NearD);
float s2 = saturate(d / FarD);
float wob = Noise.r - 0.5;
float3 col = lerp(lerp(Shallow, Mid, smoothstep(0.0, 1.0, s1 + wob * 0.35)), Deep, smoothstep(0.0, 1.0, s2 + wob * 0.2));
col *= lerp(0.88, 1.12, Noise.r);
float band = 1.0 - smoothstep(0.0, FoamW, d + wob * 200.0);
float lines = 0.5 + 0.5 * sin(d / 45.0 - Time * 0.9 + Noise.r * 14.0 + Foam.g * 6.0);
float foam = saturate(band * (Foam.r * 1.3 + 0.08) * lerp(0.72, 1.0, lines));
foam *= 1.0 - smoothstep(20000.0, 70000.0, dist);
if (Mode > 1.5) {
    return lerp(lerp(Rough, RoughFar, far), 0.8, foam).xxx;
}
if (Mode > 0.5) {
    return normalize(float3(w * (1.0 - foam * 0.7), 1.0));
}
return lerp(col, FoamColor, foam);
