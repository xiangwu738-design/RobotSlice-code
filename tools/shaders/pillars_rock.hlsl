// 石柱山水 M_PL_Rock：原包光栅照片岩石（UV，4K）上加世界投影的细节，贴近岩壁（放大 6 倍后 4K 贴图只有约 5 厘米一像素）不糊；苔藓按顶点色 R（原作 3 万多片藤蔓烤进去的位置）和朝上的面画。
// 输入：WP 世界坐标（水面 = 0），N 世界法线（几何），BaseN 已转到世界的 UV 法线，C 原包颜色，AO、Ro 原包 AO 和粗糙度，V 顶点色（R 藤蔓，G 离水高度）
//       DC_T/DC_S 细节颜色（朝上 / 朝侧面两个投影），DN_T/DN_S 细节法线，MN_T/MN_S 中尺度法线，SC_T/SC_S 苔藓颜色，SN_T/SN_S 苔藓法线，Noise 大尺度噪声
//       Mode 0 = 底色，1 = 世界法线，2 = 粗糙度
float top = smoothstep(0.35, 0.8, abs(N.z));
float3 dc = lerp(DC_S, DC_T, top);
float3 dn = lerp(DN_S, DN_T, top);
float3 mn = lerp(MN_S, MN_T, top);
float3 sc = lerp(SC_S, SC_T, top);
float3 sn = lerp(SN_S, SN_T, top);
// 投影平面内的切向（够用：细节是噪声一样的岩石纹，符号错了也看不出来）
float3 side = abs(N.x) > abs(N.y) ? float3(0, 1, 0) : float3(1, 0, 0);
float3 tA = lerp(side, float3(1, 0, 0), top);
float3 tB = lerp(float3(0, 0, 1), float3(0, 1, 0), top);
float dist = length(WP - CamPos);
float nearFade = 1.0 - smoothstep(9000.0, 22000.0, dist);   // 细节越远越淡（远处一片高频会闪）
float lumD = dot(dc, float3(0.3, 0.59, 0.11));
float grain = lerp(1.0, saturate(lumD / max(DetailMean, 0.05)) * 1.0, DetailColor * nearFade);
float3 col = pow(C, Gamma) * Tint;
col = lerp(dot(col, float3(0.3, 0.59, 0.11)).xxx, col, Saturation);
col *= lerp(1.0, AO, AOStrength);
col *= grain;
// 苔藓：顶点里烤的藤蔓位置（远看和原作一样的分布）+ 朝上的面，噪声打散成一块块
float wp2 = Noise.r;
float want = saturate(V.r * IvyWeight + smoothstep(0.55, 0.95, N.z) * TopWeight * (0.5 + wp2));
float lumM = dot(sc, float3(0.3, 0.59, 0.11));
float edge = want + (lumM - 0.28) * 0.9 + (wp2 - 0.5) * 0.5 - 0.5;
float moss = smoothstep(-MossSoft, MossSoft, edge) * step(0.001, want);
float3 mossCol = sc * MossTint * lerp(0.75, 1.25, wp2);
// 水线：离水 3 米以内发黑发湿，3–8 米之间苔藓少
float wet = 1.0 - smoothstep(30.0, 420.0, WP.z + (wp2 - 0.5) * 220.0);
moss *= smoothstep(100.0, 600.0, WP.z);
if (Mode > 1.5) {
    float r = lerp(Ro * RoughScale, 0.9, moss);
    return lerp(r, 0.25, wet * 0.8).xxx;
}
if (Mode > 0.5) {
    float3 n = BaseN * float3(BaseNScale, BaseNScale, 1.0);
    n = normalize(lerp(N, n, BaseNMix));
    float s = DetailN * nearFade;
    n += (tA * dn.x + tB * dn.y) * s;
    n += (tA * mn.x + tB * mn.y) * MidN;
    n = normalize(n);
    n = normalize(lerp(n, normalize(N + (tA * sn.x + tB * sn.y) * MossN), moss));
    return n;
}
col = lerp(col, mossCol, moss);
col *= lerp(1.0, 0.45, wet);
return col;
