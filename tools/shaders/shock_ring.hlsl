// 冲击环 / 闪光（10-31 近战特效，贴在一块正方形面片上，C++ 摆朝向、按时间改进度）：UV 0–1。
// Prm = (进度 0 → 1, 环宽, 亮度, 样式：0 冲击环，1 横向闪光（弹反），2 预警圈（保卫战）)；Color 按「屏幕上的亮度」写，乘曝光倒数 EAI
float2 q = (UV - 0.5) * 2.0;
float t = saturate(Prm.x);
float e = 0.0;
if (Prm.w < 0.5)
{
	float r = length(q);
	// 先快后慢地扩开，越往外越细越淡
	float R = 0.12 + 0.88 * (1.0 - pow(1.0 - t, 3.0));
	float w = max(Prm.y * (1.0 - 0.6 * t), 0.005);
	float ring = exp(-pow((r - R) / w, 2.0));
	// 环上按角度有深浅，不是一个规整的圆
	float a = atan2(q.y, q.x);
	float var = 0.7 + 0.3 * sin(a * 9.0 + 1.7) * sin(a * 4.0 + 0.3);
	float fill = (1.0 - smoothstep(0.0, R, r)) * 0.1 * (1.0 - t);
	e = (ring * var + fill) * pow(1.0 - t, 1.4) * (1.0 - smoothstep(0.92, 1.0, r));
}
else if (Prm.w > 1.5)
{
	// 样式 2：预警圈（10-31 保卫战：自爆无人机、迫击炮落点）。外圈一直亮着，里面一片从中心涨到边（进度 = 倒数），涨满就炸
	float r = length(q);
	float edge = exp(-pow((r - 0.94) / 0.025, 2.0));
	float fill = (1.0 - smoothstep(t - 0.015, t, r)) * 0.2;
	float front = exp(-pow((r - t) / 0.02, 2.0)) * 0.7;
	e = (edge + fill + front) * (1.0 - smoothstep(0.97, 1.0, r));
}
else
{
	// 横向一道细长的闪光 + 中心一点 + 很短的竖线（金属碰撞的反光）
	float hl = exp(-pow(q.y / 0.035, 2.0)) * pow(saturate(1.0 - abs(q.x)), 2.5);
	float star = exp(-(q.x * q.x + q.y * q.y) / 0.004) * 2.0;
	float vl = exp(-pow(q.x / 0.025, 2.0)) * pow(saturate(1.0 - abs(q.y)), 4.0) * 0.35;
	e = (hl + star + vl) * pow(1.0 - t, 2.0);
}
return Color.rgb * (e * Prm.z * max(EAI, 1.0));
