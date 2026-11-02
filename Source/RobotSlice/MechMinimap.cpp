#include "MechMinimap.h"
#include "MechLoop.h"
#include "MechSettings.h"

#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "CanvasItem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechCoreArena.h"
#include "MechCoreDirector.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHUD.h"
#include "MechHealthComponent.h"
#include "MechHudOverlay.h"
#include "MechHudTheme.h"
#include "MechPluginPod.h"
#include "MechSmoke.h"

namespace
{
	// 和 MechHUD.cpp 同一套色：主色 = 自己，蓝 = 队友，红 = 敌人，橙 = 提醒，金 = 核心。
	// 10-31 起是当前 HUD 主题（MechHudTheme.h）成员的引用：小地图 / 大地图 / 头顶名字跟着本机框架制造商的主题变
	const FLinearColor& Ink = MechHudTheme::Get().Ink;
	const FLinearColor& Dim = MechHudTheme::Get().Dim;
	const FLinearColor& Faint = MechHudTheme::Get().Faint;
	const FLinearColor& Accent = MechHudTheme::Get().Accent;
	const FLinearColor& Ally = MechHudTheme::Get().Ally;
	const FLinearColor& Enemy = MechHudTheme::Get().Enemy;
	const FLinearColor& Warn = MechHudTheme::Get().Warn;
	const FLinearColor& Gold = MechHudTheme::Get().Gold;
	const FLinearColor& Shade = MechHudTheme::Get().Shade;

	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	/** 从上往下打一条线，跳过隐形的东西（山谷的隐形顶棚、边界墙），返回第一个看得见的面的高度 */
	bool VisibleGroundZ(UWorld* World, const FVector2D& XY, float Top, float Bottom, float& OutZ)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechMapProbe), true);
		FVector From(XY.X, XY.Y, Top);
		const FVector To(XY.X, XY.Y, Bottom);
		for (int32 Tries = 0; Tries < 6; ++Tries)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
			{
				return false;
			}
			const UPrimitiveComponent* Comp = Hit.GetComponent();
			const AActor* Actor = Hit.GetActor();
			const bool bHidden = !Comp || !Comp->IsVisible() || Comp->bHiddenInGame || (Actor && (Actor->IsHidden() || Actor->IsA<APawn>()));
			if (!bHidden)
			{
				OutZ = Hit.ImpactPoint.Z;
				return true;
			}
			if (Actor)
			{
				Params.AddIgnoredActor(Actor);
			}
			From.Z = Hit.ImpactPoint.Z - 1.f;
		}
		return false;
	}

	/** 整张地图贴图的处理（后台线程）：底色去饱和压暗 + 山体明暗 + 陡坎描线 + 等高线，界外压暗；输出 sRGB 的 BGRA */
	TArray<FColor> BuildMapImage(const TArray<FColor>& Color, const TArray<float>& InHeight, int32 W, int32 H, float CmPerPx, FIntRect PlayPx, float VoidZ)
	{
		TArray<FColor> Out;
		Out.SetNumUninitialized(W * H);
		// 大场地（山谷：半米多一像素）拍出来的深度是一块块约 12 米的平台阶（原因没查清，同一套设置街道 10 厘米一像素时是好的），
		// 直接算明暗 / 等高线满屏阶梯纹 → 先按 12 米做两遍盒式模糊（只平均有效像素），真正的陡崖照样陡
		TArray<float> Height = InHeight;
		if (CmPerPx > 25.f)
		{
			const int32 Rad = FMath::Clamp(FMath::RoundToInt(1200.f / CmPerPx), 2, 40);
			TArray<float> Tmp;
			Tmp.SetNumUninitialized(W * H);
			for (int32 Pass = 0; Pass < 4; ++Pass)
			{
				const bool bX = Pass % 2 == 0;
				const TArray<float>& Src = bX ? Height : Tmp;
				TArray<float>& Dst = bX ? Tmp : Height;
				const int32 Lines = bX ? H : W, Len = bX ? W : H;
				for (int32 l = 0; l < Lines; ++l)
				{
					auto Idx = [&](int32 k) { return bX ? l * W + k : k * W + l; };
					double Sum = 0.0;
					int32 Cnt = 0;
					for (int32 k = 0; k < FMath::Min(Rad, Len); ++k)
					{
						if (Src[Idx(k)] > VoidZ) { Sum += Src[Idx(k)]; ++Cnt; }
					}
					for (int32 k = 0; k < Len; ++k)
					{
						const int32 In = k + Rad, Outk = k - Rad - 1;
						if (In < Len && Src[Idx(In)] > VoidZ) { Sum += Src[Idx(In)]; ++Cnt; }
						if (Outk >= 0 && Src[Idx(Outk)] > VoidZ) { Sum -= Src[Idx(Outk)]; --Cnt; }
						Dst[Idx(k)] = (InHeight[Idx(k)] > VoidZ && Cnt > 0) ? float(Sum / Cnt) : InHeight[Idx(k)];
					}
				}
			}
		}
		auto Valid = [&](int32 i) { return Height[i] > VoidZ; };
		auto Lum = [](const FColor& C) {
			// 底色是线性值，先转成接近人眼的亮度再统计
			const float L = (0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B) / 255.f;
			return FMath::Pow(FMath::Max(L, 0.f), 1.f / 2.2f);
		};

		// 活动范围里抽样：高度、亮度的分位数（去掉极端值，不让一棵大树或一块亮招牌把整张图压暗）
		TArray<float> Hs, Ls;
		for (int32 y = PlayPx.Min.Y; y < PlayPx.Max.Y; y += 3)
		{
			for (int32 x = PlayPx.Min.X; x < PlayPx.Max.X; x += 3)
			{
				const int32 i = y * W + x;
				if (x >= 0 && y >= 0 && x < W && y < H && Valid(i))
				{
					Hs.Add(Height[i]);
					Ls.Add(Lum(Color[i]));
				}
			}
		}
		auto Pct = [](TArray<float>& A, float P) { return A.Num() ? A[FMath::Clamp(FMath::FloorToInt(P * (A.Num() - 1)), 0, A.Num() - 1)] : 0.f; };
		Hs.Sort();
		Ls.Sort();
		const float H0 = Pct(Hs, 0.02f), H1 = FMath::Max(Pct(Hs, 0.98f), H0 + 100.f);
		const float L0 = Pct(Ls, 0.05f), L1 = FMath::Max(Pct(Ls, 0.95f), L0 + 0.05f);
		// 等高线间距：整片高差分成十来份，取整到 2 / 2.5 / 5 米……；高差不到 6 米的平地图（街道、灰盒）不画等高线，只靠墙边描线
		float Interval = 0.f;
		if (H1 - H0 > 600.f)
		{
			static const float Steps[] = {200.f, 250.f, 500.f, 1000.f, 2000.f, 2500.f, 5000.f};
			Interval = Steps[UE_ARRAY_COUNT(Steps) - 1];
			for (const float S : Steps)
			{
				if (S >= (H1 - H0) / 12.f)
				{
					Interval = S;
					break;
				}
			}
		}
		// 陡坎 / 墙：相邻像素高差超过这个就开始描线（按像素大小放宽，山谷图 0.5 米一像素时陡坡不至于满屏都是线）
		const float Edge0 = FMath::Max(80.f, CmPerPx * 1.3f);
		const float Edge1 = Edge0 * 2.6f;
		const FVector3f Bg(0.020f, 0.032f, 0.040f);
		const FVector3f Lo(0.045f, 0.075f, 0.085f);
		const FVector3f Hi(0.300f, 0.385f, 0.395f);
		const FVector3f Line(0.365f, 0.792f, 0.647f);
		const FVector3f Light = FVector3f(-0.55f, -0.75f, 1.f).GetSafeNormal();  // 光从左上（世界 −X −Y）来

		for (int32 y = 0; y < H; ++y)
		{
			for (int32 x = 0; x < W; ++x)
			{
				const int32 i = y * W + x;
				FVector3f C = Bg;
				const bool bBorder = x < 2 || y < 2 || x >= W - 2 || y >= H - 2;  // 贴图边缘留底色（采样夹边时外面是暗的）
				if (!bBorder && Valid(i))
				{
					const float h = Height[i];
					auto At = [&](int32 xx, int32 yy) {
						const int32 j = FMath::Clamp(yy, 0, H - 1) * W + FMath::Clamp(xx, 0, W - 1);
						return Valid(j) ? Height[j] : h;
					};
					const float hl = At(x - 1, y), hr = At(x + 1, y), hu = At(x, y - 1), hd = At(x, y + 1);
					// 山体明暗：按高度梯度算法线，平地 = 1
					const FVector3f N = FVector3f(-(hr - hl) / (2.f * CmPerPx), -(hd - hu) / (2.f * CmPerPx), 1.f).GetSafeNormal();
					const float Lit = FMath::Clamp(FVector3f::DotProduct(N, Light) / Light.Z, 0.3f, 1.45f);
					// 陡坎 / 墙边：四邻里最大的高差；旁边是虚空（灰盒的边缘）也算
					float Diff = FMath::Max(FMath::Max(FMath::Abs(h - hl), FMath::Abs(h - hr)), FMath::Max(FMath::Abs(h - hu), FMath::Abs(h - hd)));
					for (const int32 j : {i - 1, i + 1, i - W, i + W})
					{
						if (!Valid(j))
						{
							Diff = Edge1;
						}
					}
					const float EdgeK = FMath::SmoothStep(Edge0, Edge1, Diff);
					const float LN = FMath::Clamp((Lum(Color[i]) - L0) / (L1 - L0), 0.f, 1.f);
					const float EN = FMath::Clamp((h - H0) / (H1 - H0), 0.f, 1.f);
					// 亮度主要看高低（高处亮、低处暗，一眼看出楼 / 山 / 平台），底色只占三成多（路面、草地的斑驳纹理别太花）
					C = FMath::Lerp(Lo, Hi, FMath::Clamp(0.36f * LN + 0.44f * EN + 0.1f, 0.f, 1.f)) * Lit;
					// 留一点原来的颜色（草地偏绿、水偏蓝、路面偏灰），去饱和后压暗
					const FColor& O = Color[i];
					FVector3f Orig(FMath::Pow(O.R / 255.f, 1.f / 2.2f), FMath::Pow(O.G / 255.f, 1.f / 2.2f), FMath::Pow(O.B / 255.f, 1.f / 2.2f));
					const float OL = (Orig.X + Orig.Y + Orig.Z) / 3.f;
					Orig = FMath::Lerp(FVector3f(OL), Orig, 0.45f) * 0.55f * Lit;
					C = FMath::Lerp(C, Orig, 0.22f);
					// 等高线（陡坎上已经描了线，不再叠）
					if (Interval > 0.f && EdgeK < 0.3f)
					{
						const int32 K = FMath::FloorToInt(h / Interval);
						const int32 KR = FMath::FloorToInt(hr / Interval);
						const int32 KD = FMath::FloorToInt(hd / Interval);
						if (K != KR || K != KD)
						{
							const bool bMajor = FMath::Max(K, FMath::Max(KR, KD)) % 5 == 0;
							C = FMath::Lerp(C, Line * 0.85f, bMajor ? 0.26f : 0.11f);
						}
					}
					C = FMath::Lerp(C, Line, 0.62f * EdgeK);
				}
				// 活动范围外压暗 + 稀疏斜纹
				if (!PlayPx.Contains(FIntPoint(x, y)))
				{
					C *= 0.42f;
					if ((x + y) % 14 == 0)
					{
						C = FMath::Lerp(C, Line * 0.35f, 0.3f);
					}
				}
				Out[i] = FColor(uint8(FMath::Clamp(C.X, 0.f, 1.f) * 255.f + 0.5f), uint8(FMath::Clamp(C.Y, 0.f, 1.f) * 255.f + 0.5f),
					uint8(FMath::Clamp(C.Z, 0.f, 1.f) * 255.f + 0.5f), 255);
			}
		}
		return Out;
	}

	/** 好读的格子大小（厘米）：整片大约分成 Count 格 */
	float NiceStep(float Span, float Count)
	{
		static const float Steps[] = {500.f, 1000.f, 2000.f, 2500.f, 5000.f, 10000.f, 20000.f, 25000.f, 50000.f};
		for (const float S : Steps)
		{
			if (S >= Span / Count)
			{
				return S;
			}
		}
		return Steps[UE_ARRAY_COUNT(Steps) - 1];
	}
}

// ================================================================ 场地俯视图
void UMechMapCapture::StartWithoutImage(const FBox2D& InPlay)
{
	Play = InPlay;
	Covered = InPlay.ExpandBy(FMath::Max(2000.f, 0.06f * FMath::Max(InPlay.GetSize().X, InPlay.GetSize().Y)));
	bHasFrame = true;
	bWantImage = false;
	UE_LOG(LogTemp, Display, TEXT("[map] 不拍底图（不能渲染），只用场地框 %s"), *Play.ToString());
}

void UMechMapCapture::Start(AActor* Owner, const FBox2D& InPlay, float InRefZ, float InVoidZ)
{
	Play = InPlay;
	RefZ = InRefZ;
	VoidZ = InVoidZ;
	bHasFrame = true;
	bWantImage = true;
	StartSeconds = FPlatformTime::Seconds();
	UWorld* World = Owner->GetWorld();

	// 拍摄范围：活动范围外放一圈，按 512 像素一块铺满；最细 12 厘米一像素，长边最多 4 块（2048 像素）
	const FVector2D PlaySize = Play.GetSize();
	const FBox2D Want = Play.ExpandBy(FMath::Max(2000.f, 0.06f * FMath::Max(PlaySize.X, PlaySize.Y)));
	const FVector2D WantSize = Want.GetSize();
	const float Long = FMath::Max(WantSize.X, WantSize.Y);
	const int32 LongTiles = FMath::Clamp(FMath::CeilToInt(Long / 12.f / Tile), 1, 4);
	CmPerPx = Long / (LongTiles * Tile);
	TilesX = FMath::Clamp(FMath::CeilToInt(WantSize.X / (CmPerPx * Tile) - 0.001f), 1, 4);
	TilesY = FMath::Clamp(FMath::CeilToInt(WantSize.Y / (CmPerPx * Tile) - 0.001f), 1, 4);
	const FVector2D Cover(TilesX * Tile * CmPerPx, TilesY * Tile * CmPerPx);
	Covered = FBox2D(Want.GetCenter() - Cover * 0.5f, Want.GetCenter() + Cover * 0.5f);

	// 镜头高度：场地上最高的看得见的面再往上 20 米（打 9×5 条线；隐形顶棚 / 墙跳过）。太高的话离得远的树会按剔除距离消失
	float Top = -UE_BIG_NUMBER;
	for (int32 j = 0; j <= 4; ++j)
	{
		for (int32 i = 0; i <= 8; ++i)
		{
			float Z = 0.f;
			const FVector2D XY(FMath::Lerp(Covered.Min.X, Covered.Max.X, i / 8.f), FMath::Lerp(Covered.Min.Y, Covered.Max.Y, j / 4.f));
			if (VisibleGroundZ(World, XY, RefZ + 60000.f, RefZ - 60000.f, Z))
			{
				Top = FMath::Max(Top, Z);
			}
		}
	}
	CamZ = (Top > -UE_BIG_NUMBER ? FMath::Min(Top, RefZ + 40000.f) : RefZ + 5000.f) + 2000.f;

	const int32 W = TilesX * Tile;
	const int32 H = TilesY * Tile;
	Color.SetNumZeroed(W * H);
	Height.Init(-1e9f, W * H);

	ColorRT = NewObject<UTextureRenderTarget2D>(this);
	ColorRT->ClearColor = FLinearColor::Black;
	ColorRT->InitCustomFormat(Tile, Tile, PF_B8G8R8A8, true);
	ColorRT->UpdateResourceImmediate(true);
	DepthRT = NewObject<UTextureRenderTarget2D>(this);
	DepthRT->ClearColor = FLinearColor::Black;
	DepthRT->InitCustomFormat(Tile, Tile, PF_R32_FLOAT, true);
	DepthRT->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(Owner, TEXT("MechMapCapture"));
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = false;
	Capture->ProjectionType = ECameraProjectionMode::Orthographic;
	Capture->OrthoWidth = Tile * CmPerPx;
	Capture->bAutoCalculateOrthoPlanes = false;
	Capture->bUpdateOrthoPlanes = false;
	// 远平面一定要给：默认是个天文数字，正交投影的深度是线性的，32 位浮点在那么大的范围里一格就是好几米，
	// 读出来的高度一层层台阶（山谷图满屏阶梯纹）。给到「镜头到虚空线」再加一块的对角，精度到毫米级
	Capture->MaxViewDistanceOverride = FMath::Clamp(CamZ - FMath::Max(VoidZ, CamZ - 300000.f) + 0.75f * Tile * CmPerPx + 5000.f, 10000.f, 400000.f);
	// 只要底色和深度：灯光、阴影、全局光照、雾、大气、粒子、半透明、草、机体都不要（又省又干净）
	FEngineShowFlags& F = Capture->ShowFlags;
	F.SetDynamicShadows(false);
	F.SetLumenGlobalIllumination(false);
	F.SetLumenReflections(false);
	F.SetGlobalIllumination(false);
	F.SetAmbientOcclusion(false);
	F.SetScreenSpaceAO(false);
	F.SetDistanceFieldAO(false);
	F.SetScreenSpaceReflections(false);
	F.SetReflectionEnvironment(false);
	F.SetContactShadows(false);
	F.SetCapsuleShadows(false);
	F.SetRayTracedDistanceFieldShadows(false);
	F.SetFog(false);
	F.SetVolumetricFog(false);
	F.SetAtmosphere(false);
	F.SetCloud(false);
	F.SetLightShafts(false);
	F.SetPostProcessing(false);
	F.SetBloom(false);
	F.SetMotionBlur(false);
	F.SetEyeAdaptation(false);
	F.SetLocalExposure(false);
	F.SetTemporalAA(false);
	F.SetAntiAliasing(false);
	F.SetParticles(false);
	F.SetTranslucency(false);
	F.SetSkeletalMeshes(false);
	F.SetInstancedGrass(false);
	Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	Capture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	Capture->PostProcessSettings.bOverride_ReflectionMethod = true;
	Capture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	Capture->PostProcessBlendWeight = 1.f;
	Capture->RegisterComponent();
	NextTile = 0;
	UE_LOG(LogTemp, Display, TEXT("[map] 开拍：活动范围 %.0f×%.0f 米，底图 %d×%d（%d×%d 块，%.1f 厘米/像素），镜头高 %.0f 米（参考地面 %.0f）"),
		PlaySize.X / 100.f, PlaySize.Y / 100.f, W, H, TilesX, TilesY, CmPerPx, CamZ / 100.f, RefZ / 100.f);
}

void UMechMapCapture::CaptureTile(int32 Index)
{
	UWorld* World = Capture ? Capture->GetWorld() : nullptr;
	if (!World || !ColorRT || !DepthRT)
	{
		bFailed = true;
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	// 会动的东西每块现找：机体（含武器、背后挂件）、核心、插件舱、弹体烟雾
	Capture->HiddenActors.Reset();
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA<APawn>() || It->IsA<AMechCoreDirector>() || It->IsA<AMechPluginPod>() || It->IsA<AMechSmoke>() || (It->GetOwner() && It->GetOwner()->IsA<APawn>()))
		{
			Capture->HiddenActors.Add(*It);
		}
	}
	const int32 TX = Index % TilesX;
	const int32 TY = Index / TilesX;
	const float TileCm = Tile * CmPerPx;
	const FVector2D C(Covered.Min.X + (TX + 0.5f) * TileCm, Covered.Min.Y + (TY + 0.5f) * TileCm);
	// 朝正下方、画面向右 = 世界 +X、向下 = 世界 +Y（俯仰 −90°，偏航 −90°）
	Capture->SetWorldLocationAndRotation(FVector(C.X, C.Y, CamZ), FRotator(-90.f, -90.f, 0.f));
	Capture->TextureTarget = ColorRT;
	Capture->CaptureSource = ESceneCaptureSource::SCS_BaseColor;
	Capture->ShowFlags.SetInstancedFoliage(true);
	Capture->CaptureScene();
	// 高度只拍地形和建筑：树冠一棵棵都会被当成陡坎描一圈线（山谷图满屏青色小圈）。树林在底色里照样看得出（偏暗）
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(TEXT("CoreObstacles")))
		{
			Capture->HiddenActors.Add(*It);  // 山谷图自己摆的树和石头（ue_valley_arena.py）
		}
	}
	Capture->ShowFlags.SetInstancedFoliage(false);
	Capture->TextureTarget = DepthRT;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
	Capture->CaptureScene();
	TArray<FColor> Px;
	TArray<FLinearColor> Dp;
	FTextureRenderTargetResource* CR = ColorRT->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* DR = DepthRT->GameThread_GetRenderTargetResource();
	if (!CR || !DR || !CR->ReadPixels(Px) || !DR->ReadLinearColorPixels(Dp) || Px.Num() != Tile * Tile || Dp.Num() != Tile * Tile)
	{
		UE_LOG(LogTemp, Warning, TEXT("[map] 第 %d 块读回失败"), Index);
		bFailed = true;
		return;
	}
	const int32 W = TilesX * Tile;
	for (int32 y = 0; y < Tile; ++y)
	{
		for (int32 x = 0; x < Tile; ++x)
		{
			const int32 Src = y * Tile + x;
			const int32 Dst = (TY * Tile + y) * W + TX * Tile + x;
			Color[Dst] = Px[Src];
			const float D = Dp[Src].R;
			const float Z = CamZ - D;
			Height[Dst] = (FMath::IsFinite(D) && D > 1.f && D < 1e7f && Z > VoidZ) ? Z : -1e9f;
		}
	}
	// 校验方向和高度：块里三个不对称的点打线比对（镜像 / 转了 90° 的话起伏地形上会差很多）
	for (const FIntPoint& Q : {FIntPoint(Tile / 4, Tile / 4), FIntPoint(Tile * 3 / 4, Tile / 4), FIntPoint(Tile / 4, Tile * 5 / 8)})
	{
		const int32 Dst = (TY * Tile + Q.Y) * W + TX * Tile + Q.X;
		const FVector2D XY(Covered.Min.X + (TX * Tile + Q.X + 0.5f) * CmPerPx, Covered.Min.Y + (TY * Tile + Q.Y + 0.5f) * CmPerPx);
		float Z = 0.f;
		if (Height[Dst] > VoidZ && VisibleGroundZ(World, XY, CamZ, VoidZ, Z))
		{
			CheckErrSum += FMath::Abs(Z - Height[Dst]);
			++CheckCount;
		}
	}
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	CaptureMs += Ms;
}

void UMechMapCapture::Step()
{
	if (!bHasFrame || !bWantImage || bReady || bFailed)
	{
		return;
	}
	if (bProcessing)
	{
		if (!Processing.IsReady())
		{
			return;
		}
		bProcessing = false;
		TArray<FColor> Out = Processing.Consume();
		const int32 W = TilesX * Tile;
		const int32 H = TilesY * Tile;
		if (Out.Num() != W * H)
		{
			bFailed = true;
			return;
		}
		Texture = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, TEXT("MechMapBase"));
		if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.IsEmpty())
		{
			bFailed = true;
			return;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->LODGroup = TEXTUREGROUP_UI;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Data, Out.GetData(), Out.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Texture);
		Brush->ImageSize = FVector2D(W, H);
		Brush->DrawAs = ESlateBrushDrawType::Image;
		bReady = true;
		if (FParse::Param(FCommandLine::Get(), TEXT("MechMapTest")))
		{
			// 测试：底图存一份 BMP 方便直接看
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Map");
			IFileManager::Get().MakeDirectory(*Dir, true);
			FFileHelper::CreateBitmap(*(Dir / FString::Printf(TEXT("mapbase_%s.bmp"), *GetWorld()->GetMapName())), W, H, Out.GetData());
		}
		UE_LOG(LogTemp, Display, TEXT("[map] 底图完成：拍摄共 %.0f 毫秒（%d 块），从开拍到能用 %.1f 秒，贴图 %d×%d（%.1f MB）"), CaptureMs, TilesX * TilesY,
			FPlatformTime::Seconds() - StartSeconds, W, H, W * H * 4 / 1048576.f);
		return;
	}
	if (NextTile < TilesX * TilesY)
	{
		// 一帧拍一块（每块要等 GPU 拍完读回，十几毫秒；一次拍完整张会卡一下）
		CaptureTile(NextTile++);
		if (bFailed || NextTile < TilesX * TilesY)
		{
			if (bFailed)
			{
				Finish();
			}
			return;
		}
		Finish();
	}
}

void UMechMapCapture::Finish()
{
	UE_LOG(LogTemp, Display, TEXT("[map] 高度校验：%d 个点平均差 %.0f 厘米（几十厘米以内 = 方向和高度都对）"), CheckCount,
		CheckCount ? CheckErrSum / CheckCount : -1.0);
	if (Capture)
	{
		Capture->DestroyComponent();
		Capture = nullptr;
	}
	// 渲染目标用完就放掉（显存紧）
	for (UTextureRenderTarget2D* RT : {ColorRT.Get(), DepthRT.Get()})
	{
		if (RT)
		{
			RT->ReleaseResource();
		}
	}
	ColorRT = nullptr;
	DepthRT = nullptr;
	if (bFailed)
	{
		Color.Empty();
		Height.Empty();
		return;
	}
	const int32 W = TilesX * Tile;
	const int32 H = TilesY * Tile;
	if (FParse::Param(FCommandLine::Get(), TEXT("MechMapTest")) && GetWorld())
	{
		// 测试：原始高度（灰度，虚空是暗红）和原始底色各存一张，调处理参数时对照看
		float Lo = UE_BIG_NUMBER, Hi = -UE_BIG_NUMBER;
		for (const float Z : Height)
		{
			if (Z > VoidZ)
			{
				Lo = FMath::Min(Lo, Z);
				Hi = FMath::Max(Hi, Z);
			}
		}
		TArray<FColor> Dbg;
		Dbg.SetNumUninitialized(W * H);
		for (int32 i = 0; i < W * H; ++i)
		{
			const uint8 V = uint8(FMath::Clamp((Height[i] - Lo) / FMath::Max(Hi - Lo, 1.f), 0.f, 1.f) * 255.f);
			Dbg[i] = Height[i] > VoidZ ? FColor(V, V, V, 255) : FColor(60, 0, 0, 255);
		}
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Map");
		IFileManager::Get().MakeDirectory(*Dir, true);
		FFileHelper::CreateBitmap(*(Dir / FString::Printf(TEXT("mapheight_%s.bmp"), *GetWorld()->GetMapName())), W, H, Dbg.GetData());
		FFileHelper::CreateBitmap(*(Dir / FString::Printf(TEXT("mapcolor_%s.bmp"), *GetWorld()->GetMapName())), W, H, Color.GetData());
		UE_LOG(LogTemp, Display, TEXT("[map] 高度范围 %.0f ~ %.0f 米"), Lo / 100.f, Hi / 100.f);
	}
	const FIntRect PlayPx(FIntPoint(FMath::FloorToInt((Play.Min.X - Covered.Min.X) / CmPerPx), FMath::FloorToInt((Play.Min.Y - Covered.Min.Y) / CmPerPx)),
		FIntPoint(FMath::CeilToInt((Play.Max.X - Covered.Min.X) / CmPerPx), FMath::CeilToInt((Play.Max.Y - Covered.Min.Y) / CmPerPx)));
	// 后台线程处理（2048×1536 要几十到一百多毫秒，放游戏线程会卡一帧）
	Processing = Async(EAsyncExecution::ThreadPool, [Color = MoveTemp(Color), Height = MoveTemp(Height), W, H, Cm = CmPerPx, PlayPx, Void = VoidZ]() {
		return BuildMapImage(Color, Height, W, H, Cm, PlayPx, Void);
	});
	bProcessing = true;
}

FVector2f UMechMapCapture::ToUV(const FVector2D& World) const
{
	const FVector2D S = Covered.GetSize();
	return FVector2f((World.X - Covered.Min.X) / FMath::Max(S.X, 1.0), (World.Y - Covered.Min.Y) / FMath::Max(S.Y, 1.0));
}

// ================================================================ HUD：拍图、找图标
bool AMechHUD::IsMapKeyDown() const
{
	return MechKeys::IsDown(PlayerOwner, EMechKey::Map);  // 默认 M，设置里能改键
}

void AMechHUD::UpdateMapCapture()
{
	if (MapCapture && MapCapture->HasFrame())
	{
		MapCapture->Step();
		return;
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const AMechCharacter* Focus = GetFocusMech();
	if (!GS || GS->bMenuMode || (Focus && Focus->IsViewerMode()))
	{
		return;
	}
	MapCaptureWait += GetWorld()->GetDeltaSeconds();
	if (MapCaptureWait < 0.4f)
	{
		return;  // 等地图和场地 actor 都就位（夺芯灰盒在场地 actor 的 BeginPlay 里现搭）
	}
	FBox2D Play(ForceInit);
	float RefZ = 0.f;
	float VoidZ = -1e9f;
	const AMechCoreArena* Arena = nullptr;
	for (TActorIterator<AMechCoreArena> It(GetWorld()); It; ++It)
	{
		Arena = *It;
		break;
	}
	if (GS->bCoreMode && Arena)
	{
		// 夺芯 / 山谷：场地 actor 自己知道范围（小地图形状也是按这个范围采样的）
		const FVector2D O(Arena->GetActorLocation());
		Play = FBox2D(O + Arena->MapMin(), O + Arena->MapMax());
		RefZ = Arena->FoundryLocation().Z;
		VoidZ = Arena->KillZ();
	}
	else if (!GS->bCoreMode && !GS->MapExtent.IsZero())
	{
		// 死斗：场地中心 ± AI 活动半径（放宽一点），再包住两队出生点
		const FVector2D Ctr(GS->MapCenter);
		const FVector2D Ext = GS->MapExtent * 1.15f;
		Play = FBox2D(Ctr - Ext, Ctr + Ext);
		for (const FVector& S : GS->MapSpawn)
		{
			Play += FBox2D(FVector2D(S) - FVector2D(1500.f), FVector2D(S) + FVector2D(1500.f));
		}
		RefZ = GS->MapCenter.Z;
		VoidZ = RefZ - 6000.f;
	}
	else if (MapCaptureWait < 10.f)
	{
		return;  // 客户端上 GameState / 场地 actor 还没复制过来
	}
	else
	{
		// 兜底：玩家出生点周围 150 米
		FVector Base = Focus ? Focus->GetActorLocation() : FVector::ZeroVector;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			Base = It->GetActorLocation();
			break;
		}
		Play = FBox2D(FVector2D(Base) - FVector2D(7500.f), FVector2D(Base) + FVector2D(7500.f));
		RefZ = Base.Z;
		VoidZ = RefZ - 6000.f;
		UE_LOG(LogTemp, Warning, TEXT("[map] 没拿到场地框，用出生点周围 150 米"));
	}
	MapCapture = NewObject<UMechMapCapture>(this);
	if (FApp::CanEverRender())
	{
		MapCapture->Start(this, Play, RefZ, VoidZ);
	}
	else
	{
		MapCapture->StartWithoutImage(Play);
	}
}

void AMechHUD::UpdateSpotting(AMechCharacter* Mech, int32 MyTeam)
{
	MapSpotClock -= GetWorld()->GetDeltaSeconds();
	if (MapSpotClock > 0.f || !PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	MapSpotClock = 0.2f;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Eye = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FVector Fwd = PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector();
	const float CosView = FMath::Cos(FMath::DegreesToRadians(FMath::Min(PlayerOwner->PlayerCameraManager->GetFOVAngle() * 0.5f + 12.f, 89.f)));
	TArray<AMechCharacter*> Friends;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() == MyTeam && !It->GetHealth()->IsDead() && !It->IsTrainingDummy())
		{
			Friends.Add(*It);
		}
	}
	auto Clear = [this](const FVector& From, AActor* Ignore, AMechCharacter* Foe) {
		const FVector To = MechLoop::Near(GetWorld(), Foe->GetActorLocation(), From) + FVector(0.f, 0.f, 60.f);  // 老街循环
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MechMapSpot), false, Ignore);
		Query.AddIgnoredActor(Foe);
		return !GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Query) && !AMechSmoke::BlocksLine(GetWorld(), From, To);
	};
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* Foe = *It;
		if (Foe->GetTeam() == MyTeam || Foe->IsTrainingDummy())
		{
			continue;
		}
		if (Foe->GetHealth()->IsDead())
		{
			MapSpots.Remove(Foe);
			continue;
		}
		const FVector At = MechLoop::Near(GetWorld(), Foe->GetActorLocation(), Eye);  // 老街循环：离自己镜头最近的那份
		// 刚被打中（我方谁打的都算）、离任一队友 35 米内
		bool bSeen = Now - Foe->GetLastDamagedTime() < 0.4f;
		bSeen = bSeen || Friends.ContainsByPredicate([&At](const AMechCharacter* F) { return MechLoop::Dist(F->GetWorld(), F->GetActorLocation(), At) < 3500.f; });
		// 自己的镜头：视野里、250 米内、没被挡、没隔着烟
		if (!bSeen && FVector::Dist(Eye, At) < 25000.f && FVector::DotProduct((At - Eye).GetSafeNormal(), Fwd) > CosView)
		{
			bSeen = Clear(Eye, Mech, Foe);
		}
		// 队友（真人、AI 都算）：100 米内看得见
		for (int32 k = 0; !bSeen && k < Friends.Num(); ++k)
		{
			AMechCharacter* F = Friends[k];
			if (F != Mech && MechLoop::Dist(GetWorld(), F->GetActorLocation(), At) < 10000.f)
			{
				bSeen = Clear(F->GetActorLocation() + FVector(0.f, 0.f, 90.f), F, Foe);
			}
		}
		if (bSeen)
		{
			FMapSpot& S = MapSpots.FindOrAdd(Foe);
			S.Pos = At;
			S.Yaw = Foe->GetActorRotation().Yaw;
			S.Time = Now;
		}
	}
	for (auto It = MapSpots.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

void AMechHUD::GatherMapIcons(AMechCharacter* Mech, TArray<FMechMapIcon>& Out)
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const AMechPlayerController* PC = Cast<AMechPlayerController>(PlayerOwner);
	const int32 MyTeam = Mech ? Mech->GetTeam() : PC ? PC->GetMenuTeam() : 0;
	const float Now = GetWorld()->GetTimeSeconds();
	auto TeamColor = [MyTeam](int32 Team) { return Team < 0 ? Gold : Team == MyTeam ? Ally : Enemy; };
	auto Add = [&Out](FMechMapIcon::EType Type, const FVector& At, const FLinearColor& C) -> FMechMapIcon& {
		FMechMapIcon& I = Out.AddDefaulted_GetRef();
		I.Type = Type;
		I.At = FVector2D(At);
		I.Color = C;
		return I;
	};

	// 团队淘汰 / 团队死斗：两队出生区（淡淡的括号，认方向用）；个人死斗到处重生，不画
	if (GS && !GS->bCoreMode && !GS->IsSoloDM() && !GS->MapExtent.IsZero())
	{
		for (int32 Team = 0; Team < 2; ++Team)
		{
			Add(FMechMapIcon::Spawn, GS->MapSpawn[Team], TeamColor(Team)).Alpha = 0.45f;
		}
	}
	// 夺芯：铸造台、回收站、核心（规则和原来的夺芯小地图一样）
	AMechCoreDirector* Dir = nullptr;
	if (GS && GS->bCoreMode)
	{
		for (TActorIterator<AMechCoreDirector> It(GetWorld()); It; ++It)
		{
			Dir = *It;
			break;
		}
	}
	if (Dir && Dir->Arena)
	{
		const float SNow = GS->GetServerWorldTimeSeconds();
		const bool bBlink = FMath::Fmod(Now, 0.5f) < 0.25f;
		Add(FMechMapIcon::Foundry, Dir->Arena->FoundryLocation(), Gold).bEdge = true;
		for (int32 Team = 0; Team < 2; ++Team)
		{
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const bool bOpen = Dir->ActiveStation[Team] == Index;
				FMechMapIcon& I = Add(bOpen ? FMechMapIcon::Station : FMechMapIcon::StationOff, Dir->StationLocation(Team, Index), TeamColor(Team));
				I.bEdge = bOpen && Team == MyTeam;
			}
		}
		const float ToSwap = Dir->NextSwapTime - SNow;
		if (ToSwap > 0.f && ToSwap < 10.f)
		{
			FMechMapIcon& I = Add(FMechMapIcon::StationNext, Dir->StationLocation(MyTeam, 1 - Dir->ActiveStation[MyTeam]), Warn);
			I.Alpha = bBlink ? 1.f : 0.45f;
			I.bEdge = true;
		}
		for (const FMechCore& C : Dir->Cores)
		{
			if (C.State == EMechCoreState::Carried && C.Carrier)
			{
				if (C.Carrier->GetTeam() == MyTeam)
				{
					FMechMapIcon& I = Add(FMechMapIcon::CoreCarried, C.Carrier->GetActorLocation(), TeamColor(C.OwnerTeam));
					I.bEdge = true;
				}
				else
				{
					FMechMapIcon& I = Add(FMechMapIcon::CorePulse, C.Pulse, TeamColor(C.OwnerTeam));
					I.Alpha = FMath::Clamp(1.f - (SNow - C.PulseTime) / AMechCoreDirector::PulseEvery * 0.6f, 0.35f, 1.f);
					I.bEdge = true;
				}
				continue;
			}
			FMechMapIcon& I = Add(FMechMapIcon::Core, C.Location, TeamColor(C.OwnerTeam));
			I.bEdge = true;
			if (C.Grab > 0.f && C.Grabber)
			{
				I.Progress = C.Grab;
				I.ArcColor = TeamColor(C.Grabber->GetTeam());
			}
			else if (C.State == EMechCoreState::Ground && FVector::Dist2D(C.Location, Dir->Arena->FoundryLocation()) > 300.f)
			{
				I.Progress = FMath::Clamp(1.f - (SNow - C.DropTime) / AMechCoreDirector::RecallTime, 0.f, 1.f);
				I.ArcColor = I.Color;
			}
		}
	}
	// 插件舱
	for (TActorIterator<AMechPluginPod> It(GetWorld()); It; ++It)
	{
		FMechMapIcon& I = Add(FMechMapIcon::Pod, It->GetActorLocation(), Accent);
		I.Plugin = It->Plugin;
		I.bEdge = true;
	}
	// 机体：队友一直显示（阵亡的夺芯里还要等救，画个空框），敌人按「被发现」显示，之后在最后位置淡出
	// 11-02 修崩溃：阵亡 / 观战 / 还没出生时 Mech 是空的（打包版 GatherMapIcons 读空指针）。老街循环按自己的机体换算，没有机体就按镜头，再没有就原样
	const FVector LoopRef = Mech ? Mech->GetActorLocation() : (PlayerOwner && PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector);
	const bool bLoopRef = Mech || (PlayerOwner && PlayerOwner->PlayerCameraManager);
	auto LoopNear = [this, &LoopRef, bLoopRef](const AActor* A) { return bLoopRef ? MechLoop::Near(GetWorld(), A->GetActorLocation(), LoopRef) : A->GetActorLocation(); };
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* Other = *It;
		if (Other == Mech)
		{
			continue;
		}
		if (Other->IsTrainingDummy())
		{
			Add(FMechMapIcon::Dummy, LoopNear(Other), Dim).Alpha = 0.6f;
			continue;
		}
		const bool bDead = Other->GetHealth()->IsDead();
		if (Other->GetTeam() == MyTeam)
		{
			if (bDead && !(GS && GS->bCoreMode))
			{
				continue;
			}
			FMechMapIcon& I = Add(bDead ? FMechMapIcon::AllyDown : FMechMapIcon::Ally, LoopNear(Other), Ally);
			I.Yaw = Other->GetActorRotation().Yaw;
			I.Label = Other->DisplayName;
			I.bEdge = !bDead;
			continue;
		}
		const FMapSpot* S = bDead ? nullptr : MapSpots.Find(Other);
		if (!S)
		{
			continue;
		}
		const float Age = Now - S->Time;
		if (Age < 0.45f)
		{
			FMechMapIcon& I = Add(FMechMapIcon::Enemy, LoopNear(Other), Enemy);
			I.Yaw = Other->GetActorRotation().Yaw;
		}
		else if (Age < 3.5f)
		{
			Add(FMechMapIcon::EnemyGhost, S->Pos, Enemy).Alpha = FMath::Clamp(1.f - (Age - 0.45f) / 3.05f, 0.f, 1.f) * 0.85f;
		}
	}
	// 标点（11-01）：自己和队友的，画在目标那一层（机体下面），超出小地图贴边
	GatherPingIcons(Mech, Out);
	// 自己（大地图、小地图各自画在最上面）
	if (Mech && !Mech->GetHealth()->IsDead() && PlayerOwner && PlayerOwner->PlayerCameraManager)
	{
		Add(FMechMapIcon::Self, Mech->GetActorLocation(), Ink).Yaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
	}
}

// ================================================================ HUD：画
void AMechHUD::AddImageTris(const TSharedPtr<FSlateBrush>& Brush, const TArray<FVector2f>& Pos, const TArray<FVector2f>& UV, const TArray<FLinearColor>& Colors)
{
	const int32 N = Pos.Num() - Pos.Num() % 3;
	if (!Brush.IsValid() || N < 3 || UV.Num() < N || Colors.Num() < N)
	{
		return;
	}
	FMechHudFrame::FItem& It = HudFrame->Items.AddDefaulted_GetRef();
	It.Kind = FMechHudFrame::EKind::Image;
	It.First = HudFrame->ImgPos.Num();
	It.Count = N;
	It.Brush = Brush;
	for (int32 i = 0; i < N; ++i)
	{
		HudFrame->ImgPos.Add(Pos[i]);
		HudFrame->ImgUV.Add(UV[i]);
		FLinearColor C = Colors[i];
		C.A *= FMath::Clamp(HudAlpha, 0.f, 1.f);
		HudFrame->ImgColor.Add(C);
	}
}

void AMechHUD::DrawMapIcon(const FMechMapIcon& Icon, const FVector2D& P, float ScreenYaw, float Scale, float Alpha, bool bBig)
{
	const float U = (bBig ? 6.f : 4.2f) * UI * Scale;
	const FLinearColor C = Fade(Icon.Color, Icon.Alpha * Alpha);
	auto Arrow = [&](float S, const FLinearColor& Col) {
		const float A = FMath::DegreesToRadians(ScreenYaw);
		const FVector2D D(FMath::Cos(A), FMath::Sin(A));
		const FVector2D Perp(-D.Y, D.X);
		Triangle(P + D * S * 1.45f, P - D * S * 0.75f + Perp * S * 0.85f, P - D * S * 0.75f - Perp * S * 0.85f, Col);
	};
	auto Diamond = [&](const FVector2D& Q, float S, const FLinearColor& Col) {
		Triangle(FVector2D(Q.X, Q.Y - S), FVector2D(Q.X + S, Q.Y), FVector2D(Q.X - S, Q.Y), Col);
		Triangle(FVector2D(Q.X, Q.Y + S), FVector2D(Q.X + S, Q.Y), FVector2D(Q.X - S, Q.Y), Col);
	};
	switch (Icon.Type)
	{
	case FMechMapIcon::Self:
		Arrow(U * 1.55f, C);
		break;
	case FMechMapIcon::Ally:
		Arrow(U * 1.2f, C);
		break;
	case FMechMapIcon::AllyDown:
		Box(P.X - U * 0.8f, P.Y - U * 0.8f, U * 1.6f, U * 1.6f, Fade(C, 0.6f), 1.f);
		break;
	case FMechMapIcon::Enemy:
		Arrow(U * 1.2f, C);
		break;
	case FMechMapIcon::EnemyGhost:
		Box(P.X - U * 0.8f, P.Y - U * 0.8f, U * 1.6f, U * 1.6f, C, 1.f);
		break;
	case FMechMapIcon::Dummy:
		Box(P.X - U * 0.7f, P.Y - U * 0.7f, U * 1.4f, U * 1.4f, C, 1.f);
		break;
	case FMechMapIcon::Core:
		Diamond(P, U * 1.45f, C);
		break;
	case FMechMapIcon::CoreCarried:
		Diamond(P + FVector2D(0.f, -U * 2.f), U * 1.25f, C);
		break;
	case FMechMapIcon::CorePulse:
		Diamond(P, U * 1.4f, C);
		Brackets(P.X, P.Y, U * 2.4f, U * 0.9f, Fade(Enemy, Icon.Alpha * Alpha), 1.f);
		break;
	case FMechMapIcon::Station:
		DrawRect(C, P.X - U * 1.5f, P.Y - U * 1.5f, U * 3.f, U * 3.f);
		DrawRect(Fade(Shade, 0.7f * Alpha), P.X - U * 0.55f, P.Y - U * 0.55f, U * 1.1f, U * 1.1f);
		break;
	case FMechMapIcon::StationOff:
		Box(P.X - U * 1.5f, P.Y - U * 1.5f, U * 3.f, U * 3.f, Fade(C, 0.5f), 1.f);
		break;
	case FMechMapIcon::StationNext:
		Box(P.X - U * 1.9f, P.Y - U * 1.9f, U * 3.8f, U * 3.8f, C, 1.5f);
		break;
	case FMechMapIcon::Foundry:
		Box(P.X - U * 1.3f, P.Y - U * 1.3f, U * 2.6f, U * 2.6f, Fade(C, 0.8f), 1.5f);
		break;
	case FMechMapIcon::Pod:
		PluginIcon(P.X, P.Y, U * 1.9f, Icon.Plugin, Icon.Alpha * Alpha, 1.f);
		break;
	case FMechMapIcon::Spawn:
		Brackets(P.X, P.Y, U * 3.f, U * 1.1f, C, 1.f);
		break;
	case FMechMapIcon::Ping:
		PingGlyph(Icon.PingKind, P.X, P.Y, U * 1.9f, C, 1.f);  // 和屏幕上同一个图形（MechHudPing.cpp）
		break;
	}
	if (Icon.Progress >= 0.f)
	{
		Arc(P.X, P.Y, U * 2.6f, 0.f, Icon.Progress * 2.f * PI, Fade(Icon.ArcColor, Alpha), 2.f);
	}
}

void AMechHUD::DrawMap(AMechCharacter* Mech)
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager || !MapCapture || !MapCapture->HasFrame())
	{
		return;
	}
	const AMechPlayerController* PC = Cast<AMechPlayerController>(PlayerOwner);
	const int32 MyTeam = Mech ? Mech->GetTeam() : PC ? PC->GetMenuTeam() : 0;
	UpdateSpotting(Mech, MyTeam);
	TArray<FMechMapIcon> Icons;
	GatherMapIcons(Mech, Icons);
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("MechMapTest"));
	const bool bBig = bTest ? (MapTestStart >= 0.f && MapTestShot % 2 == 1) : IsMapKeyDown();
	BigMapBlend = bTest ? (bBig ? 1.f : 0.f) : FMath::FInterpConstantTo(BigMapBlend, bBig ? 1.f : 0.f, GetWorld()->GetDeltaSeconds(), 9.f);
	if (BigMapBlend < 1.f)
	{
		// 开狙击镜时小地图也让开（镜外本来就压暗了）
		DrawMinimap(Mech, Icons, (1.f - BigMapBlend) * (1.f - 0.9f * ScopeBlend));
	}
	if (BigMapBlend > 0.f)
	{
		DrawBigMap(Mech, Icons, BigMapBlend);
	}
	if (bTest)
	{
		MapTestTick();
	}
}

void AMechHUD::DrawMinimap(AMechCharacter* Mech, const TArray<FMechMapIcon>& Icons, float Alpha)
{
	if (Alpha <= 0.01f)
	{
		return;
	}
	const float R = 116.f * UI;
	const float CX = 34.f * UI + R;
	const float CY = 30.f * UI + R;
	const FVector2D Ctr(CX, CY);
	const FVector2D PlaySize = MapCapture->GetPlay().GetSize();
	// 小地图半径对应的世界距离：场地长边的四分之一，50–160 米（街道看 50 米，山谷看 160 米）
	const float Range = FMath::Clamp(0.25f * FMath::Max(PlaySize.X, PlaySize.Y), 5000.f, 16000.f);
	const float Scale = R / Range;
	const FVector Self3 = Mech ? Mech->GetActorLocation() : PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FVector2D Self(Self3);
	const float CamYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
	// 随视角旋转：镜头朝向转到屏幕正上方。屏幕向右 = 世界 +X、向下 = 世界 +Y 的那套坐标再转 Th
	const float Th = -HALF_PI - FMath::DegreesToRadians(CamYaw);
	const float CT = FMath::Cos(Th), ST = FMath::Sin(Th);
	auto ToScreen = [&](const FVector2D& W) {
		const FVector2D D = (W - Self) * Scale;
		return FVector2D(CX + D.X * CT - D.Y * ST, CY + D.X * ST + D.Y * CT);
	};
	auto ToWorld = [&](const FVector2D& S) {
		const FVector2D D = S - Ctr;
		return Self + FVector2D(D.X * CT + D.Y * ST, -D.X * ST + D.Y * CT) / Scale;
	};
	const float YawOffset = FMath::RadiansToDegrees(Th);

	// 底：一圈暗底（比地图大一点，和画面分开），里面是贴图圆盘
	constexpr int32 Seg = 72;
	{
		TArray<FCanvasUVTri> Tris;
		const FLinearColor Back = Fade(Shade, 0.6f * Alpha);
		for (int32 i = 0; i < Seg; ++i)
		{
			const float A0 = 2.f * PI * i / Seg, A1 = 2.f * PI * (i + 1) / Seg;
			FCanvasUVTri T;
			T.V0_Pos = Ctr;
			T.V1_Pos = Ctr + FVector2D(FMath::Sin(A0), -FMath::Cos(A0)) * (R + 5.f * UI);
			T.V2_Pos = Ctr + FVector2D(FMath::Sin(A1), -FMath::Cos(A1)) * (R + 5.f * UI);
			T.V0_Color = T.V1_Color = T.V2_Color = Back;
			Tris.Add(T);
		}
		AddTris(Tris);
	}
	if (MapCapture->IsReady())
	{
		TArray<FVector2f> Pos, UV;
		TArray<FLinearColor> Col;
		const FLinearColor Tint(1.f, 1.f, 1.f, 0.92f * Alpha);
		const FVector2f C0 = MapCapture->ToUV(ToWorld(Ctr));
		for (int32 i = 0; i < Seg; ++i)
		{
			const float A0 = 2.f * PI * i / Seg, A1 = 2.f * PI * (i + 1) / Seg;
			const FVector2D P0 = Ctr + FVector2D(FMath::Sin(A0), -FMath::Cos(A0)) * R;
			const FVector2D P1 = Ctr + FVector2D(FMath::Sin(A1), -FMath::Cos(A1)) * R;
			Pos.Append({FVector2f(Ctr), FVector2f(P0), FVector2f(P1)});
			UV.Append({C0, MapCapture->ToUV(ToWorld(P0)), MapCapture->ToUV(ToWorld(P1))});
			Col.Append({Tint, Tint, Tint});
		}
		AddImageTris(MapCapture->GetBrush(), Pos, UV, Col);
	}
	// 活动范围的边界（圆里的部分）
	{
		const FBox2D& PB = MapCapture->GetPlay();
		const FVector2D Corners[5] = {PB.Min, FVector2D(PB.Max.X, PB.Min.Y), PB.Max, FVector2D(PB.Min.X, PB.Max.Y), PB.Min};
		const FLinearColor EdgeC = Fade(Accent, 0.45f * Alpha);
		TArray<FVector2f> Run;
		for (int32 e = 0; e < 4; ++e)
		{
			for (int32 k = 0; k <= 24; ++k)
			{
				const FVector2D S = ToScreen(FMath::Lerp(Corners[e], Corners[e + 1], k / 24.f));
				if (FVector2D::Distance(S, Ctr) < R - 1.f * UI)
				{
					Run.Add(FVector2f(S));
				}
				else
				{
					Polyline(Run, EdgeC, 1.f);
					Run.Reset();
				}
			}
		}
		Polyline(Run, EdgeC, 1.f);
	}
	// 半程圈、视野扇形
	Arc(CX, CY, R * 0.5f, 0.f, 2.f * PI, Fade(Accent, 0.12f * Alpha), 1.f);
	if (Mech && !Mech->GetHealth()->IsDead())
	{
		const float Half = FMath::DegreesToRadians(FMath::Clamp(PlayerOwner->PlayerCameraManager->GetFOVAngle(), 20.f, 150.f) * 0.5f);
		const float L = R * 0.66f;
		TArray<FCanvasUVTri> Tris;
		constexpr int32 N = 12;
		for (int32 i = 0; i < N; ++i)
		{
			const float A0 = -Half + 2.f * Half * i / N, A1 = -Half + 2.f * Half * (i + 1) / N;
			FCanvasUVTri T;
			T.V0_Pos = Ctr;
			T.V1_Pos = Ctr + FVector2D(FMath::Sin(A0), -FMath::Cos(A0)) * L;
			T.V2_Pos = Ctr + FVector2D(FMath::Sin(A1), -FMath::Cos(A1)) * L;
			T.V0_Color = Fade(Accent, 0.26f * Alpha);
			T.V1_Color = T.V2_Color = Fade(Accent, 0.f);
			Tris.Add(T);
		}
		AddTris(Tris);
		DrawLine(CX, CY, CX + FMath::Sin(-Half) * L, CY - FMath::Cos(-Half) * L, Fade(Accent, 0.2f * Alpha), 1.f);
		DrawLine(CX, CY, CX + FMath::Sin(Half) * L, CY - FMath::Cos(Half) * L, Fade(Accent, 0.2f * Alpha), 1.f);
	}
	// 图标：先画目标，再画机体，自己在最上面；超出圆的目标 / 队友贴边（小一点、淡一点），敌人超出就不画
	auto DrawLayer = [&](bool bMechs) {
		for (const FMechMapIcon& I : Icons)
		{
			const bool bIsMech = I.Type == FMechMapIcon::Ally || I.Type == FMechMapIcon::Enemy || I.Type == FMechMapIcon::EnemyGhost ||
				I.Type == FMechMapIcon::AllyDown || I.Type == FMechMapIcon::Dummy || I.Type == FMechMapIcon::CoreCarried;
			if (I.Type == FMechMapIcon::Self || bIsMech != bMechs)
			{
				continue;
			}
			FVector2D P = ToScreen(I.At);
			const float D = FVector2D::Distance(P, Ctr);
			float S = 1.f;
			float A = Alpha;
			if (D > R - 7.f * UI)
			{
				if (!I.bEdge)
				{
					continue;
				}
				P = Ctr + (P - Ctr) / FMath::Max(D, 1.f) * (R - 7.f * UI);
				S = 0.8f;
				A *= 0.75f;
			}
			DrawMapIcon(I, P, I.Yaw + YawOffset, S, A, false);
		}
	};
	DrawLayer(false);
	DrawLayer(true);
	for (const FMechMapIcon& I : Icons)
	{
		if (I.Type == FMechMapIcon::Self)
		{
			DrawMapIcon(I, Ctr, -90.f, 1.f, Alpha, false);
		}
	}
	// 外圈：细青线 + 每 30° 一个刻度，正北（世界 −Y）长一点、标 N
	Arc(CX, CY, R, 0.f, 2.f * PI, Fade(Accent, 0.6f * Alpha), 1.5f);
	for (int32 k = 0; k < 12; ++k)
	{
		// 世界方向 yaw = −90°（−Y）是北；屏幕角度 = yaw + YawOffset
		const float A = FMath::DegreesToRadians(-90.f + k * 30.f + YawOffset);
		const FVector2D D(FMath::Cos(A), FMath::Sin(A));
		const float L = (k == 0 ? 9.f : k % 3 == 0 ? 6.f : 3.5f) * UI;
		DrawLine(CX + D.X * R, CY + D.Y * R, CX + D.X * (R - L), CY + D.Y * (R - L), Fade(k == 0 ? Accent : Dim, (k == 0 ? 0.95f : 0.45f) * Alpha), k == 0 ? 2.f : 1.f);
		if (k == 0)
		{
			const FVector2D NP = Ctr + D * (R + 13.f * UI);
			Text(TEXT("N"), NP.X, NP.Y - 9.f * UI, 13.f, Fade(Accent, Alpha), 0.5f);
		}
	}
	// 右下角：半程圈代表的距离
	Text(FString::Printf(TEXT("%.0fm"), Range * 0.5f / 100.f), CX + R * 0.74f, CY + R * 0.74f, 10.f, Fade(Dim, 0.7f * Alpha), 0.f);
}

void AMechHUD::DrawBigMap(AMechCharacter* Mech, const TArray<FMechMapIcon>& Icons, float Alpha)
{
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const FBox2D& PB = MapCapture->GetPlay();
	const FVector2D PS = PB.GetSize();
	const FBox2D View = PB.ExpandBy(0.035f * FMath::Max(PS.X, PS.Y));
	const FVector2D VS = View.GetSize();
	// 上面让开比分条、下面让开仪表盘：地图放在 0.12H–0.85H
	const float Scale = FMath::Min(W * 0.84f / VS.X, H * 0.73f / VS.Y);
	const float MW = VS.X * Scale;
	const float MH = VS.Y * Scale;
	const float X0 = (W - MW) * 0.5f;
	const float Y0 = H * 0.485f - MH * 0.5f;
	// 北向上：屏幕向右 = 世界 +X，向下 = 世界 +Y
	auto ToScreen = [&](const FVector2D& Wd) { return FVector2D(X0 + (Wd.X - View.Min.X) * Scale, Y0 + (Wd.Y - View.Min.Y) * Scale); };

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.72f * Alpha), 0.f, 0.f, W, H);
	DrawRect(Fade(Shade, 0.7f * Alpha), X0 - 8.f * UI, Y0 - 8.f * UI, MW + 16.f * UI, MH + 16.f * UI);
	if (MapCapture->IsReady())
	{
		const FVector2f U0 = MapCapture->ToUV(View.Min);
		const FVector2f U1 = MapCapture->ToUV(View.Max);
		const FVector2f P0(X0, Y0), P1(X0 + MW, Y0 + MH);
		const FLinearColor Tint(1.f, 1.f, 1.f, 0.95f * Alpha);
		AddImageTris(MapCapture->GetBrush(),
			{P0, FVector2f(P1.X, P0.Y), P1, P0, P1, FVector2f(P0.X, P1.Y)},
			{U0, FVector2f(U1.X, U0.Y), U1, U0, U1, FVector2f(U0.X, U1.Y)},
			{Tint, Tint, Tint, Tint, Tint, Tint});
	}
	else
	{
		// 没有底图（拍摄中 / 不能渲染）：夺芯画场地 actor 的方块形状，死斗只有网格
		for (TActorIterator<AMechCoreArena> It(GetWorld()); It; ++It)
		{
			static const FLinearColor KindColor[5] = {FLinearColor(0.30f, 0.29f, 0.27f, 0.92f), FLinearColor(0.52f, 0.50f, 0.46f, 0.95f),
				FLinearColor(0.20f, 0.15f, 0.11f, 0.95f), FLinearColor(0.05f, 0.055f, 0.06f, 0.9f), FLinearColor(0.15f, 0.30f, 0.40f, 0.92f)};
			const FVector2D O(It->GetActorLocation());
			for (const AMechCoreArena::FMapShape& S : It->GetMapShapes())
			{
				const FVector2D A = ToScreen(O + S.Box.Min);
				const FVector2D B = ToScreen(O + S.Box.Max);
				DrawRect(Fade(KindColor[FMath::Clamp<int32>(S.Kind, 0, 4)], 0.6f * Alpha), A.X, A.Y, B.X - A.X, B.Y - A.Y);
			}
			break;
		}
	}
	// 网格（从活动范围左上角起，A B C… 列、1 2 3… 行）
	const float G = NiceStep(FMath::Max(PS.X, PS.Y), 10.f);
	const FLinearColor GridC = Fade(Accent, 0.16f * Alpha);
	const int32 Cols = FMath::CeilToInt(PS.X / G - 0.01f);
	const int32 Rows = FMath::CeilToInt(PS.Y / G - 0.01f);
	for (int32 i = 0; i <= Cols; ++i)
	{
		const float X = ToScreen(FVector2D(FMath::Min(PB.Min.X + i * G, PB.Max.X), 0.f)).X;
		DrawLine(X, Y0, X, Y0 + MH, GridC, 1.f);
		if (i < Cols)
		{
			const float XN = ToScreen(FVector2D(FMath::Min(PB.Min.X + (i + 1) * G, PB.Max.X), 0.f)).X;
			Text(FString::Chr(TCHAR('A' + FMath::Min(i, 25))), (X + XN) * 0.5f, Y0 - 34.f * UI, 15.f, Fade(Ink, 0.75f * Alpha), 0.5f);
		}
	}
	for (int32 j = 0; j <= Rows; ++j)
	{
		const float Y = ToScreen(FVector2D(0.f, FMath::Min(PB.Min.Y + j * G, PB.Max.Y))).Y;
		DrawLine(X0, Y, X0 + MW, Y, GridC, 1.f);
		if (j < Rows)
		{
			const float YN = ToScreen(FVector2D(0.f, FMath::Min(PB.Min.Y + (j + 1) * G, PB.Max.Y))).Y;
			Text(FString::FromInt(j + 1), X0 - 26.f * UI, (Y + YN) * 0.5f - 10.f * UI, 15.f, Fade(Ink, 0.75f * Alpha), 0.5f);
		}
	}
	// 活动范围边框 + 四角括号
	{
		const FVector2D A = ToScreen(PB.Min);
		const FVector2D B = ToScreen(PB.Max);
		Box(A.X, A.Y, B.X - A.X, B.Y - A.Y, Fade(Accent, 0.45f * Alpha), 1.f);
		const float L = 18.f * UI;
		const FLinearColor Edge = Fade(Accent, 0.85f * Alpha);
		const float XL = X0 - 8.f * UI, XR = X0 + MW + 8.f * UI, YT = Y0 - 8.f * UI, YB = Y0 + MH + 8.f * UI;
		DrawLine(XL, YT, XL + L, YT, Edge, 1.5f);
		DrawLine(XL, YT, XL, YT + L, Edge, 1.5f);
		DrawLine(XR, YT, XR - L, YT, Edge, 1.5f);
		DrawLine(XR, YT, XR, YT + L, Edge, 1.5f);
		DrawLine(XL, YB, XL + L, YB, Edge, 1.5f);
		DrawLine(XL, YB, XL, YB - L, Edge, 1.5f);
		DrawLine(XR, YB, XR - L, YB, Edge, 1.5f);
		DrawLine(XR, YB, XR, YB - L, Edge, 1.5f);
	}
	// 比例尺（地图里左下角，一格网格的长度，垫一块暗底）和指北（右上）
	{
		const float BX = X0 + 16.f * UI;
		const float BY = Y0 + MH - 20.f * UI;
		const float BL = G * Scale;
		const FLinearColor SC = Fade(Ink, 0.9f * Alpha);
		DrawRect(Fade(Shade, 0.65f * Alpha), BX - 8.f * UI, BY - 16.f * UI, BL + 84.f * UI, 30.f * UI);
		DrawLine(BX, BY, BX + BL, BY, SC, 1.5f);
		DrawLine(BX, BY - 5.f * UI, BX, BY + 5.f * UI, SC, 1.5f);
		DrawLine(BX + BL, BY - 5.f * UI, BX + BL, BY + 5.f * UI, SC, 1.5f);
		Text(FString::Printf(TEXT("%.0f m"), G / 100.f), BX + BL + 12.f * UI, BY - 10.f * UI, 14.f, SC);
		const float NX = X0 + MW - 18.f * UI;
		const float NY = Y0 + 22.f * UI;
		Triangle(FVector2D(NX, NY - 9.f * UI), FVector2D(NX + 6.f * UI, NY + 5.f * UI), FVector2D(NX - 6.f * UI, NY + 5.f * UI), Fade(Accent, Alpha));
		Text(TEXT("N"), NX, NY + 8.f * UI, 13.f, Fade(Accent, Alpha), 0.5f);
	}
	// 自己的视野扇形
	const FMechMapIcon* SelfIcon = Icons.FindByPredicate([](const FMechMapIcon& I) { return I.Type == FMechMapIcon::Self; });
	if (SelfIcon)
	{
		const FVector2D P = ToScreen(SelfIcon->At);
		const float Half = FMath::DegreesToRadians(FMath::Clamp(PlayerOwner->PlayerCameraManager->GetFOVAngle(), 20.f, 150.f) * 0.5f);
		const float Yaw = FMath::DegreesToRadians(SelfIcon->Yaw);
		const float L = 70.f * UI;
		TArray<FCanvasUVTri> Tris;
		constexpr int32 N = 12;
		for (int32 i = 0; i < N; ++i)
		{
			const float A0 = Yaw - Half + 2.f * Half * i / N, A1 = Yaw - Half + 2.f * Half * (i + 1) / N;
			FCanvasUVTri T;
			T.V0_Pos = P;
			T.V1_Pos = P + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * L;
			T.V2_Pos = P + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * L;
			T.V0_Color = Fade(Accent, 0.3f * Alpha);
			T.V1_Color = T.V2_Color = Fade(Accent, 0.f);
			Tris.Add(T);
		}
		AddTris(Tris);
	}
	// 图标（目标 → 机体 → 自己）；队友旁边写名字
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (const FMechMapIcon& I : Icons)
		{
			const bool bIsMech = I.Type == FMechMapIcon::Ally || I.Type == FMechMapIcon::Enemy || I.Type == FMechMapIcon::EnemyGhost ||
				I.Type == FMechMapIcon::AllyDown || I.Type == FMechMapIcon::Dummy || I.Type == FMechMapIcon::CoreCarried;
			const int32 Want = I.Type == FMechMapIcon::Self ? 2 : bIsMech ? 1 : 0;
			if (Want != Pass)
			{
				continue;
			}
			const FVector2D P = ToScreen(I.At);
			DrawMapIcon(I, P, I.Yaw, 1.f, Alpha, true);
			if ((I.Type == FMechMapIcon::Ally || I.Type == FMechMapIcon::AllyDown) && !I.Label.IsEmpty())
			{
				Text(I.Label, P.X + 12.f * UI, P.Y - 9.f * UI, 12.f, Fade(Ally, (I.Type == FMechMapIcon::AllyDown ? 0.5f : 0.95f) * Alpha));
			}
		}
	}
}

// ================================================================ 机体头顶名字
void AMechHUD::DrawNames(AMechCharacter* Mech)
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager || !Mech)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const float Dt = GetWorld()->GetDeltaSeconds();
	const FVector Eye = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FVector Fwd = PlayerOwner->PlayerCameraManager->GetCameraRotation().Vector();
	const FVector2D Mid(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
	const float Hide = FMath::Clamp(1.f - ScopeBlend, 0.f, 1.f);  // 开镜时让开
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* Other = *It;
		if (Other == Mech || Other->IsTrainingDummy())
		{
			continue;  // 不画自己（第一人称时名字会贴在眼前）；木桩不用名字
		}
		FNameVis& V = NameVis.FindOrAdd(Other);
		const bool bAlly = Other->GetTeam() == Mech->GetTeam();
		const FVector Body = MechLoop::Near(GetWorld(), Other->GetActorLocation(), Eye) + FVector(0.f, 0.f, 60.f);  // 老街循环
		const float Dist = FVector::Dist(Eye, Body);
		if (Now >= V.NextCheck)
		{
			// 视线：镜头到机体胸口没被挡、也没隔着烟（每台约 0.15 秒查一次，错开）
			V.NextCheck = Now + 0.12f + FMath::FRand() * 0.06f;
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(MechNameSight), false, Mech);
			Query.AddIgnoredActor(Other);
			V.bClear = !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Body, ECC_Visibility, Query) && !AMechSmoke::BlocksLine(GetWorld(), Eye, Body);
		}
		float Want = 0.f;
		if (!Other->GetHealth()->IsDead())
		{
			// 队友始终显示（隔墙时淡一点）；敌人要看得见、150 米内
			Want = bAlly ? (V.bClear ? 1.f : 0.55f) : (V.bClear && Dist < 15000.f ? 1.f : 0.f);
		}
		V.Alpha = FMath::FInterpConstantTo(V.Alpha, Want, Dt, 5.f);
		if (V.Alpha <= 0.01f || Hide <= 0.01f || FVector::DotProduct(Body - Eye, Fwd) <= 0.f)
		{
			continue;
		}
		FVector2D P;
		if (!UGameplayStatics::ProjectWorldToScreen(PlayerOwner, MechLoop::Near(GetWorld(), Other->GetActorLocation(), Eye) + FVector(0.f, 0.f, 230.f), P))
		{
			continue;
		}
		// 随距离：20 米内原大，150 米缩到七成、淡到一半；队友更远继续缩、淡
		const float K = FMath::Clamp((Dist - 2000.f) / 13000.f, 0.f, 1.f);
		float Size = 15.f * FMath::Lerp(1.f, 0.72f, K);
		float A = FMath::Lerp(1.f, 0.5f, K);
		if (Dist > 15000.f)
		{
			const float K2 = FMath::Clamp((Dist - 15000.f) / 15000.f, 0.f, 1.f);
			Size = 15.f * FMath::Lerp(0.72f, 0.6f, K2);
			A = FMath::Lerp(0.5f, 0.38f, K2);
		}
		if (bAlly && !V.bClear)
		{
			Size *= 0.85f;
		}
		const FString& Name = Other->DisplayName;
		const FVector2D TS = Measure(Name, Size);
		// 标记三角 / 队友血条在头顶 +230 厘米那一点附近，名字再往上一点
		const float TY = P.Y - (bAlly ? 17.f : 12.f) * UI - TS.Y;
		// 别挡准星：名字压到准星附近时淡掉（瞄着的那台头顶名字一般在准星上方几十像素，不至于全看不见）
		const float ToMid = FVector2D::Distance(FVector2D(P.X, TY + TS.Y * 0.5f), Mid);
		A *= FMath::Lerp(0.2f, 1.f, FMath::SmoothStep(24.f * UI, 80.f * UI, ToMid));
		const float Shown = A * V.Alpha * Hide;
		Text(Name, P.X, TY, Size, Fade(bAlly ? Ally : Enemy, Shown), 0.5f);
	}
	for (auto It = NameVis.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

// ================================================================ 测试：开打后轮流截小地图 / 大地图
void AMechHUD::MapTestTick()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const float Now = GetWorld()->GetTimeSeconds();
	if (MapTestStart < 0.f)
	{
		if (GS && GS->Phase == EMechRoundPhase::Fight && MapCapture && (MapCapture->IsReady() || !MapCapture->IsBusy()))
		{
			MapTestStart = Now;
		}
		return;
	}
	// 第 k 张在开打后 2.5 + 3k 秒截（单数张是大地图：上一帧已经切过去了）
	if (MapTestShot < 10 && Now - MapTestStart >= 2.5f + 3.f * MapTestShot)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Map") /
			FString::Printf(TEXT("%s_%02d_%s.png"), *GetWorld()->GetMapName(), MapTestShot, MapTestShot % 2 ? TEXT("big") : TEXT("mini"));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[map] 截图 %s"), *Path);
		++MapTestShot;
	}
}
