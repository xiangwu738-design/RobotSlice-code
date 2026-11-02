// 焦土 / 弹坑贴花（10-31，保卫战地图）：贴花 UV 中心一团，边缘按世界坐标的分形噪声变得不规则，中间更黑。
// 返回 float4：rgb = 底色（焦黑到焦褐），a = 不透明度
float2 q = (UV - 0.5) * 2.0;
float r = length(q);
float2 p = WP.xy / 300.0;
float n = 0.0;
float amp = 0.5;
for (int i = 0; i < 4; i++)
{
	float2 ip = floor(p);
	float2 fp = frac(p);
	fp = fp * fp * (3.0 - 2.0 * fp);
	float h00 = frac(sin(dot(ip, float2(127.1, 311.7))) * 43758.5453);
	float h10 = frac(sin(dot(ip + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);
	float h01 = frac(sin(dot(ip + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
	float h11 = frac(sin(dot(ip + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
	n += amp * lerp(lerp(h00, h10, fp.x), lerp(h01, h11, fp.x), fp.y);
	p *= 2.03;
	amp *= 0.5;
}
float edge = 1.0 - smoothstep(0.3 + 0.5 * n, 0.98, r);
float a = saturate(edge * (0.65 + 0.5 * n));
float core = 1.0 - smoothstep(0.0, 0.5 + 0.3 * n, r);
float3 col = lerp(float3(0.07, 0.058, 0.045), float3(0.015, 0.013, 0.012), saturate(core * 1.2));
return float4(col, a);
