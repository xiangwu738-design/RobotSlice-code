// EMP 电弧（细圆柱）：正对镜头的中线发白、两边发蓝，像一根发光的电线
// Day 是共用的白天补偿 max(1, 曝光倒数*1.3)^0.85（白天约只补回四分之一）；电弧要白天夜里一样亮，这里还原成完整的曝光倒数
float k = max(1.0, pow(Day, 1.0 / 0.85) / 1.3);
float core = pow(saturate(1.0 - Fres), 2.0);
float3 c = lerp(Color.rgb, float3(1.0, 1.0, 1.0), core * 0.55);
return c * Strength * (0.35 + core) * k;
