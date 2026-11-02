// 结算场景的地面 / 墙（M_ResultsFloor 的自发光）：一层很淡的网格线，离场景中心越远越淡
// Prm = (网格间距 厘米, 线宽 占一格的比例, 淡出半径 厘米, 1 = 竖墙用 YZ 平面 / 0 = 地面用 XY 平面)
float2 c = lerp(WPos.xy, WPos.yz, Prm.w) / max(Prm.x, 1.0);
float2 g = abs(frac(c + 0.5) - 0.5);
float2 fw = max(fwidth(c), float2(1e-4, 1e-4));
float2 l = 1.0 - saturate((g - Prm.y) / fw);
float lineMask = max(l.x, l.y);
float d = length(WPos - Center.xyz);
float fade = saturate(1.0 - d / max(Prm.z, 1.0));
fade = fade * fade;
return GridColor * GridIntensity * lineMask * fade;
