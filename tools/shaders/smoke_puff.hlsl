// 一片烟（朝向镜头的贴片）：贴图绕中心转 Rot、按 Grow 缩放（贴片按最大尺寸放，涨开靠这里），序列帧在相邻两帧之间渐变
float2 uv = UV - 0.5;
float c = cos(Rot);
float s = sin(Rot);
uv = float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c) / max(Grow, 0.05) + 0.5;
float inb = (uv.x > 0.0 && uv.x < 1.0 && uv.y > 0.0 && uv.y < 1.0) ? 1.0 : 0.0;
// 离这一格序列帧中心多远（1 = 内切圆）：序列帧后半段烟团已经涨满整格，格子的方边会把烟切出直边，所以按圆形收掉
float r = length(uv - 0.5) * 2.0;
uv = saturate(uv);
float n = Cols * Rows;
float f = Frame - n * floor(Frame / n);
float f0 = floor(f);
float f1 = f0 + 1.0 - n * floor((f0 + 1.0) / n);
float w = f - f0;
float2 cell = 1.0 / float2(Cols, Rows);
float2 uv0 = (float2(f0 - Cols * floor(f0 / Cols), floor(f0 / Cols)) + uv) * cell;
float2 uv1 = (float2(f1 - Cols * floor(f1 / Cols), floor(f1 / Cols)) + uv) * cell;
float4 ta = Texture2DSample(Tex, TexSampler, uv0);
float4 tb = Texture2DSample(Tex, TexSampler, uv1);
float4 t = lerp(ta, tb, w);
// 浓度：Channel 0 用 A，1 用亮度
float density = Channel < 0.5 ? t.a : dot(t.rgb, float3(0.3, 0.59, 0.11));
// 径向软遮罩：半径一半以内不动，往外到内切圆渐渐收到 0；收的时候先吃掉淡的地方（浓度减去一截），
// 外轮廓跟着烟本身的浓淡走，是一团不规则的烟，不是一个正圆，也看不到方边
float radial = 1.0 - smoothstep(0.5, 0.97, r);
float d = saturate(density * Contrast);
d = saturate(d - (1.0 - radial) * 0.35) * radial;
float alpha = d * inb * Opacity * SoftEdge;
// 颜色：烟本身灰白，贴图的明暗做一点体积感
float shade = lerp(0.72, 1.0, saturate(dot(t.rgb, float3(0.3, 0.59, 0.11)) * 1.2));
return float4(Tint.rgb * shade, alpha);
