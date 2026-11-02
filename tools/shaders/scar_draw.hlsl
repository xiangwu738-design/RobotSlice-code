// 刀痕第二步（10-31 近战特效）：LP = 还原出的表面点在小球局部坐标里的位置（小球挂在被砍中的骨头上，跟着那块装甲走；球半径 50）。
// 刀口平面 = 局部 XY 面（法线 Z），刀走的方向 = X，Y 朝砍的人。刀口平面和机体表面的交线就是刀痕，贴着装甲的形状弯。
// Prm = (热度 1 → 0, 半长, 线宽, 前面截止 y)，Prm2 = (余烬 0–1（双刀割裂挂着时）, 时间, 圆斑 0/1（匕首、钝器）, 外侧截止 y)；都是局部单位
float3 p = LP;
float rr = length(p);
if (rr > 50.0)
{
	return float3(0.0, 0.0, 0.0);
}
float h = saturate(Prm.x);
// 表面和刀口平面差不多平行的地方（比如刀横着砍、胳膊又是横着的，胳膊顶面整片都在平面上）不画，不然亮成一大片
float3 sn = normalize(cross(ddx(p), ddy(p)) + float3(0.0, 0.0, 1e-6));
float graze = 1.0 - smoothstep(0.55, 0.85, abs(sn.z));
float soft = 1.0 - smoothstep(38.0, 50.0, rr);
// 只画朝砍的人那半边（背面那层装甲也在刀口平面上，不画）
float front = smoothstep(Prm.w - 6.0, Prm.w + 6.0, p.y);
// 也不画在被砍那台表面外面（砍人那台伸到前面的胳膊、刀也在刀口平面上；10-31 第一版整条胳膊被染成橙色）
front *= 1.0 - smoothstep(Prm2.w - 4.0, Prm2.w + 4.0, p.y);
float L = max(Prm.y, 1.0);
float x = p.x / L;
float taper = saturate(1.0 - x * x);
float w = max(Prm.z * (0.25 + 0.75 * taper), 0.2);
float d = abs(p.z);
float cut = exp(-pow(d / w, 2.0));
float halo = exp(-pow(d / (w * 2.5), 2.0)) * 0.15;
// 刀口沿长度方向亮度有起伏（不是一条均匀的线）
float n = 0.75 + 0.25 * sin(p.x * 0.9 + 1.3) * sin(p.x * 0.37 + 0.4);
float shape = (cut + halo) * taper * n;
// 圆斑：匕首扎中、钝器砸中的那一点（离中心的距离，不看刀口平面）
float spot = exp(-pow(length(p.xz) / max(L, 1.0), 2.0) * 2.5);
shape = lerp(shape, spot, Prm2.z);
// 冷却：白热 → 橙 → 暗红
float3 hot = float3(1.0, 0.92, 0.75);
float3 mid = float3(1.0, 0.42, 0.1);
float3 cold = float3(0.6, 0.07, 0.02);
float3 col = h > 0.5 ? lerp(mid, hot, (h - 0.5) * 2.0) : lerp(cold, mid, h * 2.0);
float bright = 0.2 + 4.5 * h * h + 0.8 * h;
float e = shape * bright;
// 余烬：暗红的刀口一明一暗（割裂还挂着）
e += Prm2.x * cut * taper * (0.6 + 0.35 * sin(Prm2.y * 7.0 + p.x * 0.45));
return col * (e * front * soft * lerp(graze, 1.0, Prm2.z) * max(EAI, 1.0));
