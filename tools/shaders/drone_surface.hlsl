// 无人机表面遮罩（物体空间三向投影，模型的 UV 是机甲组件库拼的，密度乱，不能按 UV 贴）。
// 输出 x = 掉漆露金属 0-1，y = 脏污明暗（1 为平均），z = 带符号曲率（正 = 凸边，负 = 凹缝，-1..1），w = 面板接缝 0-1
float3 w = pow(abs(N), 6.0);
w /= max(w.x + w.y + w.z, 0.0001);
float3 t = Texture2DSample(Tex, TexSampler, P.zy * Tile).rgb * w.x + Texture2DSample(Tex, TexSampler, P.xz * Tile).rgb * w.y + Texture2DSample(Tex, TexSampler, P.xy * Tile).rgb * w.z;
float lum = dot(t, float3(0.3, 0.59, 0.11));
// 同一张图缩小再采一次，当大块的磨损分布（边缘不要一圈都掉漆）
float3 t2 = Texture2DSample(Tex, TexSampler, (P.zy + 11.0) * Tile * 0.31).rgb * w.x + Texture2DSample(Tex, TexSampler, (P.xz + 23.0) * Tile * 0.31).rgb * w.y + Texture2DSample(Tex, TexSampler, (P.xy + 37.0) * Tile * 0.31).rgb * w.z;
float lum2 = dot(t2, float3(0.3, 0.59, 0.11));
// 屏幕空间曲率：法线变化 / 位置变化。平滑组里的倒角才有信号，硬边没有
float c = dot(DNx, DPx) / max(dot(DPx, DPx), 0.00001) + dot(DNy, DPy) / max(dot(DPy, DPy), 0.00001);
float curv = clamp(c * EdgeScale, -1.0, 1.0);
float edge = saturate(curv);
// 底漆贴图里亮的小块就是掉漆，边缘按大块分布掉
float chip = smoothstep(0.24, 0.36, lum) * ChipAmt;
float rub = smoothstep(0.30, 0.75, edge * (0.45 + lum2 * 3.5)) * WearAmt;
float wear = saturate(chip + rub);
// 面板接缝：沿 X 的环缝 + 少量沿 Z 的横缝，线宽按像素大小收，远了自动变淡
float pw = max(length(DPx) + length(DPy), 0.0001);
float dx = abs(frac((P.x + SeamOfs) / SeamStepX) - 0.5) * SeamStepX;
float dz = abs(frac((P.z + SeamOfs * 0.7) / SeamStepZ) - 0.5) * SeamStepZ;
float line_x = (1.0 - smoothstep(SeamW * 0.5, SeamW * 0.5 + pw, dx)) * saturate(1.0 - abs(N.x) * 1.4);
float line_z = (1.0 - smoothstep(SeamW * 0.5, SeamW * 0.5 + pw, dz)) * saturate(1.0 - abs(N.z) * 1.4);
float seam = saturate(max(line_x, line_z)) * saturate(SeamW * 1.5 / pw) * SeamAmt;
return float4(wear, lum / 0.13, curv, seam);
