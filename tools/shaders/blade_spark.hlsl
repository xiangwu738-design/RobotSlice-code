// 刀尖洒的火花 / 光点（11-01 刀光第二版，UMechMeleeFx::UpdateSparks：每颗一个朝镜头、顺着速度拉长的面片）
// UV.x = 0 火花头 → 1 尾巴，UV.y 横向 0 → 1；VC = 颜色（0–1），VA = 这一颗现在多亮（随寿命淡掉、闪烁）
// Prm = (亮度倍数, 火花头的白热, 0, 0)；乘曝光倒数 EAI（白天夜里屏幕上一样亮）
float a = abs(UV.y * 2.0 - 1.0);
float x = saturate(UV.x);
float w = exp(-a * a * 4.0) * (1.0 - a * a);
float tail = pow(1.0 - x, 1.4);
float head = exp(-x * 7.0);
float3 c = lerp(VC, float3(1.0, 1.0, 1.0), head * Prm.y);
return c * (w * tail * VA * Prm.x * max(EAI, 1.0));
