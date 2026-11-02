// EMP 作用范围的地面提示（10-30 第三版：主角改成不规则的闪电球，见 AMechGrenade::TickEmpBurst；这里只留很淡的一层，不再画规整的扩散环和六边形网格）。
// 画法照旧是「屏幕空间贴花」：材质挂在一个覆盖范围的球上，双面、不做深度测试，只用背面那层画（按几何判断：这一点的球面外法线背着镜头；
// TwoSidedSign 实测正负和预想相反，用它时镜头在球里什么都不画），按场景深度把像素背后的表面还原到世界坐标（相机 + 视线 × 场景深度 / 像素深度），
// 在离炸点 Rmax 以内、朝上的面上画：一层斑驳的淡蓝底色（随电光球扩开，约 1 秒淡掉）+ 一圈很淡、按角度抖动（不规整）、一闪一闪的外缘。
// Prm = (R 电光球半径, Rmax 范围, 炸开后的时间, 镜头在范围里 0-1)；Color 蓝色；Strength 整体倍数；
// EAI 是曝光倒数：数值按「屏幕上的亮度」写，乘上它抵掉自动曝光，白天夜里屏幕上一样亮
float R = max(Prm.x, 1.0);
float Rmax = Prm.y;
float age = Prm.z;
float inside = Prm.w;
float3 ray = WPos - Cam;
if (dot(WPos - Center.xyz, ray) < 0.0) return float3(0.0, 0.0, 0.0);
float3 scene = Cam + ray * (SceneD / max(PixD, 1.0));
float3 rel = scene - Center.xyz;
float d = length(rel);
float3 dsx = ddx(scene);
float3 dsy = ddy(scene);
float3 nrm = normalize(cross(dsx, dsy) + float3(0.0, 0.0, 1e-5));
float up = saturate((abs(nrm.z) - 0.6) / 0.25);
float fwd = min(fwidth(d), 60.0);
// 外缘按角度抖动（几个不同频率的正弦叠起来，随时间慢慢变），看着不是规整的圆
float ang = atan2(rel.y, rel.x);
float wob = sin(ang * 7.0 + Tm * 3.1) * 0.5 + sin(ang * 13.0 - Tm * 4.7) * 0.3 + sin(ang * 23.0 + 1.7) * 0.2;
float Redge = Rmax * (1.0 + 0.035 * wob);
float inR = 1.0 - smoothstep(Redge - 30.0, Redge, d);
float swept = 1.0 - smoothstep(R * 0.9, R, d);
// 斑驳：按地面位置的块状噪声（0.6 米一块），一闪一闪
float2 cell = floor(rel.xy / 60.0);
float rnd = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
float mottle = 0.55 + 0.45 * step(0.5, frac(rnd * 5.0 + Tm * 4.0));
float on = smoothstep(0.05, 0.25, age) * (1.0 - smoothstep(0.55, 1.05, age));
float e = up * inR * swept * mottle * 0.045 * on;
// 外缘：很细很淡，炸散以后才出来（电弧打到地上的那一圈），闪烁
float edge = exp(-pow((d - Redge) / max(14.0, fwd * 1.6), 2.0));
float edgeOn = smoothstep(0.25, 0.35, age) * (1.0 - smoothstep(0.7, 1.05, age));
float blink = 0.6 + 0.4 * step(0.4, frac(Tm * 9.0 + rnd));
e += edge * up * 0.14 * edgeOn * blink;
e *= 1.0 - 0.4 * inside;
return Color.rgb * e * Strength * max(EAI, 1.0);
