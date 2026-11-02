// 刀光第二版（11-01 用户：「目前的刀光总体是过于朴素」）。网格见 UMechMeleeFx::BuildTrail：每个采样点三个顶点（刀根、刀尖、刀尖外侧）。
// 材质是预乘透明（AlphaComposite）：返回 (加上去的光, 把背后压暗多少)。白天亮地面上只叠加光会糊成一片白、颜色出不来，
// 先把刀光底下压暗一点，刀的颜色和拉丝之间的暗缝才看得见；夜里背后本来就暗，压暗几乎不起作用
// UV.x = 年龄（0 刚扫过 → 1 快消失），UV.y = 0 刀根 → 1 刀尖 → 2 刀尖外侧辉光的外缘
// UV1.x = 这一刀刀尖走到这儿走了多远（米）：纹路、碎块贴在这条路上
// VC.r 强度（刀尖速度、淡入淡出），VC.g 心线有无，VC.b 这一刀的随机种子
// Prm  = (整体亮度, 心线宽（占刀身长）, 光面亮度, 拉丝对比)
// Prm2 = (时钟, 拉丝条数, 纹路流速, 尾巴碎裂)
// Prm3 = (外侧辉光亮度, 前沿加亮, 前沿锐度, 白热保持多久)
// Prm4 = (横向气压波纹, 波纹疏密（每米几道）, 压暗背后, 0)
// Hot 刚扫过、靠刀尖的白热，Color 主体，Deep 快散时，Core 刀尖心线
// 数值按「屏幕上的亮度」写（1 ≈ 屏幕上满白），乘曝光倒数 EAI（和第一版一样：白天夜里屏幕上一样亮）
struct FTrailNoise
{
	float Hash(float2 p)
	{
		float3 q = frac(float3(p.xyx) * 0.1031);
		q += dot(q, q.yzx + 33.33);
		return frac((q.x + q.y) * q.z);
	}
	float Value(float2 p)
	{
		float2 i = floor(p);
		float2 f = frac(p);
		f = f * f * (3.0 - 2.0 * f);
		float a = Hash(i);
		float b = Hash(i + float2(1.0, 0.0));
		float c = Hash(i + float2(0.0, 1.0));
		float d = Hash(i + float2(1.0, 1.0));
		return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
	}
};
FTrailNoise Nz;
float u = saturate(UV.x);
float v = max(UV.y, 0.0);
float vb = saturate(v);
float s = UV1.x;
float I = VC.r;
float seed = VC.b * 61.0;
float clock = Prm2.x;

// 拉丝：沿刀身分成一条条（条数 Prm2.y），每条有自己的亮暗、沿路时明时暗、微微扭；整片纹路按时钟往刀走的方向流
float across = vb * Prm2.y + seed;
float band = floor(across);
float along = s * 1.7 - clock * Prm2.z;
float wob = Nz.Value(float2(along * 0.4, band * 0.73 + 5.0)) - 0.5;
float tent = 1.0 - abs(frac(across + wob * 0.5) * 2.0 - 1.0);
float lineMask = smoothstep(0.3, 1.0, tent);
float lineLum = 0.25 + 0.9 * Nz.Value(float2(along + band * 5.3, band * 1.7 + seed));
float fine = Nz.Value(float2(along * 3.3 + 11.0, vb * Prm2.y * 2.6 + seed));
float streak = lerp(1.0, (lineMask * 0.85 + fine * 0.3) * lineLum * 1.3, Prm.w);

// 横向气压波纹（钝器）：一道道垂直于挥刀方向的波，跟着刀往前推
float wave = pow(0.5 + 0.5 * cos((s * Prm4.y - clock * 9.0) * 6.2832), 3.0);
float ripple = 1.0 + Prm4.x * (wave * 2.0 - 0.6);

// 光面：刀根那头淡、刀尖那头亮；刀尖外侧一圈软辉光（v 1 → 2），刀身里靠刀尖那一截也带一点
float body = smoothstep(0.02, 0.55, vb) * (0.15 + 0.85 * vb * vb) * step(v, 1.0001);
float outer = v > 1.0 ? pow(saturate(2.0 - v), 2.2) : exp(-(1.0 - vb) * 8.0) * 0.5;
float glow = outer * Prm3.x;

// 尾巴碎成一条条细长的碎片（噪声沿路拉长、贴在路上不流），越旧门槛越高，碎口一圈带颜色的亮边；Prm2.w = 0 时整片一起淡
float dn = Nz.Value(float2(s * 1.3, vb * 7.0 + seed)) * 0.6 + Nz.Value(float2(s * 3.6 + 7.0, vb * 17.0 + seed)) * 0.4;
float thr = pow(u, 1.2) * 1.1;
float d = dn - thr;
float alive = lerp(1.0 - u, smoothstep(0.0, 0.08, d), Prm2.w);
float rim = exp(-(d * d) / 0.0008) * smoothstep(0.1, 0.35, u) * Prm2.w;

// 颜色：刚扫过那一截最亮最白（前沿），然后是刀的颜色，快散时往深色走；靠刀尖的更白
float lead = 1.0 + Prm3.y * exp(-u * Prm3.z);
float fade = pow(1.0 - u, 0.6);
float hotEnd = 0.03 + 0.35 * Prm3.w;
float3 col = lerp(Hot, Color, smoothstep(0.0, hotEnd, u));
col = lerp(col, Deep, smoothstep(0.3, 0.95, u));
col = lerp(col, Hot, exp(-(1.0 - vb) * 12.0) * exp(-u * 6.0) * Prm3.w);

// 心线：刀尖那条边一道细而亮、微微闪的线
float cw = max(Prm.y, 0.005);
float core = exp(-pow((v - 1.0) / cw, 2.0)) * pow(1.0 - u, 0.8) * VC.g;
core *= 0.75 + 0.5 * Nz.Value(float2(s * 6.0 - clock * 14.0, seed));

float k = I * Prm.x * max(EAI, 1.0);
float3 c = col * ((body * streak * ripple * Prm.z + glow) * alive * fade * lead) + lerp(Color, Hot, 0.4) * (rim * fade * 0.6) + Core * (core * lead * lerp(1.0, alive, 0.5));
// 压暗：光面、辉光底下压一点（拉丝之间的缝压得更多，看得出一条条），越旧越轻
float shade = (body * (0.55 + 0.45 * (1.0 - lineMask * Prm.w)) + outer * 0.4) * alive * fade * I * Prm4.z;
return float4(c * k, saturate(shade));
