// 原版峡谷图的天空球（M_MC_Sky）：原包 Scene_03 的世界背景其实是一个纯色（深石板蓝 0.10/0.11/0.146 × 1.64，HDR 那几个节点没接上），
// 渲染图里地平线发白是沟里和甲板上方那几个体积雾盒子。这里画一个竖向渐变：头顶深蓝 → 地平线淡蓝白，地平线以下是雾色。
// 输入：WP 世界坐标，CamPos 相机位置；参数 Zenith / Horizon / Below 颜色，Curve 渐变曲线（越大淡色越贴地平线）
float3 d = normalize(WP - CamPos);
float t = pow(saturate(d.z), Curve);
float3 c = lerp(Horizon, Zenith, t);
float below = saturate(-d.z * 15.0);
return lerp(c, Below, below);
