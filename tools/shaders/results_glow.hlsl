// 结算场景的地环 / 光条（M_ResultsGlow，挂在平面或细长方块上）
// Prm = (环半径 0–1, 环宽, 中间软光的量, 1 = 整块均匀发光 / 0 = 环)
float r = length(UV - 0.5) * 2.0;
float ring = 1.0 - smoothstep(0.0, max(Prm.y, 1e-3), abs(r - Prm.x));
float disc = pow(saturate(1.0 - r), 2.0) * Prm.z;
float m = max(ring, disc) * step(r, 1.0);
m = lerp(m, 1.0, saturate(Prm.w));
return Color * Strength * m;
