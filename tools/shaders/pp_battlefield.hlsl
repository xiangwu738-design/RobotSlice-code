// 保卫战地图的战场调色（10-31，L_Defense 的后期材质，色调映射之前的 HDR 画面）：
// 绿色（草、树叶）往枯黄 / 焦褐偏，整体再去一点饱和；机体、队伍色、特效不是绿的，基本不受影响。
// Amount = 枯草程度 0–1，Desat = 整体去饱和 0–1
float3 c = Scene.rgb;
float lum = dot(c, float3(0.299, 0.587, 0.114));
float g = saturate((c.g - max(c.r, c.b)) / max(c.g, 1e-4) * 3.0);
float3 dry = lum * float3(1.0, 0.94, 0.74);   // 枯草偏土黄橄榄，不偏红（第一版 1.15/0.97/0.7 整体发粉）
c = lerp(c, dry, g * Amount);
c = lerp(c, float3(lum, lum, lum), Desat);
return c;
