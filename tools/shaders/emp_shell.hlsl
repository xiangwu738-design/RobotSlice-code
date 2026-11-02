// EMP 冲击球壳：只有边缘薄薄一圈菲涅耳亮边，中间几乎透明；和地面 / 机体相交的地方一圈亮环（场景深度 - 像素深度接近 0）
// Color.a = 亮边的倍数（代码里按镜头离球心多近压低：镜头在壳里 / 贴着壳时整个球壳的边会铺满画面，泛光冲成一片白）
// Fade 整体淡出；Day 是共用的白天补偿，球壳面积大，只补它的平方根（补满白天会被泛光冲成满屏白）
float rim = pow(saturate(Fres), 2.0);
float inter = 1.0 - saturate(abs(DepthDiff) / 60.0);
inter = inter * inter * inter;
float e = rim * 0.6 * Color.a + inter * 4.0;
float3 c = lerp(Color.rgb, float3(1.0, 1.0, 1.0), saturate(inter * 0.6));
return c * e * Strength * Fade * sqrt(max(Day, 1.0));
