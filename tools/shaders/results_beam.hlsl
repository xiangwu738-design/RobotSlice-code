// 结算场景的光柱（M_ResultsBeam，挂在拉长的圆锥上，尖朝光源）：正对镜头的中间亮、边缘淡（像穿过一段雾），
// 离光源越远越淡，碰到地面 / 机体的地方按深度差软掉
// Origin = 光源那头（世界坐标），Dir = 往下的方向，Prm = (长度, 边缘指数, 软边距离, 沿长度衰减指数)
float t = saturate(dot(WPos - Origin.xyz, Dir.xyz) / max(Prm.x, 1.0));
float edge = pow(saturate(abs(dot(normalize(Nrm), normalize(CamV)))), Prm.y);
float along = pow(1.0 - t, Prm.w) * saturate(t * 12.0);
float soft = saturate(DepthDiff / max(Prm.z, 1.0));
return Color * Strength * edge * along * soft;
