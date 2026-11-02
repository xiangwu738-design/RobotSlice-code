// 三向投影的细节法线（whiteout 混合），输出物体空间法线，图里再转世界空间
float3 w = pow(abs(N), 6.0);
w /= max(w.x + w.y + w.z, 0.0001);
float2 ax = Texture2DSample(Tex, TexSampler, P.zy * Tile).xy * 2.0 - 1.0;
float2 ay = Texture2DSample(Tex, TexSampler, P.xz * Tile).xy * 2.0 - 1.0;
float2 az = Texture2DSample(Tex, TexSampler, P.xy * Tile).xy * 2.0 - 1.0;
float3 nx = float3(ax * Amount + N.zy, N.x);
float3 ny = float3(ay * Amount + N.xz, N.y);
float3 nz = float3(az * Amount + N.xy, N.z);
return normalize(nx.zyx * w.x + ny.xzy * w.y + nz.xyz * w.z);
