// x = 粗糙度，y = 金属度：漆面按脏污变化，掉漆处是磨亮的金属，接缝里粗糙
float r = Rough + (clamp(S.y, 0.5, 1.6) - 1.0) * RoughVar;
r = lerp(r, WornRough, S.x);
r = lerp(r, 0.85, S.w * 0.8);
r -= saturate(S.z) * 0.12;
float m = lerp(Metallic, 1.0, S.x);
m = lerp(m, 0.0, S.w * 0.8);
return float2(saturate(r), saturate(m));
