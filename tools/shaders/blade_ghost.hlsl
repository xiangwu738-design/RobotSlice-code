// 刀的残影（11-01 刀光第二版，重斩、闪避斩）：刀模型复制几份留在刚挥过的位置，只有一圈轮廓光加一层很淡的填充，很快淡掉
// Nrm 顶点法线（世界）、Cam 指向镜头的方向；Prm = (亮度（随时间淡）, 轮廓锐度, 刀身填充, 0)；乘曝光倒数 EAI
float f = 1.0 - saturate(abs(dot(normalize(Nrm), normalize(Cam))));
float rim = pow(f, Prm.y);
return Color * ((rim + Prm.z) * Prm.x * max(EAI, 1.0));
