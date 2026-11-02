// 底色：漆（玩家副漆或厂商色）+ 厂商色条 + 脏污 + 凸边提亮 / 凹缝压暗 + 接缝 + 掉漆露金属 + 指示灯黑框
float3 c = lerp(Paint.rgb * PaintGain, Brand.rgb, UseBrand);
// 色条：Band 方向 = 轴，长度 = 位置（颜色参数接到自定义节点只有 RGB 三个分量，没有 A）
float bl = length(Band);
float bd = abs(dot(P, Band / max(bl, 0.0001)) - bl);
float band = (BandW > 0.0 ? 1.0 - smoothstep(BandW * 0.5, BandW * 0.5 + 0.12, bd) : 0.0);
c = lerp(c, Brand.rgb, band);
c *= lerp(1.0, clamp(S.y, 0.55, 1.6), GrimeAmt);
float edge = saturate(S.z);
float cav = saturate(-S.z);
c *= 1.0 + edge * EdgeLight;
c *= 1.0 - cav * 0.65;
c *= 1.0 - S.w * 0.8;
c = lerp(c, Worn.rgb, S.x);
if (LedPower > 0.0)
{
    float3 Q = float3(P.x, abs(P.y), P.z);
    float d = min(distance(Q, LedPos), distance(Q, LedPos + LedStep));
    c *= smoothstep(LedR * 1.25, LedR * 1.8, d) * 0.9 + 0.1;
}
return c;
