// 刀光（10-31 近战特效，扫掠面）：每帧记下刀根、刀尖两点，连成一条带子（UMechMeleeFx 建网格）。
// UV.x = 年龄（0 刚扫过 → 1 快消失），UV.y = 0 刀根 → 1 刀尖；VC.r = 这一刻的强度（刀尖速度、出招前后淡入淡出），VC.g = 心线强弱。
// Prm = (整体亮度, 心线宽（占刀身的比例）, 刀身面亮度, 拉丝条纹强度)；Color 刀光颜色、Core 心线颜色。
// 数值按「屏幕上的亮度」写，乘曝光倒数 EAI 抵掉自动曝光：白天夜里屏幕上一样亮（和 EMP 范围贴花一样）
float u = saturate(UV.x);
float v = saturate(UV.y);
float I = VC.r;
// 越旧越淡；刚扫过的那一截更亮一点（刀刃划过的「前沿」）
float age = pow(1.0 - u, 2.2);
float lead = 1.0 + 0.8 * exp(-u * 14.0);
// 刀身面：刀根那头淡、刀尖那头亮（白天亮地面上叠加一大片就成了一块白板，所以刀根那半边压得很淡）
float body = smoothstep(0.08, 0.6, v) * (0.15 + 0.85 * v * v);
// 拉丝：沿刀身分成细条、每条亮度不同，像高速划过空气留下的一道道纹（不是一整片平的渐变）
float k = v * 23.0;
float i0 = floor(k);
float h0 = frac(sin(i0 * 12.9898 + 4.1) * 43758.5453);
float h1 = frac(sin((i0 + 1.0) * 12.9898 + 4.1) * 43758.5453);
float st = lerp(h0, h1, smoothstep(0.0, 1.0, frac(k)));
float streak = lerp(1.0, 0.45 + 0.9 * st, Prm.w);
// 尾巴碎成一条条散掉（越旧的部分，按拉丝的条纹先后消失），不是整片一起变淡
float tail = saturate((1.0 - u) * 2.2 - 0.9 * st + 0.15);
// 心线：刀尖那条边一道细而亮的线
float cw = max(Prm.y, 0.01);
float core = exp(-pow((1.0 - v) / cw, 2.0)) * pow(1.0 - u, 0.9) * VC.g;
float3 c = Color.rgb * (body * streak * tail * Prm.z * age * lead) + Core.rgb * (core * lead * lerp(1.0, tail, 0.6));
return c * (I * Prm.x * max(EAI, 1.0));
