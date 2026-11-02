#include "MechResultsStage.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/SlateRenderer.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundSubmix.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Widgets/SLeafWidget.h"

#include "MechCatalog.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechFonts.h"
#include "MechGameMode.h"
#include "MechLoadout.h"
#include "MechMeshComponent.h"
#include "MechScore.h"
#include "MechSettings.h"
#include "MechUITheme.h"

// ================================================================ 共用（不用匿名命名空间：合并编译时会和别的文件里同名的东西撞）
namespace MechResultsPriv
{
	// 调光（控制台变量，-ExecCmds 或游戏里 ~ 直接改，下一帧生效；定下来再改这里的默认值）
	float Exposure = -5.2f;
	float LightScale = 1.f;
	float Fstop = 2.8f;
	float BeamScale = 1.f;
	FAutoConsoleVariableRef CVarExposure(TEXT("mech.Results.Exposure"), Exposure, TEXT("结算场景的固定曝光（档，越小越暗）"));
	FAutoConsoleVariableRef CVarLight(TEXT("mech.Results.Light"), LightScale, TEXT("结算场景所有灯的亮度倍数"));
	FAutoConsoleVariableRef CVarFstop(TEXT("mech.Results.Fstop"), Fstop, TEXT("结算场景景深光圈（小 = 背景更虚）"));
	FAutoConsoleVariableRef CVarBeam(TEXT("mech.Results.Beam"), BeamScale, TEXT("结算场景光柱 / 地环亮度倍数"));
	// 10-31：MVP 聚光 / 光柱单独的倍数（在上面两个之上再乘）、声音（配乐和音效的总倍数、配乐单独的倍数；最后都还乘设置里的主音量）
	float MVPSpotScale = 1.f;
	float MVPBeamScale = 1.f;
	float AudioScale = 1.f;
	float MusicScale = 1.f;
	FAutoConsoleVariableRef CVarMVPSpot(TEXT("mech.Results.MVPSpot"), MVPSpotScale, TEXT("结算场景 MVP 头顶聚光的亮度倍数"));
	FAutoConsoleVariableRef CVarMVPBeam(TEXT("mech.Results.MVPBeam"), MVPBeamScale, TEXT("结算场景 MVP 光柱 / 地环的亮度倍数"));
	FAutoConsoleVariableRef CVarAudio(TEXT("mech.Results.Volume"), AudioScale, TEXT("结算场景配乐和音效的音量倍数（再乘主音量）"));
	FAutoConsoleVariableRef CVarMusic(TEXT("mech.Results.Music"), MusicScale, TEXT("结算场景配乐（含结算表后面的底噪）的音量倍数"));

	/** 场景搭在这里：场地下方约 3.8 公里（配装摄影棚 AMechItemStudio 在 -2.5 公里，错开） */
	const FVector StageOrigin(0.f, 0.f, -380000.f);
	/** 场景的灯和物件都只在灯光通道 2：地图的太阳在 0，机体自带的补光在 1 */
	constexpr bool Ch0 = false, Ch1 = false, Ch2 = true;

	TWeakObjectPtr<AMechResultsStage> GActive;

	FLinearColor Gold() { return MechUITheme::Srgb(240, 196, 92); }
	FLinearColor Silver() { return MechUITheme::Srgb(205, 212, 220); }
	FLinearColor Bronze() { return MechUITheme::Srgb(214, 142, 92); }
	/** 3D 场景里的队伍色（线性，偏亮一点，打在暗墙上读得出来） */
	FLinearColor TeamLight(int32 Team) { return Team == 0 ? FLinearColor(0.22f, 0.55f, 1.f) : FLinearColor(1.f, 0.16f, 0.12f); }
	FLinearColor TeamUI(int32 Team) { return Team == 0 ? MechUITheme::Get().Blue : MechUITheme::Get().Red; }

	FString Dmg(float V)
	{
		// 11-01：铭牌多了一栏，一栏窄了，十万以上不带小数（「135.8K」顶到铭牌边上）
		return V >= 100000.f ? FString::Printf(TEXT("%.0fK"), V / 1000.f) : V >= 10000.f ? FString::Printf(TEXT("%.1fK"), V / 1000.f) : FString::Printf(TEXT("%d"), FMath::RoundToInt(V));
	}

	float Smooth(float A, float B, float X)
	{
		return FMath::SmoothStep(A, B, X);
	}

	/** 三次 Hermite（Catmull-Rom 切线，按关键帧时间非均匀；最后一帧切线为 0 = 缓停） */
	FVector SplineAt(const TArray<FVector>& P, const TArray<float>& U, float u)
	{
		const int32 N = P.Num();
		if (N == 1)
		{
			return P[0];
		}
		u = FMath::Clamp(u, U[0], U[N - 1]);
		int32 i = 0;
		while (i < N - 2 && u > U[i + 1])
		{
			++i;
		}
		const float h = FMath::Max(1e-4f, U[i + 1] - U[i]);
		const float s = FMath::Clamp((u - U[i]) / h, 0.f, 1.f);
		auto Tangent = [&](int32 k) {
			if (k == N - 1)
			{
				return FVector::ZeroVector;
			}
			const int32 a = FMath::Max(0, k - 1);
			const int32 b = FMath::Min(N - 1, k + 1);
			return (P[b] - P[a]) / FMath::Max(1e-4f, U[b] - U[a]);
		};
		const FVector M0 = Tangent(i) * h;
		const FVector M1 = Tangent(i + 1) * h;
		const float s2 = s * s, s3 = s2 * s;
		return (2 * s3 - 3 * s2 + 1) * P[i] + (s3 - 2 * s2 + s) * M0 + (-2 * s3 + 3 * s2) * P[i + 1] + (s3 - s2) * M1;
	}

	/** 后排的位置：前排在 0、±1、±2 个间距上，后排站空当 +0.5、-0.5、+1.5、-1.5……（第 k 好的用第 k 个，好的靠中间） */
	TArray<float> GapSlots(int32 N, float Spacing)
	{
		TArray<float> Out;
		for (int32 k = 0; k < N; ++k)
		{
			Out.Add((k % 2 == 0 ? 1.f : -1.f) * (0.5f + k / 2) * Spacing);
		}
		return Out;
	}

	/** 英文的模式名（界面不写中文提示，只有名字可以是中文） */
	FString ModeTitle(const AMechGameState* GS)
	{
		if (GS->bCoreMode)
		{
			return TEXT("CORE CONTROL");
		}
		if (GS->Rules.IsDeathmatch())
		{
			return GS->Rules.bSolo ? TEXT("FREE FOR ALL") : TEXT("TEAM DEATHMATCH");
		}
		return TEXT("TEAM ELIMINATION");
	}

	FString ModeSub(const AMechGameState* GS)
	{
		if (GS->bCoreMode)
		{
			return FString::Printf(TEXT("FIRST TO %d"), GS->CoreTarget);
		}
		if (GS->Rules.IsDeathmatch())
		{
			return FString::Printf(TEXT("FIRST TO %d"), GS->Rules.KillTarget());
		}
		return GS->Rules.RoundsToWin <= 1 ? FString(TEXT("SINGLE ROUND")) : FString::Printf(TEXT("BEST OF %d"), GS->Rules.RoundsToWin * 2 - 1);
	}
}

// ================================================================ 画面上的图形（Slate，盖在视口上：铭牌、MVP、比分、转播小标、淡入淡出）
class SMechResultsOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechResultsOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
		SetVisibility(EVisibility::HitTestInvisible);
	}

	TWeakObjectPtr<AMechResultsStage> Stage;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Els, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override;
};

int32 SMechResultsOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Els, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace MechResultsPriv;
	const AMechResultsStage* St = Stage.Get();
	if (!St)
	{
		return Layer;
	}
	const FVector2f Size = FVector2f(G.GetLocalSize());
	const float W = Size.X, H = Size.Y;
	const float u = H / 1080.f;  // 按 1080p 设计的尺寸
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FMechUITheme& Th = MechUITheme::Get();
	auto Box = [&](float X, float Y, float BW, float BH, const FLinearColor& C) {
		if (C.A > 0.003f)
		{
			FSlateDrawElement::MakeBox(Els, ++Layer, G.ToPaintGeometry(FVector2f(BW, BH), FSlateLayoutTransform(FVector2f(X, Y))), White, ESlateDrawEffect::None, C);
		}
	};
	auto TextW = [&](const FString& S, const FSlateFontInfo& F) { return static_cast<float>(Measure->Measure(S, F).X); };
	/** Align：0 左、0.5 中、1 右 */
	auto Text = [&](const FString& S, const FSlateFontInfo& F, float X, float Y, const FLinearColor& C, float Align = 0.f) {
		if (C.A <= 0.003f || S.IsEmpty())
		{
			return;
		}
		const float X0 = X - TextW(S, F) * Align;
		FSlateDrawElement::MakeText(Els, ++Layer, G.ToPaintGeometry(FVector2f(1.f, 1.f), FSlateLayoutTransform(FVector2f(X0, Y))), S, F, ESlateDrawEffect::None, C);
	};
	auto Font = [](float Sz, int32 Spacing = 0, bool bBold = false) {
		FSlateFontInfo F = MechFonts::GetCanvas(Sz, bBold);
		F.LetterSpacing = Spacing;
		return F;
	};
	auto Lines = [&](const TArray<FVector2f>& Pts, const FLinearColor& C, float Thick) {
		if (C.A > 0.003f)
		{
			FSlateDrawElement::MakeLines(Els, ++Layer, G.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C, true, Thick);
		}
	};

	const float T = St->SinceEnd();
	if (!St->bCut)
	{
		// 切过去之前：最后一下还看着场上，最后半秒压黑
		const float Black = Smooth(AMechResultsStage::PreRoll - AMechResultsStage::FadeOutLen, AMechResultsStage::PreRoll, T);
		Box(0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, Black));
		return Layer;
	}
	const float S = St->StageClock;
	const float UI = 1.f - St->TableBlend;
	const APlayerController* PC = St->OwnerPC.Get();
	FVector2D VP(1.f, 1.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(VP);
	}
	const float PxToLocal = W / FMath::Max(1.0, VP.X);
	auto Project = [&](const FVector& World, FVector2f& Out) -> bool {
		FVector2D Px;
		if (!PC || !PC->ProjectWorldLocationToScreen(World, Px, true))
		{
			return false;
		}
		Out = FVector2f(Px.X * PxToLocal, Px.Y * PxToLocal);
		return Out.X > -300.f * u && Out.X < W + 300.f * u && Out.Y > -300.f * u && Out.Y < H + 300.f * u;
	};
	/** 头顶上方一点（按展示机体的头骨头，各家外壳高矮不同；还没生成时按 3 米估） */
	auto HeadTop = [St](const AMechResultsStage::FEntry& E) -> FVector {
		if (const AMechCharacter* Mc = E.Mech.Get())
		{
			const USkeletalMeshComponent* Sk = Mc->GetMesh();
			if (Sk && Sk->GetBoneIndex(TEXT("head")) != INDEX_NONE)
			{
				return Sk->GetSocketLocation(TEXT("head")) + FVector(0.f, 0.f, 75.f);
			}
		}
		return St->ToWorld(E.Local + FVector(0.f, 0.f, E.Plinth + 300.f));
	};

	// ---------------------------------------------------------------- 铭牌（前排在脚下、后排在头顶），数字从 0 数上去
	const float PlateVis = UI * (1.f - St->CloseUp) * Smooth(0.6f, 1.4f, S);
	int32 MaxK = 1, MaxD = 1, MaxC = 1, MaxA = 1, MaxScore = 1;
	float MaxDealt = 1.f;
	for (const AMechResultsStage::FEntry& E : St->Entries)
	{
		MaxK = FMath::Max(MaxK, E.Row.Kills);
		MaxD = FMath::Max(MaxD, E.Row.Deaths);
		MaxC = FMath::Max(MaxC, E.Row.Cores);
		MaxA = FMath::Max(MaxA, E.Row.Assists);
		MaxScore = FMath::Max(MaxScore, E.Row.Score);
		MaxDealt = FMath::Max(MaxDealt, static_cast<float>(E.Row.Dealt));
	}
	if (PlateVis > 0.01f)
	{
		int32 Order = 0;
		for (const AMechResultsStage::FEntry& E : St->Entries)
		{
			const int32 i = Order++;
			const float Appear = AMechResultsStage::PlateAt + i * AMechResultsStage::PlateStep;
			const float A = PlateVis * Smooth(Appear, Appear + 0.45f, S) * (E.bFront ? 1.f : 0.82f);
			if (A < 0.01f)
			{
				continue;
			}
			const float k = E.bFront ? 1.f : 0.8f;
			const FVector Feet = St->ToWorld(E.Local + FVector(0.f, 0.f, E.Plinth));
			FVector2f P;
			if (!Project(E.bFront ? Feet - FVector(0.f, 0.f, E.Plinth > 0.f ? E.Plinth + 4.f : 6.f) : HeadTop(E), P))
			{
				continue;
			}
			// 夺芯多一栏 CORE，加宽；11-01 积分制：多一行 SCORE（90 → 116 高）、多一栏 AST（232 / 286 → 250 / 304 宽，4v4 前排间隔约 340 像素还放得下）
			const float PW = (St->bCore ? 304.f : 250.f) * u * k, PH = 116.f * u * k;
			const float Slide = (1.f - Smooth(Appear, Appear + 0.5f, S)) * 10.f * u;
			const float X = P.X - PW * 0.5f;
			const float Y = E.bFront ? P.Y + 14.f * u * k + Slide : P.Y - PH - 12.f * u * k + Slide;
			// 11-01：个人死斗的 MVP 可以不是第一名——第一名不是 MVP 时照样金边、标 #1
			const FLinearColor Edge = E.bMVP ? Gold() : St->bSolo ? (E.Rank == 1 ? Gold() : E.Rank == 2 ? Silver() : E.Rank == 3 ? Bronze() : (E.bSelf ? Th.Accent : Th.Dim))
				: TeamUI(E.Row.Team);
			// 底板 + 上沿一道色条（MVP 金边）
			Box(X, Y, PW, PH, Th.CardFill.CopyWithNewOpacity(0.8f * A));
			Box(X, Y, PW, 2.f * u * k, Edge.CopyWithNewOpacity(A));
			if (E.bMVP)
			{
				Box(X, Y + PH - 1.f * u, PW, 1.f * u, Gold().CopyWithNewOpacity(0.7f * A));
				Box(X, Y, 1.f * u, PH, Gold().CopyWithNewOpacity(0.5f * A));
				Box(X + PW - 1.f * u, Y, 1.f * u, PH, Gold().CopyWithNewOpacity(0.5f * A));
			}
			if (E.bSelf && !E.bMVP)
			{
				Box(X, Y + PH - 1.f * u, PW, 1.f * u, Th.Accent.CopyWithNewOpacity(0.6f * A));
			}
			// 名字（可以是中文）+ 右上角标签：MVP / #2 / YOU
			const float Pad = 11.f * u * k;
			Text(E.Row.Name, MechFonts::Get(15.f * u * k, true), X + Pad, Y + 7.f * u * k, Th.Ink.CopyWithNewOpacity(A));
			FString Badge;
			FLinearColor BadgeC = Th.Dim;
			if (E.bMVP)
			{
				Badge = TEXT("MVP");
				BadgeC = Gold();
			}
			else if (St->bSolo)
			{
				Badge = FString::Printf(TEXT("#%d"), E.Rank);
				BadgeC = E.Rank == 1 ? Gold() : E.Rank == 2 ? Silver() : E.Rank == 3 ? Bronze() : Th.Dim;
			}
			if (!Badge.IsEmpty())
			{
				const FSlateFontInfo TF = Font(12.f * u * k, 200, true);
				const float TW = TextW(Badge, TF) + 10.f * u * k;
				if (E.bMVP)
				{
					Box(X + PW - Pad - TW, Y + 9.f * u * k, TW, 17.f * u * k, Gold().CopyWithNewOpacity(A));
					Text(Badge, TF, X + PW - Pad - TW * 0.5f, Y + 9.f * u * k, FLinearColor(0.02f, 0.02f, 0.02f, A), 0.5f);
				}
				else
				{
					Text(Badge, TF, X + PW - Pad, Y + 9.f * u * k, BadgeC.CopyWithNewOpacity(A), 1.f);
				}
			}
			if (E.bSelf)
			{
				const FSlateFontInfo YF = Font(10.f * u * k, 240, true);
				const float OffX = Badge.IsEmpty() ? 0.f : TextW(Badge, Font(12.f * u * k, 200, true)) + 22.f * u * k;
				Text(TEXT("YOU"), YF, X + PW - Pad - OffX, Y + 11.f * u * k, Th.Accent.CopyWithNewOpacity(A), 1.f);
			}
			const float Count = AMechResultsStage::PlateCount(i, S);  // 数字跳动的声音按同一个数（UpdateAudio）
			// 11-01 积分制（MechScore.h）：名字下面一行 SCORE 积分（大字，MVP 金色）+ 横贯的一道条（全场最高 = 满）——MVP 按它挑，放在最显眼处
			{
				const FSlateFontInfo LF = Font(9.5f * u * k, 220);
				const float SY = Y + 30.f * u * k;
				Text(TEXT("SCORE"), LF, X + Pad, SY + 8.f * u * k, Th.Dim.CopyWithNewOpacity(A));
				Text(FString::FromInt(FMath::RoundToInt(E.Row.Score * Count)), Font(22.f * u * k, 0, true), X + Pad + TextW(TEXT("SCORE"), LF) + 9.f * u * k, SY,
					(E.bMVP ? Gold() : Th.Ink).CopyWithNewOpacity(A));
				const float BW = PW - Pad * 2.f;
				const float BY = Y + 59.f * u * k;
				Box(X + Pad, BY, BW, 2.f * u * k, Th.Track.CopyWithNewOpacity(0.6f * A));
				Box(X + Pad, BY, BW * FMath::Clamp(E.Row.Score / float(MaxScore) * Count, 0.f, 1.f), 2.f * u * k, (E.bMVP ? Gold() : Th.Accent).CopyWithNewOpacity(0.9f * A));
			}
			// 数据：KO / AST / DTH / DMG（夺芯多 CORE），数字从 0 数上去，下面一道条（全场最多 = 满）
			struct FCol
			{
				const TCHAR* Label;
				FString Value;
				float Fill;
				FLinearColor Color;
			};
			TArray<FCol> Cols;
			if (St->bCore)
			{
				Cols.Add({TEXT("CORE"), FString::FromInt(FMath::RoundToInt(E.Row.Cores * Count)), E.Row.Cores / float(MaxC), Gold()});
			}
			Cols.Add({TEXT("KO"), FString::FromInt(FMath::RoundToInt(E.Row.Kills * Count)), E.Row.Kills / float(MaxK), Th.Accent});
			Cols.Add({TEXT("AST"), FString::FromInt(FMath::RoundToInt(E.Row.Assists * Count)), E.Row.Assists / float(MaxA), Th.Accent});
			Cols.Add({TEXT("DTH"), FString::FromInt(FMath::RoundToInt(E.Row.Deaths * Count)), E.Row.Deaths / float(MaxD), Th.Bad});
			Cols.Add({TEXT("DMG"), Dmg(E.Row.Dealt * Count), E.Row.Dealt / MaxDealt, Th.Ink});
			const float ColW = (PW - Pad * 2.f) / Cols.Num();
			for (int32 c = 0; c < Cols.Num(); ++c)
			{
				const float CX = X + Pad + ColW * c;
				Text(Cols[c].Label, Font(9.5f * u * k, 220), CX, Y + 67.f * u * k, Th.Dim.CopyWithNewOpacity(A));
				Text(Cols[c].Value, Font(18.f * u * k, 0, true), CX, Y + 79.f * u * k, Th.Ink.CopyWithNewOpacity(A));
				const float BW = ColW - 10.f * u * k;
				const float BY = Y + PH - 9.f * u * k;
				Box(CX, BY, BW, 3.f * u * k, Th.Track.CopyWithNewOpacity(0.6f * A));
				Box(CX, BY, BW * FMath::Clamp(Cols[c].Fill * Count, 0.f, 1.f), 3.f * u * k, Cols[c].Color.CopyWithNewOpacity(0.9f * A));
			}
		}
	}

	// ---------------------------------------------------------------- 领奖台的名次数字（台子正面）
	if (St->bSolo)
	{
		const float A = UI * (1.f - St->CloseUp) * Smooth(0.3f, 1.f, S);
		for (const AMechResultsStage::FEntry& E : St->Entries)
		{
			if (E.Plinth <= 0.f || A < 0.01f)
			{
				continue;
			}
			FVector2f P;
			if (Project(St->ToWorld(E.Local + FVector(-118.f, 0.f, E.Plinth * 0.5f)), P))
			{
				const FLinearColor C = E.Rank == 1 ? Gold() : E.Rank == 2 ? Silver() : Bronze();
				const FSlateFontInfo F = Font(FMath::Clamp(E.Plinth * 0.9f, 28.f, 48.f) * u, 0, true);
				Text(FString::FromInt(E.Rank), F, P.X, P.Y - F.Size * 0.72f, C.CopyWithNewOpacity(A), 0.5f);
			}
		}
	}

	// ---------------------------------------------------------------- MVP：头顶金色菱形 + 字；特写时左下角字幕
	if (St->Entries.IsValidIndex(St->MVPIndex))
	{
		const AMechResultsStage::FEntry& M = St->Entries[St->MVPIndex];
		const float A = UI * (1.f - St->CloseUp) * Smooth(1.6f, 2.4f, S);
		FVector2f P;
		if (A > 0.01f && Project(HeadTop(M), P))
		{
			const float D = 7.f * u;
			Lines({FVector2f(P.X, P.Y - D), FVector2f(P.X + D, P.Y), FVector2f(P.X, P.Y + D), FVector2f(P.X - D, P.Y), FVector2f(P.X, P.Y - D)}, Gold().CopyWithNewOpacity(A),
				1.6f * u);
			Box(P.X - D * 0.35f, P.Y - D * 0.35f, D * 0.7f, D * 0.7f, Gold().CopyWithNewOpacity(A));
			Text(TEXT("MVP"), Font(13.f * u, 420, true), P.X, P.Y - D - 22.f * u, Gold().CopyWithNewOpacity(A), 0.5f);
		}
		const float L = UI * St->CloseUp;
		if (L > 0.01f)
		{
			const float Slide = (1.f - L) * 36.f * u;
			const float X = 86.f * u - Slide;
			const float Y = H - 296.f * u;  // 11-01：多一行 MVP 理由，整块往上挪 28、加高 28
			// 底下一块半透明暗底（金字压在亮背景 / 金色氛围光上看不清），右边再接一段更淡的，像转播字幕条
			Box(X - 46.f * u, Y - 16.f * u, 470.f * u, 196.f * u, FLinearColor(0.f, 0.f, 0.f, 0.45f * L));
			Box(X + 424.f * u, Y - 16.f * u, 160.f * u, 196.f * u, FLinearColor(0.f, 0.f, 0.f, 0.2f * L));
			Box(X - 20.f * u, Y + 4.f * u, 4.f * u, 160.f * u, Gold().CopyWithNewOpacity(L));
			Text(TEXT("MVP"), Font(19.f * u, 640, true), X, Y, Gold().CopyWithNewOpacity(L));
			Text(M.Row.Name, MechFonts::Get(40.f * u, true), X - 2.f * u, Y + 26.f * u, Th.Ink.CopyWithNewOpacity(L));
			// 11-01 积分制：为什么是他——积分 + 最突出的两项（MechScore::Reason，比如「SCORE 1840  ·  AST 6  ·  CORE 2」），金色
			Text(St->MVPReason, Font(17.f * u, 160, true), X, Y + 94.f * u, Gold().CopyWithNewOpacity(0.95f * L));
			FString Line = FString::Printf(TEXT("%d KO     %d AST     %d DTH     %s DMG"), M.Row.Kills, M.Row.Assists, M.Row.Deaths, *Dmg(M.Row.Dealt));
			if (St->bCore)
			{
				Line = FString::Printf(TEXT("%d CORE     "), M.Row.Cores) + Line;
			}
			Text(Line, Font(14.f * u, 160), X, Y + 124.f * u, Th.Ink.CopyWithNewOpacity(0.75f * L));
			// 框架型号「名字」（MechCatalog 的草案名；以后各家外壳上来了就是那一家的型号）
			const MechCatalog::FInfo& Fi = MechCatalog::Info(MechLoadout::Frame, static_cast<int32>(M.Row.Loadout.Frame));
			Text(FString::Printf(TEXT("%s「%s」"), Fi.Model, Fi.Title), MechFonts::Get(11.f * u), X, Y + 150.f * u, Th.Dim.CopyWithNewOpacity(0.8f * L));
		}
	}

	// ---------------------------------------------------------------- 上方：胜负 + 比分（团队）/ 名次 + 冠军（个人）
	if (const AMechGameState* GS = St->GetWorld() ? St->GetWorld()->GetGameState<AMechGameState>() : nullptr)
	{
		const float A = UI * Smooth(0.5f, 1.3f, S) * (1.f - St->CloseUp);  // MVP 特写时让开
		const float CX = W * 0.5f;
		// 10-31：MVP 光柱调亮以后正好从上方正中穿过这几行字，字底下垫一层很淡的阴影（错开 1.5 像素的黑字），亮背景上也读得出
		auto Shadowed = [&](const FString& Str, const FSlateFontInfo& F, float X, float Y, const FLinearColor& C) {
			Text(Str, F, X + 1.5f * u, Y + 1.5f * u, FLinearColor(0.f, 0.f, 0.f, 0.55f * C.A), 0.5f);
			Text(Str, F, X, Y, C, 0.5f);
		};
		Shadowed(ModeTitle(GS) + TEXT("   ·   ") + ModeSub(GS), Font(11.f * u, 320), CX, 44.f * u, Th.Dim.CopyWithNewOpacity(A));
		FString Big;
		FLinearColor BigC = Th.Ink;
		const FMechResultRow* Champ = St->Entries.Num() ? &St->Entries[0].Row : nullptr;
		if (St->bSolo)
		{
			int32 MyRank = 0;
			for (const AMechResultsStage::FEntry& E : St->Entries)
			{
				MyRank = E.bSelf ? E.Rank : MyRank;
			}
			Big = MyRank > 0 ? FString::Printf(TEXT("#%d"), MyRank) : FString(TEXT("FINAL"));
			BigC = MyRank == 1 ? Gold() : Th.Ink;
		}
		else if (St->bDraw)
		{
			Big = TEXT("DRAW");
		}
		else if (St->SelfTeam == 0 || St->SelfTeam == 1)
		{
			Big = St->SelfTeam == St->Winner ? TEXT("VICTORY") : TEXT("DEFEAT");
			BigC = St->SelfTeam == St->Winner ? Th.Accent : Th.Bad;
		}
		else
		{
			Big = St->Winner == 0 ? TEXT("BLUE WINS") : TEXT("RED WINS");
			BigC = TeamUI(St->Winner);
		}
		const FSlateFontInfo BF = Font(40.f * u, 420, true);
		Shadowed(Big, BF, CX, 64.f * u, BigC.CopyWithNewOpacity(A));
		const float LineY = 126.f * u;
		if (St->bSolo)
		{
			if (Champ)
			{
				const FSlateFontInfo LF = Font(11.f * u, 300, true);
				const FSlateFontInfo NF = MechFonts::Get(15.f * u, true);
				const FString Tail = FString::Printf(TEXT("%d KO"), Champ->Kills);
				const float Gap = 14.f * u;
				const float Total = TextW(TEXT("WINNER"), LF) + Gap + TextW(Champ->Name, NF) + Gap + TextW(Tail, LF);
				float X = CX - Total * 0.5f;
				Text(TEXT("WINNER"), LF, X, LineY + 4.f * u, Gold().CopyWithNewOpacity(A));
				X += TextW(TEXT("WINNER"), LF) + Gap;
				Text(Champ->Name, NF, X, LineY, Th.Ink.CopyWithNewOpacity(A));
				X += TextW(Champ->Name, NF) + Gap;
				Text(Tail, LF, X, LineY + 4.f * u, Th.Dim.CopyWithNewOpacity(A));
			}
		}
		else
		{
			const FSlateFontInfo SF = Font(28.f * u, 0, true);
			Text(FString::FromInt(GS->TeamScore[0]), SF, CX - 30.f * u, LineY - 6.f * u, TeamUI(0).CopyWithNewOpacity(A), 1.f);
			Text(TEXT(":"), SF, CX, LineY - 8.f * u, Th.Dim.CopyWithNewOpacity(A), 0.5f);
			Text(FString::FromInt(GS->TeamScore[1]), SF, CX + 30.f * u, LineY - 6.f * u, TeamUI(1).CopyWithNewOpacity(A), 0.f);
			Box(CX - 120.f * u, LineY + 34.f * u, 80.f * u, 2.f * u, TeamUI(0).CopyWithNewOpacity(0.6f * A));
			Box(CX + 40.f * u, LineY + 34.f * u, 80.f * u, 2.f * u, TeamUI(1).CopyWithNewOpacity(0.6f * A));
		}
	}

	// ---------------------------------------------------------------- 转播小标（左上）+ 底边进度线
	{
		const float A = Smooth(0.2f, 0.9f, S) * UI;
		const float X = 44.f * u, Y = 38.f * u;
		const FSlateFontInfo CF = Font(16.f * u, 560, true);
		Text(TEXT("CONSENT"), CF, X, Y, Th.Ink.CopyWithNewOpacity(0.92f * A));
		const float LX = X + TextW(TEXT("CONSENT"), CF) + 16.f * u;
		const float Blink = 0.55f + 0.45f * FMath::Abs(FMath::Sin(S * 2.4f));
		Box(LX, Y + 8.f * u, 7.f * u, 7.f * u, FLinearColor(0.95f, 0.18f, 0.16f, A * Blink));
		Text(TEXT("LIVE"), Font(10.f * u, 260), LX + 12.f * u, Y + 4.f * u, Th.Ink.CopyWithNewOpacity(0.75f * A));
		Box(X, Y + 30.f * u, 196.f * u, 1.f * u, Th.Faint.CopyWithNewOpacity(A));
		Text(TEXT("MATCH RESULT"), Font(9.5f * u, 340), X, Y + 37.f * u, Th.Dim.CopyWithNewOpacity(A));
		Box(0.f, H - 2.f * u, W * FMath::Clamp(S / AMechResultsStage::TableAt, 0.f, 1.f), 2.f * u, Th.Accent.CopyWithNewOpacity(0.35f * UI));
	}

	// 切过来时从黑里淡出
	const float Black = 1.f - Smooth(0.f, AMechResultsStage::FadeInLen, St->SinceEnd() - AMechResultsStage::PreRoll);
	Box(0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, Black));
	return Layer;
}

// ================================================================ 任意键跳过（不吃掉按键）
class FMechResultsInput : public IInputProcessor
{
public:
	TWeakObjectPtr<AMechResultsStage> Stage;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& E) override
	{
		const FKey K = E.GetKey();
		if (!E.IsRepeat() && !K.IsModifierKey() && !E.IsAltDown() && !E.IsCommandDown() && K != EKeys::Tab && K != EKeys::F12)
		{
			Fire();
		}
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& E) override
	{
		Fire();
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("MechResultsInput"); }

private:
	void Fire()
	{
		if (AMechResultsStage* O = Stage.Get())
		{
			O->RequestSkip();
		}
	}
};

// ================================================================ actor
AMechResultsStage::AMechResultsStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

double AMechResultsStage::ServerNow() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
}

float AMechResultsStage::SinceEnd() const
{
	return static_cast<float>(ServerNow() - EndTime);
}

bool AMechResultsStage::IsActive(const UWorld* World)
{
	const AMechResultsStage* S = MechResultsPriv::GActive.Get();
	return S && World && S->GetWorld() == World && !S->IsActorBeingDestroyed();
}

float AMechResultsStage::ExtraMatchOverTime(const UWorld* World)
{
	// 没有画面的单机（-nullrhi 的逻辑测试）不用等；没画面的开房主机照样等（客户端要播）
	const bool bHeadlessSolo = !FApp::CanEverRender() && (!World || World->GetNetMode() == NM_Standalone);
	if (bHeadlessSolo || FParse::Param(FCommandLine::Get(), TEXT("MechNoResultsStage")))
	{
		return 0.f;
	}
	return PreRoll + TableAt;
}

bool AMechResultsStage::BeginFor(AMechPlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || !FApp::CanEverRender() || !GEngine || !GEngine->GameViewport ||
		FParse::Param(FCommandLine::Get(), TEXT("MechNoResultsStage")))
	{
		return false;
	}
	if (IsActive(PC->GetWorld()))
	{
		return true;
	}
	const AMechGameState* GS = PC->GetWorld()->GetGameState<AMechGameState>();
	if (!GS)
	{
		return false;
	}
	// 收到得太晚（中途加入、卡了很久）：直接看结算表
	if (GS->MatchEndTime >= 0.f && GS->GetServerWorldTimeSeconds() - GS->MatchEndTime > PreRoll + TableAt - 1.f)
	{
		return false;
	}
	// 自动截图：-MechAutoBattle（打完截几帧）或 -MechResultsShots（联机测试的客户端）；客户端文件名前面加 client_
	FString Tag;
	if (FParse::Param(FCommandLine::Get(), TEXT("MechAutoBattle")) || FParse::Param(FCommandLine::Get(), TEXT("MechResultsShots")))
	{
		const FString Mode = GS->bCoreMode ? TEXT("Core") : GS->Rules.IsDeathmatch() ? (GS->Rules.bSolo ? TEXT("DMSolo") : TEXT("DMTeam")) : TEXT("Elim");
		Tag = FString::Printf(TEXT("%sauto_%s_%s"), PC->GetNetMode() == NM_Client ? TEXT("client_") : TEXT(""),
			*FPackageName::GetShortName(PC->GetWorld()->GetMapName()).Replace(TEXT("UEDPIE_0_"), TEXT("")), *Mode);
	}
	return Start(PC, false, Tag) != nullptr;
}

AMechResultsStage* AMechResultsStage::Start(AMechPlayerController* PC, bool bInTest, const FString& InTag)
{
	using namespace MechResultsPriv;
	UWorld* World = PC->GetWorld();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.ObjectFlags |= RF_Transient;
	AMechResultsStage* S = World->SpawnActor<AMechResultsStage>(AMechResultsStage::StaticClass(), FTransform(MechResultsPriv::StageOrigin), P);
	if (!S)
	{
		return nullptr;
	}
	S->OwnerPC = PC;
	S->bTest = bInTest;
	S->TestTag = InTag;
	S->StageOrigin = MechResultsPriv::StageOrigin;
	const AMechGameState* GS = World->GetGameState<AMechGameState>();
	S->bEndFromServer = !bInTest && GS && GS->MatchEndTime >= 0.f;
	S->EndTime = S->bEndFromServer ? GS->MatchEndTime : S->ServerNow();
	if (bInTest)
	{
		S->EndTime -= SpawnAt - 0.05f;  // 测试：马上开始生成
	}
	S->bShots = !InTag.IsEmpty();
	if (bInTest)
	{
		S->ShotTimes = {0.5f, 1.7f, 3.0f, 4.4f, 5.6f, 6.9f, 8.4f, 9.8f, 11.2f};
	}
	else
	{
		S->ShotTimes = {0.7f, 3.0f, 5.4f, 7.8f, 9.9f, 11.4f};
	}
	// 10-31 -MechResultsPoseShots（测试）：不拍上面那几张，第 3 秒起挨个拍每台机体的正面（看站姿）
	S->bPoseShots = bInTest && FParse::Param(FCommandLine::Get(), TEXT("MechResultsPoseShots"));
	if (S->bPoseShots)
	{
		S->ShotTimes.Reset();
	}
	// 10-31 -MechResultsRecord：录本机主混音（要不带 -nosound）；-MechResultsMute 扬声器不出声（主混音的输出音量在录音之后才乘，录音照录）
	if (FParse::Param(FCommandLine::Get(), TEXT("MechResultsRecord")))
	{
		if (FAudioDevice* Dev = World->GetAudioDeviceRaw())
		{
			USoundSubmix& Main = Dev->GetMainSubmixObject();
			Main.StartRecordingOutput(Dev, 40.f);
			// 离屏测试的窗口不在前台：引擎按「失去焦点」把主音量乘 0（UnfocusedVolumeMultiplier），声音根本不出（第一次录出来全是 0）。录音时改成 1
			FApp::SetUnfocusedVolumeMultiplier(1.f);
			const bool bMute = FParse::Param(FCommandLine::Get(), TEXT("MechResultsMute"));
			if (bMute)
			{
				Main.SetSubmixOutputVolume(World, 0.f);
			}
			S->bRecording = true;
			S->RecordStartS = S->SinceEnd() - PreRoll;
			UE_LOG(LogTemp, Display, TEXT("[results] 开始录本机主混音（场景第 %.2f 秒）%s"), S->RecordStartS, bMute ? TEXT("，扬声器静音") : TEXT(""));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[results] -MechResultsRecord：没有声音设备（去掉 -nosound）"));
		}
	}
	GActive = S;
	if (GEngine && GEngine->GameViewport)
	{
		S->Overlay = SNew(SMechResultsOverlay);
		S->Overlay->Stage = S;
		GEngine->GameViewport->AddViewportWidgetContent(S->Overlay.ToSharedRef(), 13);  // HUD 10、开场 12、选配 40、菜单 50
	}
	if (FSlateApplication::IsInitialized() && !bInTest)
	{
		S->Input = MakeShared<FMechResultsInput>();
		S->Input->Stage = S;
		FSlateApplication::Get().RegisterInputPreProcessor(S->Input);
	}
	UE_LOG(LogTemp, Display, TEXT("[results] 本机开始结算场景（%s），比赛结束的服务器时间 %.2f（%s），现在 %.2f"), bInTest ? TEXT("测试") : TEXT("正常"), S->EndTime,
		S->bEndFromServer ? TEXT("服务器") : TEXT("本机估"), S->ServerNow());
	return S;
}

void AMechResultsStage::RequestSkip()
{
	if (bCut && !bSkipped && !bTableOpen && StageClock >= 1.2f && !bTest)
	{
		bSkipped = true;
	}
}

// ---------------------------------------------------------------- 结算行 → 站位
void AMechResultsStage::BuildEntries(const AMechGameState* GS)
{
	using namespace MechResultsPriv;
	Entries.Reset();
	bSolo = GS->IsSoloDM();
	bCore = GS->bCoreMode;
	Winner = bSolo ? -1 : GS->MatchWinner;
	bDraw = !bSolo && Winner < 0;
	if (const AMechPlayerController* PC = OwnerPC.Get())
	{
		SelfName = PC->GetMenuName();
		SelfTeam = PC->GetPawn() || !SelfName.IsEmpty() ? PC->GetMenuTeam() : -1;
	}
	if (bTest)
	{
		SelfName = TEXT("你");
	}
	TArray<FMechResultRow> Rows;
	for (const FMechResultRow& R : GS->Results)
	{
		if (!R.Name.IsEmpty() && R.Team >= 0)
		{
			Rows.Add(R);
		}
	}
	// 11-01 积分制（MechScore.h）：团队模式按积分排（站位「好的靠中间」也按它；交芯已经算在积分里）；个人死斗的名次照旧按击毁
	const bool bScoreSort = !bSolo;
	Rows.Sort([bScoreSort](const FMechResultRow& A, const FMechResultRow& B) {
		if (bScoreSort && A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		if (A.Kills != B.Kills)
		{
			return A.Kills > B.Kills;
		}
		if (A.Dealt != B.Dealt)
		{
			return A.Dealt > B.Dealt;
		}
		if (A.Deaths != B.Deaths)
		{
			return A.Deaths < B.Deaths;
		}
		return A.Name < B.Name;
	});
	if (bSolo && !GS->WinnerName.IsEmpty())
	{
		// 个人死斗的胜者以服务器判的为准（同击毁时排序可能不同）
		const int32 W = Rows.IndexOfByPredicate([GS](const FMechResultRow& R) { return R.Name == GS->WinnerName; });
		if (W > 0)
		{
			const FMechResultRow R = Rows[W];
			Rows.RemoveAt(W);
			Rows.Insert(R, 0);
		}
	}
	if (Rows.Num() > 10)
	{
		Rows.SetNum(10);  // 中途接管、掉线改名会多出几行；场上最多 8 台
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		FEntry& E = Entries.AddDefaulted_GetRef();
		E.Row = Rows[i];
		E.bSelf = !SelfName.IsEmpty() && Rows[i].Name == SelfName;
		E.Rank = i + 1;
		if (E.bSelf && !bSolo && SelfTeam < 0)
		{
			SelfTeam = Rows[i].Team;
		}
	}
	if (Entries.Num() == 0)
	{
		return;
	}
	// MVP（11-01 积分制，用户：「不要每次都让kd高的人当mvp」）：积分最高的——团队 = 胜方里（平局 = 全场）；
	// 个人死斗 = 全场（名次、领奖台、胜者照旧按击毁，所以 MVP 可以不是第一名）
	MVPIndex = MechScore::PickMVP(Rows, !bSolo && Winner >= 0 ? Winner : -1);
	MVPIndex = Entries.IsValidIndex(MVPIndex) ? MVPIndex : 0;
	Entries[MVPIndex].bMVP = true;
	MVPReason = MechScore::Reason(Entries[MVPIndex].Row, Rows);

	auto FaceCamera = [](FEntry& E) {
		// 都朝向镜头大致所在的位置（正前方 22 米），两边的稍微往里转
		const FVector To = FVector(-2200.f, 0.f, 0.f) - E.Local;
		E.Yaw = To.Rotation().Yaw;
	};
	TArray<int32> Front, Back;
	if (bSolo)
	{
		// 领奖台：第一名中间最高，第二名镜头左边，第三名右边；其余后排
		static const FVector Podium[3] = {FVector(0.f, 0.f, 0.f), FVector(90.f, -430.f, 0.f), FVector(90.f, 430.f, 0.f)};
		static const float Height[3] = {60.f, 36.f, 18.f};
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			FEntry& E = Entries[i];
			if (i < 3)
			{
				E.bFront = true;
				E.Local = Podium[i];
				E.Plinth = Height[i];
				E.bThumbs = i == 0 || E.bMVP;
				E.Lean = i == 0 || E.bMVP ? -5.f : -2.f;
				E.AimDown = 32.f;
				Front.Add(i);
			}
			else if (E.bMVP)
			{
				// 11-01 积分制：MVP 是第四名以后（积分最高、击毁不是最多）——不站后排（镜头要推到他胸前、聚光要照到他），
				// 站在领奖台左边地上（第二名的台子再往外 4.3 米），竖大拇指
				E.bFront = true;
				E.Local = FVector(40.f, -860.f, 0.f);
				E.bThumbs = true;
				E.Lean = -5.f;
				E.AimDown = 32.f;
				Front.Add(i);
			}
			else
			{
				E.bFront = false;
				E.Lean = 18.f;
				E.AimDown = 42.f;  // 10-31：72 → 42（低姿持枪改成两手 IK：左手托护木、右手沿枪管退回胸前，压太低时狙击枪的枪托翘到脸前）
				Back.Add(i);
			}
			E.GlowTeam = E.bSelf ? 0 : 1;
		}
		// 后排站在台子之间的空当里、再往里退一点（台子在 0、±4.3 米）
		const TArray<float> Gaps = GapSlots(Back.Num(), 430.f * 1.25f);  // 后排远 9 米，透视会往中间收，按镜头距离放宽（不然正好躲在台子后面）
		for (int32 k = 0; k < Back.Num(); ++k)
		{
			Entries[Back[k]].Local = FVector(900.f, Gaps[k], 0.f);
		}
	}
	else if (bDraw)
	{
		// 平局：两队并排（蓝左红右），每队里好的靠中间，全场最好的稍微往前
		int32 Count[2] = {0, 0};
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			FEntry& E = Entries[i];
			const int32 Side = E.Row.Team == 0 ? -1 : 1;
			const int32 k = Count[E.Row.Team == 0 ? 0 : 1]++;
			E.bFront = true;
			E.Local = FVector(E.bMVP ? -60.f : 0.f, Side * (230.f + k * 390.f), 0.f);
			E.Lean = 0.f;
			E.AimDown = 38.f;
			E.GlowTeam = E.Row.Team == 0 ? 0 : 1;
			Front.Add(i);
		}
	}
	else
	{
		// 团队：胜方前排（MVP 居中、往前一点、竖大拇指），败方后排（和前排错开、上身前倾）
		TArray<int32> Others;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			FEntry& E = Entries[i];
			E.GlowTeam = E.Row.Team == 0 ? 0 : 1;
			if (E.Row.Team == Winner)
			{
				E.bFront = true;
				Front.Add(i);
				if (i != MVPIndex)
				{
					Others.Add(i);
				}
				E.bThumbs = i == MVPIndex;
				E.Lean = i == MVPIndex ? -5.f : -3.f;
				E.AimDown = 32.f;
			}
			else
			{
				E.bFront = false;
				E.Lean = 18.f;
				E.AimDown = 42.f;  // 10-31：72 → 42（同上）
				Back.Add(i);
			}
		}
		Entries[MVPIndex].Local = FVector(-90.f, 0.f, 0.f);
		// 其余胜方从 MVP 两边往外排（先右后左）
		for (int32 k = 0; k < Others.Num(); ++k)
		{
			const int32 Step = k / 2 + 1;
			Entries[Others[k]].Local = FVector(0.f, (k % 2 == 0 ? 1.f : -1.f) * Step * 400.f, 0.f);
		}
		// 败方后排站在前排的空当里（前排在 0、±4、±8 米，后排在 ±2、±6、±10 米；好的靠中间，先右后左），不被前排挡住
		const TArray<float> Gaps = GapSlots(Back.Num(), 400.f * 1.3f);  // 后排远 6.5 米，透视会往中间收，按镜头距离放宽
		for (int32 k = 0; k < Back.Num(); ++k)
		{
			Entries[Back[k]].Local = FVector(650.f, Gaps[k], 0.f);
		}
	}
	for (FEntry& E : Entries)
	{
		FaceCamera(E);
		// 打刀比打枪多的拿着刀站（竖大拇指的把武器都收着）
		E.bBlade = !E.bThumbs && E.Row.Blades > E.Row.Bullets && E.Row.Blades > 0;
	}
	// 取景用：前排中心、半宽，MVP 胸口
	FBox2D Span(ForceInit);
	for (const FEntry& E : Entries)
	{
		Span += FVector2D(E.Local.Y - 180.f, 0.f);
		Span += FVector2D(E.Local.Y + 180.f, 0.f);
	}
	GroupCenter = FVector(0.f, (Span.Min.X + Span.Max.X) * 0.5f, 0.f);
	GroupHalfW = FMath::Max(360.f, (Span.Max.X - Span.Min.X) * 0.5f);
	// 全景的视野和距离（2v2 近、4v4 / 8 人远）；搭台时前墙、侧墙按它往外放
	WideFov = FMath::Clamp(30.f + GroupHalfW / 70.f, 32.f, 44.f);
	WideDist = FMath::Max(1250.f, GroupHalfW * 1.12f / FMath::Tan(FMath::DegreesToRadians(WideFov * 0.5f)));
	const FEntry& M = Entries[MVPIndex];
	MVPChest = M.Local + FVector(0.f, 0.f, M.Plinth + 160.f);
	FString Report;
	for (const FEntry& E : Entries)
	{
		Report += FString::Printf(TEXT("\n    %s%s 队%d %s 位置(%.0f,%.0f) 台%.0f SCORE%d KO%d AST%d DTH%d DMG%d CORE%d %s%s%s"), *E.Row.Name, E.bSelf ? TEXT("(自己)") : TEXT(""),
			E.Row.Team, E.bFront ? TEXT("前排") : TEXT("后排"), E.Local.X, E.Local.Y, E.Plinth, E.Row.Score, E.Row.Kills, E.Row.Assists, E.Row.Deaths, E.Row.Dealt, E.Row.Cores,
			E.bMVP ? TEXT("MVP ") : TEXT(""), E.bThumbs ? TEXT("竖拇指 ") : TEXT(""), *MechLoadout::Describe(E.Row.Loadout));
	}
	UE_LOG(LogTemp, Display, TEXT("[results] %s，胜方 %d%s，%d 台：%s"), bSolo ? TEXT("个人死斗") : bCore ? TEXT("夺芯") : GS->Rules.IsDeathmatch() ? TEXT("团队死斗") : TEXT("团队淘汰"),
		Winner, bDraw ? TEXT("（平局）") : TEXT(""), Entries.Num(), *Report);
	UE_LOG(LogTemp, Display, TEXT("[results] MVP %s（第 %d 名）：%s"), *Entries[MVPIndex].Row.Name, Entries[MVPIndex].Rank, *MVPReason);
}

// ---------------------------------------------------------------- 搭台：地面、墙、灯、地环、光柱
UStaticMeshComponent* AMechResultsStage::AddMesh(UStaticMesh* Mesh, UMaterialInterface* Mat, const FVector& At, const FRotator& Rot, const FVector& Scale)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(GetRootComponent());
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetLightingChannels(MechResultsPriv::Ch0, MechResultsPriv::Ch1, MechResultsPriv::Ch2);
	C->bVisibleInRayTracing = false;
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	C->RegisterComponent();
	C->SetRelativeTransform(FTransform(Rot, At, Scale));
	Meshes.Add(C);
	return C;
}

UMaterialInstanceDynamic* AMechResultsStage::FloorMID(const FLinearColor& Color, float Roughness, const FLinearColor& Grid, float GridIntensity, float GridSize,
	float FadeRadius, bool bWall)
{
	if (bCustomMats)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(FloorMat, this);
		M->SetVectorParameterValue(TEXT("Color"), Color);
		M->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		M->SetVectorParameterValue(TEXT("GridColor"), Grid);
		M->SetScalarParameterValue(TEXT("GridIntensity"), GridIntensity);
		M->SetVectorParameterValue(TEXT("Center"), FLinearColor(StageOrigin.X, StageOrigin.Y, StageOrigin.Z, 0.f));
		M->SetVectorParameterValue(TEXT("Prm"), FLinearColor(GridSize, 0.012f, FadeRadius, bWall ? 1.f : 0.f));
		return M;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(PlainMat, this);
	if (M)
	{
		M->SetVectorParameterValue(TEXT("Color"), Color);
	}
	return M;
}

UMaterialInstanceDynamic* AMechResultsStage::GlowMID(const FLinearColor& Color, float Strength, float Radius, float Width, float Disc, float Solid)
{
	UMaterialInterface* Base = bCustomMats ? GlowMat : BeamMat;
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetScalarParameterValue(TEXT("Strength"), Strength);
	M->SetVectorParameterValue(TEXT("Prm"), FLinearColor(Radius, Width, Disc, Solid));
	// 退回的 M_MechBeam 用 Intensity
	M->SetScalarParameterValue(TEXT("Intensity"), Strength * 0.002f);
	BeamMIDs.Emplace(M, Strength);
	return M;
}

void AMechResultsStage::AddRing(const FVector& Feet, float Size, const FLinearColor& Color, float Strength, float Radius, float Width, float Disc)
{
	if (!bCustomMats)
	{
		return;  // 退回材质画不出环，干脆不画
	}
	AddMesh(PlaneMesh, GlowMID(Color, Strength, Radius, Width, Disc, 0.f), Feet + FVector(0.f, 0.f, 0.8f), FRotator::ZeroRotator, FVector(Size / 100.f));
}

UMaterialInstanceDynamic* AMechResultsStage::AddBeam(const FVector& Top, const FVector& Bottom, float BottomRadius, const FLinearColor& Color, float Strength,
	float EdgeExp, float AlongExp, float SoftDist)
{
	if (!bCustomMats || !BeamMat || !ConeMesh)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(BeamMat, this);
	const FVector Axis = Top - Bottom;
	const float Len = Axis.Size();
	M->SetVectorParameterValue(TEXT("Color"), Color);
	M->SetScalarParameterValue(TEXT("Strength"), Strength);
	const FVector TopW = ToWorld(Top);
	M->SetVectorParameterValue(TEXT("Origin"), FLinearColor(TopW.X, TopW.Y, TopW.Z, 0.f));
	const FVector Dir = (-Axis).GetSafeNormal();
	M->SetVectorParameterValue(TEXT("Dir"), FLinearColor(Dir.X, Dir.Y, Dir.Z, 0.f));
	M->SetVectorParameterValue(TEXT("Prm"), FLinearColor(Len, EdgeExp, SoftDist, AlongExp));
	BeamMIDs.Emplace(M, Strength);
	// 引擎圆锥：高 100、底半径 50、尖朝 +Z、原点在中间
	UStaticMeshComponent* C = AddMesh(ConeMesh, M, (Top + Bottom) * 0.5f, FRotationMatrix::MakeFromZ(Axis).Rotator(),
		FVector(BottomRadius / 50.f, BottomRadius / 50.f, Len / 100.f));
	if (C)
	{
		C->SetCastShadow(false);
	}
	return M;
}

USpotLightComponent* AMechResultsStage::AddSpot(const FVector& At, const FVector& Target, const FLinearColor& Color, float Candela, float Outer, float Inner,
	bool bShadows, float SourceRadius, float Radius)
{
	USpotLightComponent* L = NewObject<USpotLightComponent>(this);
	L->SetMobility(EComponentMobility::Movable);
	L->SetupAttachment(GetRootComponent());
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetIntensity(Candela * MechResultsPriv::LightScale);
	L->SetLightColor(Color);
	L->SetAttenuationRadius(Radius);
	L->SetOuterConeAngle(Outer);
	L->SetInnerConeAngle(Inner);
	L->SetSourceRadius(SourceRadius);
	L->SetSoftSourceRadius(SourceRadius * 2.f);
	L->SetCastShadows(bShadows);
	L->SetVolumetricScatteringIntensity(0.f);
	L->SetLightingChannels(MechResultsPriv::Ch0, MechResultsPriv::Ch1, MechResultsPriv::Ch2);
	L->RegisterComponent();
	L->SetRelativeLocationAndRotation(At, (Target - At).Rotation());
	Lights.Add(L);
	LightBase.Add(Candela);
	return L;
}

void AMechResultsStage::BuildStage()
{
	using namespace MechResultsPriv;
	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	ConeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	FloorMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Results/M_ResultsFloor.M_ResultsFloor"));
	GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Results/M_ResultsGlow.M_ResultsGlow"));
	BeamMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Results/M_ResultsBeam.M_ResultsBeam"));
	PlainMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	bCustomMats = FloorMat && GlowMat && BeamMat;
	if (!bCustomMats)
	{
		UE_LOG(LogTemp, Warning, TEXT("[results] 没有 /Game/Results/M_Results* 材质（tools/ue_make_results_fx.py），地面 / 墙退回基础材质，没有地环和光柱"));
	}
	const FLinearColor Cyan(0.25f, 0.85f, 1.f);
	// ---- 封闭的暗场：地面（有反射）、背景墙（淡网格）、两侧墙、身后墙、顶。
	// 身后墙按全景镜头距离往外放（人多时镜头退到 40 多米，以前墙固定在 34 米，镜头跑到墙外面、画面全黑）
	const float FrontX = -(WideDist * 1.15f + 600.f);
	const float SideY = FMath::Max(3000.f, GroupHalfW + 2400.f);
	const float SpanX = (1900.f - FrontX) / 100.f + 2.f;
	const float SpanY = SideY * 2.f / 100.f + 2.f;
	const FVector Mid((1900.f + FrontX) * 0.5f, GroupCenter.Y, 0.f);
	AddMesh(CubeMesh, FloorMID(FLinearColor(0.016f, 0.018f, 0.022f), 0.2f, Cyan, 1.2f, 200.f, 2600.f, false), Mid + FVector(0.f, 0.f, -10.f), FRotator::ZeroRotator,
		FVector(SpanX, SpanY, 0.2f));
	UMaterialInstanceDynamic* WallM = FloorMID(FLinearColor(0.011f, 0.012f, 0.015f), 0.55f, Cyan, 0.5f, 300.f, 2800.f, true);
	AddMesh(CubeMesh, WallM, FVector(1900.f, GroupCenter.Y, 900.f), FRotator::ZeroRotator, FVector(0.4f, SpanY, 18.f));
	AddMesh(CubeMesh, FloorMID(FLinearColor(0.008f, 0.009f, 0.011f), 0.7f, Cyan, 0.f, 300.f, 100.f, true), FVector(FrontX, GroupCenter.Y, 900.f), FRotator::ZeroRotator,
		FVector(0.4f, SpanY, 18.f));
	for (const float Side : {-1.f, 1.f})
	{
		AddMesh(CubeMesh, FloorMID(FLinearColor(0.009f, 0.01f, 0.012f), 0.7f, Cyan, 0.f, 300.f, 100.f, true), FVector(Mid.X, GroupCenter.Y + Side * SideY, 900.f),
			FRotator::ZeroRotator, FVector(SpanX, 0.4f, 18.f));
	}
	AddMesh(CubeMesh, FloorMID(FLinearColor(0.006f, 0.007f, 0.008f), 0.8f, Cyan, 0.f, 300.f, 100.f, false), Mid + FVector(0.f, 0.f, 1810.f), FRotator::ZeroRotator,
		FVector(SpanX, SpanY, 0.2f));

	// ---- 领奖台（个人死斗前三名）：深色方台 + 正面上沿一道金 / 银 / 铜色光条
	for (const FEntry& E : Entries)
	{
		if (E.Plinth <= 0.f)
		{
			continue;
		}
		const FLinearColor C = E.Rank == 1 ? FLinearColor(1.f, 0.72f, 0.3f) : E.Rank == 2 ? FLinearColor(0.75f, 0.82f, 0.9f) : FLinearColor(0.95f, 0.5f, 0.25f);
		AddMesh(CubeMesh, FloorMID(FLinearColor(0.025f, 0.027f, 0.031f), 0.32f, Cyan, 0.f, 200.f, 100.f, false), E.Local + FVector(0.f, 0.f, E.Plinth * 0.5f),
			FRotator::ZeroRotator, FVector(2.3f, 2.3f, E.Plinth / 100.f));
		if (bCustomMats)
		{
			AddMesh(CubeMesh, GlowMID(C, 26.f, 0.f, 0.f, 0.f, 1.f), E.Local + FVector(-116.f, 0.f, E.Plinth - 2.f), FRotator::ZeroRotator, FVector(0.03f, 2.32f, 0.03f));
		}
	}
	// ---- 脚下的地环：前排队伍色（个人死斗按名次色）、MVP 金色 + 一圈软光
	for (const FEntry& E : Entries)
	{
		const FVector Feet = E.Local + FVector(0.f, 0.f, E.Plinth);
		if (E.bMVP)
		{
			// MVP 的地环跟着聚光「打亮」（之前暗着亮三成），记下来 UpdateSpot 每帧调
			const int32 From = BeamMIDs.Num();
			AddRing(Feet, 520.f, FLinearColor(1.f, 0.72f, 0.3f), 16.f, 0.62f, 0.05f, 0.45f);
			for (int32 k = From; k < BeamMIDs.Num(); ++k)
			{
				MVPGlows.Add(BeamMIDs[k]);
			}
		}
		else if (E.bFront)
		{
			// 11-01：个人死斗第一名不是 MVP 时（积分制）地环也是金色
			const FLinearColor C = bSolo ? (E.Rank == 1 ? FLinearColor(1.f, 0.72f, 0.3f) : E.Rank == 2 ? FLinearColor(0.75f, 0.82f, 0.9f) : FLinearColor(0.95f, 0.5f, 0.25f))
				: TeamLight(E.Row.Team);
			AddRing(Feet, 330.f, C, 7.f, 0.8f, 0.045f, 0.08f);
		}
		else
		{
			AddRing(Feet, 300.f, TeamLight(E.GlowTeam), 2.2f, 0.82f, 0.04f, 0.f);
		}
	}

	// ---- 灯：主光（左前上方、暖白、投影）、补光（右前、冷）、两侧轮廓光（背后、冷白）、后排一盏冷光、
	//         队伍色氛围光（打在背景墙上）、MVP 头顶窄聚光（金白、投影）
	const FVector C = GroupCenter;
	const float HW = GroupHalfW;
	AddSpot(C + FVector(-750.f, -950.f, 900.f), C + FVector(60.f, 0.f, 110.f), FLinearColor(1.f, 0.92f, 0.82f), 42000.f, 44.f, 24.f, true, 40.f);
	AddSpot(C + FVector(-1150.f, 850.f, 280.f), C + FVector(0.f, 0.f, 140.f), FLinearColor(0.68f, 0.8f, 1.f), 7500.f, 60.f, 30.f, false, 80.f);
	// 轮廓光从背后高处陡着往下打、锥角收窄：以前从 5.6 米高斜着平打（锥 38 度），光越过机体打到镜头前的地上，
	// 光滑地面把它反进镜头，画面两个下角各一大团白
	for (const float Side : {-1.f, 1.f})
	{
		AddSpot(C + FVector(480.f, Side * (HW * 0.55f + 250.f), 980.f), C + FVector(-60.f, Side * HW * 0.3f, 165.f), FLinearColor(0.72f, 0.9f, 1.f), 60000.f, 26.f,
			14.f, false, 25.f);
	}
	bool bHasBack = false;
	FVector BackC = FVector::ZeroVector;
	int32 NBack = 0;
	for (const FEntry& E : Entries)
	{
		if (!E.bFront)
		{
			bHasBack = true;
			BackC += E.Local;
			++NBack;
		}
	}
	if (bHasBack)
	{
		BackC /= NBack;
		AddSpot(BackC + FVector(-500.f, 0.f, 1100.f), BackC + FVector(0.f, 0.f, 60.f), FLinearColor(0.6f, 0.7f, 0.9f), 11000.f, 48.f, 28.f, false, 60.f);
	}
	// 队伍色氛围光：从地面往背景墙上打一大片（胜方亮、败方暗；个人死斗中间金色、两边暗红）
	auto Wash = [&](float Y, const FLinearColor& Color, float Candela) {
		AddSpot(FVector(1100.f, Y, 30.f), FVector(1900.f, Y, 700.f), Color, Candela, 58.f, 20.f, false, 200.f, 5000.f);
	};
	if (bSolo)
	{
		Wash(0.f, FLinearColor(1.f, 0.62f, 0.25f), 150000.f);
		Wash(-1500.f, TeamLight(1), 50000.f);
		Wash(1500.f, TeamLight(1), 50000.f);
	}
	else if (bDraw)
	{
		Wash(-900.f, TeamLight(0), 130000.f);
		Wash(900.f, TeamLight(1), 130000.f);
	}
	else
	{
		Wash(C.Y - 700.f, TeamLight(Winner), 150000.f);
		Wash(C.Y + 700.f, TeamLight(Winner), 150000.f);
		if (bHasBack)
		{
			Wash(C.Y - 2000.f, TeamLight(1 - Winner), 40000.f);
			Wash(C.Y + 2000.f, TeamLight(1 - Winner), 40000.f);
		}
	}
	const FEntry& M = Entries[MVPIndex];
	const FVector MFeet = M.Local + FVector(0.f, 0.f, M.Plinth);
	// 10-31：MVP 聚光 95000 → 120000 坎德拉、内锥 9 → 10.5 度（光斑边更利落）；场景第 SpotOnAt 秒才「打亮」（UpdateSpot）
	MVPSpot = AddSpot(MFeet + FVector(-140.f, 0.f, 1150.f), MFeet + FVector(0.f, 0.f, 20.f), FLinearColor(1.f, 0.88f, 0.66f), 120000.f, 15.f, 10.5f, true, 20.f);
	MVPSpotBase = LightBase.Num() ? LightBase.Last() : 0.f;

	// ---- 光柱（假体积光）：MVP 头顶一道，背后几道斜的。
	// 10-31 用户：「光柱偏淡」。以前一层（底半径 3 米、亮度 0.9、沿长度衰减指数 0.9）：固定曝光 -5.2 下自发光要十几才看得见（地环是 16），
	// 0.9 几乎是黑的，而且画面里看得见的下半截衰减到只剩一两成。→ 两层：外面一层淡的光晕（4）+ 里面一道窄而实的光芯（7，边缘指数大 = 边利落），
	// 沿长度衰减指数 0.35 / 0.25，一直亮到脚下；颜色往白里靠一点（读成「光」，不是金色的雾）。
	// 软边 70 → 450 / 280 厘米：光柱罩在 MVP 前面的那一层（离机体一两米）淡掉，不然特写整个画面蒙一层白雾、机体发灰（第一版特写平均亮度 90 → 136）；
	// 背后是 19 米外的背景墙，照样亮。亮度按不过曝定（mech.Results.MVPBeam 可调）
	const FVector BeamTop = MFeet + FVector(-140.f, 0.f, 1150.f);
	MVPGlows.Add(TPair<TObjectPtr<UMaterialInstanceDynamic>, float>(AddBeam(BeamTop, MFeet, 330.f, FLinearColor(1.f, 0.86f, 0.62f), 4.f, 1.3f, 0.35f, 450.f), 4.f));
	MVPGlows.Add(TPair<TObjectPtr<UMaterialInstanceDynamic>, float>(AddBeam(BeamTop, MFeet, 140.f, FLinearColor(1.f, 0.9f, 0.74f), 7.f, 2.2f, 0.25f, 280.f), 7.f));
	for (int32 k = 0; k < 4; ++k)
	{
		const float Y = C.Y + (k - 1.5f) * FMath::Max(700.f, HW * 0.75f);
		const float Lean = (k % 2 == 0 ? -1.f : 1.f) * 260.f;
		AddBeam(FVector(1300.f, Y + Lean, 1780.f), FVector(1000.f, Y - Lean, 0.f), 210.f, FLinearColor(0.55f, 0.82f, 1.f), 0.32f);
	}
	UE_LOG(LogTemp, Display, TEXT("[results] 搭台：%d 个物件、%d 盏灯，%s"), Meshes.Num(), Lights.Num(), bCustomMats ? TEXT("自定义材质") : TEXT("退回基础材质"));
	UpdateSpot(-1.f);  // MVP 聚光、光柱先关着（地环暗着亮三成），场景第 SpotOnAt 秒打亮
	LoadAudio();
}

// ---------------------------------------------------------------- 10-31 MVP 聚光「打亮」
void AMechResultsStage::UpdateSpot(float S)
{
	using namespace MechResultsPriv;
	// 合闸：0.06 秒冲到 1.15 倍，0.3 秒内落回 1（大灯打亮那一下的冲劲），之前是 0
	const float T = S - SpotOnAt;
	const float Gate = T < 0.f ? 0.f : T < 0.06f ? 1.15f * T / 0.06f : FMath::Lerp(1.15f, 1.f, FMath::SmoothStep(0.06f, 0.3f, T));
	// 特写时光柱淡到三成：镜头离 MVP 八九米、整个画面都在光柱的光晕里，满亮度时整张蒙一层白（特写平均亮度 90 → 128）
	const float Near = 1.f - 0.7f * FMath::SmoothStep(0.f, 1.f, CloseUp);
	const float Key = Gate * 1000.f + LightScale * 10.f + MVPSpotScale + BeamScale * 7.f + MVPBeamScale * 3.f + Near * 100000.f;
	if (Key == LastSpotKey)
	{
		return;  // 稳住以后不再每帧刷灯和材质
	}
	LastSpotKey = Key;
	if (MVPSpot)
	{
		MVPSpot->SetIntensity(MVPSpotBase * LightScale * MVPSpotScale * Gate);
	}
	for (int32 k = 0; k < MVPGlows.Num(); ++k)
	{
		if (UMaterialInstanceDynamic* M = MVPGlows[k].Key)
		{
			const float Least = k == 0 ? 0.3f : 0.f;  // 0 号是地环：打亮之前暗着亮三成，认得出 MVP 站哪
			M->SetScalarParameterValue(TEXT("Strength"), MVPGlows[k].Value * BeamScale * MVPBeamScale * FMath::Max(Least, Gate) * (k == 0 ? 1.f : Near));
		}
	}
}

float AMechResultsStage::PlateCount(int32 i, float S)
{
	const float Appear = PlateAt + i * PlateStep;
	return FMath::SmoothStep(Appear + CountFrom, Appear + CountTo, S);
}

// ---------------------------------------------------------------- 10-31 配乐和音效
// 用户：「没有音乐和音效」。配乐是自己合成的（SourceAssets/sfx_results/make_results_music.py：冷调电子，96 拍 / 分，一小节 2.5 秒，
// 第 0 秒起 = 场景切过来、第三小节（5 秒）= 推到 MVP 特写、第 10 秒的收束和弦 = 结算表弹出，之后淡下去；结算表后面换一层循环底噪）；
// 音效从音效全集里挑的再加工（同目录 make_results_sfx.py，来源写在 来源.txt）。响度按告警 / 战斗音效定：配乐整段约 -24 LUFS、音效最响 0.4 秒约 -20～-30 LUFS，
// 都烤进了 wav（代码里音量 1），最后都乘设置里的主音量（FMechSettings::ApplyAudio → 音频设备的主音量）。
// 各端按同一条时间轴（服务器的 MatchEndTime）在本机放，日志「[results] 声音」记每一下是场景第几秒放的。
namespace MechResultsPriv
{
	/** 镜头推近 / 拉远的风声什么时候起（风声本身慢慢涌起来，最响那一刻落在镜头最快的时候：推近约 4 秒、拉远约 8.5 秒） */
	constexpr float WhooshInAt = 2.95f;
	constexpr float WhooshOutAt = 7.15f;
}

void AMechResultsStage::LoadAudio()
{
	auto Load = [](const TCHAR* Name) {
		return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/Results/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	};
	SndMusic = Load(TEXT("S_results_music"));
	SndBed = Load(TEXT("S_results_bed"));
	SndWhooshIn = Load(TEXT("S_results_whoosh_in"));
	SndWhooshOut = Load(TEXT("S_results_whoosh_out"));
	SndSpot = Load(TEXT("S_results_spot"));
	SndTick = Load(TEXT("S_results_tick"));
	SndLock = Load(TEXT("S_results_lock"));
	SndTable = Load(TEXT("S_results_table"));
	bAudioLoaded = true;
	if (!SndMusic || !SndSpot || !SndTick)
	{
		UE_LOG(LogTemp, Warning, TEXT("[results] /Game/Audio/Results 的声音不全（tools/ue_import_results_audio.py 导入），缺的那几下不放"));
	}
}

UAudioComponent* AMechResultsStage::Cue(const TCHAR* Name, USoundBase* Sound, float Volume, float Pitch, float StartAt, bool bLog, float FadeIn)
{
	using namespace MechResultsPriv;
	UAudioComponent* A = Sound ? UGameplayStatics::CreateSound2D(this, Sound, Volume * AudioScale, Pitch, StartAt, nullptr, false, true) : nullptr;
	if (A)
	{
		A->bIsUISound = true;  // 界面声音：单机开着菜单暂停时也不停在半截
		if (FadeIn > 0.f)
		{
			A->FadeIn(FadeIn, 1.f, StartAt);
		}
		else
		{
			A->Play(StartAt);
		}
	}
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[results] 声音 场景第 %.2f 秒：%s（音量 %.2f%s）%s"), StageClock, Name, Volume * AudioScale,
			StartAt > 0.01f ? *FString::Printf(TEXT("，从第 %.2f 秒放起"), StartAt) : TEXT(""),
			A ? TEXT("") : Sound ? TEXT("——没有声音设备（-nosound）") : TEXT("——没有这个声音资源"));
	}
	return A;
}

void AMechResultsStage::FadeOutAudio(float Seconds)
{
	for (UAudioComponent* A : {Music.Get(), Bed.Get()})
	{
		if (IsValid(A) && A->IsPlaying())
		{
			A->FadeOut(Seconds, 0.f);
		}
	}
}

void AMechResultsStage::UpdateAudio(float S)
{
	using namespace MechResultsPriv;
	if (!bAudioLoaded || !bCut)
	{
		return;
	}
	// 配乐：场景第 0 秒起；晚到的（客户端收到得晚、刚切过来就已经过了一会儿）从对应的位置放起，和别人对齐
	if (!bMusicCued)
	{
		bMusicCued = true;
		if (!bSkipped && S < TableAt - 0.5f)
		{
			Music = Cue(TEXT("配乐"), SndMusic, MusicScale * FMechSettings::Get().MusicVolume, 1.f, S > 0.15f ? S : 0.f);  // 11-01 再乘设置里的音乐音量
		}
	}
	// 一次性的几下：到点放；过了 0.4 秒以上才看到的（晚到、卡了一下）就不补了，免得挤在一起
	auto Once = [&](bool& bDone, float At, const TCHAR* Name, USoundBase* Sound) {
		if (!bDone && S >= At)
		{
			bDone = true;
			if (!bSkipped && S < At + 0.4f)
			{
				Cue(Name, Sound, 1.f);
			}
		}
	};
	Once(bSpotCued, SpotOnAt, TEXT("MVP 聚光打亮"), SndSpot);
	Once(bWhooshInCued, WhooshInAt, TEXT("镜头推近"), SndWhooshIn);
	Once(bWhooshOutCued, WhooshOutAt, TEXT("镜头拉远"), SndWhooshOut);
	// 铭牌数字跳动：看得见的数字（KO / DTH / CORE，DMG 按显示的精度）一变就「嗒」一下，最密 0.065 秒一下；越数到后面音调越高一点
	if (!bLockCued && Entries.Num() > 0)
	{
		int64 Digits = 0;
		float Avg = 0.f;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const FMechResultRow& R = Entries[i].Row;
			const float C = PlateCount(i, S);
			const float Dealt = R.Dealt * C;
			Digits += FMath::RoundToInt(R.Kills * C) + FMath::RoundToInt(R.Deaths * C) + FMath::RoundToInt(R.Cores * C) +
				FMath::RoundToInt(R.Assists * C) + FMath::RoundToInt(R.Score * C) +  // 11-01：铭牌多了 SCORE、AST
				(Dealt >= 10000.f ? FMath::RoundToInt(Dealt / 100.f) : FMath::RoundToInt(Dealt));
			Avg += C / Entries.Num();
		}
		if (LastDigits >= 0 && Digits != LastDigits && S - LastTickS >= 0.065f && !bSkipped)
		{
			LastTickS = S;
			FirstTickS = FirstTickS < 0.f ? S : FirstTickS;
			++TickCount;
			Cue(TEXT("数字跳动"), SndTick, 0.7f + 0.3f * Avg, 0.94f + 0.18f * Avg + FMath::FRandRange(-0.015f, 0.015f), 0.f, false);
		}
		LastDigits = Digits;
		// 全部数完：定格一下
		const float LockAt = PlateAt + (Entries.Num() - 1) * PlateStep + CountTo;
		if (S >= LockAt)
		{
			bLockCued = true;
			UE_LOG(LogTemp, Display, TEXT("[results] 声音 场景第 %.2f–%.2f 秒：数字跳动 %d 下"), FirstTickS, LastTickS, TickCount);
			if (!bSkipped && S < LockAt + 0.4f)
			{
				Cue(TEXT("数字定格"), SndLock, 1.f);
			}
		}
	}
	// 结算表弹出：一声；配乐在第 10 秒自己收束（跳过的话提前淡掉），后面换一层循环底噪慢慢淡入，跟着结算表一直到下一局
	if (bTableOpen && !bTableCued)
	{
		bTableCued = true;
		Cue(TEXT("结算表弹出"), SndTable, 1.f);
		if (IsValid(Music) && S < TableAt - 0.3f)
		{
			Music->FadeOut(0.6f, 0.f);
			UE_LOG(LogTemp, Display, TEXT("[results] 声音 场景第 %.2f 秒：跳过，配乐 0.6 秒淡出"), S);
		}
		Bed = Cue(TEXT("底噪循环（2.5 秒淡入）"), SndBed, MusicScale * FMechSettings::Get().MusicVolume, 1.f, 0.f, true, 2.5f);  // 11-01 乘音乐音量
	}
}

void AMechResultsStage::StopRecording()
{
	if (!bRecording)
	{
		return;
	}
	bRecording = false;
	FAudioDevice* Dev = GetWorld() ? GetWorld()->GetAudioDeviceRaw() : nullptr;
	if (!Dev)
	{
		return;
	}
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Audio/Results"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Name = (TestTag.IsEmpty() ? FString(TEXT("results")) : TestTag) + TEXT("_rec");
	Dev->GetMainSubmixObject().StopRecordingOutput(Dev, EAudioRecordingExportType::WavFile, Name, Dir);
	UE_LOG(LogTemp, Display, TEXT("[results] 录音写到 %s/%s.wav（录音第 0 秒 = 场景第 %.2f 秒），停在场景第 %.2f 秒"), *Dir, *Name, RecordStartS, StageClock);
}

// ---------------------------------------------------------------- 10-31 站姿检查
void AMechResultsStage::LogPoses(const TCHAR* When) const
{
	for (const FEntry& E : Entries)
	{
		const AMechCharacter* M = E.Mech.Get();
		const UMechMeshComponent* Mesh = M ? M->GetMechMesh() : nullptr;
		if (!Mesh)
		{
			continue;
		}
		const FQuat Vis = Mesh->VisualActorQuat();
		const FVector Fwd = Vis.GetForwardVector(), Right = Vis.GetRightVector();
		// 胸口：两根锁骨根的连线；胯：两条大腿根的连线（相对机体右方，偏航 + 侧歪）
		auto Line = [&](const TCHAR* A, const TCHAR* B, float& Yaw, float& Roll) {
			const FVector D = (Mesh->GetBoneLocation(B) - Mesh->GetBoneLocation(A)).GetSafeNormal();
			const FVector Flat = FVector(D.X, D.Y, 0.f).GetSafeNormal();
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(Right, Flat), FVector::UpVector), FVector::DotProduct(Flat, Right)));
			Roll = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D.Z, -1.f, 1.f)));
		};
		float CY, CR, HY, HR;
		Line(TEXT("clavicle_l"), TEXT("clavicle_r"), CY, CR);
		Line(TEXT("thigh_l"), TEXT("thigh_r"), HY, HR);
		FString Gun = TEXT("枪收着");
		const USceneComponent* G = Mesh->Props[1];
		if (G && !M->bResultsStow && M->GetCombat() && !M->GetCombat()->IsBladeInHand())
		{
			const FVector B = G->GetForwardVector();
			const float GY = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(B, Right), FVector::DotProduct(B, Fwd)));
			Gun = FString::Printf(TEXT("枪管 偏航 %+.0f 俯仰 %+.0f（目标俯仰 %+.0f）"), GY, FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(B.Z, -1.f, 1.f))), Mesh->DebugShowTwist.Z);
		}
		else if (M->GetCombat() && M->GetCombat()->IsBladeInHand())
		{
			Gun = TEXT("拿刀");
		}
		UE_LOG(LogTemp, Display, TEXT("[results] 站姿（%s）%s %s%s：胸口 偏航 %+.1f 侧歪 %+.1f（转正前 %+.0f / %+.0f），胯 偏航 %+.1f，%s，%s"), When, *E.Row.Name,
			*MechLoadout::Describe(E.Row.Loadout).Left(14), E.bThumbs ? TEXT(" 竖拇指") : TEXT(""), CY, CR, Mesh->DebugShowTwist.X, Mesh->DebugShowTwist.Y, HY, *Gun,
			E.bFront ? TEXT("前排") : TEXT("后排"));
	}
}

void AMechResultsStage::PoseShotTick(float S)
{
	// 场景第 3 秒起，每台 1.2 秒：镜头摆到它正前方 8 米、略高，0.6 秒后（TSR 稳了）截一张；拍完退出
	constexpr float From = 3.f, Each = 1.2f;
	const int32 k = FMath::FloorToInt((S - From) / Each);
	if (S < From || !Cam)
	{
		return;
	}
	if (k >= Entries.Num())
	{
		if (QuitAt < 0.f)
		{
			QuitAt = S + 0.3f;
		}
		return;
	}
	const FEntry& E = Entries[k];
	const FVector Feet = ToWorld(E.Local + FVector(0.f, 0.f, E.Plinth));
	const FVector Facing = FRotator(0.f, E.Yaw, 0.f).Vector();
	const FVector Eye = Feet + Facing * 820.f + FVector(0.f, 0.f, 190.f);
	const FVector Look = Feet + FVector(0.f, 0.f, 150.f);
	Cam->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
	UCameraComponent* C = Cam->GetCameraComponent();
	C->SetFieldOfView(30.f);
	C->PostProcessSettings.DepthOfFieldFocalDistance = FVector::Dist(Eye, Look);
	if (k != PoseShot)
	{
		PoseShot = k;
		PoseShotAt = S;
		if (AMechPlayerController* PC = OwnerPC.Get())
		{
			if (PC->PlayerCameraManager)
			{
				PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			}
		}
	}
	else if (PoseShotAt >= 0.f && S - PoseShotAt >= 0.6f)
	{
		PoseShotAt = -1.f;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Results") / FString::Printf(TEXT("%s_pose_%02d.png"), *TestTag, k);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogTemp, Display, TEXT("[results] 站姿截图 %s：%s"), *Path, *E.Row.Name);
	}
}

// ---------------------------------------------------------------- 展示机体（一帧生成一台）
void AMechResultsStage::SpawnNextMech()
{
	if (!Entries.IsValidIndex(SpawnCursor))
	{
		return;
	}
	SetupMech(Entries[SpawnCursor++]);
}

void AMechResultsStage::SetupMech(FEntry& E)
{
	using namespace MechResultsPriv;
	UWorld* World = GetWorld();
	const float Half = GetDefault<AMechCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform At(FRotator(0.f, E.Yaw, 0.f), ToWorld(E.Local + FVector(0.f, 0.f, E.Plinth + Half + 1.f)));
	AMechCharacter* M = World->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!M)
	{
		return;
	}
	// 本机自己的展示品：不复制（房主生成的不会发给客户端）、不给 AI、不碰撞、不受重力
	M->SetReplicates(false);
	M->AutoPossessAI = EAutoPossessAI::Disabled;
	M->SetTeam(E.GlowTeam);
	M->DisplayName = E.Row.Name;
	M->FinishSpawning(At);
	M->SetActorEnableCollision(false);
	if (UCharacterMovementComponent* Move = M->GetCharacterMovement())
	{
		Move->DisableMovement();
		Move->SetComponentTickEnabled(false);
	}
	// 网格按框架取（以后各家企业的轻中重外壳从 MechCatalog::FrameMesh 换）
	if (USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, MechCatalog::FrameMesh(static_cast<int32>(E.Row.Loadout.Frame))))
	{
		if (M->GetMesh()->GetSkeletalMeshAsset() != Asset)
		{
			M->GetMesh()->SetSkeletalMeshAsset(Asset);
		}
	}
	M->SetLoadout(E.Row.Loadout, true);
	M->SetPaint(E.Row.Paint);
	M->SetTeam(E.GlowTeam);
	if (UMechCombatComponent* Combat = M->GetCombat())
	{
		if (E.bBlade)
		{
			Combat->SetStanceNow(EMechStance::Blade);
		}
		// 枪口压低：胜方低姿端着、败方往地上垂（摆姿势的瞄准方向，世界坐标）
		const FVector Fwd = FRotator(0.f, E.Yaw, 0.f).Vector();
		const float Down = FMath::DegreesToRadians(E.AimDown);
		Combat->AimOverride = Fwd * FMath::Cos(Down) - FVector::UpVector * FMath::Sin(Down);
	}
	M->SetResultsPose(E.bThumbs, E.Lean, E.AimDown);  // 10-31：上身转正；端着长枪的按 AimDown 低姿压枪口（以肩为轴转手臂，枪托留在肩窝）
	// 灯光通道：只吃场景的灯（通道 2）；机体自带的补光、推进器灯关掉
	TArray<UPrimitiveComponent*> Prims;
	M->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* P : Prims)
	{
		P->SetLightingChannels(Ch0, Ch1, Ch2);
	}
	TArray<ULightComponent*> Own;
	M->GetComponents<ULightComponent>(Own);
	for (ULightComponent* L : Own)
	{
		L->SetVisibility(false);
	}
	M->GetMesh()->PrestreamTextures(8.f, true);
	E.Mech = M;
}

// ---------------------------------------------------------------- 镜头
void AMechResultsStage::BuildCamera()
{
	// 场景坐标：镜头在 -X 一侧往 +X 看，+Y 在画面右边。全景的距离按整组的宽度和视野算（2v2 近、4v4 远）
	const float Fw = WideFov;
	const float D = WideDist;
	const FVector C = GroupCenter;
	const FVector M = MVPChest;
	Keys.Reset();
	// 0：低机位从左边横移推进（全景）
	Keys.Add({0.f, C + FVector(-D * 1.08f, -GroupHalfW * 0.95f, 70.f), C + FVector(80.f, -GroupHalfW * 0.25f, 150.f), Fw + 3.f});
	// 1：转到正面、往前推
	Keys.Add({3.f, C + FVector(-D * 0.84f, GroupHalfW * 0.3f, 135.f), FVector(60.f, (C.Y + M.Y) * 0.5f, 150.f), Fw});
	// 2：推到 MVP 左前方，低一点往上看（膝盖往上入画；重型框架比轻型高大半个头，别贴太近）
	Keys.Add({5.3f, M + FVector(-860.f, -300.f, -40.f), M + FVector(0.f, 0.f, -15.f), 30.f});
	// 3：绕着 MVP 转到右前方
	Keys.Add({7.5f, M + FVector(-930.f, 360.f, 10.f), M + FVector(30.f, 0.f, -25.f), 31.f});
	// 4：拉回正面略高的全景，停住
	Keys.Add({PathLen, C + FVector(-D * 0.98f, 0.f, 235.f), C + FVector(140.f, 0.f, 135.f), Fw});
}

void AMechResultsStage::UpdateCamera(float S, float DeltaSeconds)
{
	using namespace MechResultsPriv;
	if (!Cam || Keys.Num() == 0)
	{
		return;
	}
	TArray<FVector> Ps, Ls;
	TArray<float> Us;
	for (const FCamKey& K : Keys)
	{
		Ps.Add(K.Pos);
		Ls.Add(K.Look);
		Us.Add(K.T);
	}
	const float u = FMath::Clamp(S, 0.f, PathLen);
	CamPos = SplineAt(Ps, Us, u);
	CamLook = SplineAt(Ls, Us, u);
	int32 k = 0;
	while (k < Keys.Num() - 2 && u > Keys[k + 1].T)
	{
		++k;
	}
	const float Seg = FMath::Clamp((u - Keys[k].T) / FMath::Max(0.01f, Keys[k + 1].T - Keys[k].T), 0.f, 1.f);
	CamFov = FMath::Lerp(Keys[k].Fov, Keys[k + 1].Fov, FMath::SmoothStep(0.f, 1.f, Seg));
	// 停住以后轻轻漂一点（像转播的摇臂），从 0 慢慢起
	if (S > PathLen)
	{
		const float Amp = FMath::SmoothStep(0.f, 3.f, S - PathLen);
		CamPos += FVector(FMath::Sin(S * 0.21f) * 30.f, FMath::Sin(S * 0.13f + 1.f) * 60.f, FMath::Sin(S * 0.17f) * 12.f) * Amp;
	}
	// MVP 特写程度（铭牌让开、字幕出来）：按镜头离 MVP 多近
	CloseUp = FMath::Clamp(1.f - (FVector::Dist(CamPos, MVPChest) - 1000.f) / 380.f, 0.f, 1.f) * (S < PathLen ? 1.f : 0.f);
	// 结算表打开后：镜头往左偏、稍微退后，机体让到面板右边
	const float B = FMath::SmoothStep(0.f, 1.f, TableBlend);
	if (B > 0.001f)
	{
		const FVector Fwd = (CamLook - CamPos).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal();
		const float Dist = FVector::Dist(CamLook, CamPos);
		CamPos -= Fwd * Dist * 0.12f * B;
		CamLook -= Right * Dist * FMath::Tan(FMath::DegreesToRadians(CamFov * 0.5f)) * 0.4f * B;
	}
	const FVector P = ToWorld(CamPos);
	const FVector L = ToWorld(CamLook);
	Cam->SetActorLocationAndRotation(P, (L - P).Rotation());
	UCameraComponent* C = Cam->GetCameraComponent();
	C->SetFieldOfView(CamFov);
	FPostProcessSettings& PP = C->PostProcessSettings;
	PP.AutoExposureBias = Exposure;
	PP.DepthOfFieldFocalDistance = FVector::Dist(P, L);
	PP.DepthOfFieldFstop = FMath::Max(0.7f, Fstop);
	// 控制台变量改了亮度：灯和光柱跟着改（没改就不动，免得每帧刷渲染状态）
	static float LastLight = -1.f, LastBeam = -1.f;
	if (LastLight != LightScale)
	{
		LastLight = LightScale;
		LastSpotKey = -1.f;  // MVP 聚光 / 光柱按打亮动画重新设（UpdateSpot 在这之后）
		for (int32 i = 0; i < Lights.Num(); ++i)
		{
			if (Lights[i])
			{
				Lights[i]->SetIntensity(LightBase[i] * LightScale);
			}
		}
	}
	if (LastBeam != BeamScale)
	{
		LastBeam = BeamScale;
		LastSpotKey = -1.f;
		for (const auto& BM : BeamMIDs)
		{
			if (BM.Key)
			{
				BM.Key->SetScalarParameterValue(TEXT("Strength"), BM.Value * BeamScale);
			}
		}
	}
}

void AMechResultsStage::Cut()
{
	using namespace MechResultsPriv;
	AMechPlayerController* PC = OwnerPC.Get();
	bCut = true;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.ObjectFlags |= RF_Transient;
	Cam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(MechResultsPriv::StageOrigin), P);
	if (Cam)
	{
		Cam->SetReplicates(false);
		UCameraComponent* C = Cam->GetCameraComponent();
		C->SetConstraintAspectRatio(false);
		C->PostProcessBlendWeight = 1.f;
		FPostProcessSettings& PP = C->PostProcessSettings;
		// 固定曝光（暗场不自动调亮）、关地图后期里的调色 / 局部曝光 / 色散 / 动态模糊，轻微景深、暗角、少量颗粒
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = Exposure;
		PP.bOverride_LocalExposureHighlightContrastScale = true;
		PP.LocalExposureHighlightContrastScale = 1.f;
		PP.bOverride_LocalExposureShadowContrastScale = true;
		PP.LocalExposureShadowContrastScale = 1.f;
		PP.bOverride_ColorGradingIntensity = true;
		PP.ColorGradingIntensity = 0.f;
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.bOverride_ColorGamma = true;
		PP.ColorGamma = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.bOverride_ColorGain = true;
		PP.ColorGain = FVector4(1.f, 1.f, 1.f, 1.f);
		PP.bOverride_ColorOffset = true;
		PP.ColorOffset = FVector4(0.f, 0.f, 0.f, 0.f);
		PP.bOverride_SceneColorTint = true;
		PP.SceneColorTint = FLinearColor::White;
		PP.bOverride_WhiteTemp = true;
		PP.WhiteTemp = 6500.f;
		PP.bOverride_SceneFringeIntensity = true;
		PP.SceneFringeIntensity = 0.f;
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.f;
		PP.bOverride_LumenSkylightLeaking = true;
		PP.LumenSkylightLeaking = 0.f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.55f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.5f;
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.06f;
		PP.bOverride_LensFlareIntensity = true;
		PP.LensFlareIntensity = 0.f;
		PP.bOverride_DepthOfFieldFocalDistance = true;
		PP.bOverride_DepthOfFieldFstop = true;
		PP.bOverride_DepthOfFieldMinFstop = true;
		PP.DepthOfFieldMinFstop = 0.7f;
		PP.bOverride_DepthOfFieldSensorWidth = true;
		PP.DepthOfFieldSensorWidth = 24.576f;
	}
	if (PC)
	{
		if (AHUD* Hud = PC->GetHUD())
		{
			bPrevShowHUD = Hud->bShowHUD;
			Hud->bShowHUD = false;
		}
	}
	// 本机视口：地图的雾 / 天光 / 大气 / 云 / 平行光 / 地形 / 植被都关掉（场景在地下 3.8 公里，雾按高度算会浓成一片白；地图也不用白渲）
	if (GEngine && GEngine->GameViewport)
	{
		FEngineShowFlags& F = GEngine->GameViewport->EngineShowFlags;
		SavedFlags.bFog = F.Fog;
		SavedFlags.bVolFog = F.VolumetricFog;
		SavedFlags.bSky = F.SkyLighting;
		SavedFlags.bAtmo = F.Atmosphere;
		SavedFlags.bCloud = F.Cloud;
		SavedFlags.bDirectional = F.DirectionalLights;
		SavedFlags.bLandscape = F.Landscape;
		SavedFlags.bFoliage = F.InstancedFoliage;
		SavedFlags.bGrass = F.InstancedGrass;
		bFlagsSaved = true;
		F.SetFog(false);
		F.SetVolumetricFog(false);
		F.SetSkyLighting(false);
		F.SetAtmosphere(false);
		F.SetCloud(false);
		F.SetDirectionalLights(false);
		F.SetLandscape(false);
		F.SetInstancedFoliage(false);
		F.SetInstancedGrass(false);
	}
	const float S = SinceEnd() - PreRoll;
	StageClock = FMath::Max(0.f, S);
	UpdateCamera(StageClock, 0.f);
	if (PC && Cam)
	{
		PC->SetViewTarget(Cam);
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[results] 切到结算场景（比赛结束后 %.2f 秒），%d 台展示机体"), SinceEnd(), Entries.Num());
}

void AMechResultsStage::OpenTable()
{
	bTableOpen = true;
	if (AMechPlayerController* PC = OwnerPC.Get())
	{
		PC->OpenMenu(EMechMenuPage::Results);
	}
	UE_LOG(LogTemp, Display, TEXT("[results] 打开结算表（场景第 %.2f 秒%s）"), StageClock, bSkipped ? TEXT("，按键跳过") : TEXT(""));
	if (FrameMs.Num() > 10)
	{
		TArray<float> Sorted = FrameMs;
		Sorted.Sort();
		float Sum = 0.f;
		for (const float Ms : Sorted)
		{
			Sum += Ms;
		}
		UE_LOG(LogTemp, Display, TEXT("[results] 场景镜头期间帧时间（真实时间，%d 帧，截图帧不算）：平均 %.1f 毫秒（%.0f 帧），中位 %.1f，95%% %.1f，最慢 %.1f"), Sorted.Num(),
			Sum / Sorted.Num(), 1000.f * Sorted.Num() / FMath::Max(1.f, Sum), Sorted[Sorted.Num() / 2], Sorted[FMath::Min(Sorted.Num() - 1, Sorted.Num() * 95 / 100)],
			Sorted.Last());
	}
}

void AMechResultsStage::Finish()
{
	using namespace MechResultsPriv;
	// 10-31：配乐 / 底噪淡出（声音组件不挂在场景上，场景收掉以后自己淡完、自己销毁）
	FadeOutAudio(0.8f);
	StopRecording();
	if (Input.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Input);
	}
	Input.Reset();
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	if (bFlagsSaved && GEngine && GEngine->GameViewport)
	{
		FEngineShowFlags& F = GEngine->GameViewport->EngineShowFlags;
		F.SetFog(SavedFlags.bFog);
		F.SetVolumetricFog(SavedFlags.bVolFog);
		F.SetSkyLighting(SavedFlags.bSky);
		F.SetAtmosphere(SavedFlags.bAtmo);
		F.SetCloud(SavedFlags.bCloud);
		F.SetDirectionalLights(SavedFlags.bDirectional);
		F.SetLandscape(SavedFlags.bLandscape);
		F.SetInstancedFoliage(SavedFlags.bFoliage);
		F.SetInstancedGrass(SavedFlags.bGrass);
		bFlagsSaved = false;
	}
	AMechPlayerController* PC = OwnerPC.Get();
	if (PC && bCut)
	{
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = bPrevShowHUD;
		}
		if (Cam && PC->GetViewTarget() == Cam)
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PC->SetViewTarget(Pawn);
			}
			else
			{
				PC->SetViewTarget(PC);
			}
		}
	}
	for (FEntry& E : Entries)
	{
		if (AMechCharacter* M = E.Mech.Get())
		{
			M->Destroy();
		}
	}
	if (Cam)
	{
		Cam->Destroy();
		Cam = nullptr;
	}
	if (GActive.Get() == this)
	{
		GActive.Reset();
	}
	bCut = false;
}

void AMechResultsStage::EndPlay(const EEndPlayReason::Type Reason)
{
	Finish();
	UE_LOG(LogTemp, Display, TEXT("[results] 结算场景结束"));
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------- 每帧
void AMechResultsStage::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AMechPlayerController* PC = OwnerPC.Get();
	AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!PC || !GS)
	{
		Destroy();
		return;
	}
	if (!bTest)
	{
		// 下一局开始 / 回房间：收掉
		if (GS->Phase != EMechRoundPhase::MatchOver)
		{
			Destroy();
			return;
		}
		if (GS->MatchEndTime >= 0.f && !bEndFromServer)
		{
			EndTime = GS->MatchEndTime;  // 服务器的时间到了：对齐
			bEndFromServer = true;
		}
	}
	const float T = SinceEnd();
	if (T >= SpawnAt && Entries.Num() == 0 && Keys.Num() == 0)
	{
		BuildEntries(GS);
		if (Entries.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[results] 没有结算数据，直接开结算表"));
			OpenTable();
			bTableOpen = true;
			Destroy();
			return;
		}
		BuildStage();
		BuildCamera();
	}
	if (Keys.Num() > 0 && SpawnCursor < Entries.Num())
	{
		SpawnNextMech();
		// 晚到的（已经过了切换时间）：一次全部生成
		while (T >= PreRoll && SpawnCursor < Entries.Num())
		{
			SpawnNextMech();
		}
	}
	if (!bCut && Keys.Num() > 0 && T >= PreRoll)
	{
		Cut();
	}
	if (!bCut)
	{
		return;
	}
	// 跳过：镜头直接到最后的全景（硬切），马上开结算表
	float S = T - PreRoll;
	if (bSkipped && SkipS < 0.f)
	{
		SkipS = FMath::Max(0.f, PathLen - S);
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
	S += FMath::Max(0.f, SkipS);
	StageClock = S;
	if (!bTableOpen && (S >= TableAt || bSkipped) && !bPoseShots)
	{
		OpenTable();
	}
	if (bTableOpen)
	{
		TableBlend = FMath::Min(1.f, TableBlend + DeltaSeconds / 0.9f);
	}
	// 后来才挂上的部件（无人机机身、推进口）也换到场景的灯光通道
	if (S < 4.f && GFrameCounter % 8 == 0)
	{
		for (FEntry& E : Entries)
		{
			if (AMechCharacter* M = E.Mech.Get())
			{
				TArray<UPrimitiveComponent*> Prims;
				M->GetComponents<UPrimitiveComponent>(Prims);
				for (UPrimitiveComponent* P : Prims)
				{
					if (P->LightingChannels.bChannel0 || !P->LightingChannels.bChannel2)
					{
						P->SetLightingChannels(MechResultsPriv::Ch0, MechResultsPriv::Ch1, MechResultsPriv::Ch2);
					}
				}
			}
		}
	}
	UpdateCamera(S, DeltaSeconds);
	UpdateSpot(S);
	UpdateAudio(S);
	if (Cam && PC->GetViewTarget() != Cam)
	{
		PC->SetViewTarget(Cam);  // 客户端收到 / 换了机体时引擎会把视角切回去，这里每帧切回来
	}
	if (!bTableOpen && S > 0.3f && GFrameCounter > SkipFramesUntil)
	{
		FrameMs.Add(FApp::GetDeltaTime() * 1000.f);
	}
	// 10-31 测试：第 4.4 秒（全景转正面、MVP 还没特写）记一遍每台的站姿；-MechResultsPoseShots 挨个拍正面
	if ((bTest || bShots) && !bPoseLogged && S >= 4.4f)
	{
		bPoseLogged = true;
		LogPoses(TEXT("第 4.4 秒"));
	}
	if (bPoseShots)
	{
		PoseShotTick(S);
	}
	if (bShots)
	{
		TestTick(S);
	}
}

void AMechResultsStage::TestTick(float S)
{
	if (ShotTimes.IsValidIndex(NextShot) && S >= ShotTimes[NextShot])
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Results") / FString::Printf(TEXT("%s_%02d.png"), *TestTag, NextShot);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		SkipFramesUntil = GFrameCounter + 3;  // 截图那一帧本身卡一下，不算进帧时间统计
		UE_LOG(LogTemp, Display, TEXT("[results] 截图 %s（场景第 %.2f 秒，镜头 %s → %s，视野 %.1f）"), *Path, S, *CamPos.ToString(), *CamLook.ToString(), CamFov);
		++NextShot;
		// 测试 / 联机测试的客户端（-MechResultsShots）截完就退出；自动对战的主机由 GameMode 到点退出
		if (!ShotTimes.IsValidIndex(NextShot) && (bTest || FParse::Param(FCommandLine::Get(), TEXT("MechResultsShots"))))
		{
			QuitAt = S + (bRecording ? 4.5f : 0.8f);  // 录音：多录 2.5 秒底噪，停了再等 2 秒（wav 在后台线程写）
		}
	}
	if (bRecording && QuitAt > 0.f && QuitAt < 1e8f && S >= QuitAt - 2.f)
	{
		StopRecording();
	}
	if (QuitAt > 0.f && S >= QuitAt)
	{
		QuitAt = 1e9f;
		UE_LOG(LogTemp, Display, TEXT("[results] 测试结束，退出"));
		if (AMechPlayerController* PC = OwnerPC.Get())
		{
			PC->ConsoleCommand(TEXT("quit"));
		}
	}
}

// ================================================================ 测试：假数据（-MechResultsTest=Elim|DMTeam|DMSolo|Core）
bool AMechResultsStage::SetupTest(AGameModeBase* Mode)
{
	FString Which;
	if (!Mode || !FParse::Value(FCommandLine::Get(), TEXT("MechResultsTest="), Which))
	{
		return false;
	}
	AMechGameState* GS = Mode->GetGameState<AMechGameState>();
	if (!GS)
	{
		return false;
	}
	FRandomStream Rand(20261031 + Which.Len());
	int32 Size = 3;
	FParse::Value(FCommandLine::Get(), TEXT("MechTeam="), Size);
	Size = FMath::Clamp(Size, 1, 4);
	const bool bIsSolo = Which.Equals(TEXT("DMSolo"), ESearchCase::IgnoreCase);
	const bool bIsDM = bIsSolo || Which.Equals(TEXT("DMTeam"), ESearchCase::IgnoreCase);
	const bool bIsCore = Which.Equals(TEXT("Core"), ESearchCase::IgnoreCase);
	const bool bIsDraw = FParse::Param(FCommandLine::Get(), TEXT("MechResultsDraw"));
	GS->Rules = FMechRules();
	GS->Rules.Mode = bIsDM ? EMechMode::Deathmatch : EMechMode::Elimination;
	GS->Rules.bSolo = bIsSolo;
	GS->bCoreMode = bIsCore;
	GS->TeamSize = Size;
	GS->TeamCount[0] = GS->TeamCount[1] = Size;
	static const TCHAR* Names[] = {TEXT("你"), TEXT("白鹭"), TEXT("铁锈"), TEXT("零号"), TEXT("雾港"), TEXT("灰狼"), TEXT("青柠"), TEXT("砂砾")};
	static const FColor Paints[][3] = {
		{FColor(0x5a, 0x63, 0x6c), FColor(0x38, 0x3f, 0x46), FColor(0xcf, 0x5f, 0x1b)},
		{FColor(0xd6, 0xd8, 0xd4), FColor(0x4a, 0x50, 0x58), FColor(0x2a, 0xb0, 0xd8)},
		{FColor(0x5b, 0x60, 0x48), FColor(0x30, 0x33, 0x28), FColor(0xd0, 0x3a, 0x2a)},
		{FColor(0x26, 0x29, 0x2e), FColor(0x15, 0x17, 0x1a), FColor(0xe0, 0xb0, 0x30)},
		{FColor(0xa8, 0x96, 0x74), FColor(0x5a, 0x50, 0x40), FColor(0x30, 0x80, 0xc0)},
		{FColor(0x48, 0x5e, 0x78), FColor(0x28, 0x30, 0x3c), FColor(0xf0, 0x70, 0x20)},
		{FColor(0x8a, 0x2e, 0x2a), FColor(0x2e, 0x2a, 0x2a), FColor(0xe8, 0xe0, 0xd0)},
		{FColor(0x3e, 0x6e, 0x6a), FColor(0x22, 0x30, 0x30), FColor(0xf0, 0xc0, 0x40)},
	};
	auto Pick = [&Rand](int32 Category) {
		for (int32 Try = 0; Try < 12; ++Try)
		{
			const int32 i = Rand.RandRange(0, MechLoadout::NumItems(Category) - 1);
			if (MechLoadout::Item(Category, i).bReady)
			{
				return i;
			}
		}
		return 0;
	};
	const int32 N = bIsSolo ? FMath::Clamp(Size * 2, 2, 8) : Size * 2;
	TArray<FMechResultRow> Rows;
	for (int32 i = 0; i < N; ++i)
	{
		FMechResultRow& R = Rows.AddDefaulted_GetRef();
		// 团队：偶数蓝、奇数红；「你」= 第 0 个（蓝队）
		R.Team = bIsSolo ? 2 + i : i % 2;
		R.Name = Names[i % UE_ARRAY_COUNT(Names)];
		R.bHuman = i == 0;
		for (int32 c = 0; c < MechLoadout::NumCategories; ++c)
		{
			MechLoadout::Set(R.Loadout, c, Pick(c));
		}
		const int32 P = i == 0 ? 0 : 1 + (i - 1) % (UE_ARRAY_COUNT(Paints) - 1);
		R.Paint.Primary = Paints[P][0];
		R.Paint.Secondary = Paints[P][1];
		R.Paint.Accent = Paints[P][2];
		if (i == 0)
		{
			R.Paint = FMechSettings::Get().Paint;
		}
	}
	// 胜方：团队淘汰 / 夺芯 = 蓝队（「你」赢）；团队死斗 = 红队（看「败北」那一版）；个人死斗「你」第二
	const int32 Win = bIsDraw ? -1 : (bIsDM && !bIsSolo) ? 1 : 0;
	for (int32 i = 0; i < N; ++i)
	{
		FMechResultRow& R = Rows[i];
		const bool bWin = !bIsSolo && R.Team == Win;
		if (bIsSolo)
		{
			static const int32 SoloKills[] = {11, 15, 9, 7, 5, 4, 3, 1};
			R.Kills = SoloKills[i];
			R.Deaths = FMath::Max(1, 13 - R.Kills + Rand.RandRange(-2, 2));
		}
		else if (bIsDM)
		{
			R.Kills = (bWin ? 7 : 4) + Rand.RandRange(0, 6);
			R.Deaths = (bWin ? 5 : 8) + Rand.RandRange(0, 4);
		}
		else
		{
			R.Kills = (bWin ? 2 : 1) + Rand.RandRange(0, 3);
			R.Deaths = (bWin ? 1 : 2) + Rand.RandRange(0, 1);
		}
		R.Dealt = R.Kills * 9800 + Rand.RandRange(2000, 14000);
		R.Taken = R.Deaths * 9500 + Rand.RandRange(1000, 8000);
		R.Bullets = Rand.RandRange(40, 260);
		R.Missiles = Rand.RandRange(0, 30);
		R.Blades = (i % 3 == 2) ? R.Bullets + Rand.RandRange(10, 60) : Rand.RandRange(0, 30);  // 每三台里一台主要用刀（拿刀站）
		R.Cores = bIsCore ? (bWin ? Rand.RandRange(1, 3) : Rand.RandRange(0, 1)) : 0;
		// 11-01 积分制：助攻 / 失衡 / 控制 / 救人也给一些；团队第 3 台「铁锈」、个人死斗第 4 名「零号」是助攻型（击毁不多、助攻和输出多），
		// 测「MVP 不是击毁最多的人」（个人死斗里他站到领奖台左边地上）
		const bool bHelper = bIsSolo ? i == 3 : i == 2;
		R.Assists = Rand.RandRange(0, 4) + (bHelper ? (bIsSolo ? 18 : 7) : 0);
		R.Breaks = Rand.RandRange(0, 2) + (bHelper ? 4 : 0);
		R.Controls = Rand.RandRange(0, 3) + (bHelper ? 5 : 0);
		R.Rescues = bHelper ? 2 : 0;
		R.Dealt += bHelper ? (bIsSolo ? 60000 : 25000) : 0;
		MechScore::FromCounts(R);
	}
	GS->Results = Rows;
	GS->MatchWinner = bIsSolo ? Rows[1].Team : Win;
	GS->RoundWinner = GS->MatchWinner;
	GS->WinnerName = bIsSolo ? Rows[1].Name : FString();
	int32 Score[2] = {0, 0};
	for (const FMechResultRow& R : Rows)
	{
		if (R.Team == 0 || R.Team == 1)
		{
			Score[R.Team] += bIsCore ? R.Cores : R.Kills;
		}
	}
	if (bIsCore)
	{
		GS->CoreTarget = 5;
		Score[Win < 0 ? 0 : Win] = 5;
	}
	else if (!bIsDM)
	{
		Score[0] = Win == 1 ? 1 : 2;
		Score[1] = Win == 1 ? 2 : 1;
		if (Win < 0)
		{
			Score[0] = Score[1] = 1;
		}
	}
	else if (!bIsSolo && Win >= 0)
	{
		Score[Win] = GS->Rules.KillTarget();
	}
	GS->TeamScore[0] = Score[0];
	GS->TeamScore[1] = Score[1];
	UE_LOG(LogTemp, Display, TEXT("[results] 测试：%s，%d 台，胜方 %d，比分 %d : %d"), *Which, N, GS->MatchWinner, Score[0], Score[1]);
	// 本机控制器准备好以后起场景（只截图，不按键跳过）
	FTimerHandle H;
	const FString Tag = Which;
	Mode->GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Mode, [Mode, Tag]() {
		AMechPlayerController* PC = Cast<AMechPlayerController>(Mode->GetWorld()->GetFirstPlayerController());
		if (PC)
		{
			Start(PC, true, Tag);
		}
	}), 1.2f, false);
	return true;
}
