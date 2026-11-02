// 梦核旷野图的天空球（M_DF_Sky）第二步：HDR 天空（Poly Haven kloppenheim_06_puresky，CC0）调成梦核的粉蓝色。
// 输入：Sky 立方体贴图采样的颜色，D 视线方向（未旋转的 z = 仰角的正弦）
// 参数：Scale 整体亮度，ClampMax 亮度上限（压掉太阳光斑），HorizonTint / ZenithTint 地平线附近 / 头顶的色偏，
//       HorizonWidth 色偏从地平线往上的范围（仰角正弦），Pastel 色偏强度，Desat 去饱和，
//       BelowColor 地平线以下的颜色（和高度雾同色），HorizonBlend 地平线往上这么高（仰角正弦）以内渐变到 BelowColor，
//       远处地面被雾盖成雾色，天边也是雾色，两边接得上（天空球和行星在雾的截止距离外，雾不管它们）
float3 c = Sky * Scale;
float l = dot(c, float3(0.2126, 0.7152, 0.0722));
c *= min(1.0, ClampMax / max(l, 1e-4));
float el = D.z;
float hz = 1.0 - saturate(el / max(HorizonWidth, 1e-3));
hz = hz * hz;
float3 tint = lerp(ZenithTint, HorizonTint, hz);
c = lerp(c, c * tint, Pastel);
float l2 = dot(c, float3(0.2126, 0.7152, 0.0722));
c = lerp(c, float3(l2, l2, l2), Desat);
float below = 1.0 - saturate(el / max(HorizonBlend, 1e-3));
below = below * below;
c = lerp(c, BelowColor, below);
return c;
