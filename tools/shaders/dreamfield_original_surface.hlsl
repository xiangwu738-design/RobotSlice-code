// 自制世界空间材质：大尺度草色、细草叶与墙板刻线，不采样外部贴图。
struct Noise {
 float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
 float n(float2 p) {
  float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
  return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
 }
};
Noise noise;
float3 p=P*.01;
if (Kind<1.5) {
 float broad=noise.n(p.xy/61.)*.55+noise.n(p.xy/23.+19)*.30+noise.n(p.xy/9.7)*.15;
 float3 col=lerp(float3(.070,.155,.029),float3(.17,.235,.052),smoothstep(.08,.92,broad));
 // 黄绿中混入冷绿，草坪近处的叶纹、底层阴影由自制平铺纹理补足。
 float nearFade=1-smoothstep(1800,11000,distance(P,Camera));
 float small=noise.n(p.xy*5.7);
 col*=lerp(1.,.92+.14*small,nearFade*.5);
 if(Kind>.5) col*=lerp(.70,1.12,saturate(1-UV.y));
 return col*Tint;
}
// 世界坐标按主法线投影，接缝宽度由导数钳制，远处不闪。
float3 an=abs(N);
float2 q=an.x>an.y && an.x>an.z?p.yz:(an.y>an.z?p.xz:p.xy);
float2 grid=q/float2(2.1,3.7), f=frac(grid);
float edge=min(min(f.x,1-f.x),min(f.y,1-f.y));
float aa=max(length(fwidth(grid)),.0005);
float seam=1-smoothstep(.004,.004+aa,edge);
float shade=.85+.20*noise.hash(floor(grid));
float inset=step(.12,f.x)*step(f.x,.87)*step(.15,f.y)*step(f.y,.86);
float etch=(1-smoothstep(.002,.002+aa,abs(f.y-.72)))*inset;
float grime=noise.n(q*float2(.42,.055)+31);
float grit=noise.n(q*19.);
float runoff=.91+.09*grime;
return Tint*shade*runoff*(.96+.04*grit)*(1-.27*seam-.07*etch);
