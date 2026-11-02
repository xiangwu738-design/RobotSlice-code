// 刀刃热光（10-31 近战特效，加在武器母材质 M_WeaponPBR 的自发光上；默认热度 0 = 和以前一样）：
// 刀身（局部 X 过了护手）上法线朝刃口那一侧的面最亮，整片刀身微微发光，越靠刀尖越亮。
// HeatPrm = (热度, 刀身起点 X, 刃口方向 ±1（局部 Z）, 刀尖 X)；HeatColor 按「屏幕上的亮度」写，乘曝光倒数 EAI
float heat = HeatPrm.x;
if (heat <= 0.001)
{
	return float3(0.0, 0.0, 0.0);
}
float along = saturate((LPos.x - HeatPrm.y) / 10.0);
float tipK = saturate((LPos.x - HeatPrm.y) / max(HeatPrm.w - HeatPrm.y, 1.0));
float3 nl = normalize(LNrm + float3(0.0, 0.0, 1e-5));
float edge = pow(saturate(nl.z * HeatPrm.z), 2.0);
float m = along * (0.12 + 0.88 * edge) * (0.55 + 0.45 * tipK);
return HeatColor.rgb * (heat * m * max(EAI, 1.0));
