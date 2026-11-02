// 梦核旷野图的天空球（M_DF_Sky）第一步：视线方向（从相机指向天空球上这一点），绕 Z 转 Angle 度后拿去采 HDR 立方体贴图。
// 天空球半径几十公里，方向只看视线，所以和天空球放在哪无关（相当于无穷远）。
float3 d = normalize(WP - CamPos);
float a = radians(Angle);
float s = sin(a);
float c = cos(a);
return float3(c * d.x + s * d.y, -s * d.x + c * d.y, d.z);
