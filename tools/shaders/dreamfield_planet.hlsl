// 梦核旷野图天上的大行星（M_DF_Planet，峡谷包 Texture_Planet_02 的花纹），不受光，边缘一圈淡粉的大气光。
// 输入：Tex 行星贴图颜色，N 世界法线，V 从相机指向这一点的方向；参数 Tint 染色，Glow 整体亮度，RimColor 边缘大气色，Lit 受光方向（世界，指向光源），
// Veil / VeilColor 往天空色蒙一层（第一版直接贴图 × 染色，又红又艳，像贴上去的；峡谷包渲染图里行星是半透明的淡粉）
float mu = saturate(dot(N, -V));
float limb = pow(mu, 0.35);
float day = saturate(dot(N, normalize(Lit)) * 0.8 + 0.45);
float3 body = Tex * Tint * Glow * limb * day;
float rim = pow(1.0 - mu, 3.0);
body = lerp(body, VeilColor, Veil);
return body + RimColor * rim;
