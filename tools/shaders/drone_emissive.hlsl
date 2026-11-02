// 指示灯：物体空间两个小点（第二个暗一点），左右对称各一组，慢呼吸；白天按曝光补偿，否则看不见
if (LedPower <= 0.0)
{
    return float3(0.0, 0.0, 0.0);
}
float3 Q = float3(P.x, abs(P.y), P.z);
float d1 = distance(Q, LedPos);
float d2 = distance(Q, LedPos + LedStep);
float a = 1.0 - smoothstep(LedR * 0.55, LedR, d1);
float b = 1.0 - smoothstep(LedR * 0.55, LedR, d2);
float pulse = 0.72 + 0.28 * sin(T * 2.1);
return LedColor.rgb * LedPower * Day * (a * pulse + b * 0.35);
