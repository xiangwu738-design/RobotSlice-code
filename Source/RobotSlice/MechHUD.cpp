#include "MechHUD.h"
#include "MechFeed.h"
#include "MechLoop.h"

#include "Engine/GameViewportClient.h"
#include "MechHudOverlay.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "MechFonts.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechCoreArena.h"
#include "MechCoreDirector.h"
#include "MechEffectComponent.h"
#include "MechPluginPod.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechHudAlerts.h"
#include "MechHudTheme.h"
#include "MechLoadout.h"
#include "MechMovementComponent.h"
#include "MechWeapons.h"
#include "MechShoulderComponent.h"
#include "MechTacticalComponent.h"
#include "MechMeshComponent.h"
#include "MechSettings.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 一套色：主色 = 自己的状态，提醒色 = 装填、过热、低弹、失衡，红 = 敌人，蓝 = 队友，金 = 核心。
	// 10-31 起是当前 HUD 主题（MechHudTheme.h）成员的引用：换主题时这些名字跟着变，下面的画法一行不用改
	const FLinearColor& Ink = MechHudTheme::Get().Ink;
	const FLinearColor& Dim = MechHudTheme::Get().Dim;
	const FLinearColor& Faint = MechHudTheme::Get().Faint;
	const FLinearColor& Accent = MechHudTheme::Get().Accent;
	const FLinearColor& Track = MechHudTheme::Get().Track;
	const FLinearColor& Ally = MechHudTheme::Get().Ally;
	const FLinearColor& Enemy = MechHudTheme::Get().Enemy;
	const FLinearColor& Warn = MechHudTheme::Get().Warn;
	const FLinearColor& Heal = MechHudTheme::Get().Heal;
	const FLinearColor& Gold = MechHudTheme::Get().Gold;
	const FLinearColor& PlateColor = MechHudTheme::Get().Plate;

	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}
}

AMechHUD::AMechHUD()
{
	static ConstructorHelpers::FObjectFinder<USoundBase> Lock(TEXT("/Game/Audio/Combat/S_lock_beep"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Start(TEXT("/Game/Audio/Combat/S_round_start"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Win(TEXT("/Game/Audio/Combat/S_round_win"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Lose(TEXT("/Game/Audio/Combat/S_round_lose"));
	LockSound = Lock.Object;
	RoundStartSound = Start.Object;
	WinSound = Win.Object;
	LoseSound = Lose.Object;
	Alerts = CreateDefaultSubobject<UMechHudAlerts>(TEXT("Alerts"));
}

void AMechHUD::BeginPlay()
{
	Super::BeginPlay();
	HudFrame = MakeShared<FMechHudFrame>();
	if (GEngine && GEngine->GameViewport && PlayerOwner && PlayerOwner->IsLocalController())
	{
		HudOverlay = SNew(SMechHudOverlay, HudFrame.ToSharedRef());
		GEngine->GameViewport->AddViewportWidgetContent(HudOverlay.ToSharedRef(), 10);  // 菜单是 50，盖在 HUD 上面
	}
}

void AMechHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HudOverlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudOverlay.ToSharedRef());
	}
	HudOverlay.Reset();
	Super::EndPlay(EndPlayReason);
}

AMechCharacter* AMechHUD::GetFocusMech() const
{
	if (AMechCharacter* Own = Cast<AMechCharacter>(GetOwningPawn()))
	{
		return Own;
	}
	return PlayerOwner ? Cast<AMechCharacter>(PlayerOwner->GetViewTarget()) : nullptr;
}

// ---------------------------------------------------------------- 小工具
FSlateFontInfo AMechHUD::HudFont(float Size) const
{
	// 字体跟主题走（10-31）：小字（标签、名字、距离）和大字（读数、比分）各一种
	const FMechHudTheme& Th = MechHudTheme::Get();
	const EMechHudFace Face = Size >= 20.f ? Th.NumberFace : Th.LabelFace;
	if (Face != EMechHudFace::Bahnschrift)
	{
		return MechFonts::GetHudFace(static_cast<int32>(Face), Size * UI * 0.66f);
	}
	return MechFonts::GetCanvas(FMath::Max(6.f, FMath::RoundToFloat(Size * UI * 0.66f)), Size >= 30.f);
}

void AMechHUD::Text(const FString& S, float X, float Y, float Size, const FLinearColor& Color, float AlignX)
{
	const float W = AlignX != 0.f ? Measure(S, Size).X : 0.f;
	const FLinearColor Shown(Color.R, Color.G, Color.B, Color.A * HudAlpha);
	if (Shown.A <= 0.f)
	{
		return;
	}
	// 按目标像素大小直接栅格化（UFont 加缩放是把小字形贴图放大，汉字会糊）；压一层淡影子，亮背景（霓虹、爆炸）上也看得清
	const FSlateFontInfo Font = HudFont(Size);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		FMechHudFrame::FItem& It = HudFrame->Items.AddDefaulted_GetRef();
		It.Kind = FMechHudFrame::EKind::Text;
		It.Pos = FVector2f(X - W * AlignX + (Pass == 0 ? 1.f : 0.f), Y + (Pass == 0 ? 1.f : 0.f));
		It.Color = Pass == 0 ? FLinearColor(0.f, 0.f, 0.f, Shown.A * 0.6f) : Shown;
		It.Text = S;
		It.Font = Font;
	}
}

FVector2D AMechHUD::Measure(const FString& S, float Size)
{
	const TSharedPtr<FSlateFontMeasure> FontMeasure = FEngineFontServices::Get().GetFontMeasure();
	return FontMeasure.IsValid() ? FVector2D(FontMeasure->Measure(S, HudFont(Size))) : FVector2D::ZeroVector;
}

void AMechHUD::DrawRect(FLinearColor Color, float X, float Y, float W, float H)
{
	Color.A *= HudAlpha;
	if (W <= 0.f || H <= 0.f || Color.A <= 0.f)
	{
		return;
	}
	FMechHudFrame::FItem& It = HudFrame->Items.AddDefaulted_GetRef();
	It.Kind = FMechHudFrame::EKind::Rect;
	It.Pos = FVector2f(X, Y);
	It.Size = FVector2f(W, H);
	It.Color = Color;
}

void AMechHUD::DrawLine(float X0, float Y0, float X1, float Y1, FLinearColor Color, float Thick)
{
	Polyline({FVector2f(X0, Y0), FVector2f(X1, Y1)}, Color, Thick);
}

void AMechHUD::Polyline(const TArray<FVector2f>& Points, const FLinearColor& Color, float Thick)
{
	if (Points.Num() < 2 || Color.A * HudAlpha <= 0.f)
	{
		return;
	}
	FMechHudFrame::FItem& It = HudFrame->Items.AddDefaulted_GetRef();
	It.Kind = FMechHudFrame::EKind::Lines;
	It.First = HudFrame->Points.Num();
	It.Count = Points.Num();
	It.Color = FLinearColor(Color.R, Color.G, Color.B, Color.A * HudAlpha);
	// Slate 抗锯齿线的过渡是往线宽里收 1 像素（两边共 2 像素），不补的话细线中间没有实色、又细又淡；线宽再乘主题的倍数（新纪粗、相位细）
	It.Thick = FMath::Max(Thick * MechHudTheme::Get().LineScale, 1.f) + 1.6f;
	HudFrame->Points.Append(Points);
}

void AMechHUD::AddTris(const TArray<FCanvasUVTri>& Tris)
{
	if (Tris.IsEmpty())
	{
		return;
	}
	FMechHudFrame::FItem& It = HudFrame->Items.AddDefaulted_GetRef();
	It.Kind = FMechHudFrame::EKind::Tris;
	It.First = HudFrame->TriPos.Num();
	It.Count = Tris.Num() * 3;
	for (const FCanvasUVTri& T : Tris)
	{
		HudFrame->TriPos.Append({FVector2f(T.V0_Pos), FVector2f(T.V1_Pos), FVector2f(T.V2_Pos)});
		auto Fade3 = [this](FLinearColor C) { C.A *= FMath::Clamp(HudAlpha, 0.f, 1.f); return C; };
		HudFrame->TriColor.Append({Fade3(T.V0_Color), Fade3(T.V1_Color), Fade3(T.V2_Color)});
	}
}

void AMechHUD::Panel(float X, float Y, float W, float H, float Alpha)
{
	DrawRect(Fade(PlateColor, Alpha), X, Y, W, H);
}

void AMechHUD::Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, const FLinearColor& Back)
{
	DrawRect(Back, X - 2.f * UI, Y - 2.f * UI, W + 4.f * UI, H + 4.f * UI);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Frac, 0.f, 1.f), H);
}

void AMechHUD::SegBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, int32 Segments)
{
	DrawRect(FLinearColor(0.02f, 0.03f, 0.04f, 0.65f), X, Y, W, H);
	DrawRect(Fade(Fill, 0.18f), X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Frac, 0.f, 1.f), H);
	const FLinearColor Gap(0.f, 0.f, 0.f, 0.7f);
	for (int32 i = 1; i < Segments; ++i)
	{
		DrawRect(Gap, X + W * i / Segments - 1.f * UI, Y, 2.f * UI, H);
	}
}

void AMechHUD::Box(float X, float Y, float W, float H, const FLinearColor& Color, float Thick)
{
	DrawLine(X, Y, X + W, Y, Color, Thick);
	DrawLine(X + W, Y, X + W, Y + H, Color, Thick);
	DrawLine(X + W, Y + H, X, Y + H, Color, Thick);
	DrawLine(X, Y + H, X, Y, Color, Thick);
}

void AMechHUD::Brackets(float CX, float CY, float Half, float Len, const FLinearColor& Color, float Thick)
{
	for (int32 sx = -1; sx <= 1; sx += 2)
	{
		for (int32 sy = -1; sy <= 1; sy += 2)
		{
			const float X = CX + sx * Half;
			const float Y = CY + sy * Half;
			DrawLine(X, Y, X - sx * Len, Y, Color, Thick);
			DrawLine(X, Y, X, Y - sy * Len, Color, Thick);
		}
	}
}

void AMechHUD::Arc(float CX, float CY, float R, float From, float To, const FLinearColor& Color, float Thick)
{
	// 一串折线（每段约 4 像素长），Slate 画的时候抗锯齿
	const int32 N = FMath::Clamp(FMath::CeilToInt(FMath::Abs(To - From) * R / 4.f), 6, 1000);
	TArray<FVector2f> Pts;
	Pts.Reserve(N + 1);
	for (int32 i = 0; i <= N; ++i)
	{
		const float A = FMath::Lerp(From, To, float(i) / N);
		Pts.Add(FVector2f(CX + R * FMath::Sin(A), CY - R * FMath::Cos(A)));
	}
	Polyline(Pts, Color, Thick);
}

void AMechHUD::SoftLine(float X0, float Y0, float X1, float Y1, const FLinearColor& Color, float Thick)
{
	DrawLine(X0, Y0, X1, Y1, Color, Thick);  // Slate 画层的线本来就抗锯齿
}

void AMechHUD::Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	FCanvasUVTri T;
	T.V0_Pos = A, T.V1_Pos = B, T.V2_Pos = C;
	T.V0_Color = T.V1_Color = T.V2_Color = Color;
	AddTris({T});
	// 三角形本身边不抗锯齿：沿边再描一圈抗锯齿的细线
	Polyline({FVector2f(A), FVector2f(B), FVector2f(C), FVector2f(A)}, Color, 1.f);
}

// ---------------------------------------------------------------- 主题（10-31）
void AMechHUD::UpdateTheme(AMechCharacter* Mech)
{
	const bool bOwn = Mech && Mech == GetOwningPawn();
	const EMechHudThemeId Want = MechHudTheme::Resolve(Mech, bOwn);
	MechHudTheme::Use(Want);
	if (ThemeShown != static_cast<int32>(Want))
	{
		UE_LOG(LogTemp, Display, TEXT("[hudtheme] HUD 主题 → %s（%s）"), MechHudTheme::Code(Want),
			!Mech ? TEXT("没有机体") : bOwn ? TEXT("自己操控的机体") : TEXT("观战"));
		ThemeShown = static_cast<int32>(Want);
	}
}

bool AMechHUD::CanSegment(const FString& S)
{
	if (S.IsEmpty())
	{
		return false;
	}
	for (const TCHAR Ch : S)
	{
		if (!((Ch >= TEXT('0') && Ch <= TEXT('9')) || Ch == TEXT('-') || Ch == TEXT(':') || Ch == TEXT('.') || Ch == TEXT('/') || Ch == TEXT('%') || Ch == TEXT(' ')))
		{
			return false;
		}
	}
	return true;
}

FString AMechHUD::Pad(int32 Value, int32 Width) const
{
	return MechHudTheme::Get().bPadDigits && Value >= 0 ? FString::Printf(TEXT("%0*d"), Width, Value) : FString::FromInt(Value);
}

namespace MechHudSeg  // 七段数码管：段位 a 上 b 右上 c 右下 d 下 e 左下 f 左上 g 中
{
	constexpr uint8 Digit[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
	/** 字形高度 / 宽度 / 段粗 / 段间缝 / 字距（都按字号 × UI 的比例），和 Bahnschrift 同字号的数字差不多高、略宽 */
	constexpr float HeightK = 0.72f;
	constexpr float WidthK = 0.62f;
	constexpr float ThickK = 0.17f;
	constexpr float GapK = 0.04f;
	constexpr float AdvanceK = 0.24f;
	/** 文字框上沿到数字顶的距离（和 Text 对齐用） */
	constexpr float TopK = 0.16f;

	float CellWidth(TCHAR Ch, float Hd)
	{
		const float W = Hd * WidthK;
		switch (Ch)
		{
		case TEXT(':'):
		case TEXT('.'):
			return Hd * ThickK * 1.6f;
		case TEXT('/'):
			return W * 0.7f;
		case TEXT(' '):
			return W * 0.45f;
		case TEXT('%'):
			return W * 0.9f;
		default:
			return W;
		}
	}
}

FVector2D AMechHUD::MeasureSeg(const FString& S, float Size)
{
	const float Hd = Size * UI * MechHudSeg::HeightK;
	float W = 0.f;
	for (int32 i = 0; i < S.Len(); ++i)
	{
		W += MechHudSeg::CellWidth(S[i], Hd) + (i + 1 < S.Len() ? Hd * MechHudSeg::AdvanceK : 0.f);
	}
	// 高度照文字框给（各处按 Measure().Y 竖直居中的代码不用改）
	return FVector2D(W, Measure(TEXT("0"), Size).Y);
}

void AMechHUD::SegDigits(const FString& S, float X, float Y, float Size, const FLinearColor& Color)
{
	using namespace MechHudSeg;
	const float Hd = Size * UI * HeightK;
	const float T = FMath::Max(2.f, Hd * ThickK);
	const float G = FMath::Max(1.f, Hd * GapK);
	const float Top = Y + Size * UI * TopK;
	const float Mid = Top + Hd * 0.5f;
	const FLinearColor Ghost = Fade(Color, MechHudTheme::Get().GhostAlpha);
	float CX = X;
	// 先投一层淡影子（亮背景上也看得清），再画字
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const float O = Pass == 0 ? 1.f : 0.f;
		const FLinearColor Shadow(0.f, 0.f, 0.f, Color.A * 0.45f);
		CX = X;
		for (int32 i = 0; i < S.Len(); ++i)
		{
			const TCHAR Ch = S[i];
			const float W = CellWidth(Ch, Hd);
			auto Seg = [&](float SX, float SY, float SW, float SH, bool bOn) {
				const FLinearColor C = Pass == 0 ? (bOn ? Shadow : FLinearColor::Transparent) : bOn ? Color : Ghost;
				if (C.A > 0.f)
				{
					DrawRect(C, FMath::RoundToFloat(SX + O), FMath::RoundToFloat(SY + O), FMath::Max(1.f, FMath::RoundToFloat(SW)), FMath::Max(1.f, FMath::RoundToFloat(SH)));  // 对齐整像素：数码管的段边要利落
				}
			};
			if ((Ch >= TEXT('0') && Ch <= TEXT('9')) || Ch == TEXT('-'))
			{
				const uint8 M = Ch == TEXT('-') ? 0x40 : Digit[Ch - TEXT('0')];
				// 横段伸进竖段半个段粗（拐角接上，像真的数码管），段与段之间留一道细缝
				const float HX0 = CX + T * 0.5f + G, HW = W - T - 2.f * G;
				const float VT = Top + T * 0.5f + G, VB = Mid + G;
				const float VH = Hd * 0.5f - T * 0.5f - 2.f * G;
				Seg(HX0, Top, HW, T, (M & 0x01) != 0);                          // a
				Seg(CX + W - T, VT, T, VH, (M & 0x02) != 0);                  // b
				Seg(CX + W - T, VB, T, VH, (M & 0x04) != 0);                  // c
				Seg(HX0, Top + Hd - T, HW, T, (M & 0x08) != 0);               // d
				Seg(CX, VB, T, VH, (M & 0x10) != 0);                          // e
				Seg(CX, VT, T, VH, (M & 0x20) != 0);                          // f
				Seg(HX0, Mid - T * 0.5f, HW, T, (M & 0x40) != 0);             // g
			}
			else if (Ch == TEXT(':'))
			{
				Seg(CX + (W - T) * 0.5f, Top + Hd * 0.28f - T * 0.5f, T, T, true);
				Seg(CX + (W - T) * 0.5f, Top + Hd * 0.72f - T * 0.5f, T, T, true);
			}
			else if (Ch == TEXT('.'))
			{
				Seg(CX + (W - T) * 0.5f, Top + Hd - T, T, T, true);
			}
			else if (Ch == TEXT('/') || Ch == TEXT('%'))
			{
				// 斜杠用抗锯齿的线；百分号 = 斜杠 + 两个小方块
				const FLinearColor C = Pass == 0 ? Shadow : Color;
				DrawLine(CX + O, Top + Hd + O, CX + W + O, Top + O, C, FMath::Max(1.f, T * 0.55f) / FMath::Max(MechHudTheme::Get().LineScale, 0.1f));
				if (Ch == TEXT('%'))
				{
					const float Q = T * 1.1f;
					Seg(CX, Top, Q, Q, true);
					Seg(CX + W - Q, Top + Hd - Q, Q, Q, true);
				}
			}
			CX += W + (i + 1 < S.Len() ? Hd * AdvanceK : 0.f);
		}
	}
}

FVector2D AMechHUD::MeasureReadout(const FString& S, float Size)
{
	return MechHudTheme::Get().bSegmentDigits && CanSegment(S) ? MeasureSeg(S, Size) : Measure(S, Size);
}

void AMechHUD::Readout(const FString& S, float X, float Y, float Size, const FLinearColor& Color, float AlignX)
{
	if (!MechHudTheme::Get().bSegmentDigits || !CanSegment(S))
	{
		Text(S, X, Y, Size, Color, AlignX);
		return;
	}
	const float W = AlignX != 0.f ? MeasureSeg(S, Size).X : 0.f;
	SegDigits(S, X - W * AlignX, Y, Size, Color);
}

void AMechHUD::Hazard(float X, float Y, float W, float H, float Alpha)
{
	if (W <= 0.f || H <= 0.f || Alpha <= 0.f)
	{
		return;
	}
	DrawRect(FLinearColor(0.015f, 0.01f, 0.f, 0.72f * Alpha), X, Y, W, H);
	// 斜条：上边比下边往右错 H（45°），周期 = 2.2 × H，黄条占一半；超出矩形的部分按左右边裁掉
	const FLinearColor Y0 = Fade(MechHudTheme::Get().Hazard, 0.92f * Alpha);
	const float Period = H * 2.2f;
	TArray<FCanvasUVTri> Tris;
	auto ClipX = [](const TArray<FVector2D>& In, float Edge, bool bKeepGreater) {
		TArray<FVector2D> Out;
		for (int32 i = 0; i < In.Num(); ++i)
		{
			const FVector2D& A = In[i];
			const FVector2D& B = In[(i + 1) % In.Num()];
			const bool bA = bKeepGreater ? A.X >= Edge : A.X <= Edge;
			const bool bB = bKeepGreater ? B.X >= Edge : B.X <= Edge;
			if (bA)
			{
				Out.Add(A);
			}
			if (bA != bB)
			{
				const double K = (Edge - A.X) / (B.X - A.X);
				Out.Add(FVector2D(Edge, A.Y + (B.Y - A.Y) * K));
			}
		}
		return Out;
	};
	for (float X0 = X - H; X0 < X + W; X0 += Period)
	{
		TArray<FVector2D> P = {FVector2D(X0 + H, Y), FVector2D(X0 + H + Period * 0.5f, Y), FVector2D(X0 + Period * 0.5f, Y + H), FVector2D(X0, Y + H)};
		P = ClipX(P, X, true);
		P = ClipX(P, X + W, false);
		for (int32 i = 1; i + 1 < P.Num(); ++i)
		{
			FCanvasUVTri T;
			T.V0_Pos = P[0], T.V1_Pos = P[i], T.V2_Pos = P[i + 1];
			T.V0_Color = T.V1_Color = T.V2_Color = Y0;
			Tris.Add(T);
		}
	}
	AddTris(Tris);
}

void AMechHUD::Ruler(float X, float Y, float W, int32 N, int32 Major, const FLinearColor& Color)
{
	if (N <= 0)
	{
		return;
	}
	for (int32 i = 0; i <= N; ++i)
	{
		const bool bMajor = Major > 0 && i % Major == 0;
		const float TX = X + W * i / N;
		DrawRect(Fade(Color, bMajor ? 1.f : 0.6f), TX - 0.5f * UI, Y, FMath::Max(1.f, UI), (bMajor ? 5.f : 2.5f) * UI);
	}
}

void AMechHUD::Cuts(float X, float Y, float W, float H, int32 N)
{
	const FLinearColor Gap(0.f, 0.f, 0.f, 0.72f);
	for (int32 i = 1; i < N; ++i)
	{
		DrawRect(Gap, X + W * i / N - 1.f * UI, Y, 2.f * UI, H);
	}
}

void AMechHUD::DrawDecor(AMechCharacter* Mech)
{
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	// 相位电子：屏幕四角一圈极简细线框（第一人称已经有取景框，不重复画）
	if (Th.bHairFrame && !(Mech && Mech->IsFirstPerson()))
	{
		const float In = 20.f * UI;
		const float LX = 44.f * UI;
		const float LY = 28.f * UI;
		const FLinearColor C = Fade(Ink, 0.32f);
		for (const FVector2D& P : {FVector2D(In, In), FVector2D(W - In, In), FVector2D(In, H - In), FVector2D(W - In, H - In)})
		{
			const float SX = P.X < W * 0.5f ? 1.f : -1.f;
			const float SY = P.Y < H * 0.5f ? 1.f : -1.f;
			DrawLine(P.X, P.Y, P.X + SX * LX, P.Y, C, 1.f);
			DrawLine(P.X, P.Y, P.X, P.Y + SY * LY, C, 1.f);
		}
	}
	// 维克托重工：比分条下方的方位刻度带（±45°，每 5° 一格、15° 长一点、30° 标三位数方位 / N E S W），正中一个下指的小三角 + 方框里的当前方位
	if (Th.bHeadingTape && Mech && PlayerOwner && PlayerOwner->PlayerCameraManager && !Mech->GetHealth()->IsDead())
	{
		const float CX = W * 0.5f;
		const float Y = 96.f * UI;
		const float Half = 210.f * UI;
		const float PerDeg = Half / 45.f;
		const float Yaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
		const float Heading = FMath::Fmod(FMath::Fmod(Yaw + 90.f, 360.f) + 360.f, 360.f);  // 世界 −Y 是北（和小地图一致）
		const FLinearColor C = Ink;
		DrawRect(Fade(PlateColor, 0.8f), CX - Half - 10.f * UI, Y - 12.f * UI, Half * 2.f + 20.f * UI, 44.f * UI);  // 一条淡暗底（天空亮时也看得清）
		DrawRect(Fade(Dim, 0.85f), CX - Half, Y, Half * 2.f, FMath::Max(1.f, UI));
		const int32 First = FMath::CeilToInt((Heading - 45.f) / 5.f);
		const int32 Last = FMath::FloorToInt((Heading + 45.f) / 5.f);
		for (int32 k = First; k <= Last; ++k)
		{
			const float Deg = k * 5.f;
			const float X = CX + (Deg - Heading) * PerDeg;
			const int32 D = ((k * 5) % 360 + 360) % 360;
			const bool b30 = D % 30 == 0;
			const bool b15 = D % 15 == 0;
			const float Edge = FMath::Clamp(1.f - FMath::Abs(X - CX) / Half, 0.f, 1.f);
			const FLinearColor TC = Fade(C, FMath::Min(1.f, Edge * 3.f));
			DrawRect(TC, X - 0.75f * UI, Y, FMath::Max(1.f, 1.5f * UI), (b30 ? 10.f : b15 ? 7.f : 4.f) * UI);
			if (b30 && FMath::Abs(X - CX) > 40.f * UI)  // 正中方框两边留开，别和当前方位叠在一起
			{
				static const TCHAR* Card[4] = {TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W")};
				const FString L = D % 90 == 0 ? FString(Card[D / 90]) : FString::Printf(TEXT("%03d"), D);
				Text(L, X, Y + 11.f * UI, 13.f, TC, 0.5f);
			}
		}
		Triangle(FVector2D(CX - 5.f * UI, Y - 8.f * UI), FVector2D(CX + 5.f * UI, Y - 8.f * UI), FVector2D(CX, Y - 1.f * UI), Fade(Accent, 0.95f));
		const FString HS = FString::Printf(TEXT("%03d"), FMath::RoundToInt(Heading) % 360);
		const float BW = 42.f * UI, BH = 18.f * UI;
		const float BY = Y + 11.f * UI;
		DrawRect(Fade(PlateColor, 1.2f), CX - BW * 0.5f, BY, BW, BH);
		Box(CX - BW * 0.5f, BY, BW, BH, Fade(Accent, 0.9f), 1.f);
		Text(HS, CX, BY + (BH - Measure(HS, 12.f).Y) * 0.5f, 12.f, Accent, 0.5f);
	}
}

// ---------------------------------------------------------------- 总入口
void AMechHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UI = Canvas->SizeY / 1080.f;
	if (!HudFrame.IsValid())
	{
		HudFrame = MakeShared<FMechHudFrame>();
	}
	HudFrame->Reset();
	HudFrame->CanvasSize = FVector2f(Canvas->SizeX, Canvas->SizeY);
	HudFrame->FrameNumber = GFrameCounter;
	UpdatePhaseSounds();
	UpdateMapCapture();  // 场地俯视图开局拍一次：放在菜单判断前面，选配 / 房间阶段就把它拍完
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const AMechPlayerController* PC = Cast<AMechPlayerController>(PlayerOwner);
	if ((GS && GS->bMenuMode) || (PC && PC->IsMenuOpen()) || (GS && (GS->Phase == EMechRoundPhase::Draft || GS->Phase == EMechRoundPhase::Lobby)))
	{
		return;
	}
	AMechCharacter* Mech = GetFocusMech();
	UpdateTheme(Mech);  // HUD 主题跟着本机框架的制造商走（MechHudTheme.h）；换配装 / 重生当帧生效
	static const bool bThemeShots = [] {
		FString Tag;  // -MechHudThemeShots 或 -MechHudThemeShots=day|night（带等号时 FParse::Param 认不出）
		return FParse::Param(FCommandLine::Get(), TEXT("MechHudThemeShots")) || FParse::Value(FCommandLine::Get(), TEXT("MechHudThemeShots="), Tag);
	}();
	if (bThemeShots)
	{
		MechHudThemeTest::Tick(*this, Mech, GetWorld()->GetDeltaSeconds());
		UpdateTheme(Mech);  // 测试刚切了主题：这一帧就按新的画
	}
	if (Mech && Mech->IsViewerMode())
	{
		DrawViewer(Mech);
		return;
	}
	// 第一人称启动时各块 HUD 按顺序闪两下亮起（时间按 3 秒的完整启动算，短启动按比例压缩）
	const float Boot = Mech && Mech->IsFirstPerson() ? Mech->GetBootTime() : -1.f;
	const float BootScale = Mech ? 3.f / FMath::Max(0.5f, Mech->GetBootLength()) : 1.f;
	auto Gate = [this, Boot, BootScale](float At) {
		const float T = Boot * BootScale;
		HudAlpha = Boot < 0.f ? 1.f : T < At ? 0.f : T < At + 0.3f ? (FMath::Fmod(T * 22.f, 2.f) < 1.f ? 1.f : 0.25f) : 1.f;
	};
	if (Mech && Mech->IsFirstPerson())
	{
		Gate(0.9f);
		DrawVisor(Mech);
	}
	if (Mech)
	{
		Gate(1.4f);
		DrawMarkers(Mech);
		DrawNames(Mech);
		DrawPods(Mech);
		DrawPings(Mech);  // 中键标点（11-01，MechHudPing.cpp）
		DrawOffscreen(Mech);
		DrawLockBox(Mech);
		DrawEaster(Mech);  // 11-02 彩蛋：无人机 / 机械狗的锁定框、标记、全场提示（MechHudEaster.cpp）
		Gate(1.2f);
		DrawScope(Mech);
		DrawReticle(Mech);
		HudAlpha = 1.f;
		DrawDamage(Mech);
		Gate(1.55f);
		DrawStrip(Mech);
		Gate(1.75f);
		DrawDash(Mech);
		DrawTactical(Mech);
		DrawAllies(Mech);
		Gate(1.95f);
		DrawSelf(Mech);
		DrawPopups(Mech);
	}
	Gate(2.15f);
	DrawRound();
	DrawDecor(Mech);  // 主题专属装饰：维克托方位刻度带、相位四角细线框
	DrawFeed();
	if (GS && GS->bCoreMode)
	{
		DrawCore(Mech);
	}
	if (GS && GS->bRangeMode)
	{
		DrawDefense(Mech);  // 保卫战（场上有波次调度器才画）
	}
	if (GS && GS->IsDeathmatch())
	{
		DrawDeathmatch(Mech);  // 重生读条、重生保护（MechHudModes.cpp）
	}
	DrawMap(Mech);  // 小地图 / 按住地图键的大地图（所有模式）
	HudAlpha = 1.f;
	if (Alerts)
	{
		Alerts->Draw(*this, Mech);  // 战机式告警：判断、声音、威胁方向 / 告警灯 / 低装甲呼吸（MechHudAlerts.cpp）
	}
	if (Boot >= 0.f)
	{
		DrawBoot(Mech, Boot * BootScale);
	}
	DrawPingWheel();  // 按住标点键的轮盘（11-01，MechHudPing.cpp）
	static const bool bForceScoreboard = FParse::Param(FCommandLine::Get(), TEXT("MechScoreboardShots"));  // 11-01 测试：一直画计分板，配 -MechAutoBattle 的截图看积分列
	if (bForceScoreboard || MechKeys::IsDown(PlayerOwner, EMechKey::Scoreboard))
	{
		DrawScoreboard();
	}
	if (Alerts)
	{
		Alerts->PostProcess(*this, *HudFrame, Mech);  // 整帧：受击抖动、EMP 撕裂闪断、上电逐块亮起、击毁时收成一条线
	}
}

// ---------------------------------------------------------------- 模型查看：当前状态 + 按键说明
void AMechHUD::DrawViewer(AMechCharacter* Mech)
{
	const float X = 28.f * UI;
	float Y = 22.f * UI;
	const bool bHelp = Mech->IsViewerHelpShown();
	Panel(X - 14.f * UI, Y - 10.f * UI, 900.f * UI, (bHelp ? 222.f : 80.f) * UI);
	DrawRect(Accent, X - 14.f * UI, Y - 10.f * UI, 3.f * UI, (bHelp ? 222.f : 80.f) * UI);
	Text(TEXT("模型查看"), X, Y, 22.f, Ink);
	Text(TEXT("Esc 菜单"), X + 870.f * UI, Y + 6.f * UI, 13.f, Dim, 1.f);
	Y += 34.f * UI;
	Text(Mech->GetViewerStatus(), X, Y, 16.f, Warn);
	if (!bHelp)
	{
		return;
	}
	static const TCHAR* Lines[] = {
		TEXT("鼠标  转镜头        滚轮  远近        F  取景（全身 / 上身 / 腿）"),
		TEXT("1 站立  2 慢走  3 跑  4 左横移  5 右横移  6 后退  7 冲刺滑行  8 下落  9 轻击连招  0 开火（含换弹）"),
		TEXT("← →  轮换全部动作（另有 重斩、格挡、八方向闪避、四方向失衡、拔刀收刀）     Q  切射击 / 近战"),
		TEXT("P  暂停        [ ]  慢放 / 加快（0.1 0.25 0.5 1 倍）"),
		TEXT("N  白天 / 夜晚        B  补光        G  只看武器        H  隐藏说明        Z  换枪        X  换刀        C  换框架"),
		TEXT("左键 / 右键 / R 也能手动操作（射击：开火 / 瞄准 / 换弹，近战：轻击、按住重斩 / 格挡）"),
	};
	Y += 30.f * UI;
	for (const TCHAR* L : Lines)
	{
		Text(L, X, Y, 14.f, Dim);
		Y += 22.f * UI;
	}
}
// ---------------------------------------------------------------- 阵亡：屏幕四边一圈暗红细框（夺芯的重生倒计时由 DrawCore 画在正中）
void AMechHUD::DrawSelf(AMechCharacter* Mech)
{
	if (!Mech->GetHealth()->IsDead())
	{
		return;
	}
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float In = 18.f * UI;
	const float L = 90.f * UI;
	const FLinearColor C = Fade(Enemy, 0.7f);
	Box(In, In, W - 2.f * In, H - 2.f * In, Fade(Enemy, 0.25f), 1.f);
	for (const FVector2D& P : {FVector2D(In, In), FVector2D(W - In, In), FVector2D(In, H - In), FVector2D(W - In, H - In)})
	{
		const float SX = P.X < W * 0.5f ? 1.f : -1.f;
		const float SY = P.Y < H * 0.5f ? 1.f : -1.f;
		DrawLine(P.X, P.Y, P.X + SX * L, P.Y, C, 2.f);
		DrawLine(P.X, P.Y, P.X, P.Y + SY * L * 0.6f, C, 2.f);
	}
}

// ---------------------------------------------------------------- 左下：队友装甲（一台一条细线，阵亡的只剩暗线）
void AMechHUD::DrawAllies(AMechCharacter* Mech)
{
	const float X = 48.f * UI;
	float Y = Canvas->SizeY - 52.f * UI;
	const FLinearColor C = Mech->GetTeam() == 0 ? Ally : Enemy;
	const float BarW = 120.f * UI;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (*It == Mech || It->GetTeam() != Mech->GetTeam() || It->IsTrainingDummy())
		{
			continue;
		}
		if (It->GetHealth()->IsDead())
		{
			DrawRect(Faint, X, Y + 1.f * UI, BarW, 1.f * UI);
		}
		else
		{
			DrawRect(Fade(C, 0.2f), X, Y, BarW, 3.f * UI);
			DrawRect(C, X, Y, BarW * It->GetHealth()->GetAPFraction(), 3.f * UI);
		}
		Y -= 12.f * UI;
	}
}

// ---------------------------------------------------------------- 准星下方的细条：战斗时要盯的数值都在这里
void AMechHUD::DrawStrip(AMechCharacter* Mech)
{
	UMechHealthComponent* Health = Mech->GetHealth();
	if (Health->IsDead())
	{
		return;
	}
	UMechCombatComponent* Combat = Mech->GetCombat();
	const UMechMovementComponent* Move = Mech->GetMechMovement();
	const MechWeapons::FGun& G = Combat->GetGun();
	const MechWeapons::FBlade& B = Combat->GetBlade();
	const float Now = GetWorld()->GetTimeSeconds();
	const float Dt = GetWorld()->GetDeltaSeconds();
	const float CX = Canvas->SizeX * 0.5f;
	const float Y = Canvas->SizeY * 0.73f;
	const bool bBlade = Combat->GetStance() == EMechStance::Blade;
	const float APFrac = FMath::Clamp(Health->GetAPFraction(), 0.f, 1.f);
	const float ENFrac = FMath::Clamp(Mech->GetEnergy() / FMath::Max(1.f, Mech->GetMaxEnergy()), 0.f, 1.f);
	const int32 Ammo = Combat->GetAmmo();

	const FMechCore* Held = nullptr;
	if (Mech->IsCarryingCore())
	{
		for (TActorIterator<AMechCoreDirector> It(GetWorld()); It; ++It)
		{
			Held = It->CoreCarriedBy(Mech);
			break;
		}
	}

	// 亮度：数值有变化，或在装填、冷却、蓄力、过热、失衡、低 AP、带芯时全亮；平静 1.2 秒后降到一半
	const bool bDropped = PrevAPFrac >= 0.f && APFrac < PrevAPFrac - 0.0005f;
	const bool bBusy = Combat->IsReloading() || Mech->IsOverheated() || Health->IsStaggered() || APFrac < 0.3f
		|| Mech->GetShoulderReady() < 1.f || Mech->IsNetted() || Combat->GetMeleeHoldFraction() > 0.f || Combat->GetCharge() > 0.f
		|| (bBlade && Combat->GetHeavyReady() < 1.f) || Held != nullptr || Combat->GetSwitchProgress() >= 0.f;
	if (bBusy || bDropped || FMath::Abs(APFrac - PrevAPFrac) > 0.0005f || FMath::Abs(ENFrac - PrevENFrac) > 0.001f || Ammo != PrevAmmo || bBlade != bPrevBlade)
	{
		StripHot = Now;
	}
	PrevAPFrac = APFrac;
	PrevENFrac = ENFrac;
	PrevAmmo = Ammo;
	bPrevBlade = bBlade;
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float Calm = FMath::Clamp((Now - StripHot - 1.2f) / 0.6f, 0.f, 1.f);
	const float OuterAlpha = HudAlpha;
	HudAlpha *= FMath::Lerp(1.f, Th.CalmAlpha, Calm);

	const float Gap = 34.f * UI;   // 中间留给弹药数字
	const float Len = 230.f * UI;
	const float T = 4.f * UI * Th.BarScale;          // 条的粗细跟主题（新纪粗、相位细）
	const float SubT = 2.f * UI * FMath::Max(0.75f, Th.BarScale * 0.8f);
	const float Top = Y - T * 0.5f;
	const float Sub = Top + T + 6.f * UI;
	const bool bBlink = FMath::Fmod(Now, 0.3f) < 0.15f;

	// 失衡值：AP 条上面一道细线；失衡时整条闪红
	const float Stag = Health->GetStaggerFraction();
	if (Health->IsStaggered())
	{
		DrawRect(bBlink ? Enemy : Warn, CX - Gap - Len, Top - 6.f * UI, Len, 2.f * UI);
	}
	else if (Stag > 0.01f)
	{
		DrawRect(Fade(Warn, 0.85f), CX - Gap - Len * Stag, Top - 6.f * UI, Len * Stag, 2.f * UI);
	}

	// AP：从外往里减；刚掉的那截先留一段浅色，0.4 秒后追上来
	if (APFrac >= APTrail)
	{
		APTrail = APFrac;
	}
	if (bDropped)
	{
		APTrailHold = 0.4f;
	}
	APTrailHold -= Dt;
	if (APTrailHold <= 0.f)
	{
		APTrail = FMath::FInterpConstantTo(APTrail, APFrac, Dt, 0.8f);
	}
	if (Th.BarShadow > 0.f)
	{
		// 相位：白色细条下面压一道暗影（亮地面、白天天空上也看得出条）
		const FLinearColor Sh(0.f, 0.f, 0.f, Th.BarShadow);
		DrawRect(Sh, CX - Gap - Len, Top + 1.f * UI, Len, T + 1.f * UI);
		DrawRect(Sh, CX + Gap, Top + 1.f * UI, Len, T + 1.f * UI);
	}
	DrawRect(Track, CX - Gap - Len, Top, Len, T);
	DrawRect(Fade(Ink, 0.55f), CX - Gap - Len * APTrail, Top, Len * (APTrail - APFrac), T);
	DrawRect(APFrac < 0.3f ? Enemy : Accent, CX - Gap - Len * APFrac, Top, Len * APFrac, T);

	// 能量：从里往外；过热橙红闪，不够一次快速闪避变橙
	const FLinearColor ENCol = Mech->IsOverheated() ? (bBlink ? Enemy : Warn) : Move->Energy < Move->QuickBoostCost ? Warn : Accent;
	DrawRect(Track, CX + Gap, Top, Len, T);
	DrawRect(ENCol, CX + Gap, Top, Len * ENFrac, T);
	if (Th.bSegmentBars)
	{
		// 新纪：两条切成一格一格（像工程机械的分段电量表）
		Cuts(CX - Gap - Len, Top, Len, T, 20);
		Cuts(CX + Gap, Top, Len, T, 20);
	}
	if (Th.bRulers)
	{
		// 维克托：四分之一处的刻度（上下各探出一点）
		for (int32 q = 1; q < 4; ++q)
		{
			DrawRect(Fade(Dim, 0.8f), CX - Gap - Len * q / 4.f - 0.5f * UI, Top - 3.f * UI, FMath::Max(1.f, UI), T + 3.f * UI);
			DrawRect(Fade(Dim, 0.8f), CX + Gap + Len * q / 4.f - 0.5f * UI, Top - 3.f * UI, FMath::Max(1.f, UI), T + 3.f * UI);
		}
	}

	// 左下一层：肩部装备冷却（导弹锁上 / 钩爪对准敌人且就绪时红；紧急脱离这类就绪但现在用不了（EMP 中、被网住、失衡、带芯）时橙）
	const float MLen = 110.f * UI;
	const float Missile = FMath::Clamp(Mech->GetShoulderReady(), 0.f, 1.f);
	const bool bShoulderHot = Mech->GetLoadout().Shoulder == EMechShoulder::Missiles ? Combat->GetMissileLock() >= 1.f : Mech->GetShoulder()->GetAimHint() == 2;
	DrawRect(Track, CX - Gap - MLen, Sub, MLen, SubT);
	const bool bShoulderBlocked = MechIsTacticalShoulder(Mech->GetLoadout().Shoulder) && !Mech->GetTactical()->CanUse();
	DrawRect(Missile >= 1.f ? (bShoulderHot ? Enemy : bShoulderBlocked ? Warn : Accent) : Fade(Accent, 0.6f), CX - Gap - MLen * Missile, Sub, MLen * Missile, SubT);

	// 右下一层：现在的能量还够快速闪避几次（最多 4 格；带着核心不能闪，全暗）
	const int32 Dodges = Move->bCarryLock ? 0 : FMath::Min(4, FMath::FloorToInt(Move->Energy / FMath::Max(1.f, Move->QuickBoostCost)));
	for (int32 i = 0; i < 4; ++i)
	{
		DrawRect(i < Dodges ? Accent : Track, CX + Gap + i * 29.f * UI, Sub, 22.f * UI, SubT);
	}

	// 中间：按姿态和武器换内容
	const float InL = CX - Gap + 6.f * UI;
	const float InW = Gap * 2.f - 12.f * UI;
	auto Num = [&](const FString& S, const FLinearColor& C) {
		Readout(S, CX, Y - MeasureReadout(S, 22.f).Y * 0.5f, 22.f, C, 0.5f);  // 新纪是七段数码管
	};
	auto Line = [&](float Frac, const FLinearColor& C) {
		DrawRect(Track, InL, Sub, InW, SubT);
		DrawRect(C, InL, Sub, InW * FMath::Clamp(Frac, 0.f, 1.f), SubT);
	};
	auto Pips = [&](int32 Count, int32 Lit, const FLinearColor& On, float PY) {
		const float PW = 5.f * UI;
		const float PG = 3.f * UI;
		const float X0 = CX - (Count * PW + (Count - 1) * PG) * 0.5f;
		for (int32 i = 0; i < Count; ++i)
		{
			DrawRect(i < Lit ? On : Track, X0 + i * (PW + PG), PY, PW, PW);
		}
	};
	if (!bBlade)
	{
		if (Combat->IsReloading())
		{
			// 装填：数字换成一道橙色进度（准星刻度同时变橙）
			DrawRect(Track, InL, Y - 1.f * UI, InW, 2.f * UI);
			DrawRect(Warn, InL, Y - 1.f * UI, InW * FMath::Clamp(Combat->GetReloadProgress(), 0.f, 1.f), 2.f * UI);
		}
		else if (G.HeatPerShot > 0.f)
		{
			const float HeatNow = Combat->GetHeat();
			const FLinearColor HC = Combat->IsOverheated() ? (bBlink ? Enemy : Warn) : HeatNow > 0.7f ? Warn : Ink;
			Num(FString::FromInt(FMath::RoundToInt(HeatNow * 100.f)), HC);
			Line(HeatNow, HC);
		}
		else if (G.EnergyPerShot > 0.f)
		{
			const int32 Shots = FMath::FloorToInt(Move->Energy / G.EnergyPerShot);
			Num(FString::FromInt(Shots), Shots > 0 ? Ink : Warn);
		}
		else
		{
			const int32 Mag = FMath::Max(1, Combat->GetMagazine());
			const bool bHealed = Now - Combat->GetLastHealTime() < 2.f;
			Num(Pad(Ammo, 2), bHealed ? Heal : Ammo <= Mag / 5 ? Warn : Ink);
			if (G.StreakBonus > 0.f && Combat->GetStreak() > 1)
			{
				Line(float(Combat->GetStreak() - 1) / MechWeapons::StreakMax, Accent);  // 连中加成
			}
			if (G.bHeavyRefill && Now - Combat->GetLastRefillTime() < 1.2f)
			{
				// 双持手枪：重斩砍中补满弹匣的那一下，数字下面一道金色细条闪一下（10-30 晚起取代「闪避装满」的冷却条）
				Line(1.f, Fade(Gold, 1.f - (Now - Combat->GetLastRefillTime()) / 1.2f));
			}
		}
	}
	else if (B.Knives > 0)
	{
		// 投掷匕首：手上几把；下面 4 格是连投计数，满了下一把是强力匕首（4 格一起变橙）
		Num(FString::FromInt(Combat->GetKnives()), Combat->GetKnives() > 0 ? Ink : Warn);
		const int32 Streak = Combat->GetKnifeStreak();
		Pips(4, Streak, Streak >= 4 ? Warn : Accent, Sub - 1.5f * UI);
	}
	else
	{
		// 其他刀：连段第几下 + 重斩冷却
		Pips(4, Combat->GetComboStep(), Accent, Y - 2.5f * UI);
		Line(Combat->GetHeavyReady(), Combat->GetHeavyReady() >= 1.f ? Accent : Fade(Accent, 0.6f));
	}

	// 带着核心：细条上方一颗金色菱形；交芯时上面一道金线读条（被打到失衡会断）
	if (Held)
	{
		const float DY = Top - 24.f * UI;
		const float S = 6.f * UI;
		Triangle(FVector2D(CX, DY - S), FVector2D(CX + S, DY), FVector2D(CX - S, DY), Gold);
		Triangle(FVector2D(CX, DY + S), FVector2D(CX + S, DY), FVector2D(CX - S, DY), Gold);
		if (Held->Channel > 0.f)
		{
			DrawRect(Fade(Gold, 0.25f), CX - Gap - Len, Top - 12.f * UI, (Gap + Len) * 2.f, 2.f * UI);
			DrawRect(Gold, CX - Gap - Len, Top - 12.f * UI, (Gap + Len) * 2.f * FMath::Clamp(Held->Channel, 0.f, 1.f), 2.f * UI);
		}
	}
	HudAlpha = OuterAlpha;
}

// ---------------------------------------------------------------- 底部仪表盘：居中一整块（梯形暗底 + 青色上沿），速度表在正中，两边读数，两端是插件栏和姿态
void AMechHUD::DrawDash(AMechCharacter* Mech)
{
	UMechHealthComponent* Health = Mech->GetHealth();
	if (Health->IsDead())
	{
		return;
	}
	const float H = Canvas->SizeY;
	const float CX = Canvas->SizeX * 0.5f;
	UMechCombatComponent* Combat = Mech->GetCombat();
	const UMechMovementComponent* Move = Mech->GetMechMovement();
	const MechWeapons::FGun& G = Combat->GetGun();
	const MechWeapons::FBlade& B = Combat->GetBlade();
	const bool bBlade = Combat->GetStance() == EMechStance::Blade;
	const float Half = 500.f * UI;
	const float TopY = H - 108.f * UI;
	const float Slant = 44.f * UI;

	const FMechHudTheme& Th = MechHudTheme::Get();
	// 底座（按主题三种样式，10-31）：
	//  梯形（联赛标准 / 新纪）：上窄下宽的半透明暗带（上淡下浓），上沿主色细线、两侧斜边淡一点，上沿两端各五道刻度；新纪在两个上角加黄黑警示斜纹
	//  方框（维克托）：方形暗底 + 四角折角 + 上沿一排刻度尺（MFD 的边框）
	//  细线（相位）：不铺底，只一道很淡的细线和两端短竖线
	if (Th.Dash == EMechDashStyle::Frame)
	{
		const float L = CX - Half - Slant * 0.5f, R = CX + Half + Slant * 0.5f;
		const float Bot = H - 6.f * UI;
		DrawRect(Th.DashUp, L, TopY, R - L, Bot - TopY);
		const FLinearColor EC = Fade(Accent, 0.8f);
		const float K = 16.f * UI;
		for (int32 c = 0; c < 4; ++c)
		{
			const float X = (c & 1) ? R : L;
			const float Y = (c & 2) ? Bot : TopY;
			const float SX = (c & 1) ? -1.f : 1.f;
			const float SY = (c & 2) ? -1.f : 1.f;
			DrawLine(X, Y, X + SX * K, Y, EC, 1.5f);
			DrawLine(X, Y, X, Y + SY * K, EC, 1.5f);
		}
		DrawLine(L + K + 6.f * UI, TopY, R - K - 6.f * UI, TopY, Fade(Accent, 0.3f), 1.f);
		Ruler(L + K + 6.f * UI, TopY, R - L - 2.f * (K + 6.f * UI), 60, 5, Fade(Dim, 0.55f));
	}
	else if (Th.Dash == EMechDashStyle::Hairline)
	{
		const float L = CX - Half + 20.f * UI, R = CX + Half - 20.f * UI;
		const float Y = TopY + 6.f * UI;
		DrawLine(L, Y, R, Y, Fade(Ink, 0.22f), 1.f);
		DrawLine(L, Y, L, Y + 7.f * UI, Fade(Ink, 0.35f), 1.f);
		DrawLine(R, Y, R, Y + 7.f * UI, Fade(Ink, 0.35f), 1.f);
	}
	else
	{
		const FLinearColor Up = Th.DashUp;
		const FLinearColor Down = Th.DashDown;
		const FVector2D A(CX - Half, TopY);
		const FVector2D B2(CX + Half, TopY);
		const FVector2D C(CX + Half + Slant, H);
		const FVector2D D(CX - Half - Slant, H);
		TArray<FCanvasUVTri> Band;
		FCanvasUVTri T;
		T.V0_Pos = A, T.V1_Pos = B2, T.V2_Pos = C;
		T.V0_Color = Up, T.V1_Color = Up, T.V2_Color = Down;
		Band.Add(T);
		T.V0_Pos = A, T.V1_Pos = C, T.V2_Pos = D;
		T.V0_Color = Up, T.V1_Color = Down, T.V2_Color = Down;
		Band.Add(T);
		AddTris(Band);
		DrawLine(A.X, A.Y, B2.X, B2.Y, Fade(Accent, 0.55f), 1.5f);
		SoftLine(A.X, A.Y, D.X, D.Y, Fade(Accent, 0.3f), 1.f);
		SoftLine(B2.X, B2.Y, C.X, C.Y, Fade(Accent, 0.3f), 1.f);
		for (int32 k = 1; k <= 5 && Th.bDashTicks && !Th.bHazard; ++k)
		{
			const float L = (k == 5 ? 8.f : 4.f) * UI;
			DrawLine(A.X + k * 9.f * UI, TopY, A.X + k * 9.f * UI, TopY + L, Fade(Accent, 0.5f), 1.f);
			DrawLine(B2.X - k * 9.f * UI, TopY, B2.X - k * 9.f * UI, TopY + L, Fade(Accent, 0.5f), 1.f);
		}
		if (Th.bHazard)
		{
			// 新纪：两个上角各一块黄黑斜纹（贴着上沿、往里 64 像素），像工程机械的警示贴
			const float HW = 64.f * UI, HH = 9.f * UI;
			Hazard(A.X + 2.f * UI, TopY + 3.f * UI, HW, HH, 1.f);
			Hazard(B2.X - HW - 2.f * UI, TopY + 3.f * UI, HW, HH, 1.f);
		}
	}

	// 正中：速度表（分段弧，满格 = 快速闪避的速度；过热时橙）
	const float GY = H - 26.f * UI;
	const float R = 68.f * UI;
	const float Speed = Mech->GetCharacterMovement()->Velocity.Size();
	const int32 Segs = 30;
	const float Step = 3.5f / Segs;
	const int32 Lit = FMath::RoundToInt(FMath::Clamp(Speed / FMath::Max(1.f, Move->QuickBoostSpeed), 0.f, 1.f) * Segs);
	const FLinearColor On = Mech->IsOverheated() ? Warn : Accent;
	for (int32 i = 0; i < Segs; ++i)
	{
		Arc(CX, GY, R, -1.75f + i * Step + 0.02f, -1.75f + (i + 1) * Step - 0.02f, i < Lit ? On : Track, FMath::Max(3.f, 6.f * UI));
	}
	Readout(Pad(FMath::RoundToInt(Speed * 0.036f), 3), CX, GY - 54.f * UI, 34.f, Ink, 0.5f);
	if (Th.bUnits)
	{
		Text(Th.bExtraLabels ? TEXT("KM/H") : TEXT("km/h"), CX, GY - 14.f * UI, 12.f, Dim, 0.5f);
	}

	// 一组读数：上面小标签或图标，中间大数字（可带“/最大”），下面一道细读条
	const float LabelY = H - 92.f * UI;
	const float ValueY = H - 74.f * UI;
	const float BarY = H - 30.f * UI;
	const float BarW = 86.f * UI;
	const float BarT = 2.f * UI * FMath::Max(0.75f, Th.BarScale);
	auto Gauge = [&](float X, float AlignX, const FString& Value, const FString& InOf, const FLinearColor& C, float Frac, const FLinearColor& BarC) {
		const FString Of = Th.bUnits ? InOf : FString();  // 相位：只留读数本身
		const float VW = MeasureReadout(Value, 30.f).X + (Of.IsEmpty() ? 0.f : Measure(Of, 15.f).X);
		const float Left = X - VW * AlignX;
		Readout(Value, Left, ValueY, 30.f, C);
		if (!Of.IsEmpty())
		{
			Text(Of, Left + MeasureReadout(Value, 30.f).X, ValueY + 12.f * UI, 15.f, Dim);
		}
		const float F = FMath::Clamp(Frac, 0.f, 1.f);
		const float BL = X - BarW * AlignX;
		DrawRect(Track, BL, BarY, BarW, BarT);
		DrawRect(BarC, AlignX > 0.5f ? BL + BarW * (1.f - F) : BL, BarY, BarW * F, BarT);
		if (Th.bSegmentBars)
		{
			Cuts(BL, BarY, BarW, BarT, 10);
		}
		if (Th.bRulers)
		{
			Ruler(BL, BarY + BarT + 1.f * UI, BarW, 10, 5, Fade(Dim, 0.7f));
		}
	};

	// 左：能量、AP（右对齐，读条从里往外）
	const float ENFrac = Mech->GetEnergy() / FMath::Max(1.f, Mech->GetMaxEnergy());
	const float APFrac = Health->GetAPFraction();
	Text(TEXT("EN"), CX - 130.f * UI, LabelY, 13.f, Fade(Ink, 0.6f), 1.f);
	Gauge(CX - 130.f * UI, 1.f, Pad(FMath::RoundToInt(Mech->GetEnergy()), 3), FString(), Mech->IsOverheated() ? Warn : Ink, ENFrac, Mech->IsOverheated() ? Warn : Accent);
	// 新纪建设的现场抢修中：AP 读数和读条变绿（一明一暗）
	const bool bRepairing = Health->IsRepairing();
	const FLinearColor RepairC = Fade(FLinearColor(0.35f, 1.f, 0.5f), 0.75f + 0.25f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 6.f));
	const FLinearColor APInk = bRepairing ? RepairC : APFrac < 0.3f ? Enemy : Ink;
	Text(bRepairing ? TEXT("AP +") : TEXT("AP"), CX - 260.f * UI, LabelY, 13.f, bRepairing ? RepairC : Fade(Ink, 0.6f), 1.f);
	Gauge(CX - 260.f * UI, 1.f, Pad(FMath::RoundToInt(Health->GetAP()), 4), FString(), APInk, APFrac, bRepairing ? RepairC : APFrac < 0.3f ? Enemy : Accent);

	// 右：当前武器读数（弹匣 / 热量 / 可打发数 / 匕首 / 重斩冷却），上面是枪或刀的小图标
	FString Value;
	FString Of;
	FLinearColor VC = Ink;
	float Frac = 1.f;
	FLinearColor FC = Accent;
	if (!bBlade)
	{
		if (Combat->IsReloading())
		{
			Value = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Combat->GetReloadProgress() * 100.f));
			VC = Warn;
			Frac = Combat->GetReloadProgress();
			FC = Warn;
		}
		else if (G.HeatPerShot > 0.f)
		{
			Value = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Combat->GetHeat() * 100.f));
			VC = Combat->IsOverheated() ? Enemy : Combat->GetHeat() > 0.7f ? Warn : Ink;
			Frac = Combat->GetHeat();
			FC = Combat->IsOverheated() ? Enemy : Combat->GetHeat() > 0.7f ? Warn : Accent;
		}
		else if (G.EnergyPerShot > 0.f)
		{
			const int32 Shots = FMath::FloorToInt(Move->Energy / G.EnergyPerShot);
			const int32 MaxShots = FMath::Max(1, FMath::FloorToInt(Move->MaxEnergy / G.EnergyPerShot));
			Value = FString::FromInt(Shots);
			Of = FString::Printf(TEXT(" /%d"), MaxShots);
			Frac = float(Shots) / MaxShots;
			VC = Shots > 0 ? Ink : Warn;
		}
		else
		{
			const int32 Mag = FMath::Max(1, Combat->GetMagazine());
			const bool bLow = Combat->GetAmmo() <= Mag / 5;
			Value = Pad(Combat->GetAmmo(), 2);
			Of = TEXT(" /") + Pad(Mag, 2);
			Frac = float(Combat->GetAmmo()) / Mag;
			VC = bLow ? Warn : Ink;
			FC = bLow ? Warn : Accent;
		}
	}
	else if (B.Knives > 0)
	{
		Value = FString::FromInt(Combat->GetKnives());
		Of = FString::Printf(TEXT(" /%d"), B.Knives);
		Frac = float(Combat->GetKnives()) / B.Knives;
		VC = Combat->GetKnives() > 0 ? Ink : Warn;
	}
	else
	{
		Value = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Combat->GetHeavyReady() * 100.f));
		Frac = Combat->GetHeavyReady();
		VC = Frac >= 1.f ? Ink : Dim;
	}
	const float WX = CX + 130.f * UI;
	const FLinearColor IconC = Fade(Ink, 0.6f);
	const uint8 GunIcon = MechFeedIcon::Shooter0 + static_cast<uint8>(Mech->GetLoadout().Shooter);
	const uint8 BladeIcon = MechFeedIcon::Melee0 + static_cast<uint8>(Mech->GetLoadout().Melee);
	auto EquipmentIcon = [&](uint8 Icon, float X, float Y, float Width, float Height, const FLinearColor& Color) {
		if (!WeaponSilhouette(Icon, X, Y, Width * UI, Height * UI, Color))
			FeedIcon(Icon, X + Width * UI * .5f, Y + Height * UI * .5f, 5.f * UI, Color);
	};
	EquipmentIcon(bBlade ? BladeIcon : GunIcon, WX, LabelY, 56.f, 14.f, IconC);
	if (G.bHeavyRefill)
	{
		// 双持手枪（10-30 晚起）：武器图标右边一个小菱形 = 「重斩砍中补满弹匣」。平时暗的空心（提醒有这个机制），
		// 刚补满的 1.2 秒里实心金色、先放大再收回（两种姿态都画：补满是在拿刀时发生的）
		const float Since = GetWorld()->GetTimeSeconds() - Combat->GetLastRefillTime();
		const bool bFresh = Since < 1.2f;
		const float DX = WX + 64.f * UI, DY = LabelY + 7.f * UI;
		const float DR = (bFresh ? 4.5f + 3.f * FMath::Max(0.f, 1.f - Since / 0.3f) : 4.5f) * UI;
		if (bFresh)
		{
			const FLinearColor DC = Fade(Gold, 1.f - Since / 1.2f * 0.6f);
			Triangle(FVector2D(DX, DY - DR), FVector2D(DX + DR, DY), FVector2D(DX - DR, DY), DC);
			Triangle(FVector2D(DX, DY + DR), FVector2D(DX + DR, DY), FVector2D(DX - DR, DY), DC);
		}
		else
		{
			const FLinearColor DC = Fade(Ink, 0.35f);
			SoftLine(DX, DY - DR, DX + DR, DY, DC, 1.2f);
			SoftLine(DX + DR, DY, DX, DY + DR, DC, 1.2f);
			SoftLine(DX, DY + DR, DX - DR, DY, DC, 1.2f);
			SoftLine(DX - DR, DY, DX, DY - DR, DC, 1.2f);
		}
	}
	Gauge(WX, 0.f, Value, Of, VC, Frac, FC);
	if (Th.bExtraLabels)
	{
		// 维克托：图标旁再写一个缩写（MFD 每块读数都有名字）
		Text(TEXT("WPN"), WX + (G.bHeavyRefill ? 76.f : 64.f) * UI, LabelY, 12.f, Fade(Ink, 0.6f));
		Text(TEXT("SHD"), CX + 260.f * UI + 56.f * UI, LabelY, 12.f, Fade(Ink, 0.6f));
	}

	// 肩部装备：与击毁播报相同的真实模型剪影；冷却时写剩几秒，就绪时写 --、读条满（导弹锁上 / 钩爪对准敌人时红）。
	// 紧急脱离这四件（11-01 并入肩部）：就绪但现在用不了时橙，刚转好图标闪两下；地雷存量小格移到读条下方。
	const float MX = CX + 260.f * UI;
	const EMechShoulder ShoulderType = Mech->GetLoadout().Shoulder;
	const float Missile = FMath::Clamp(Mech->GetShoulderReady(), 0.f, 1.f);
	const bool bMissileReady = Missile >= 1.f;
	const bool bHot = ShoulderType == EMechShoulder::Missiles ? Combat->GetMissileLock() >= 1.f : Mech->GetShoulder()->GetAimHint() == 2;
	const bool bTacticalShoulder = MechIsTacticalShoulder(ShoulderType);
	const UMechTacticalComponent* Tac = Mech->GetTactical();
	const FLinearColor MC = bMissileReady ? (bHot ? Enemy : bTacticalShoulder && !Tac->CanUse() ? Warn : Accent) : Dim;
	const float SinceReady = GetWorld()->GetTimeSeconds() - Tac->GetReadySince();
	const bool bReadyFlash = bTacticalShoulder && bMissileReady && SinceReady < 0.5f && FMath::Fmod(SinceReady, 0.25f) < 0.12f;
	const FLinearColor MIcon = bReadyFlash ? Ink : bMissileReady ? MC : IconC;
	EquipmentIcon(MechFeedIcon::Shoulder0 + static_cast<uint8>(ShoulderType), MX, LabelY, 48.f, 14.f, MIcon);
	if (ShoulderType == EMechShoulder::Mine)
	{
		// 地雷存量放在读条下方，给剪影和 SHD 缩写让出完整一行。
		DrawTacticalMines(Mech, MX, BarY + 7.f * UI, Tac->CanUse() ? Accent : Warn);
	}
	Gauge(MX, 0.f, bMissileReady ? FString(Th.bPadDigits ? TEXT("RDY") : TEXT("--")) : FString::Printf(TEXT("%.1f"), Mech->GetShoulderTimer()), FString(), bMissileReady ? MC : Dim, Missile, MC);

	// 左端：插件栏（从里往外排；没有插件时三个空槽）。每个效果一个六边形，有时限的外面一圈剩余时间，下面小点是层数
	{
		const AMechGameState* FxGS = GetWorld()->GetGameState<AMechGameState>();
		const float ServerNow = FxGS ? FxGS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		const float FY = H - 60.f * UI;
		const float HR = 11.f * UI;
		auto Hexagon = [&](float X, const FLinearColor& C, float Thick) {
			TArray<FVector2f> Hex;
			for (int32 k = 0; k <= 6; ++k)
			{
				const float A = PI / 6.f + k * PI / 3.f;
				Hex.Add(FVector2f(X + HR * FMath::Cos(A), FY + HR * FMath::Sin(A)));
			}
			Polyline(Hex, C, Thick);
		};
		int32 Slot = 0;
		if (const UMechEffectComponent* Fx = Mech->GetEffects())
		{
			for (const FMechEffect& E : Fx->GetEffects())
			{
				const MechEffects::FDef* Def = MechEffects::Find(E.Id);
				if (!Def)
				{
					continue;
				}
				const float FX = CX - 390.f * UI - Slot++ * 38.f * UI;
				PluginIcon(FX, FY, HR, E.Id, 1.f);
				if (E.EndTime >= 0.f && E.Duration > 0.f)
				{
					Arc(FX, FY, 15.f * UI, 0.f, 2.f * PI * FMath::Clamp((E.EndTime - ServerNow) / E.Duration, 0.f, 1.f), Fade(Def->Color, 0.8f), 1.5f);
				}
				for (int32 s = 0; s < E.Stacks; ++s)
				{
					DrawRect(Def->Color, FX - (E.Stacks - 1) * 3.f * UI + s * 6.f * UI - 1.5f * UI, FY + 21.f * UI, 3.f * UI, 3.f * UI);
				}
			}
		}
		for (; Slot < 3 && Th.bEmptySlots; ++Slot)
		{
			Hexagon(CX - 390.f * UI - Slot * 38.f * UI, Faint, 1.f);
		}
	}

	// 右端：姿态（枪 / 刀两个图标，当前的亮；切换中下面走一道线）
	const float SX = CX + 350.f * UI;
	const float SY = H - 70.f * UI;
	const FLinearColor GunC = bBlade ? Faint : Ink;
	const FLinearColor BladeC = bBlade ? Ink : Faint;
	EquipmentIcon(GunIcon, SX, SY - 3.f * UI, 56.f, 18.f, GunC);
	EquipmentIcon(BladeIcon, SX + 64.f * UI, SY - 3.f * UI, 56.f, 18.f, BladeC);
	DrawRect(Track, SX, BarY, 120.f * UI, BarT);
	const float Switch = Combat->GetSwitchProgress();
	if (Switch >= 0.f)
	{
		DrawRect(Accent, SX, BarY, 120.f * UI * Switch, BarT);
	}
	else
	{
		DrawRect(Accent, bBlade ? SX + 64.f * UI : SX, BarY, 56.f * UI, BarT);
	}
}

// ---------------------------------------------------------------- 准星：射击是点 + 四根刻度，近战是菱形；周围不放别的
void AMechHUD::DrawReticle(AMechCharacter* Mech)
{
	if (Mech->GetHealth()->IsDead())
	{
		return;
	}
	const float CX = Canvas->SizeX * 0.5f;
	const float CY = Canvas->SizeY * 0.5f;
	UMechCombatComponent* Combat = Mech->GetCombat();
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bBlade = Combat->GetStance() == EMechStance::Blade;

	if (!bBlade && ScopeBlend < 0.5f)
	{
		// 右键瞄准时刻度收紧；装填时刻度变橙（余光就能看到）；蓄力枪按住时外面一圈细环（满了变橙）
		const bool bAim = Combat->IsAiming();
		const FLinearColor C = Combat->IsReloading() ? Warn : Ink;
		const float Gap = (bAim ? 5.f : 9.f) * UI;
		const float Len = (bAim ? 6.f : 8.f) * UI;
		DrawLine(CX - Gap - Len, CY, CX - Gap, CY, C, 1.5f);
		DrawLine(CX + Gap, CY, CX + Gap + Len, CY, C, 1.5f);
		DrawLine(CX, CY - Gap - Len, CX, CY - Gap, C, 1.5f);
		DrawLine(CX, CY + Gap, CX, CY + Gap + Len, C, 1.5f);
		DrawRect(C, CX - 1.f * UI, CY - 1.f * UI, 2.f * UI, 2.f * UI);
		const float Charge = Combat->GetCharge();
		if (Combat->GetGun().ChargeTime > 0.f && Charge > 0.f)
		{
			Arc(CX, CY, 22.f * UI, 0.f, 2.f * PI, Track, 1.f);
			Arc(CX, CY, 22.f * UI, 0.f, 2.f * PI * FMath::Min(Charge, 1.f), Charge >= 1.f ? Warn : Accent, 2.f);
		}
	}
	else if (bBlade)
	{
		// 菱形；目标进突进距离时上下两道短弧亮起；按住左键时蓄力环（重斩 / 匕首，满了变橙）；格挡时两侧弧，挡下瞬间闪白，破防红
		const float R = 9.f * UI;
		const FLinearColor C = Combat->IsMeleeActive() ? Accent : Ink;
		SoftLine(CX, CY - R, CX + R, CY, C, 1.5f);
		SoftLine(CX + R, CY, CX, CY + R, C, 1.5f);
		SoftLine(CX, CY + R, CX - R, CY, C, 1.5f);
		SoftLine(CX - R, CY, CX, CY - R, C, 1.5f);
		AMechCharacter* Target = Combat->GetTarget();
		if (Target && MechLoop::Dist(GetWorld(), Target->GetActorLocation(), Mech->GetActorLocation()) <= Combat->GetBladeRange())
		{
			Arc(CX, CY, 40.f * UI, -0.5f, 0.5f, Accent, 2.f);
			Arc(CX, CY, 40.f * UI, PI - 0.5f, PI + 0.5f, Accent, 2.f);
		}
		const float Hold = Combat->GetMeleeHoldFraction();
		if (Hold > 0.f)
		{
			Arc(CX, CY, 22.f * UI, 0.f, 2.f * PI, Track, 1.f);
			Arc(CX, CY, 22.f * UI, 0.f, 2.f * PI * Hold, Hold >= 1.f ? Warn : Accent, 2.5f);
		}
		if (Combat->IsGuarding() || Combat->IsGuardBroken())
		{
			const bool bBlockFlash = Combat->GetSinceBlock() < 0.25f;
			const FLinearColor GC = Combat->IsGuardBroken() ? Enemy : bBlockFlash ? FLinearColor::White : Fade(Ink, 0.8f);
			const float GR = (bBlockFlash ? 62.f : 56.f) * UI;
			Arc(CX, CY, GR, PI * 0.5f + 0.4f, PI * 1.5f - 0.4f, GC, 2.5f);
			Arc(CX, CY, GR, -PI * 0.5f + 0.4f, PI * 0.5f - 0.4f, GC, 2.5f);
		}
	}

	// 格挡反馈（10-30）：刀被挡住 / 弹反 / 破防时，挡的和砍的双方准星周围都闪一下，0.45 秒（只有图形，没有字）：
	//  挡住 = 两侧一对竖杠往外弹开（冷白）；弹反 = 菱形一圈扩散 + 四根刺（挡的一方金色，被弹的一方橙红）；
	//  破防 = 断开的圆环 + 一道斜划（被破的一方红，砸破别人的一方橙白）
	{
		const float Age = Combat->GetBlockFxAge();
		if (Age >= 0.f && Age < 0.45f)
		{
			const float T = Age / 0.45f;
			const float A = 1.f - T * T;
			const bool bMine = Combat->IsBlockFxDefender();
			const int32 Kind = Combat->GetBlockFxKind();
			if (Kind == 1)
			{
				const FLinearColor C = Fade(bMine ? Gold : Enemy, A);
				const float R = FMath::Lerp(24.f, 74.f, FMath::Sqrt(T)) * UI;
				SoftLine(CX, CY - R, CX + R, CY, C, 2.5f);
				SoftLine(CX + R, CY, CX, CY + R, C, 2.5f);
				SoftLine(CX, CY + R, CX - R, CY, C, 2.5f);
				SoftLine(CX - R, CY, CX, CY - R, C, 2.5f);
				const float In = R + 6.f * UI, Out = R + 18.f * UI;
				DrawLine(CX, CY - In, CX, CY - Out, C, 2.f);
				DrawLine(CX, CY + In, CX, CY + Out, C, 2.f);
				DrawLine(CX - In, CY, CX - Out, CY, C, 2.f);
				DrawLine(CX + In, CY, CX + Out, CY, C, 2.f);
			}
			else if (Kind == 2)
			{
				const FLinearColor C = Fade(bMine ? Enemy : FLinearColor(1.f, 0.7f, 0.45f), A);
				const float R = 50.f * UI;
				const float Shake = (1.f - T) * 3.f * UI * FMath::Sin(Age * 90.f);
				Arc(CX + Shake, CY, R, -PI * 0.5f + 0.35f, PI * 0.5f - 0.15f, C, 3.f);
				Arc(CX + Shake, CY, R, PI * 0.5f + 0.15f, PI * 1.5f - 0.35f, C, 3.f);
				SoftLine(CX - 22.f * UI + Shake, CY + 22.f * UI, CX + 22.f * UI + Shake, CY - 22.f * UI, C, 3.f);
			}
			else
			{
				const FLinearColor C = Fade(FLinearColor(0.85f, 0.93f, 1.f), A);
				const float X = FMath::Lerp(30.f, 46.f, FMath::Sqrt(T)) * UI;
				const float H = 14.f * UI;
				DrawLine(CX - X, CY - H, CX - X, CY + H, C, 3.f);
				DrawLine(CX + X, CY - H, CX + X, CY + H, C, 3.f);
				DrawLine(CX - X - 6.f * UI, CY - H * 0.6f, CX - X - 6.f * UI, CY + H * 0.6f, Fade(C, 0.5f * A), 1.5f);
				DrawLine(CX + X + 6.f * UI, CY - H * 0.6f, CX + X + 6.f * UI, CY + H * 0.6f, Fade(C, 0.5f * A), 1.5f);
			}
		}
	}

	// 重击（重斩砍中，10-30 晚玩家：重击的反馈要明显）：准星四个斜角各一道粗短的楔形往外甩、0.4 秒淡掉，暖白色；
	// 和格挡（竖杠）/ 弹反（菱形）/ 破防（断环）分开。配双持手枪时补满弹匣再叠一个「装满」：准星下方两排短格从中间往两边一下填满、金色
	{
		const float HitNow = GetWorld()->GetTimeSeconds();
		const float HAge = HitNow - Combat->GetLastHeavyHitTime();
		if (HAge >= 0.f && HAge < 0.4f)
		{
			const float T = HAge / 0.4f;
			const FLinearColor C = Fade(FLinearColor(1.f, 0.93f, 0.82f), 1.f - T * T);
			const float In = FMath::Lerp(14.f, 30.f, FMath::Sqrt(T)) * UI;
			const float Out = In + FMath::Lerp(26.f, 16.f, T) * UI;
			for (int32 k = 0; k < 4; ++k)
			{
				const float SX = (k & 1) ? 1.f : -1.f;
				const float SY = (k & 2) ? 1.f : -1.f;
				DrawLine(CX + SX * In * 0.707f, CY + SY * In * 0.707f, CX + SX * Out * 0.707f, CY + SY * Out * 0.707f, C, 5.f);
			}
		}
		const float RAge = HitNow - Combat->GetLastRefillTime();
		if (RAge >= 0.f && RAge < 0.9f)
		{
			const float A = 1.f - FMath::Max(0.f, RAge - 0.5f) / 0.4f;
			const float Fill = FMath::Clamp(RAge / 0.18f, 0.f, 1.f);
			const int32 N = 6;
			for (int32 Row = 0; Row < 2; ++Row)
			{
				const float Y0 = CY + (48.f + Row * 8.f) * UI;
				for (int32 i = 0; i < N; ++i)
				{
					// 从中间往两边亮起
					const float Order = FMath::Abs(i - (N - 1) * 0.5f) / (N * 0.5f);
					const bool bLit = Order <= Fill;
					DrawRect(Fade(bLit ? Gold : Track, A), CX + (i - N * 0.5f) * 9.f * UI + 1.f * UI, Y0, 7.f * UI, 4.f * UI);
				}
			}
		}
	}

	// 拼刀（11-01）：准星上两道长刀交叉，像剪刀一样张开；中间一圈火星往外崩，左右一对箭头朝外甩（两人被弹开）。0.55 秒，白心橙边，只有图形没有字。
	// 和格挡（两侧竖杠）、弹反（菱形）、破防（断环 + 斜划）、重击（四角短楔形）分开；不分赢输，只有一种
	{
		const float CAge = Combat->GetClashFxAge();
		if (CAge >= 0.f && CAge < 0.55f)
		{
			const float T = CAge / 0.55f;
			const float A = 1.f - T * T;
			const float Burst = FMath::Sqrt(T);
			const FLinearColor Core = Fade(FLinearColor(1.f, 0.97f, 0.9f), A);
			const FLinearColor Edge = Fade(FLinearColor(1.f, 0.55f, 0.18f), 0.6f * A);
			const float Open = FMath::DegreesToRadians(FMath::Lerp(14.f, 40.f, Burst));
			const float BX = FMath::Sin(Open) * 76.f * UI;
			const float BY = FMath::Cos(Open) * 76.f * UI;
			SoftLine(CX - BX, CY + BY, CX + BX, CY - BY, Edge, 7.f);
			SoftLine(CX + BX, CY + BY, CX - BX, CY - BY, Edge, 7.f);
			SoftLine(CX - BX, CY + BY, CX + BX, CY - BY, Core, 3.f);
			SoftLine(CX + BX, CY + BY, CX - BX, CY - BY, Core, 3.f);
			for (int32 k = 0; k < 8; ++k)
			{
				const float Ang = (k + 0.5f) * PI / 4.f;
				const float In = FMath::Lerp(10.f, 44.f, Burst) * UI;
				const float Out = In + FMath::Lerp(20.f, 6.f, T) * UI;
				DrawLine(CX + FMath::Cos(Ang) * In, CY + FMath::Sin(Ang) * In, CX + FMath::Cos(Ang) * Out, CY + FMath::Sin(Ang) * Out, Core, 2.f);
			}
			const float AX = FMath::Lerp(34.f, 112.f, Burst) * UI;
			const float AH = 15.f * UI;
			for (const float Dir : {-1.f, 1.f})
			{
				SoftLine(CX + Dir * AX, CY, CX + Dir * (AX - AH), CY - AH, Core, 3.f);
				SoftLine(CX + Dir * AX, CY, CX + Dir * (AX - AH), CY + AH, Core, 3.f);
			}
		}
	}

	// 钩爪：准星指的地方够得着时，准星右下一个小爪形（地形青、敌人红、按住右键锁定队友时蓝）
	if (const UMechShoulderComponent* Sh = Mech->GetShoulder())
	{
		if (Sh->GetAimHint() > 0 && Sh->GetReady() >= 1.f && ScopeBlend < 0.5f)
		{
			const FLinearColor HC = Sh->GetAimHint() == 3 ? Ally : Sh->GetAimHint() == 2 ? Enemy : Fade(Accent, 0.85f);
			const float HX = CX + 20.f * UI, HY = CY + 20.f * UI;
			SoftLine(HX, HY + 8.f * UI, HX, HY + 2.f * UI, HC, 1.5f);
			SoftLine(HX, HY + 2.f * UI, HX - 4.f * UI, HY - 2.f * UI, HC, 1.5f);
			SoftLine(HX, HY + 2.f * UI, HX + 4.f * UI, HY - 2.f * UI, HC, 1.5f);
		}
		// 钩爪救队友（按住右键、钩爪就绪）：锁定的队友身上一对蓝色括号 + 底下一个朝自己的小箭头（按 E 会把他拉过来）
		if (AMechCharacter* Pal = Sh->GetAllyAim(); Pal && Sh->GetReady() >= 1.f && PlayerOwner)
		{
			FVector2D P;
			if (UGameplayStatics::ProjectWorldToScreen(PlayerOwner, MechLoop::Near(GetWorld(), Pal->GetMechMesh()->VisualPivot(), Mech->GetActorLocation()) - FVector(0.f, 0.f, 60.f), P))
			{
				const float D = MechLoop::Dist(GetWorld(), Pal->GetActorLocation(), Mech->GetActorLocation());
				const float PH = FMath::Clamp(9000.f / FMath::Max(D, 100.f), 28.f, 90.f) * UI * 1.25f;
				const float Pulse = 0.7f + 0.3f * FMath::Sin(Now * 10.f);
				Brackets(P.X, P.Y, PH, 12.f * UI, Fade(Ally, Pulse), 2.f);
				const float AY = P.Y + PH + 10.f * UI;
				Triangle(FVector2D(P.X - 7.f * UI, AY), FVector2D(P.X + 7.f * UI, AY), FVector2D(P.X, AY + 8.f * UI), Fade(Ally, Pulse));
			}
		}
		// 火炮架炮：准星外一圈架炮进度（满了变橙），架好后外面一对大括号（有锁定目标时红）
		if (Sh->IsArtilleryHeld())
		{
			const float D = Sh->GetArtilleryDeploy();
			const float AR = 46.f * UI;
			Arc(CX, CY, AR, 0.f, 2.f * PI, Track, 1.f);
			Arc(CX, CY, AR, -PI * 0.5f, -PI * 0.5f + 2.f * PI * D, D >= 1.f ? Warn : Accent, 2.f);
			if (D >= 1.f)
			{
				const FLinearColor RC = Combat->GetTarget() ? Enemy : Ink;
				Brackets(CX, CY, 78.f * UI, 16.f * UI, RC, 1.5f);
				DrawLine(CX - 30.f * UI, CY, CX - 12.f * UI, CY, RC, 1.5f);
				DrawLine(CX + 12.f * UI, CY, CX + 30.f * UI, CY, RC, 1.5f);
				DrawLine(CX, CY + 12.f * UI, CX, CY + 30.f * UI, RC, 1.5f);
			}
		}
		// 护盾开着：准星上方一道弧，随剩余时间缩短
		if (Sh->IsShieldUp())
		{
			const float L = Sh->GetShieldLeft();
			Arc(CX, CY, 60.f * UI, -PI * 0.5f - 0.9f * L, -PI * 0.5f + 0.9f * L, Fade(Accent, 0.9f), 2.f);
		}
		// 被 EMP 打中：准星周围几道一闪一闪的雪花横纹
		if (Sh->IsEmped())
		{
			for (int32 k = 0; k < 7; ++k)
			{
				const float NY = CY + FMath::FRandRange(-90.f, 90.f) * UI;
				const float NX = CX + FMath::FRandRange(-160.f, 100.f) * UI;
				DrawRect(Fade(Ink, FMath::FRandRange(0.15f, 0.5f)), NX, NY, FMath::FRandRange(20.f, 70.f) * UI, 1.5f * UI);
			}
		}
		// 被网住：准星外一圈菱形网格（挣扎时抖一下），下面一道剩余时间
		if (Sh->IsNetted())
		{
			const float Shake = FMath::Clamp(1.f - (Now - Sh->GetLastStruggleTime()) / 0.15f, 0.f, 1.f) * 5.f * UI;
			const float OX = FMath::FRandRange(-Shake, Shake), OY = FMath::FRandRange(-Shake, Shake);
			const float Cell = 34.f * UI, Rad = 150.f * UI;
			const FLinearColor NC = Fade(Warn, 0.55f);
			for (int32 s = -1; s <= 1; s += 2)
			{
				// 两组斜线（沿 (1, ±1) 方向），间距 Cell，裁到半径 Rad 的圆里
				const FVector2D D(0.7071f, s * 0.7071f), N(0.7071f, -s * 0.7071f);
				for (int32 k = -6; k <= 6; ++k)
				{
					const float C0 = k * Cell;
					if (FMath::Abs(C0) >= Rad)
					{
						continue;
					}
					const float H = FMath::Sqrt(Rad * Rad - C0 * C0);
					const FVector2D A = N * C0 - D * H, B = N * C0 + D * H;
					SoftLine(CX + OX + A.X, CY + OY + A.Y, CX + OX + B.X, CY + OY + B.Y, NC, 1.2f);
				}
			}
			const float Left = Sh->GetNetLeft();
			DrawRect(Track, CX - 60.f * UI, CY + Rad + 16.f * UI, 120.f * UI, 3.f * UI);
			DrawRect(Warn, CX - 60.f * UI, CY + Rad + 16.f * UI, 120.f * UI * Left, 3.f * UI);
		}
	}

	// 命中提示：斜叉，刚打中时先放大再收回；导弹、刀更大更久，直击橙，击毁红且更大
	const float SinceHit = Now - Combat->GetLastHitTime();
	const bool bHeavyHit = Combat->GetLastHitKind() != EMechHitKind::Bullet;
	const float HitShow = Combat->WasLastHitKill() ? 0.45f : bHeavyHit ? 0.3f : 0.18f;
	if (SinceHit < HitShow)
	{
		const bool bKill = Combat->WasLastHitKill();
		const bool bDirect = Combat->WasLastHitDirect();
		FLinearColor C = bKill ? Enemy : bDirect ? Warn : Ink;
		C.A *= FMath::Clamp((HitShow - SinceHit) / 0.08f, 0.f, 1.f);
		const float Pop = (1.f + 0.6f * FMath::Clamp(1.f - SinceHit / 0.07f, 0.f, 1.f)) * (bHeavyHit ? 1.3f : 1.f);
		const float R0 = (bKill ? 14.f : 9.f) * UI * Pop;
		const float R1 = (bKill ? 30.f : bDirect ? 24.f : 18.f) * UI * Pop;
		for (int32 sx = -1; sx <= 1; sx += 2)
		{
			for (int32 sy = -1; sy <= 1; sy += 2)
			{
				SoftLine(CX + sx * R0, CY + sy * R0, CX + sx * R1, CY + sy * R1, C, bKill ? 3.f : 2.f);
			}
		}
		if (bKill)
		{
			Arc(CX, CY, R1 + 8.f * UI, 0.f, 2.f * PI, Fade(C, 0.6f), 1.5f);
		}
	}
}

// ---------------------------------------------------------------- 狙击镜：右键开镜时画（镜框从外往里收进来）
void AMechHUD::DrawScope(AMechCharacter* Mech)
{
	UMechCombatComponent* Combat = Mech->GetCombat();
	const MechWeapons::FGun& G = Combat->GetGun();
	const bool bScoped = Combat->IsAiming() && G.EnergyPerShot > 0.f && Combat->GetStance() == EMechStance::Gun;
	ScopeBlend = FMath::FInterpConstantTo(ScopeBlend, bScoped ? 1.f : 0.f, GetWorld()->GetDeltaSeconds(), 7.f);
	if (ScopeBlend <= 0.01f)
	{
		return;
	}
	const float A = ScopeBlend;
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;
	const float R = H * 0.44f * FMath::Lerp(1.25f, 1.f, A);
	const float Far = FMath::Sqrt(W * W + H * H);

	// 镜框外压暗（圆环一直铺到屏幕外），镜框内边缘一圈渐暗
	TArray<FCanvasUVTri> Tris;
	const int32 N = FMath::Clamp(FMath::CeilToInt(2.f * PI * R / 4.f), 120, 1200);
	auto Ring = [&](float R0, float R1, const FLinearColor& C0, const FLinearColor& C1) {
		for (int32 i = 0; i < N; ++i)
		{
			const float A0 = 2.f * PI * i / N;
			const float A1 = 2.f * PI * (i + 1) / N;
			const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
			const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
			const FVector2D C(CX, CY);
			FCanvasUVTri T;
			T.V0_Pos = C + D0 * R0, T.V1_Pos = C + D0 * R1, T.V2_Pos = C + D1 * R1;
			T.V0_Color = C0, T.V1_Color = C1, T.V2_Color = C1;
			Tris.Add(T);
			T.V0_Pos = C + D0 * R0, T.V1_Pos = C + D1 * R1, T.V2_Pos = C + D1 * R0;
			T.V0_Color = C0, T.V1_Color = C1, T.V2_Color = C0;
			Tris.Add(T);
		}
	};
	const FLinearColor Black(0.f, 0.005f, 0.01f, 0.94f * A);
	Ring(R + 1.5f, Far, Black, Black);
	Ring(R - 1.5f, R + 1.5f, FLinearColor(0.f, 0.005f, 0.01f, 0.f), Black);  // 镜框边缘羽化，不然一圈台阶
	Ring(R * 0.84f, R, FLinearColor(0.f, 0.005f, 0.01f, 0.f), FLinearColor(0.f, 0.005f, 0.01f, 0.6f * A));
	AddTris(Tris);
	Arc(CX, CY, R, 0.f, 2.f * PI, Fade(Ink, 0.55f * A), 2.f);
	Arc(CX, CY, R * 0.985f, 0.f, 2.f * PI, Fade(Faint, 0.6f * A), 1.f);

	// 十字：中间细线，外段粗（像镜里的立柱）；刻度每 1/12 半径一格，每 3 格长一点
	const FLinearColor Line = Fade(Ink, 0.9f * A);
	const FLinearColor Post(0.02f, 0.03f, 0.04f, 0.85f * A);
	const float Gap = 16.f * UI;
	const float Inner = R * 0.58f;
	for (int32 s = -1; s <= 1; s += 2)
	{
		DrawLine(CX + s * Gap, CY, CX + s * Inner, CY, Line, 1.2f);
		DrawLine(CX + s * Inner, CY, CX + s * R, CY, Post, 5.f * UI);
		DrawLine(CX, CY + s * Gap, CX, CY + s * Inner, Line, 1.2f);
		DrawLine(CX, CY + s * Inner, CX, CY + s * R, Post, 5.f * UI);
		for (int32 k = 1; k <= 6; ++k)
		{
			const float D = Gap + (Inner - Gap) * k / 7.f;
			const float L = (k % 3 == 0 ? 9.f : 5.f) * UI;
			DrawLine(CX + s * D, CY - L, CX + s * D, CY + L, Line, 1.f);
			DrawLine(CX - L, CY + s * D, CX + L, CY + s * D, Line, 1.f);
		}
	}
	DrawRect(Fade(Accent, A), CX - 1.5f * UI, CY - 1.5f * UI, 3.f * UI, 3.f * UI);

	// 上膛：开一枪后中心一圈从空到满（狙击枪两发之间 1.4 秒）
	const float Ready = FMath::Clamp((GetWorld()->GetTimeSeconds() - Combat->GetLastShotTime()) / FMath::Max(G.Interval, 0.01f), 0.f, 1.f);
	if (Ready < 1.f)
	{
		Arc(CX, CY, 26.f * UI, 0.f, 2.f * PI * Ready, Fade(Accent, A), 2.f);
	}

	// 读数：左下距离（开镜时子弹打准星，所以量准星打到的地方），右下还能打几发（格子）
	const float Meters = FVector::Dist(Combat->GetFreeAimPoint(), Mech->GetActorLocation()) / 100.f;
	const float TY = CY + R * 0.62f;
	Text(Meters > 1500.f ? FString(TEXT("---")) : FString::Printf(TEXT("%.0f m"), Meters), CX - R * 0.42f, TY, 24.f, Fade(Ink, A), 0.5f);
	const UMechMovementComponent* Move = Mech->GetMechMovement();
	const int32 Shots = FMath::FloorToInt(Move->Energy / G.EnergyPerShot);
	const int32 MaxShots = FMath::Max(1, FMath::FloorToInt(Move->MaxEnergy / G.EnergyPerShot));
	const float PipW = 22.f * UI;
	const float PX = CX + R * 0.42f - (MaxShots * (PipW + 4.f * UI)) * 0.5f;
	for (int32 i = 0; i < MaxShots; ++i)
	{
		DrawRect(Fade(i < Shots ? Accent : Track, A), PX + i * (PipW + 4.f * UI), TY + 10.f * UI, PipW, 4.f * UI);
	}
}
// ---------------------------------------------------------------- 伤害飘字：命中点弹出数字往上飘；头部金色大字，四肢灰，失衡直击橙，击毁红
void AMechHUD::DrawPopups(AMechCharacter* Mech)
{
	if (!PlayerOwner)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	for (const FMechDamagePopup& P : Mech->GetCombat()->GetDamagePopups())
	{
		const float Age = Now - P.Time;
		FVector2D S;
		if (Age < 0.f || Age > 1.1f || !UGameplayStatics::ProjectWorldToScreen(PlayerOwner, P.At, S))
		{
			continue;
		}
		const bool bHead = P.Part == EMechHitPart::Head;
		const bool bBig = bHead || P.bDirect || P.bKill || P.Kind != EMechHitKind::Bullet;
		FLinearColor C = P.bKill ? Enemy : P.bDirect ? Warn : bHead ? Gold : P.Part == EMechHitPart::Limb ? Dim : Ink;
		const float Alpha = FMath::Clamp((1.1f - Age) / 0.35f, 0.f, 1.f);
		C.A *= Alpha;
		const float Pop = 1.f + 0.45f * FMath::Clamp(1.f - Age / 0.09f, 0.f, 1.f);
		const float Size = (bBig ? 26.f : 18.f) * Pop;
		const float X = S.X + (26.f + Age * 18.f) * UI;
		const float Y = S.Y - (18.f + Age * 70.f) * UI;
		const FString Num = FString::Printf(TEXT("%.0f"), P.Damage);
		Text(Num, X + 2.f * UI, Y + 2.f * UI, Size, FLinearColor(0.f, 0.f, 0.f, 0.6f * Alpha), 0.5f);
		Text(Num, X, Y, Size, C, 0.5f);
	}
}
// ---------------------------------------------------------------- 锁定框：跟着目标走，导弹锁定进度让框收紧，锁上变红；目标失衡时框闪橙
void AMechHUD::DrawLockBox(AMechCharacter* Mech)
{
	UMechCombatComponent* Combat = Mech->GetCombat();
	AMechCharacter* Target = Combat->GetTarget();
	// 被烟幕干扰：准星外画一个抖动的残缺框和几道雪花横纹（看得出是被干扰了，不是没瞄准）。
	// 画在准星上、不画在敌人身上：烟里 / 烟后的敌人不能被 HUD 标出位置（10-30 烟雾弹加强）
	if (!Target && PlayerOwner && !Mech->GetHealth()->IsDead())
	{
		if (Combat->GetJammedTarget())
		{
			const FVector2D P(Canvas->SizeX * 0.5f, Canvas->SizeY * 0.5f);
			{
				const float JH = 46.f * UI;
				const FLinearColor JC = Fade(Warn, FMath::FRandRange(0.35f, 0.85f));
				const float JX = P.X + FMath::FRandRange(-4.f, 4.f) * UI;
				const float JY = P.Y + FMath::FRandRange(-3.f, 3.f) * UI;
				// 四个角只画两个，每帧随机换（像信号断断续续）
				const int32 Pick = FMath::RandRange(0, 3);
				for (int32 c = 0; c < 4; ++c)
				{
					if (c == Pick || c == (Pick + 2) % 4)
					{
						continue;
					}
					const float SX = (c & 1) ? 1.f : -1.f;
					const float SY = (c & 2) ? 1.f : -1.f;
					DrawLine(JX + SX * JH, JY + SY * JH, JX + SX * (JH - 12.f * UI), JY + SY * JH, JC, 1.5f);
					DrawLine(JX + SX * JH, JY + SY * JH, JX + SX * JH, JY + SY * (JH - 12.f * UI), JC, 1.5f);
				}
				for (int32 k = 0; k < 4; ++k)
				{
					const float NY = JY + FMath::FRandRange(-JH, JH);
					const float NW = FMath::FRandRange(0.2f, 0.9f) * JH;
					DrawRect(Fade(Ink, FMath::FRandRange(0.15f, 0.45f)), JX - JH + FMath::FRandRange(0.f, JH), NY, NW, 1.5f * UI);
				}
			}
		}
	}
	// 导弹锁定进度只在配装是导弹时显示（别的肩部武器不用锁定）
	const float MissileLock = Mech->GetLoadout().Shoulder == EMechShoulder::Missiles ? Combat->GetMissileLock() : 0.f;
	const bool bLocked = Target && MissileLock >= 1.f;
	if (Target && PlayerOwner && !Mech->GetHealth()->IsDead())
	{
		FVector2D P;
		// 10-31 联机抖动：框在画面上的机体（网格）上，不在胶囊上——客户端上别人的胶囊每来一个包跳一下，网格是按时间回放的、比胶囊晚一点
		const FVector At = MechLoop::Near(GetWorld(), Target->GetMechMesh()->VisualPivot(), Mech->GetActorLocation()) - FVector(0.f, 0.f, 80.f);  // 老街循环：框在离自己最近的那份上
		if (UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P))
		{
			UMechHealthComponent* TH = Target->GetHealth();
			const float Now = GetWorld()->GetTimeSeconds();
			const float Dist = FVector::Dist(At, Mech->GetActorLocation());
			const float Half = FMath::Clamp(9000.f / FMath::Max(Dist, 100.f), 20.f, 90.f) * UI;
			const float BoxHalf = Half * (1.6f - 0.6f * MissileLock);
			const FLinearColor C = bLocked ? Enemy : TH->IsStaggered() && FMath::Fmod(Now, 0.3f) < 0.15f ? Warn : Fade(Ink, 0.9f);
			Brackets(P.X, P.Y, BoxHalf, 12.f * UI, C, 1.5f);
			if (bLocked)
			{
				Brackets(P.X, P.Y, BoxHalf + 6.f * UI, 8.f * UI, C, 1.f);
			}
			// 框下面：目标 AP（红）和失衡值（橙，失衡时整条红），再下面距离
			const float BW = 120.f * UI;
			const float TX = P.X - BW * 0.5f;
			const float TY = P.Y + BoxHalf + 10.f * UI;
			DrawRect(Fade(Enemy, 0.2f), TX, TY, BW, 3.f * UI);
			DrawRect(Enemy, TX, TY, BW * TH->GetAPFraction(), 3.f * UI);
			DrawRect(Fade(Warn, 0.2f), TX, TY + 6.f * UI, BW, 2.f * UI);
			DrawRect(TH->IsStaggered() ? Enemy : Warn, TX, TY + 6.f * UI, BW * (TH->IsStaggered() ? 1.f : TH->GetStaggerFraction()), 2.f * UI);
			Text(FString::Printf(TEXT("%.0fm"), Dist / 100.f), P.X, TY + 12.f * UI, 13.f, C, 0.5f);
			if (Target->IsTrainingDummy() && Now - Target->GetDummyLastHitTime() < 6.f)
			{
				// 木桩读数：上一击 / 这一轮 / 每秒
				Text(FString::Printf(TEXT("%.0f   %.0f   %.0f/s"), Target->GetDummyLastHit(), Target->GetDummyStreakDamage(), Target->GetDummyDPS()),
					P.X, TY + 34.f * UI, 15.f, Ink, 0.5f);
			}
		}
	}
	if (bLocked && !bWasLocked && Mech->IsPlayerControlled() && LockSound)
	{
		UGameplayStatics::PlaySound2D(this, LockSound, 0.5f);
	}
	bWasLocked = bLocked;
}

// ---------------------------------------------------------------- 敌我标记（屏幕内）
void AMechHUD::DrawMarkers(AMechCharacter* Mech)
{
	if (!PlayerOwner)
	{
		return;
	}
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* Other = *It;
		if (Other == Mech || Other->GetHealth()->IsDead())
		{
			continue;
		}
		FVector2D P;
		if (!UGameplayStatics::ProjectWorldToScreen(PlayerOwner, MechLoop::Near(GetWorld(), Other->GetMechMesh()->VisualPivot(), Mech->GetActorLocation()) + FVector(0.f, 0.f, 130.f), P))  // 头顶名字跟着画面上的机体（同上）
		{
			continue;
		}
		const bool bAlly = Other->GetTeam() == Mech->GetTeam();
		const FLinearColor C = bAlly ? Ally : Enemy;
		const float S = 7.f * UI;
		Triangle(FVector2D(P.X - S, P.Y - S), FVector2D(P.X + S, P.Y - S), FVector2D(P.X, P.Y + S * 0.6f), Fade(C, 0.85f));
		if (bAlly)
		{
			DrawRect(Fade(C, 0.2f), P.X - 24.f * UI, P.Y - 12.f * UI, 48.f * UI, 2.f * UI);
			DrawRect(C, P.X - 24.f * UI, P.Y - 12.f * UI, 48.f * UI * Other->GetHealth()->GetAPFraction(), 2.f * UI);
		}
	}
}
// ---------------------------------------------------------------- 屏幕外的敌人：屏幕边缘的箭头 + 距离
void AMechHUD::DrawOffscreen(AMechCharacter* Mech)
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PlayerOwner->PlayerCameraManager->GetCameraRotation();
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* Other = *It;
		if (Other->GetTeam() == Mech->GetTeam() || Other->GetHealth()->IsDead())
		{
			continue;
		}
		const FVector At = MechLoop::Near(GetWorld(), Other->GetActorLocation(), CamLoc);  // 老街循环
		FVector2D P;
		if (UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P) && P.X > 0.f && P.X < W && P.Y > 0.f && P.Y < H)
		{
			continue;
		}
		const FVector Local = CamRot.UnrotateVector(At - CamLoc);
		FVector2D Dir(Local.Y, -Local.Z * 0.5f);
		if (!Dir.Normalize())
		{
			Dir = FVector2D(0.f, 1.f);
		}
		const FVector2D Center(W * 0.5f, H * 0.5f);
		const FVector2D Pos = Center + FVector2D(Dir.X * W * 0.42f, Dir.Y * H * 0.38f);
		const FVector2D Perp(-Dir.Y, Dir.X);
		const float S = 11.f * UI;
		Triangle(Pos + Dir * S * 1.4f, Pos - Dir * S * 0.5f + Perp * S, Pos - Dir * S * 0.5f - Perp * S, Fade(Enemy, 0.85f));
		const FVector2D Label = Pos - Dir * 26.f * UI;
		Text(FString::Printf(TEXT("%.0fm"), FVector::Dist(At, Mech->GetActorLocation()) / 100.f), Label.X, Label.Y - 8.f * UI, 12.f, Fade(Enemy, 0.9f), 0.5f);
	}
}
// ---------------------------------------------------------------- 受击：屏幕四边一闪 + 受击方向的弧
void AMechHUD::DrawDamage(AMechCharacter* Mech)
{
	const float Since = GetWorld()->GetTimeSeconds() - Mech->GetLastDamagedTime();
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager || (Since > 1.4f && DamageArcs.IsEmpty()))
	{
		return;
	}
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	// 10-30 玩家：被击中的反馈再强一点，分档（UMechHealthComponent::GetLastHurtTier：0 子弹 / 1 重击 / 2 直击、失衡、很重）：
	// 越重四边红框越厚越久、方向弧越宽；最重一档再加四角向里收的红色折角。都在屏幕边缘和准星外圈，不挡准星
	const UMechHealthComponent* HP = Mech->GetHealth();
	const int32 Tier = GetWorld()->GetTimeSeconds() - HP->GetLastHurtTime() < 1.3f ? FMath::Clamp(HP->GetLastHurtTier(), 0, 2) : 0;
	static const float EdgeTime[3] = {0.35f, 0.5f, 0.7f};
	static const float EdgeAlpha[3] = {0.26f, 0.38f, 0.5f};
	static const float EdgeThick[3] = {14.f, 20.f, 28.f};
	if (Since < EdgeTime[Tier])
	{
		const FLinearColor Red(0.9f, 0.05f, 0.02f, EdgeAlpha[Tier] * (1.f - Since / EdgeTime[Tier]));
		const float T = EdgeThick[Tier] * UI;
		DrawRect(Red, 0.f, 0.f, W, T);
		DrawRect(Red, 0.f, H - T, W, T);
		DrawRect(Red, 0.f, T, T, H - 2.f * T);
		DrawRect(Red, W - T, T, T, H - 2.f * T);
		if (Tier == 2)
		{
			// 四角折角：从角上往里收，0.7 秒内收进去并淡掉
			const FLinearColor Hot(1.f, 0.25f, 0.12f, 0.8f * (1.f - Since / EdgeTime[Tier]));
			const float In = FMath::Lerp(60.f, 110.f, Since / EdgeTime[Tier]) * UI;
			const float Len = 70.f * UI;
			for (int32 c = 0; c < 4; ++c)
			{
				const float SX = (c & 1) ? -1.f : 1.f;
				const float SY = (c & 2) ? -1.f : 1.f;
				const float X0 = (c & 1) ? W - In : In;
				const float Y0 = (c & 2) ? H - In : In;
				DrawLine(X0, Y0, X0 + SX * Len, Y0, Hot, 4.f);
				DrawLine(X0, Y0, X0, Y0 + SY * Len, Hot, 4.f);
			}
		}
	}
	// 11-02 方向弧：每个来源一条（同一个来源的新一击刷新它），多条同时显示、各自淡出；更粗
	const float Now = GetWorld()->GetTimeSeconds();
	if (Mech->GetLastDamagedTime() != LastDamageSeen)
	{
		LastDamageSeen = Mech->GetLastDamagedTime();
		AActor* Src = Mech->GetLastHitAttacker();
		FVector Dir = Mech->GetLastHitFrom().GetSafeNormal2D();
		if (Src && Src != Mech)
		{
			Dir = MechLoop::Delta(GetWorld(), Mech->GetActorLocation(), Src->GetActorLocation()).GetSafeNormal2D();  // 老街循环：最近的那份
		}
		FDamageArc* Arc = nullptr;
		for (FDamageArc& Old : DamageArcs)
		{
			if (Src && Old.Source.Get() == Src)
			{
				Arc = &Old;
				break;
			}
		}
		if (!Arc)
		{
			if (DamageArcs.Num() >= 8)
			{
				DamageArcs.RemoveAt(0);
			}
			Arc = &DamageArcs.AddDefaulted_GetRef();
		}
		Arc->Source = Src;
		Arc->WorldDir = Dir;
		Arc->Time = Now;
		Arc->Tier = Tier;
	}
	DamageArcs.RemoveAll([Now](const FDamageArc& D) { return Now - D.Time > 1.4f; });
	const float CamYaw = PlayerOwner->PlayerCameraManager->GetCameraRotation().Yaw;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;
	static const float ArcHalf[3] = {0.42f, 0.52f, 0.64f};
	static const float ArcThick[3] = {11.f, 14.f, 18.f};
	for (FDamageArc& D : DamageArcs)
	{
		FVector Dir = D.WorldDir;
		if (AActor* Src = D.Source.Get())
		{
			Dir = MechLoop::Delta(GetWorld(), Mech->GetActorLocation(), Src->GetActorLocation()).GetSafeNormal2D();  // 来源在动，弧跟着转
		}
		const FVector Local = FRotator(0.f, CamYaw, 0.f).UnrotateVector(Dir);
		const float Ang = FMath::Atan2(Local.Y, Local.X);  // 0 = 正前方（屏幕上方），顺时针为正
		const float Age = Now - D.Time;
		const float A = FMath::Clamp(1.f - Age / 1.4f, 0.f, 1.f);
		Arc(CX, CY, 190.f * UI, Ang - ArcHalf[D.Tier], Ang + ArcHalf[D.Tier], FLinearColor(1.f, 0.1f, 0.05f, 0.95f * A), ArcThick[D.Tier] * UI);
		Arc(CX, CY, 206.f * UI + D.Tier * 4.f * UI, Ang - ArcHalf[D.Tier] * 0.6f, Ang + ArcHalf[D.Tier] * 0.6f, FLinearColor(1.f, 0.25f, 0.12f, 0.6f * A), 4.f + D.Tier);
		if (D.Tier == 2 && Age < 0.25f)
		{
			Arc(CX, CY, 182.f * UI, Ang - 0.3f, Ang + 0.3f, FLinearColor(1.f, 0.9f, 0.8f, 0.9f * (1.f - Age / 0.25f)), 3.f);  // 最重一档：方向弧里一道白热的芯
		}
	}
}
// ---------------------------------------------------------------- 上方正中：时间、比分、进度线、存活格；大字只写数字
void AMechHUD::DrawRound()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS || GS->Phase == EMechRoundPhase::Waiting)
	{
		return;
	}
	if (GS->IsDeathmatch())
	{
		DrawDMRound();  // 死斗的比分条按击毁数画（MechHudModes.cpp）
		return;
	}
	const float CX = Canvas->SizeX * 0.5f;
	const float H = Canvas->SizeY;
	const float Remaining = GS->GetPhaseRemaining();
	const AMechCharacter* Mech = GetFocusMech();
	const AMechPlayerController* PC = Cast<AMechPlayerController>(PlayerOwner);
	const int32 MyTeam = Mech ? Mech->GetTeam() : PC ? PC->GetMenuTeam() : 0;
	const bool bFight = GS->Phase == EMechRoundPhase::Fight;

	// 时间（加时、最后 20 秒橙）和两边比分
	const float Y = 20.f * UI;
	FString Clock = TEXT("--:--");
	if (bFight)
	{
		const int32 Sec = FMath::CeilToInt(Remaining);
		Clock = FString::Printf(TEXT("%d:%02d"), Sec / 60, Sec % 60);
	}
	Readout(Clock, CX, Y, 22.f, bFight && (Remaining < 20.f || GS->bOvertime) ? Warn : Ink, 0.5f);
	Readout(FString::FromInt(GS->TeamScore[0]), CX - 66.f * UI, Y - 3.f * UI, 26.f, Ally, 1.f);
	Readout(FString::FromInt(GS->TeamScore[1]), CX + 66.f * UI, Y - 3.f * UI, 26.f, Enemy, 0.f);

	// 进度线：两队从两端往中间长，长到中间就赢（夺芯按交芯分，死斗按回合）；加时时中间一颗橙色菱形
	const float LY = Y + 36.f * UI;
	const float Half = 220.f * UI;
	const float Goal = FMath::Max(1, GS->bCoreMode ? GS->CoreTarget : GS->RoundsToWin);
	DrawRect(Faint, CX - Half, LY, Half * 2.f, FMath::Max(1.f, UI));
	const float L0 = Half * FMath::Clamp(GS->TeamScore[0] / Goal, 0.f, 1.f);
	const float L1 = Half * FMath::Clamp(GS->TeamScore[1] / Goal, 0.f, 1.f);
	DrawRect(Ally, CX - Half, LY - 0.5f * UI, L0, 2.f * UI);
	DrawRect(Enemy, CX + Half - L1, LY - 0.5f * UI, L1, 2.f * UI);
	if (GS->bOvertime)
	{
		const float S = 4.f * UI;
		Triangle(FVector2D(CX, LY - S), FVector2D(CX + S, LY), FVector2D(CX - S, LY), Warn);
		Triangle(FVector2D(CX, LY + S), FVector2D(CX + S, LY), FVector2D(CX - S, LY), Warn);
	}
	// 存活格：实心活着、空心被击毁
	int32 Alive[2] = {0, 0};
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetHealth()->IsDead() && !It->IsTrainingDummy() && It->GetTeam() >= 0 && It->GetTeam() < 2)
		{
			++Alive[It->GetTeam()];
		}
	}
	const float Pip = 7.f * UI;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const FLinearColor C = Team == 0 ? Ally : Enemy;
		for (int32 i = 0; i < GS->TeamCount[Team]; ++i)  // 11-02 两队台数可以不一样
		{
			const float PX = Team == 0 ? CX - 14.f * UI - (i + 1) * (Pip + 5.f * UI) : CX + 14.f * UI + i * (Pip + 5.f * UI);
			const float PY = LY + 9.f * UI;
			if (i < Alive[Team])
			{
				DrawRect(C, PX, PY, Pip, Pip);
			}
			else
			{
				Box(PX, PY, Pip, Pip, Fade(C, 0.45f), 1.f);
			}
		}
	}

	// 大字：开局倒计时只写数字；回合 / 整场结束写比分，颜色看输赢（整场结果由结算页显示，菜单没开时才在这里写）
	FString Big;
	FLinearColor BigColor = Ink;
	switch (GS->Phase)
	{
	case EMechRoundPhase::Countdown:
		Big = FString::FromInt(FMath::Max(1, FMath::CeilToInt(Remaining)));
		break;
	case EMechRoundPhase::RoundOver:
		Big = FString::Printf(TEXT("%d : %d"), GS->TeamScore[0], GS->TeamScore[1]);
		BigColor = GS->RoundWinner < 0 ? Ink : GS->RoundWinner == MyTeam ? Ally : Enemy;
		break;
	case EMechRoundPhase::MatchOver:
		Big = FString::Printf(TEXT("%d : %d"), GS->TeamScore[0], GS->TeamScore[1]);
		BigColor = GS->MatchWinner < 0 ? Ink : GS->MatchWinner == MyTeam ? Ally : Enemy;
		break;
	default:
		break;
	}
	if (!Big.IsEmpty())
	{
		const float BY = H * 0.3f;
		const float BW = MeasureReadout(Big, 64.f).X + 120.f * UI;
		DrawRect(Fade(BigColor, 0.7f), CX - BW * 0.5f, BY - 12.f * UI, BW, 2.f * UI);
		DrawRect(Fade(BigColor, 0.7f), CX - BW * 0.5f, BY + 92.f * UI, BW, 2.f * UI);
		if (MechHudTheme::Get().bHazard)
		{
			// 新纪：横幅两头各一段黄黑斜纹
			const float HW = 44.f * UI, HH = 8.f * UI;
			for (const float HX : {CX - BW * 0.5f, CX + BW * 0.5f - HW})
			{
				Hazard(HX, BY - 22.f * UI, HW, HH, 1.f);
				Hazard(HX, BY + 96.f * UI, HW, HH, 1.f);
			}
		}
		Readout(Big, CX, BY, 64.f, BigColor, 0.5f);
	}
}

void AMechHUD::UpdatePhaseSounds()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS || GS->Phase == LastPhase)
	{
		return;
	}
	const AMechCharacter* Mech = GetFocusMech();
	const AMechPlayerController* PC = Cast<AMechPlayerController>(PlayerOwner);
	const int32 MyTeam = Mech ? Mech->GetTeam() : PC ? PC->GetMenuTeam() : 0;
	USoundBase* Sound = nullptr;
	if (GS->Phase == EMechRoundPhase::Fight)
	{
		Sound = RoundStartSound;
	}
	else if (GS->Phase == EMechRoundPhase::RoundOver && GS->RoundWinner >= 0)
	{
		Sound = GS->RoundWinner == MyTeam ? WinSound : LoseSound;
	}
	else if (GS->Phase == EMechRoundPhase::MatchOver && GS->IsDeathmatch() && GS->MatchWinner >= 0)
	{
		// 死斗没有回合结算，整场结束时响一声输赢（个人死斗按名字认自己）
		const bool bWon = GS->IsSoloDM() ? (Mech && GS->WinnerName == Mech->DisplayName) : GS->MatchWinner == MyTeam;
		Sound = bWon ? WinSound : LoseSound;
	}
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound, 0.6f);
	}
	LastPhase = GS->Phase;
}

// 右上的击毁通报 11-01 起由信息区接管（AMechHUD::DrawFeed 在 MechHudFeed.cpp：击毁 + 队友技能 / 插件 / 标点，结构化事件，不再解析「A 击毁了 B」文字）

// ---------------------------------------------------------------- 按住 Tab：计分板（列头只用缩写）
void AMechHUD::DrawScoreboard()
{
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!GS)
	{
		return;
	}
	if (GS->IsDeathmatch())
	{
		DrawDMScoreboard();  // 死斗：击毁 / 阵亡 / 输出（MechHudModes.cpp）
		return;
	}
	// 11-01 积分制（MechScore.h）：加 SCORE 积分、AST 助攻两列，队内按积分排（以前按输出）；夺芯把 TKN 承受换成 CORE 交芯；
	// 保卫战（测试场那条路，只有玩家一队）不画上面的两队比分
	const bool bCoreBoard = GS->bCoreMode;
	const bool bCoop = GS->bRangeMode;
	TArray<FMechResultRow> Rows = GS->Results;
	Rows.Sort([](const FMechResultRow& A, const FMechResultRow& B) {
		return A.Team != B.Team ? A.Team < B.Team : A.Score != B.Score ? A.Score > B.Score : A.Dealt > B.Dealt;
	});
	TMap<FString, const AMechCharacter*> ByName;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		ByName.Add(It->DisplayName, *It);
	}
	const float PW = 780.f * UI;
	const float RowH = 32.f * UI;
	const float PH = 96.f * UI + Rows.Num() * RowH;
	const float X = (Canvas->SizeX - PW) * 0.5f;
	const float Y = (Canvas->SizeY - PH) * 0.42f;
	DrawRect(Fade(MechHudTheme::Get().Shade, 0.62f), X, Y, PW, PH);
	DrawRect(Accent, X, Y, PW, 2.f * UI);
	if (!bCoop)
	{
		Text(FString::FromInt(GS->TeamScore[0]), X + PW * 0.5f - 18.f * UI, Y + 16.f * UI, 26.f, Ally, 1.f);
		Text(TEXT(":"), X + PW * 0.5f, Y + 16.f * UI, 26.f, Dim, 0.5f);
		Text(FString::FromInt(GS->TeamScore[1]), X + PW * 0.5f + 18.f * UI, Y + 16.f * UI, 26.f, Enemy, 0.f);
	}
	const float Cols[] = {28.f, 270.f, 360.f, 460.f, 540.f, 620.f, 710.f};
	const TCHAR* Heads[] = {TEXT(""), TEXT("AP"), TEXT("SCORE"), TEXT("KO"), TEXT("AST"), TEXT("DMG"), bCoreBoard ? TEXT("CORE") : TEXT("TKN")};
	float RY = Y + 62.f * UI;
	for (int32 c = 1; c < 7; ++c)
	{
		Text(Heads[c], X + Cols[c] * UI, RY, 11.f, c == 2 ? Gold : Dim);
	}
	RY += 24.f * UI;
	for (const FMechResultRow& R : Rows)
	{
		const FLinearColor C = R.Team == 0 ? Ally : Enemy;
		const AMechCharacter* const* Found = ByName.Find(R.Name);
		const bool bDead = Found && (*Found)->GetHealth()->IsDead();
		DrawRect(Fade(C, 0.6f), X + 14.f * UI, RY + 2.f * UI, 2.f * UI, RowH - 10.f * UI);
		Text(R.Name, X + Cols[0] * UI, RY, 16.f, bDead ? Fade(C, 0.45f) : C);
		if (Found)
		{
			Text(bDead ? FString(TEXT("—")) : FString::Printf(TEXT("%.0f"), (*Found)->GetHealth()->GetAP()), X + Cols[1] * UI, RY, 16.f, bDead ? Faint : Ink);
		}
		Text(FString::FromInt(R.Score), X + Cols[2] * UI, RY, 16.f, Ink);
		Text(FString::FromInt(R.Kills), X + Cols[3] * UI, RY, 16.f, Ink);
		Text(FString::FromInt(R.Assists), X + Cols[4] * UI, RY, 16.f, Ink);
		Text(FString::FromInt(R.Dealt), X + Cols[5] * UI, RY, 16.f, Dim);
		Text(FString::FromInt(bCoreBoard ? R.Cores : R.Taken), X + Cols[6] * UI, RY, 16.f, bCoreBoard ? Ink : Dim);
		RY += RowH;
	}
}

// ---------------------------------------------------------------- 夺芯：核心、交芯点的世界标记（只有菱形、距离和倒计时数字），阵亡时的重生倒计时
void AMechHUD::DrawCore(AMechCharacter* Mech)
{
	AMechCoreDirector* Dir = nullptr;
	for (TActorIterator<AMechCoreDirector> It(GetWorld()); It; ++It)
	{
		Dir = *It;
		break;
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!Dir || !Dir->Arena || !GS || !PlayerOwner || !PlayerOwner->PlayerCameraManager || GS->Phase == EMechRoundPhase::Waiting)
	{
		return;
	}
	const float Now = GS->GetServerWorldTimeSeconds();
	const AMechPlayerController* PC = Cast<AMechPlayerController>(PlayerOwner);
	const int32 MyTeam = Mech ? Mech->GetTeam() : PC ? PC->GetMenuTeam() : 0;
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PlayerOwner->PlayerCameraManager->GetCameraRotation();
	const bool bBlink = FMath::Fmod(GetWorld()->GetTimeSeconds(), 0.5f) < 0.25f;
	auto CoreColor = [&](int32 Side) { return Side < 0 ? Gold : Side == MyTeam ? Ally : Enemy; };
	// 世界里的标记：屏幕内画菱形 + 距离（上面可选一个倒计时数字），屏幕外贴到屏幕边上
	auto Marker = [&](const FVector& At, const FLinearColor& C, const FString& Label, float Size, float Alpha) -> FVector2D {
		FVector2D P;
		const FVector Local = CamRot.UnrotateVector(At - CamLoc);
		const float Pad = 36.f * UI;
		const bool bOn = Local.X > 0.f && UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P) && P.X > Pad && P.X < W - Pad && P.Y > Pad && P.Y < H - Pad;
		if (!bOn)
		{
			FVector2D D(Local.Y, -Local.Z);
			if (!D.Normalize())
			{
				D = FVector2D(0.f, 1.f);
			}
			const float SX = (W * 0.5f - Pad) / FMath::Max(FMath::Abs(D.X), 0.001f);
			const float SY = (H * 0.5f - Pad) / FMath::Max(FMath::Abs(D.Y), 0.001f);
			P = FVector2D(W * 0.5f, H * 0.5f) + D * FMath::Min(SX, SY);
		}
		const FLinearColor CC = Fade(C, Alpha);
		const float S = Size * UI;
		Triangle(FVector2D(P.X, P.Y - S), FVector2D(P.X + S, P.Y), FVector2D(P.X - S, P.Y), CC);
		Triangle(FVector2D(P.X, P.Y + S), FVector2D(P.X + S, P.Y), FVector2D(P.X - S, P.Y), CC);
		if (!Label.IsEmpty())
		{
			Text(Label, P.X, P.Y - S - 22.f * UI, 15.f, CC, 0.5f);
		}
		const float Meters = FVector::Dist(At, Mech ? Mech->GetActorLocation() : CamLoc) / 100.f;
		Text(FString::Printf(TEXT("%.0fm"), Meters), P.X, P.Y + S + 2.f * UI, 11.f, Fade(CC, 0.85f), 0.5f);
		return P;
	};
	const bool bCarrying = Mech && !Mech->GetHealth()->IsDead() && Dir->CoreCarriedBy(Mech) != nullptr;

	// 交芯点：自家开着的大、亮（自己带着核心时更大、一呼一吸），对面开着的小、淡；10 秒内要切换时，下一个点上标倒计时（橙，闪）
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const bool bMine = Team == MyTeam;
		const float Breath = bMine && bCarrying ? 0.7f + 0.3f * FMath::Sin(GetWorld()->GetTimeSeconds() * 6.f) : 1.f;
		Marker(Dir->StationLocation(Team, Dir->ActiveStation[Team]) + FVector(0.f, 0.f, 150.f), bMine ? Ally : Enemy, FString(),
			bMine ? (bCarrying ? 12.f : 9.f) : 6.f, bMine ? 0.95f * Breath : 0.55f);
	}
	const float ToSwap = Dir->NextSwapTime - Now;
	if (ToSwap > 0.f && ToSwap < 10.f)
	{
		Marker(Dir->StationLocation(MyTeam, 1 - Dir->ActiveStation[MyTeam]) + FVector(0.f, 0.f, 150.f), Warn,
			FString::FromInt(FMath::CeilToInt(ToSwap)), 7.f, bBlink ? 0.95f : 0.5f);
	}

	// 核心：队友带着的跟着队友；对面带着的只每 3 秒闪一次位置（外面套一圈括号），越久越淡
	for (const FMechCore& C : Dir->Cores)
	{
		if (C.State == EMechCoreState::Carried && C.Carrier)
		{
			if (C.Carrier == Mech)
			{
				continue;  // 自己带着的画在准星下方的细条上
			}
			if (C.Carrier->GetTeam() == MyTeam)
			{
				Marker(C.Carrier->GetActorLocation() + FVector(0.f, 0.f, 260.f), Ally, FString(), 8.f, 1.f);
			}
			else
			{
				const float Age = Now - C.PulseTime;
				const float Alpha = FMath::Clamp(1.f - Age / AMechCoreDirector::PulseEvery * 0.6f, 0.35f, 1.f);
				const FVector2D P = Marker(C.Pulse, Enemy, FString(), 8.f, Alpha);
				Brackets(P.X, P.Y, 16.f * UI, 6.f * UI, Fade(Enemy, Alpha), 1.5f);
			}
			continue;
		}
		const FLinearColor Col = CoreColor(C.OwnerTeam);
		const bool bAtFoundry = FVector::Dist2D(C.Location, Dir->Arena->FoundryLocation()) < 300.f;
		const FVector2D P = Marker(FVector(C.Location) + FVector(0.f, 0.f, 90.f), Col, FString(), 9.f, 1.f);
		if (C.Grab > 0.f && C.Grabber)
		{
			// 有人在捡：外圈读条，颜色是捡的那一队
			Arc(P.X, P.Y, 22.f * UI, 0.f, C.Grab * 2.f * PI, C.Grabber->GetTeam() == MyTeam ? Ally : Enemy, 3.f);
		}
		if (C.State == EMechCoreState::Ground && !bAtFoundry)
		{
			// 掉在外面：回到中央的倒计时圈
			const float Left = FMath::Clamp(1.f - (Now - C.DropTime) / AMechCoreDirector::RecallTime, 0.f, 1.f);
			Arc(P.X, P.Y, 16.f * UI, 0.f, Left * 2.f * PI, Fade(Col, 0.9f), 2.f);
		}
	}
	// 场上没有核心：中央铸造台上标出产出倒计时
	const float ToNeutral = Dir->NextNeutralTime - Now;
	if (Dir->Cores.Num() == 0 && ToNeutral > 0.f)
	{
		Marker(Dir->Arena->FoundryLocation() + FVector(0.f, 0.f, 90.f), Gold, FString::FromInt(FMath::CeilToInt(ToNeutral)), 9.f, 0.6f);
	}

	// 地图（核心、回收站、带芯者这些）归地图系统画：DrawMap（MechMinimap.cpp）

	// 阵亡：正中一圈重生读条 + 剩几秒
	if (Mech && Mech->GetHealth()->IsDead())
	{
		const FMechDowned* D = Dir->DownedOf(Mech);
		if (D && D->RespawnAt >= 0.f)
		{
			const float Left = FMath::Max(0.f, D->RespawnAt - Now);
			const float Total = FMath::Max(0.1f, D->RespawnAt - D->DiedAt);
			const float CX = W * 0.5f;
			const float CY = H * 0.5f;
			Arc(CX, CY, 46.f * UI, 0.f, 2.f * PI, Track, 2.f);
			Arc(CX, CY, 46.f * UI, 0.f, 2.f * PI * FMath::Clamp(1.f - Left / Total, 0.f, 1.f), Accent, 2.f);
			const FString S = FString::FromInt(FMath::CeilToInt(Left));
			Text(S, CX, CY - Measure(S, 40.f).Y * 0.5f, 40.f, Ink, 0.5f);
		}
	}
}

// ---------------------------------------------------------------- 第一人称：传感器边框 + 受击雪花
void AMechHUD::DrawVisor(AMechCharacter* Mech)
{
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const FLinearColor Edge = Fade(Accent, 0.35f);
	const float In = 22.f * UI;
	const float L = 70.f * UI;
	// 四角的取景框
	for (const FVector2D& C : {FVector2D(In, In), FVector2D(W - In, In), FVector2D(In, H - In), FVector2D(W - In, H - In)})
	{
		const float SX = C.X < W * 0.5f ? 1.f : -1.f;
		const float SY = C.Y < H * 0.5f ? 1.f : -1.f;
		DrawLine(C.X, C.Y, C.X + SX * L, C.Y, Edge, 1.5f);
		DrawLine(C.X, C.Y, C.X, C.Y + SY * L * 0.6f, Edge, 1.5f);
	}
	// 中线两侧的小刻度（俯仰参考）
	for (int32 i = -2; i <= 2; ++i)
	{
		const float Y = H * 0.5f + i * 60.f * UI;
		const float Len = (i == 0 ? 18.f : 9.f) * UI;
		DrawLine(In + 10.f * UI, Y, In + 10.f * UI + Len, Y, Fade(Edge, 0.7f), 1.f);
		DrawLine(W - In - 10.f * UI, Y, W - In - 10.f * UI - Len, Y, Fade(Edge, 0.7f), 1.f);
	}
	// 受击：画面闪几道横向雪花（越近越多）
	const float Since = GetWorld()->GetTimeSeconds() - Mech->GetLastDamagedTime();
	if (Since >= 0.f && Since < 0.22f && !Mech->GetHealth()->IsDead())
	{
		FRandomStream Rand(static_cast<int32>(GFrameCounter / 2));
		const float A = 1.f - Since / 0.22f;
		for (int32 i = 0; i < 9; ++i)
		{
			const float Y = Rand.FRandRange(0.f, H);
			const float X0 = Rand.FRandRange(0.f, W * 0.7f);
			DrawRect(FLinearColor(0.85f, 0.95f, 1.f, 0.10f * A), X0, Y, Rand.FRandRange(W * 0.1f, W * 0.5f), Rand.FRandRange(1.f, 4.f) * UI);
		}
	}
}
// ---------------------------------------------------------------- 第一人称启动画面（按 3 秒的完整启动排时间）
void AMechHUD::DrawBoot(AMechCharacter* Mech, float T)
{
	if (!Mech)
	{
		return;
	}
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	// 测试：-MechBootShots 把启动画面按时间点截下来（Saved/Screenshots/Boot）
	static const bool bShots = FParse::Param(FCommandLine::Get(), TEXT("MechBootShots"));
	static int32 NextShot = 0;
	static const float ShotAt[] = {0.12f, 0.5f, 0.95f, 1.45f, 1.9f, 2.35f, 2.7f};
	if (bShots && NextShot < 7 && T >= ShotAt[NextShot])
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Boot") / FString::Printf(TEXT("boot_%d.png"), NextShot), true, false);
		++NextShot;
	}
	// 1) 黑屏，一条亮线从中间展开
	if (T < 0.25f)
	{
		DrawRect(FLinearColor::Black, 0.f, 0.f, W, H);
		const float Len = W * FMath::SmoothStep(0.f, 1.f, T / 0.25f);
		DrawRect(FLinearColor(0.75f, 0.95f, 1.f, 0.9f), W * 0.5f - Len * 0.5f, H * 0.5f - 1.f * UI, Len, 2.f * UI);
		return;
	}
	// 2) 画面从暗处带扫描线出来
	const float Dark = FMath::Clamp(1.f - (T - 0.25f) / 1.1f, 0.f, 1.f);
	if (Dark > 0.f)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.88f * Dark), 0.f, 0.f, W, H);
	}
	const float Scan = 0.05f + 0.22f * Dark;
	if (T < 2.6f)
	{
		for (float Y = FMath::Fmod(T * 90.f, 6.f) * UI; Y < H; Y += 6.f * UI)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, Scan), 0.f, Y, W, 2.f * UI);
		}
	}
	// 3) 自检：七行读条一行行走完（编号 + 18 格读条 + 完成后亮一个方块，不写字）
	const float LogAlpha = FMath::Clamp(1.f - (T - 2.45f) / 0.45f, 0.f, 1.f);
	const float X = W * 0.07f;
	float Y = H * 0.32f;
	for (int32 i = 0; i < 7; ++i)
	{
		const float Start = 0.35f + i * 0.24f;
		if (T < Start || LogAlpha <= 0.f)
		{
			break;
		}
		const float Typed = FMath::Clamp((T - Start) / 0.3f, 0.f, 1.f);
		Text(FString::Printf(TEXT("%02d"), i + 1), X, Y, 14.f, Fade(Dim, LogAlpha));
		const float BX = X + 34.f * UI;
		for (int32 k = 0; k < 18; ++k)
		{
			DrawRect(Fade(k < FMath::FloorToInt(Typed * 18.f) ? Accent : Track, LogAlpha), BX + k * 12.f * UI, Y + 6.f * UI, 9.f * UI, 5.f * UI);
		}
		if (Typed >= 1.f)
		{
			DrawRect(Fade(Ink, LogAlpha), BX + 18.f * 12.f * UI + 8.f * UI, Y + 4.f * UI, 9.f * UI, 9.f * UI);
		}
		Y += 24.f * UI;
	}
	// 4) 最后：机体代号横幅
	if (T > 2.25f)
	{
		const float A = FMath::Clamp((T - 2.25f) / 0.15f, 0.f, 1.f) * FMath::Clamp((3.f - T) / 0.35f, 0.f, 1.f);
		const float CX = W * 0.5f;
		const float CY = H * 0.56f;  // 开局倒计时的大字在 0.3 附近，横幅放在它下面
		const FString Big = TEXT("HX-07");
		const float BW = Measure(Big, 40.f).X + 160.f * UI;
		DrawRect(Fade(Accent, A), CX - BW * 0.5f, CY - 14.f * UI, BW, 2.f * UI);
		DrawRect(Fade(Accent, A), CX - BW * 0.5f, CY + 60.f * UI, BW, 2.f * UI);
		Text(Big, CX, CY, 40.f, Fade(Ink, A), 0.5f);
	}
}

// ---------------------------------------------------------------- 插件
void AMechHUD::PluginIcon(float X, float Y, float R, FName Id, float Alpha, float Thick)
{
	const MechEffects::FDef* Def = MechEffects::Find(Id);
	if (!Def)
	{
		return;
	}
	const FLinearColor C = Fade(Def->Color, Alpha);
	TArray<FVector2f> Hex;
	for (int32 k = 0; k <= 6; ++k)
	{
		const float A = PI / 6.f + k * PI / 3.f;
		Hex.Add(FVector2f(X + R * FMath::Cos(A), Y + R * FMath::Sin(A)));
	}
	Polyline(Hex, C, Thick);
	const float G = R * 0.5f;
	switch (Def->Glyph)
	{
	case 1:  // 上三角：伤害
		Triangle(FVector2D(X, Y - G), FVector2D(X + G * 0.9f, Y + G * 0.7f), FVector2D(X - G * 0.9f, Y + G * 0.7f), C);
		break;
	case 2:  // 方块：装甲
		DrawRect(C, X - G * 0.7f, Y - G * 0.7f, G * 1.4f, G * 1.4f);
		break;
	case 3:  // 双箭头：推进
		for (const float Off : {-0.35f, 0.35f})
		{
			SoftLine(X - G * 0.8f, Y + G * (Off + 0.4f), X, Y + G * (Off - 0.4f), C, Thick);
			SoftLine(X, Y + G * (Off - 0.4f), X + G * 0.8f, Y + G * (Off + 0.4f), C, Thick);
		}
		break;
	case 4:  // 圆弧：装填 / 冷却
		Arc(X, Y, G * 0.85f, -2.4f, 2.4f, C, Thick + 0.5f);
		DrawRect(C, X - 1.5f * UI, Y - G * 0.85f - 1.5f * UI, 3.f * UI, 3.f * UI);
		break;
	case 5:  // 叉：冲击
		SoftLine(X - G * 0.8f, Y - G * 0.8f, X + G * 0.8f, Y + G * 0.8f, C, Thick + 0.5f);
		SoftLine(X - G * 0.8f, Y + G * 0.8f, X + G * 0.8f, Y - G * 0.8f, C, Thick + 0.5f);
		break;
	case 6:  // 两道横杠：稳定
		DrawRect(C, X - G * 0.8f, Y - G * 0.45f, G * 1.6f, G * 0.3f);
		DrawRect(C, X - G * 0.8f, Y + G * 0.15f, G * 1.6f, G * 0.3f);
		break;
	case 7:  // 闪电：EMP
		SoftLine(X + G * 0.3f, Y - G * 0.9f, X - G * 0.45f, Y + G * 0.1f, C, Thick + 0.5f);
		SoftLine(X - G * 0.45f, Y + G * 0.1f, X + G * 0.45f, Y - G * 0.1f, C, Thick + 0.5f);
		SoftLine(X + G * 0.45f, Y - G * 0.1f, X - G * 0.3f, Y + G * 0.9f, C, Thick + 0.5f);
		break;
	default:
		DrawRect(C, X - G * 0.3f, Y - G * 0.3f, G * 0.6f, G * 0.6f);
		break;
	}
}

void AMechHUD::DrawPods(AMechCharacter* Mech)
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager)
	{
		return;
	}
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PlayerOwner->PlayerCameraManager->GetCameraRotation();
	for (TActorIterator<AMechPluginPod> It(GetWorld()); It; ++It)
	{
		const FVector At = It->GetActorLocation() + FVector(0.f, 0.f, 120.f);
		FVector2D P = FVector2D::ZeroVector;
		const FVector Local = CamRot.UnrotateVector(At - CamLoc);
		const float Pad = 40.f * UI;
		const bool bOn = Local.X > 0.f && UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P) && P.X > Pad && P.X < W - Pad && P.Y > Pad && P.Y < H - Pad;
		if (!bOn)
		{
			FVector2D D(Local.Y, -Local.Z);
			if (!D.Normalize())
			{
				D = FVector2D(0.f, 1.f);
			}
			const float SX = (W * 0.5f - Pad) / FMath::Max(FMath::Abs(D.X), 0.001f);
			const float SY = (H * 0.5f - Pad) / FMath::Max(FMath::Abs(D.Y), 0.001f);
			P = FVector2D(W * 0.5f, H * 0.5f) + D * FMath::Min(SX, SY);
		}
		const float Pulse = 0.75f + 0.25f * FMath::Sin(Now * 4.f);
		PluginIcon(P.X, P.Y, 13.f * UI, It->Plugin, Pulse);
		const float Meters = FVector::Dist(At, Mech->GetActorLocation()) / 100.f;
		Text(FString::Printf(TEXT("%.0fm"), Meters), P.X, P.Y + 17.f * UI, 11.f, Fade(Dim, Pulse), 0.5f);
	}
	// 刚拿到的插件：准星下方（细条上面）闪一下它的图标，放大后收回、慢慢淡掉
	if (const UMechEffectComponent* Fx = Mech->GetEffects())
	{
		TArray<FName> Now_;
		for (const FMechEffect& E : Fx->GetEffects())
		{
			Now_.Add(E.Id);
			if (!SeenEffects.Contains(E.Id) && MechEffects::Find(E.Id) && MechEffects::Find(E.Id)->bPlugin)
			{
				FlashEffect = E.Id;
				FlashStart = Now;
			}
		}
		SeenEffects = Now_;
	}
	const float Age = Now - FlashStart;
	if (Age < 1.6f && !Mech->GetHealth()->IsDead())
	{
		const float Pop = 1.f + 0.5f * FMath::Clamp(1.f - Age / 0.15f, 0.f, 1.f);
		const float A = FMath::Clamp((1.6f - Age) / 0.5f, 0.f, 1.f);
		PluginIcon(W * 0.5f, H * 0.73f - 70.f * UI, 22.f * UI * Pop, FlashEffect, A, 2.5f);
	}
}
