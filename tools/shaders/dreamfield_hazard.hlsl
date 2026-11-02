// 梦核旷野图的黄黑警示条纹（M_DW_Hazard：封锁大门的门缝和门脚、电梯井上下口）。世界坐标、按法线主方向选平面，45° 斜纹。
// 输入：WP 世界坐标（厘米），N 世界法线；参数 Width 条纹宽（厘米），ColorA 黄、ColorB 黑；返回底色
float3 an = abs(N);
float2 p;
if (an.z >= an.x && an.z >= an.y) { p = WP.xy; }
else if (an.x >= an.y) { p = WP.yz; }
else { p = WP.xz; }
float t = frac((p.x + p.y) / (Width * 2.0));
float edge = 0.03;
float m = smoothstep(0.5 - edge, 0.5 + edge, t) * (1.0 - smoothstep(1.0 - edge, 1.0, t)) + (1.0 - smoothstep(0.0, edge, t)) * 0.0;
return lerp(ColorA, ColorB, m);
