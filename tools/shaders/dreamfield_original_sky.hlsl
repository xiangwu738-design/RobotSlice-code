// 复用校园 nature_sky.hlsl 的视线、解析圆盘、边缘抗锯齿与程序星点方法。
// 梦核专用配色与行星表面；云交给体积云组件，不再在天空球上画二维噪声。
struct Noise {
 float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
 float n(float2 p) {
  float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
  return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
 }
 float fbm(float2 p) { return n(p)*.57+n(p*2.07+17)*.28+n(p*4.17+31)*.15; }
};
Noise noise;
float3 d=normalize(P-Camera);
float3 horizon=float3(1.15,.88,1.08), zenith=float3(.24,.53,.93);
float3 result=lerp(horizon,zenith,smoothstep(-.04,.72,d.z));
result+=Atmosphere.rgb*.22;
// 旧天空工程中的解析球面圆盘，改成无贴图的淡玫瑰色行星。
float3 md=normalize(float3(.848,.227,.48));
float radius=radians(14.0),dm=dot(d,md);
float3 tangent=d-md*dm;
float r=length(tangent)/sin(radius);
if (dm>0 && r<1.02) {
 float edge=1-smoothstep(1-max(fwidth(r),.0001),1+max(fwidth(r),.0001),r);
 float3 normal=normalize(tangent/sin(radius)-md*sqrt(saturate(1-r*r)));
 float3 right=normalize(cross(float3(0,0,1),md)),up=cross(md,right);
 float2 uv=float2(dot(normal,right),dot(normal,up));
 float warp=noise.fbm(uv*3.1+4);
 float bands=noise.fbm(float2(uv.x*5+warp*3,uv.y*12+warp*2));
 float3 albedo=lerp(float3(.72,.43,.52),float3(1.08,.81,.83),bands);
 float shade=.48+.52*saturate(dot(normal,normalize(float3(-.498,-.637,.588))));
 float rim=pow(saturate(r),10)*.22;
 float3 planet=lerp(albedo*shade+rim*float3(.55,.69,1),horizon,.34);
 result=lerp(result,planet,edge*.84);
}
return max(result,0);
