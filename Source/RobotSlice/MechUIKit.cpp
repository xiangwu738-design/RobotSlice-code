#include "MechUIKit.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "MechFonts.h"
#include "MechSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "UObject/StrongObjectPtr.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace MechKitPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的小函数撞
{
	const FSlateBrush* WhiteBrush()
	{
		static FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	FLinearColor A(const FLinearColor& C, float Alpha)
	{
		return FLinearColor(C.R, C.G, C.B, C.A * Alpha);
	}

	/** 一块实心矩形（局部坐标）；透明的不画 */
	void FillRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& C)
	{
		if (C.A <= 0.003f || Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), WhiteBrush(), ESlateDrawEffect::None, C);
	}

	/** 矩形描边：用四块实心矩形拼（比画线清楚，不会糊成两像素） */
	void Frame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& C, float T)
	{
		FillRect(Out, Layer, G, Pos, FVector2f(Size.X, T), C);
		FillRect(Out, Layer, G, FVector2f(Pos.X, Pos.Y + Size.Y - T), FVector2f(Size.X, T), C);
		FillRect(Out, Layer, G, FVector2f(Pos.X, Pos.Y + T), FVector2f(T, Size.Y - 2.f * T), C);
		FillRect(Out, Layer, G, FVector2f(Pos.X + Size.X - T, Pos.Y + T), FVector2f(T, Size.Y - 2.f * T), C);
	}

	/** 四角折角（像瞄准框）：每个角一个 L */
	void Corners(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& C, float Len, float T)
	{
		const float X0 = Pos.X, Y0 = Pos.Y, X1 = Pos.X + Size.X, Y1 = Pos.Y + Size.Y;
		FillRect(Out, Layer, G, FVector2f(X0, Y0), FVector2f(Len, T), C);
		FillRect(Out, Layer, G, FVector2f(X0, Y0), FVector2f(T, Len), C);
		FillRect(Out, Layer, G, FVector2f(X1 - Len, Y0), FVector2f(Len, T), C);
		FillRect(Out, Layer, G, FVector2f(X1 - T, Y0), FVector2f(T, Len), C);
		FillRect(Out, Layer, G, FVector2f(X0, Y1 - T), FVector2f(Len, T), C);
		FillRect(Out, Layer, G, FVector2f(X0, Y1 - Len), FVector2f(T, Len), C);
		FillRect(Out, Layer, G, FVector2f(X1 - Len, Y1 - T), FVector2f(Len, T), C);
		FillRect(Out, Layer, G, FVector2f(X1 - T, Y1 - Len), FVector2f(T, Len), C);
	}

	/** 横向渐变（Stops：0–1 的位置 + 颜色） */
	void HGradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Pos, const FVector2f& Size,
		std::initializer_list<TPair<float, FLinearColor>> Stops)
	{
		TArray<FSlateGradientStop> S;
		for (const TPair<float, FLinearColor>& P : Stops)
		{
			S.Add(FSlateGradientStop(FVector2f(P.Key * Size.X, 0.f), P.Value));
		}
		// Orient_Vertical = 竖条纹，颜色从左往右变（ElementBatcher 里按 Position.X 排）
		FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(S), Orient_Vertical);
	}

	/** 小箭头「›」 */
	void Chevron(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2f& Center, float Half, const FLinearColor& C, float Thick)
	{
		TArray<FVector2f> P = {Center + FVector2f(-Half * 0.5f, -Half), Center + FVector2f(Half * 0.5f, 0.f), Center + FVector2f(-Half * 0.5f, Half)};
		FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), MoveTemp(P), ESlateDrawEffect::None, C, true, Thick);
	}

	/** 指数平滑（和帧率无关）：Speed 越大越快，16 ≈ 0.1 秒基本到位 */
	float Approach(float Cur, float Target, float Dt, float Speed)
	{
		return Cur + (Target - Cur) * (1.f - FMath::Exp(-Dt * Speed));
	}

	// 引擎控件的样式：放在函数内静态里，换配色时原地改（控件存的是指针，下一帧就画新颜色）
	FScrollBarStyle& BarStyle()
	{
		static FScrollBarStyle S;
		return S;
	}
	FScrollBoxStyle& BoxStyle()
	{
		static FScrollBoxStyle S;
		return S;
	}
	FEditableTextBoxStyle& EditStyle()
	{
		static FEditableTextBoxStyle S;
		return S;
	}

	void RefreshStyles()
	{
		const FMechUITheme& T = MechUITheme::Get();
		// 滚动条：3 像素细条，平时淡、悬停强调色；没有两头的按钮图和上下阴影（引擎默认那套灰色粗条是「廉价感」的来源之一）
		BarStyle()
			.SetHorizontalBackgroundImage(FSlateNoResource())
			.SetVerticalBackgroundImage(FSlateColorBrush(A(T.Ink, 0.05f)))
			.SetVerticalTopSlotImage(FSlateNoResource())
			.SetVerticalBottomSlotImage(FSlateNoResource())
			.SetHorizontalTopSlotImage(FSlateNoResource())
			.SetHorizontalBottomSlotImage(FSlateNoResource())
			.SetNormalThumbImage(FSlateColorBrush(A(T.Ink, 0.24f)))
			.SetHoveredThumbImage(FSlateColorBrush(A(T.Accent, 0.75f)))
			.SetDraggedThumbImage(FSlateColorBrush(T.Accent))
			.SetThickness(3.f);
		BoxStyle()
			.SetTopShadowBrush(FSlateNoResource())
			.SetBottomShadowBrush(FSlateNoResource())
			.SetLeftShadowBrush(FSlateNoResource())
			.SetRightShadowBrush(FSlateNoResource())
			.SetBarThickness(3.f);
		// 输入框：直角、深色底、细描边；悬停描边变亮，获得焦点描边变强调色
		EditStyle()
			.SetBackgroundImageNormal(FSlateRoundedBoxBrush(A(T.Ink, 0.04f), 0.f, A(T.Ink, 0.22f), 1.f))
			.SetBackgroundImageHovered(FSlateRoundedBoxBrush(A(T.Ink, 0.06f), 0.f, A(T.Ink, 0.45f), 1.f))
			.SetBackgroundImageFocused(FSlateRoundedBoxBrush(A(T.Accent, 0.07f), 0.f, T.Accent, 1.f))
			.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.f, A(T.Ink, 0.12f), 1.f))
			.SetPadding(FMargin(12.f, 8.f))
			.SetTextStyle(FTextBlockStyle()
				.SetFont(MechFonts::Get(17))
				.SetColorAndOpacity(T.Ink)
				.SetSelectedBackgroundColor(A(T.Accent, 0.35f))
				.SetHighlightColor(T.Accent))
			.SetForegroundColor(T.Ink)
			.SetFocusedForegroundColor(T.Ink)
			.SetReadOnlyForegroundColor(T.Dim)
			.SetBackgroundColor(FLinearColor::White)
			.SetScrollBarStyle(BarStyle());
	}

	bool bInited = false;
}

// ================================================================ 基础
void MechKit::Init()
{
	if (MechKitPriv::bInited)
	{
		return;
	}
	MechKitPriv::bInited = true;
	ApplyPalette(FMechSettings::Get().MenuPalette);
}

void MechKit::ApplyPalette(int32 Index)
{
	MechKitPriv::bInited = true;
	MechUITheme::Set(MechUITheme::Preset(Index));
	MechKitPriv::RefreshStyles();
}

const FMechUITheme& MechKit::Th()
{
	return MechUITheme::Get();
}

FSlateFontInfo MechKit::Font(float Size, bool bBold)
{
	return MechFonts::Get(Size, bBold);
}

FSlateFontInfo MechKit::Light(float Size)
{
	return MechFonts::GetLight(Size);
}

FSlateFontInfo MechKit::Latin(float Size, int32 LetterSpacing)
{
	return MechFonts::GetLatin(Size, LetterSpacing);
}

TAttribute<FSlateColor> MechKit::Color(FLinearColor FMechUITheme::*Member, float Alpha)
{
	return TAttribute<FSlateColor>::CreateLambda([Member, Alpha]() { return FSlateColor(MechKitPriv::A(MechUITheme::Get().*Member, Alpha)); });
}

FLinearColor MechKit::Mix(const FLinearColor& A, const FLinearColor& B, float T)
{
	return FMath::Lerp(A, B, FMath::Clamp(T, 0.f, 1.f));
}

const FSlateBrush* MechKit::White()
{
	return MechKitPriv::WhiteBrush();
}

const FSlateBrush* MechKit::NoBrush()
{
	static FSlateNoResource Brush;
	return &Brush;
}

const FSlateBrush* MechKit::Box(const FLinearColor& Fill, const FLinearColor& Line, float LineWidth)
{
	// 按 8 位颜色取整做键：颜色在动画里连续变时最多也就几百个，不会无限长
	static TMap<uint32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
	const uint32 Key = HashCombine(HashCombine(GetTypeHash(Fill.ToFColor(false)), GetTypeHash(Line.ToFColor(false))), GetTypeHash(FMath::RoundToInt(LineWidth * 4.f)));
	TSharedPtr<FSlateRoundedBoxBrush>& B = Cache.FindOrAdd(Key);
	if (!B.IsValid())
	{
		B = MakeShared<FSlateRoundedBoxBrush>(Fill, 0.f, Line, LineWidth);
	}
	return B.Get();
}

float MechKit::EaseOut(float T)
{
	const float X = 1.f - FMath::Clamp(T, 0.f, 1.f);
	return 1.f - X * X * X;
}

double MechKit::Now()
{
	return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetCurrentTime() : FPlatformTime::Seconds();
}

// ================================================================ 菜单音效（11-01，用户：「给按钮加一些音效吧」）
namespace MechKitPriv
{
	/** 整体音量（再乘设置里的主音量，引擎统一乘）；觉得菜单音效整体太响 / 太轻就改这个，各个声音之间的比例烤在 wav 里（make_ui_sfx.py） */
	constexpr float UISoundMaster = 0.6f;
	/** 悬停 tick 的最短间隔；刚放过别的音之后多久不放悬停（页面重建后光标落在新按钮上） */
	constexpr double HoverMinGap = 0.09;
	constexpr double HoverAfterOther = 0.25;
	/** 各种类的相对音量（在 wav 本身的响度之上再调） */
	constexpr float UISoundGain[static_cast<int32>(EMechUISound::Count)] = {1.f, 1.f, 1.f, 1.f, 1.f};

	struct FUISoundBank
	{
		TStrongObjectPtr<USoundBase> Sound[static_cast<int32>(EMechUISound::Count)];  // 强引用：路径加载的资源不然会被 GC
		int32 Counts[static_cast<int32>(EMechUISound::Count)] = {};
		double LastHover = -100.0;
		double LastOther = -100.0;
		bool bLoaded = false;
	};

	FUISoundBank& Bank()
	{
		static FUISoundBank B;
		return B;
	}
}

void MechKit::PlayUISound(EMechUISound Kind)
{
	using namespace MechKitPriv;
	if (Kind >= EMechUISound::Count)
	{
		return;
	}
	FUISoundBank& B = Bank();
	const int32 K = static_cast<int32>(Kind);
	const double Now = FPlatformTime::Seconds();
	if (Kind == EMechUISound::Hover)
	{
		if (Now - B.LastHover < HoverMinGap || Now - B.LastOther < HoverAfterOther)
		{
			return;
		}
		B.LastHover = Now;
	}
	else
	{
		B.LastOther = Now;
	}
	++B.Counts[K];
	// 测试用：启动参数 -UISfxLog 时每次触发写一行日志（种类序号 0 悬停 1 点击 2 确认 3 返回 4 滑条），平时不写
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("UISfxLog"));
	if (bLog)
	{
		UE_LOG(LogTemp, Log, TEXT("[uisfx] 触发 %d（累计 %d）"), K, B.Counts[K]);
	}
	if (!GEngine || !GEngine->UseSound())
	{
		return;
	}
	if (!B.bLoaded)
	{
		B.bLoaded = true;
		static const TCHAR* Names[] = {TEXT("S_ui_hover"), TEXT("S_ui_click"), TEXT("S_ui_confirm"), TEXT("S_ui_back"), TEXT("S_ui_slider")};
		for (int32 i = 0; i < static_cast<int32>(EMechUISound::Count); ++i)
		{
			USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/UI/%s.%s"), Names[i], Names[i]), nullptr, LOAD_NoWarn | LOAD_Quiet);
			B.Sound[i].Reset(S);
			if (!S)
			{
				UE_LOG(LogTemp, Warning, TEXT("[uisfx] 找不到 /Game/Audio/UI/%s（tools/ue_import_ui_sfx.py 导入），这一种不放"), Names[i]);
			}
		}
	}
	USoundBase* S = B.Sound[K].Get();
	UWorld* World = nullptr;
	for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
	{
		if (Ctx.World() && (Ctx.WorldType == EWorldType::Game || Ctx.WorldType == EWorldType::PIE))
		{
			World = Ctx.World();
			break;
		}
	}
	if (S && World)
	{
		// 悬停每次音高随机一点点，连着扫过去不单调；bIsUISound 默认 true，暂停时也放
		const float Pitch = Kind == EMechUISound::Hover ? FMath::FRandRange(0.94f, 1.08f) : FMath::FRandRange(0.97f, 1.03f);
		UGameplayStatics::PlaySound2D(World, S, UISoundMaster * UISoundGain[K], Pitch);
	}
}

int32 MechKit::UISoundCount(EMechUISound Kind)
{
	return Kind < EMechUISound::Count ? MechKitPriv::Bank().Counts[static_cast<int32>(Kind)] : 0;
}

EMechUISound MechKit::SoundForLabel(const FString& Label, EMechUISound Default)
{
	static const TCHAR* BackWords[] = {TEXT("返回"), TEXT("取消"), TEXT("撤销"), TEXT("退出"), TEXT("解散"), TEXT("关闭"), TEXT("BACK"), TEXT("CANCEL")};
	static const TCHAR* ConfirmWords[] = {TEXT("开始"), TEXT("开房"), TEXT("应用"), TEXT("确定"), TEXT("确认"), TEXT("再来一局"), TEXT("START"), TEXT("APPLY")};
	for (const TCHAR* W : BackWords)
	{
		if (Label.Contains(W))
		{
			return EMechUISound::Back;
		}
	}
	for (const TCHAR* W : ConfirmWords)
	{
		if (Label.Contains(W))
		{
			return EMechUISound::Confirm;
		}
	}
	return Default;
}

// ================================================================ 可点的控件
void SMechKitButton::Construct(const FArguments& InArgs)
{
	Look = InArgs._Look;
	Selected = InArgs._Selected;
	bClickable = InArgs._Clickable;
	OnClick = InArgs._OnClick;
	OnHover = InArgs._OnHover;
	Outline = InArgs._Outline;
	ClickSound = InArgs._ClickSound != EMechUISound::Auto ? InArgs._ClickSound : (Look == EMechKitLook::Primary ? EMechUISound::Confirm : EMechUISound::Click);
	SelectT = Selected.Get() ? 1.f : 0.f;  // 一出来就是选中的不播动画
	ChildSlot[InArgs._Content.Widget];
}

void SMechKitButton::SetContent(const TSharedRef<SWidget>& InContent)
{
	ChildSlot[InContent];
}

void SMechKitButton::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const float Dt = FMath::Min(InDeltaTime, 0.1f);
	const bool bEnabled = IsEnabled();
	const bool bActive = IsHovered() || bNavFocus;  // 键盘 / 手柄导航选到它，和鼠标悬停一个样子
	HoverT = MechKitPriv::Approach(HoverT, bActive && bEnabled ? 1.f : 0.f, Dt, 18.f);
	PressT = MechKitPriv::Approach(PressT, bPressed && bActive ? 1.f : 0.f, Dt, 32.f);
	SelectT = MechKitPriv::Approach(SelectT, Selected.Get() ? 1.f : 0.f, Dt, 16.f);
}

bool SMechKitButton::SupportsKeyboardFocus() const
{
	return bClickable;
}

FReply SMechKitButton::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	// 只有导航（方向键、Tab、手柄）选到时才显示成悬停；被点击 / 代码设焦点时不亮
	bNavFocus = InFocusEvent.GetCause() == EFocusCause::Navigation;
	if (bNavFocus && bClickable && IsEnabled())
	{
		MechKit::PlayUISound(EMechUISound::Hover);
	}
	if (bNavFocus && OnHover && IsEnabled())
	{
		OnHover();
	}
	return SCompoundWidget::OnFocusReceived(MyGeometry, InFocusEvent);
}

void SMechKitButton::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bNavFocus = false;
	bPressed = false;
	SCompoundWidget::OnFocusLost(InFocusEvent);
}

FReply SMechKitButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// 和 SButton 一样：回车 / 空格 / 手柄 A 按下记住，松开才算点击（改键时松开才进「等新键」，下一次按下才是新键）
	if (bClickable && IsEnabled() && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
	{
		if (!InKeyEvent.IsRepeat())
		{
			bPressed = true;
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SMechKitButton::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bPressed && FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Accept)
	{
		bPressed = false;
		if (IsEnabled() && OnClick)
		{
			MechKit::PlayUISound(ClickSound);
			OnClick();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyUp(MyGeometry, InKeyEvent);
}

FReply SMechKitButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bClickable || !IsEnabled() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = true;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMechKitButton::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// 快速连点：第二下 Slate 发的是双击而不是按下，当成按下处理（不然连点会丢一次）
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SMechKitButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	// 松开时还在按钮上才算点击（按下后拖出去再松开 = 取消）；和 SButton 一样在松开时触发——
	// 按键设置页靠这一点：点一行进入「等新键」是松开那一下，不会被当成新键
	const bool bInside = MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition());
	if (bInside && IsEnabled() && OnClick)
	{
		MechKit::PlayUISound(ClickSound);
		OnClick();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

void SMechKitButton::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (bClickable && IsEnabled() && !FSlateApplication::Get().HasAnyMouseCaptor())
	{
		MechKit::PlayUISound(EMechUISound::Hover);
	}
	if (OnHover && IsEnabled())
	{
		OnHover();
	}
}

void SMechKitButton::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bPressed = false;
}

int32 SMechKitButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace MechKitPriv;
	const FMechUITheme& T = MechUITheme::Get();
	const FGeometry& G = AllottedGeometry;
	const FVector2f S = FVector2f(G.GetLocalSize());
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const float Fade = bEnabled ? 1.f : 0.45f;
	// 所有颜色都乘上外面的淡入透明度（切页淡入时整页一起淡）
	auto C = [&Tint, Fade](const FLinearColor& In, float Alpha = 1.f) {
		return FLinearColor(In.R * Tint.R, In.G * Tint.G, In.B * Tint.B, In.A * Alpha * Tint.A * Fade);
	};
	const float H = HoverT;
	const float P = PressT;
	const float Sel = SelectT;
	FSlateWindowElementList& Out = OutDrawElements;
	const int32 L = LayerId;
	switch (Look)
	{
	case EMechKitLook::MenuItem:
	{
		// 悬停：底色从左往右淡出；按下再亮一点
		if (H > 0.005f || P > 0.005f)
		{
			HGradient(Out, L, G, FVector2f::ZeroVector, S,
				{{0.f, C(T.Accent, 0.17f * H + 0.14f * P)}, {0.55f, C(T.Accent, 0.045f * H)}, {1.f, C(T.Accent, 0.f)}});
		}
		FillRect(Out, L, G, FVector2f(0.f, S.Y - 1.f), FVector2f(S.X, 1.f), C(T.Line, 0.8f));
		// 左边竖条：平时是一小截淡刻度，悬停时长成强调色的长条
		const float BarH = FMath::Lerp(10.f, S.Y - 22.f, MechKit::EaseOut(H));
		FillRect(Out, L + 1, G, FVector2f(0.f, (S.Y - BarH) * 0.5f), FVector2f(FMath::Lerp(2.f, 3.f, H), BarH), C(MechKit::Mix(T.Faint, T.Accent, H)));
		if (H > 0.01f)
		{
			Chevron(Out, L + 1, G, FVector2f(S.X - 22.f + 6.f * H, S.Y * 0.5f), 5.f, C(T.Accent, H), 1.5f);
		}
		break;
	}
	case EMechKitLook::Action:
	{
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(MechKit::Mix(A(T.Ink, 0.035f), A(T.Accent, 0.13f), H)));
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Accent, 0.12f * P));
		Frame(Out, L + 1, G, FVector2f::ZeroVector, S, C(MechKit::Mix(A(T.Ink, 0.24f), T.Accent, H)), 1.f);
		if (H > 0.01f)
		{
			Corners(Out, L + 2, G, FVector2f::ZeroVector, S, C(T.Accent, H), 7.f, 2.f);
		}
		break;
	}
	case EMechKitLook::Primary:
	{
		// 实心强调色；悬停往白提一点、外面多一圈淡描边；按下压暗
		const FLinearColor Fill = bEnabled ? MechKit::Mix(MechKit::Mix(T.Accent, FLinearColor::White, 0.16f * H), FLinearColor::Black, 0.2f * P) : A(T.Ink, 0.08f);
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(Fill, bEnabled ? 1.f : 1.f / Fade));
		if (H > 0.01f)
		{
			Frame(Out, L + 1, G, FVector2f(-4.f, -4.f), S + FVector2f(8.f, 8.f), C(T.Accent, 0.4f * H), 1.f);
		}
		if (!bEnabled)
		{
			Frame(Out, L + 1, G, FVector2f::ZeroVector, S, C(T.Ink, 0.14f / Fade), 1.f);
		}
		break;
	}
	case EMechKitLook::Chip:
	{
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Accent, 0.15f * Sel));
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Ink, 0.05f * H * (1.f - Sel)));
		Frame(Out, L + 1, G, FVector2f::ZeroVector, S, C(MechKit::Mix(MechKit::Mix(A(T.Ink, 0.16f), A(T.Ink, 0.42f), H), T.Accent, Sel)), 1.f);
		break;
	}
	case EMechKitLook::Segment:
	{
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Accent, 0.14f * Sel));
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Ink, 0.05f * H * (1.f - Sel)));
		const float W = S.X * MechKit::EaseOut(Sel);
		FillRect(Out, L + 1, G, FVector2f((S.X - W) * 0.5f, S.Y - 2.f), FVector2f(W, 2.f), C(T.Accent));
		break;
	}
	case EMechKitLook::Tab:
	{
		FillRect(Out, L, G, FVector2f(0.f, S.Y - 1.f), FVector2f(S.X, 1.f), C(T.Ink, 0.28f * H * (1.f - Sel)));
		const float W = S.X * MechKit::EaseOut(Sel);
		FillRect(Out, L + 1, G, FVector2f((S.X - W) * 0.5f, S.Y - 2.f), FVector2f(W, 2.f), C(T.Accent));
		break;
	}
	case EMechKitLook::Row:
	{
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Ink, 0.035f * H));
		FillRect(Out, L + 1, G, FVector2f(0.f, 10.f), FVector2f(2.f, FMath::Max(0.f, S.Y - 20.f)), C(T.Accent, H));
		FillRect(Out, L, G, FVector2f(0.f, S.Y - 1.f), FVector2f(S.X, 1.f), C(T.Line));
		break;
	}
	case EMechKitLook::Card:
	{
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.CardFill));
		FillRect(Out, L, G, FVector2f::ZeroVector, S, C(T.Ink, 0.03f * H + 0.04f * P));
		// 描边和折角在内容之后画（下面）：卡片的照片铺满到边，先画会被照片盖住
		break;
	}
	case EMechKitLook::Switch:
	{
		// 左边一条 44×22 的轨道，滑块从左滑到右；开 = 强调色
		const FVector2f TrackPos(0.f, FMath::RoundToFloat((S.Y - 22.f) * 0.5f));
		const FVector2f TrackSize(44.f, 22.f);
		const float On = MechKit::EaseOut(Sel);
		FillRect(Out, L, G, TrackPos, TrackSize, C(MechKit::Mix(A(T.Ink, 0.06f), A(T.Accent, 0.22f), Sel)));
		Frame(Out, L + 1, G, TrackPos, TrackSize, C(MechKit::Mix(A(T.Ink, 0.3f + 0.25f * H), T.Accent, Sel)), 1.f);
		FillRect(Out, L + 2, G, TrackPos + FVector2f(FMath::Lerp(4.f, TrackSize.X - 18.f, On), 4.f), FVector2f(14.f, 14.f),
			C(MechKit::Mix(MechKit::Mix(T.Dim, T.Ink, H), T.Accent, Sel)));
		break;
	}
	default:
		break;
	}
	const int32 Top = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, L + 3, InWidgetStyle, bParentEnabled);
	if (Look == EMechKitLook::Card)
	{
		const FLinearColor Given = Outline ? Outline() : FLinearColor(0.f, 0.f, 0.f, -1.f);
		const FLinearColor Line = Given.A >= 0.f ? MechKit::Mix(Given, T.Accent, Sel) : MechKit::Mix(MechKit::Mix(A(T.Ink, 0.12f), A(T.Accent, 0.6f), H), T.Accent, Sel);
		Frame(Out, Top + 1, G, FVector2f::ZeroVector, S, C(Line), Sel > 0.5f ? 1.5f : 1.f);
		if (Sel > 0.01f)
		{
			Corners(Out, Top + 2, G, FVector2f::ZeroVector, S, C(T.Accent, Sel), 12.f, 3.f);
		}
		return Top + 2;
	}
	return Top;
}

// ================================================================ 滑条
void SMechKitSlider::Construct(const FArguments& InArgs)
{
	Get = InArgs._Get;
	Set = InArgs._Set;
	OnCommit = InArgs._OnCommit;
	Min = InArgs._Min;
	Max = FMath::Max(InArgs._Max, InArgs._Min + KINDA_SMALL_NUMBER);
	Step = FMath::Max(InArgs._Step, 0.0001f);
	Width = InArgs._Width;
}

float SMechKitSlider::Fraction() const
{
	return Get ? FMath::Clamp((Get() - Min) / (Max - Min), 0.f, 1.f) : 0.f;
}

void SMechKitSlider::SetFromX(const FGeometry& G, const FVector2D& ScreenPos)
{
	constexpr float Pad = 8.f;
	const float LocalX = G.AbsoluteToLocal(ScreenPos).X;
	const float W = FMath::Max(1.f, static_cast<float>(G.GetLocalSize().X) - 2.f * Pad);
	const float F = FMath::Clamp((LocalX - Pad) / W, 0.f, 1.f);
	float V = Min + F * (Max - Min);
	V = FMath::Clamp(Min + FMath::RoundToFloat((V - Min) / Step) * Step, Min, Max);
	if (Set && Get && !FMath::IsNearlyEqual(V, Get()))
	{
		Set(V);
	}
}

void SMechKitSlider::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	HoverT = MechKitPriv::Approach(HoverT, IsHovered() || bDragging || bNavFocus ? 1.f : 0.f, FMath::Min(InDeltaTime, 0.1f), 18.f);
}

FReply SMechKitSlider::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	bNavFocus = InFocusEvent.GetCause() == EFocusCause::Navigation;
	return SLeafWidget::OnFocusReceived(MyGeometry, InFocusEvent);
}

void SMechKitSlider::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	bNavFocus = false;
	SLeafWidget::OnFocusLost(InFocusEvent);
}

FReply SMechKitSlider::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const EUINavigation Dir = FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent);
	if ((Dir == EUINavigation::Left || Dir == EUINavigation::Right) && Get && Set && IsEnabled())
	{
		Set(FMath::Clamp(Get() + (Dir == EUINavigation::Right ? Step : -Step), Min, Max));
		MechKit::PlayUISound(EMechUISound::Slider);
		if (OnCommit)
		{
			OnCommit();
		}
		return FReply::Handled();
	}
	return SLeafWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SMechKitSlider::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	bDragging = true;
	SetFromX(MyGeometry, MouseEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SMechKitSlider::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && HasMouseCapture())
	{
		SetFromX(MyGeometry, MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMechKitSlider::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}
	bDragging = false;
	MechKit::PlayUISound(EMechUISound::Slider);
	if (OnCommit)
	{
		OnCommit();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SMechKitSlider::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Get || !Set || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	const float V = FMath::Clamp(Get() + Step * FMath::Sign(MouseEvent.GetWheelDelta()), Min, Max);
	Set(V);
	MechKit::PlayUISound(EMechUISound::Slider);
	if (OnCommit)
	{
		OnCommit();
	}
	return FReply::Handled();
}

void SMechKitSlider::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (bDragging)
	{
		bDragging = false;
		MechKit::PlayUISound(EMechUISound::Slider);
		if (OnCommit)
		{
			OnCommit();
		}
	}
}

int32 SMechKitSlider::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace MechKitPriv;
	const FMechUITheme& T = MechUITheme::Get();
	const FGeometry& G = AllottedGeometry;
	const FVector2f S = FVector2f(G.GetLocalSize());
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const float Fade = ShouldBeEnabled(bParentEnabled) ? 1.f : 0.45f;
	auto C = [&Tint, Fade](const FLinearColor& In, float Alpha = 1.f) {
		return FLinearColor(In.R * Tint.R, In.G * Tint.G, In.B * Tint.B, In.A * Alpha * Tint.A * Fade);
	};
	constexpr float Pad = 8.f;
	const float X0 = Pad;
	const float W = FMath::Max(1.f, S.X - 2.f * Pad);
	const float Y = FMath::RoundToFloat(S.Y * 0.5f);
	const float F = Fraction();
	const float Tx = X0 + W * F;
	// 轨道、填充、五道刻度（0 / 25 / 50 / 75 / 100%）
	FillRect(OutDrawElements, LayerId, G, FVector2f(X0, Y - 1.f), FVector2f(W, 2.f), C(T.Ink, 0.16f));
	FillRect(OutDrawElements, LayerId + 1, G, FVector2f(X0, Y - 1.f), FVector2f(W * F, 2.f), C(T.Accent));
	for (int32 i = 0; i <= 4; ++i)
	{
		FillRect(OutDrawElements, LayerId, G, FVector2f(FMath::RoundToFloat(X0 + W * i / 4.f), Y + 6.f), FVector2f(1.f, 4.f), C(T.Ink, 0.2f));
	}
	// 滑块：竖条；悬停 / 拖动时外面一圈淡底
	const float Hv = HoverT;
	FillRect(OutDrawElements, LayerId + 1, G, FVector2f(Tx - 7.f, Y - 12.f), FVector2f(14.f, 24.f), C(T.Accent, 0.16f * Hv));
	FillRect(OutDrawElements, LayerId + 2, G, FVector2f(Tx - 2.f - Hv, Y - 9.f - Hv), FVector2f(4.f + 2.f * Hv, 18.f + 2.f * Hv), C(MechKit::Mix(T.Ink, T.Accent, 0.55f + 0.45f * Hv)));
	return LayerId + 2;
}

// ================================================================ 文字
TSharedRef<STextBlock> MechKit::Text(const TAttribute<FText>& InText, const FSlateFontInfo& InFont, const TAttribute<FSlateColor>& InColor)
{
	return SNew(STextBlock).Text(InText).Font(InFont).ColorAndOpacity(InColor);
}

TSharedRef<SWidget> MechKit::Kicker(const FText& Label)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
		[
			SNew(SBox)
			.WidthOverride(22.f)
			.HeightOverride(2.f)
			[
				SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Color(&FMechUITheme::Accent))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Text(Label, Latin(12, 260), Color(&FMechUITheme::Accent))
		];
}

TSharedRef<SWidget> MechKit::PageTitle(const FText& KickerText, const FText& Title, const TAttribute<FText>& Sub)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Kicker(KickerText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[Text(Title, Light(42), Color(&FMechUITheme::Ink))]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Text(Sub)
			.Font(Font(15))
			.ColorAndOpacity(Color(&FMechUITheme::Dim))
			.AutoWrapText(true)
			.LineHeightPercentage(1.15f)
			.Visibility_Lambda([Sub]() { return Sub.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		];
}

TSharedRef<SWidget> MechKit::Hairline(float Alpha)
{
	return SNew(SBox).HeightOverride(1.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Color(&FMechUITheme::Line, Alpha))];
}

TSharedRef<SWidget> MechKit::Section(const FText& Title, const TAttribute<FText>& Note)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Bottom)
		[
			Text(Title, Font(14, true), Color(&FMechUITheme::Ink, 0.92f))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(14.f, 0.f, 0.f, 1.f))
		[
			Text(Note, Font(13), Color(&FMechUITheme::Dim))
		];
}

TSharedRef<SWidget> MechKit::KeyCap(const TAttribute<FText>& Key, TFunction<int32()> State, float MinWidth)
{
	auto GetState = [State]() { return State ? State() : 0; };
	return SNew(SBorder)
		.BorderImage_Lambda([GetState]() {
			const FMechUITheme& T = Th();
			const int32 St = GetState();
			return St == 2 ? Box(MechKitPriv::A(T.Accent, 0.12f), T.Accent, 1.f)
				: St == 1 ? Box(MechKitPriv::A(T.Warn, 0.07f), MechKitPriv::A(T.Warn, 0.7f), 1.f)
				: Box(MechKitPriv::A(T.Ink, 0.05f), MechKitPriv::A(T.Ink, 0.28f), 1.f);
		})
		.Padding(FMargin(10.f, 3.f, 10.f, 4.f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(FMath::Max(0.f, MinWidth - 20.f))
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(Key)
				.Font(Latin(15))
				.ColorAndOpacity_Lambda([GetState]() {
					const FMechUITheme& T = Th();
					const int32 St = GetState();
					if (St == 2)
					{
						// 等新键时一闪一闪
						const float Pulse = 0.6f + 0.4f * FMath::Sin(static_cast<float>(Now()) * 7.f);
						return FSlateColor(MechKitPriv::A(T.Accent, Pulse));
					}
					return FSlateColor(St == 1 ? T.Warn : T.Ink);
				})
			]
		];
}

// ================================================================ 可点的（工厂）
namespace MechKitPriv
{
	/** 文字颜色跟着按钮的悬停 / 选中变：平时 Rest、悬停到 Hot、选中 Selected；禁用时是 Faint */
	TAttribute<FSlateColor> FollowButton(const TWeakPtr<SMechKitButton>& W, FLinearColor FMechUITheme::*Rest, float RestAlpha, FLinearColor FMechUITheme::*Hot,
		FLinearColor FMechUITheme::*Sel = nullptr)
	{
		return TAttribute<FSlateColor>::CreateLambda([W, Rest, RestAlpha, Hot, Sel]() {
			const FMechUITheme& T = MechUITheme::Get();
			const TSharedPtr<SMechKitButton> B = W.Pin();
			if (!B.IsValid())
			{
				return FSlateColor(A(T.*Rest, RestAlpha));
			}
			if (!B->IsEnabled())
			{
				return FSlateColor(T.Faint);
			}
			FLinearColor Out = MechKit::Mix(A(T.*Rest, RestAlpha), T.*Hot, B->GetHover());
			if (Sel)
			{
				Out = MechKit::Mix(Out, T.*Sel, B->GetSelect());
			}
			return FSlateColor(Out);
		});
	}
}

TSharedRef<SWidget> MechKit::MenuItem(const FText& Label, TFunction<void()> OnClick, TAttribute<bool> Enabled, const FText& Tag)
{
	TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(EMechKitLook::MenuItem).ClickSound(SoundForLabel(Label.ToString())).OnClick(OnClick).IsEnabled(Enabled);
	const TWeakPtr<SMechKitButton> W = B;
	B->SetContent(
		SNew(SBox)
		.HeightOverride(56.f)
		.Padding(FMargin(26.f, 0.f, 44.f, 0.f))
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font(23))
				.ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Ink, 0.82f, &FMechUITheme::Ink))
				// 悬停时字往右滑一点
				.RenderTransform_Lambda([W]() {
					const TSharedPtr<SMechKitButton> P = W.Pin();
					return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(P.IsValid() ? 9.f * EaseOut(P->GetHover()) : 0.f, 0.f)));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Tag)
				.Font(Latin(11, 220))
				.ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Dim, 0.45f, &FMechUITheme::Accent))
			]
		]);
	return B;
}

TSharedRef<SWidget> MechKit::Button(const TAttribute<FText>& Label, TFunction<void()> OnClick, bool bPrimary, TAttribute<bool> Enabled, float Width, float Height)
{
	TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(bPrimary ? EMechKitLook::Primary : EMechKitLook::Action).ClickSound(SoundForLabel(Label.Get().ToString(), bPrimary ? EMechUISound::Confirm : EMechUISound::Click)).OnClick(OnClick).IsEnabled(Enabled);
	const TWeakPtr<SMechKitButton> W = B;
	B->SetContent(
		SNew(SBox)
		.WidthOverride(Width > 0.f ? FOptionalSize(Width) : FOptionalSize())
		.MinDesiredWidth(120.f)
		.HeightOverride(Height)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(24.f, 0.f))
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Font(17))
			.ColorAndOpacity(bPrimary ? MechKitPriv::FollowButton(W, &FMechUITheme::AccentInk, 1.f, &FMechUITheme::AccentInk)
									  : MechKitPriv::FollowButton(W, &FMechUITheme::Ink, 0.88f, &FMechUITheme::Ink))
		]);
	return B;
}

TSharedRef<SWidget> MechKit::ChipWith(const TSharedRef<SWidget>& Content, TFunction<bool()> IsOn, TFunction<void()> OnClick, TAttribute<bool> Enabled)
{
	return SNew(SMechKitButton)
		.Look(EMechKitLook::Chip)
		.Selected_Lambda([IsOn]() { return IsOn && IsOn(); })
		.OnClick(OnClick)
		.IsEnabled(Enabled)
		[
			SNew(SBox)
			.MinDesiredWidth(56.f)
			.HeightOverride(38.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(16.f, 0.f))
			[
				Content
			]
		];
}

TSharedRef<SWidget> MechKit::Chip(const TAttribute<FText>& Label, TFunction<bool()> IsOn, TFunction<void()> OnClick, TAttribute<bool> Enabled)
{
	TSharedRef<SMechKitButton> B = SNew(SMechKitButton)
		.Look(EMechKitLook::Chip)
		.Selected_Lambda([IsOn]() { return IsOn && IsOn(); })
		.OnClick(OnClick)
		.IsEnabled(Enabled);
	const TWeakPtr<SMechKitButton> W = B;
	B->SetContent(
		SNew(SBox)
		.MinDesiredWidth(56.f)
		.HeightOverride(38.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.Padding(FMargin(16.f, 0.f))
		[
			SNew(STextBlock).Text(Label).Font(Font(15)).ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Dim, 1.f, &FMechUITheme::Ink, &FMechUITheme::Ink))
		]);
	return B;
}

TSharedRef<SWidget> MechKit::Segmented(const TArray<FText>& Options, TFunction<int32()> Get, TFunction<void(int32)> Set, TFunction<bool(int32)> Enabled, float CellMinWidth)
{
	TSharedRef<SHorizontalBox> Cells = SNew(SHorizontalBox);
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		if (i > 0)
		{
			// 格与格之间一道竖细线
			Cells->AddSlot().AutoWidth()[SNew(SBox).WidthOverride(1.f)[SNew(SBorder).BorderImage(White()).BorderBackgroundColor(Color(&FMechUITheme::Ink, 0.12f))]];
		}
		TSharedRef<SMechKitButton> B = SNew(SMechKitButton)
			.Look(EMechKitLook::Segment)
			.Selected_Lambda([Get, i]() { return Get && Get() == i; })
			.OnClick([Set, i]() { if (Set) Set(i); })
			.IsEnabled_Lambda([Enabled, i]() { return !Enabled || Enabled(i); });
		const TWeakPtr<SMechKitButton> W = B;
		B->SetContent(
			SNew(SBox)
			.MinDesiredWidth(CellMinWidth)
			.HeightOverride(38.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(16.f, 0.f))
			[
				SNew(STextBlock).Text(Options[i]).Font(Font(15)).ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Dim, 1.f, &FMechUITheme::Ink, &FMechUITheme::Ink))
			]);
		Cells->AddSlot().AutoWidth()[B];
	}
	return SNew(SBorder)
		.BorderImage_Lambda([]() { return Box(FLinearColor::Transparent, MechKitPriv::A(Th().Ink, 0.18f), 1.f); })
		.Padding(FMargin(1.f))
		[
			Cells
		];
}

TSharedRef<SWidget> MechKit::Switch(TFunction<bool()> Get, TFunction<void(bool)> Set)
{
	TSharedRef<SMechKitButton> B = SNew(SMechKitButton)
		.Look(EMechKitLook::Switch)
		.Selected_Lambda([Get]() { return Get && Get(); })
		.OnClick([Get, Set]() { if (Get && Set) Set(!Get()); });
	const TWeakPtr<SMechKitButton> W = B;
	B->SetContent(
		SNew(SBox)
		.MinDesiredWidth(84.f)
		.HeightOverride(38.f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(58.f, 0.f, 4.f, 0.f))
		[
			SNew(STextBlock)
			.Font(Font(15))
			.Text_Lambda([Get]() { return Get && Get() ? INVTEXT("开") : INVTEXT("关"); })
			.ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Dim, 1.f, &FMechUITheme::Ink, &FMechUITheme::Ink))
		]);
	return B;
}

TSharedRef<SWidget> MechKit::Slider(TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, float Step, TFunction<FText(float)> Format,
	TFunction<void()> OnCommit, float Width)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SMechKitSlider).Get(Get).Set(Set).OnCommit(OnCommit).Min(Min).Max(Max).Step(Step).Width(Width)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
		[
			SNew(SBox)
			.WidthOverride(64.f)
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Font(Latin(18))
				.ColorAndOpacity(Color(&FMechUITheme::Ink))
				.Text_Lambda([Get, Format]() { return Get && Format ? Format(Get()) : FText::GetEmpty(); })
			]
		];
}

TSharedRef<SWidget> MechKit::Stepper(const TAttribute<FText>& Value, TFunction<void(int32)> OnStep, float Width)
{
	auto Arrow = [OnStep](int32 Dir) {
		TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(EMechKitLook::Action).OnClick([OnStep, Dir]() { if (OnStep) OnStep(Dir); });
		const TWeakPtr<SMechKitButton> W = B;
		B->SetContent(
			SNew(SBox)
			.WidthOverride(38.f)
			.HeightOverride(38.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Dir < 0 ? FText::FromString(TEXT("‹")) : FText::FromString(TEXT("›")))
				.Font(Font(20))
				.ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Dim, 1.f, &FMechUITheme::Accent))
			]);
		return B;
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Arrow(-1)]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(FMath::Max(80.f, Width - 76.f))
			.HAlign(HAlign_Center)
			[
				Text(Value, Latin(17), Color(&FMechUITheme::Ink))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()[Arrow(1)];
}

TSharedRef<SWidget> MechKit::Tabs(const TArray<FText>& Labels, TFunction<int32()> Get, TFunction<void(int32)> Set, TFunction<bool(int32)> Enabled, float TabWidth,
	float FontSize)
{
	TSharedRef<SHorizontalBox> TabRow = SNew(SHorizontalBox);
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		TSharedRef<SMechKitButton> B = SNew(SMechKitButton)
			.Look(EMechKitLook::Tab)
			.Selected_Lambda([Get, i]() { return Get && Get() == i; })
			.OnClick([Set, i]() { if (Set) Set(i); })
			.IsEnabled_Lambda([Enabled, i]() { return !Enabled || Enabled(i); });
		const TWeakPtr<SMechKitButton> W = B;
		B->SetContent(
			SNew(SBox)
			.WidthOverride(TabWidth > 0.f ? FOptionalSize(TabWidth) : FOptionalSize())
			.HeightOverride(48.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(FMargin(TabWidth > 0.f ? 0.f : 20.f, 0.f))
			[
				SNew(STextBlock).Text(Labels[i]).Font(Font(FontSize)).ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Dim, 1.f, &FMechUITheme::Ink, &FMechUITheme::Ink))
			]);
		TabRow->AddSlot().AutoWidth().Padding(FMargin(0.f, 0.f, 4.f, 0.f))[B];
	}
	return SNew(SOverlay)
		+ SOverlay::Slot().VAlign(VAlign_Bottom)[Hairline(1.2f)]
		+ SOverlay::Slot()[TabRow];
}

TSharedRef<SWidget> MechKit::Row(const FText& Name, const TAttribute<FText>& Desc, const TSharedRef<SWidget>& Control, float NameWidth, TFunction<bool()> Pending)
{
	TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(EMechKitLook::Row).Clickable(false);
	const TWeakPtr<SMechKitButton> W = B;
	B->SetContent(
		SNew(SBox)
		.MinDesiredHeight(64.f)
		.Padding(FMargin(18.f, 10.f, 14.f, 10.f))
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(NameWidth)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(Name).Font(Font(17)).ColorAndOpacity(MechKitPriv::FollowButton(W, &FMechUITheme::Ink, 0.9f, &FMechUITheme::Ink))
						]
						// 改了还没应用：名字后面一个小标记
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(10.f, 1.f, 0.f, 0.f))
						[
							SNew(SBorder)
							.Visibility_Lambda([Pending]() { return Pending && Pending() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
							.BorderImage_Lambda([]() { return Box(MechKitPriv::A(Th().Warn, 0.12f), MechKitPriv::A(Th().Warn, 0.6f), 1.f); })
							.Padding(FMargin(6.f, 0.f, 6.f, 1.f))
							[
								Text(INVTEXT("未应用"), Font(11), Color(&FMechUITheme::Warn))
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 3.f, 16.f, 0.f))
					[
						SNew(STextBlock)
						.Text(Desc)
						.Font(Font(13))
						.ColorAndOpacity(Color(&FMechUITheme::Dim))
						.AutoWrapText(true)
						.Visibility_Lambda([Desc]() { return Desc.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				Control
			]
		]);
	return B;
}

TSharedRef<SMechKitButton> MechKit::Card(const TSharedRef<SWidget>& Content, TFunction<void()> OnClick, TFunction<void()> OnHover, TAttribute<bool> Selected,
	TFunction<FLinearColor()> Outline)
{
	return SNew(SMechKitButton)
		.Look(EMechKitLook::Card)
		.OnClick(OnClick)
		.OnHover(OnHover)
		.Selected(Selected)
		.Outline(Outline)
		[
			Content
		];
}

// ================================================================ 引擎控件的样式
const FScrollBoxStyle& MechKit::ScrollBoxStyle()
{
	Init();
	return MechKitPriv::BoxStyle();
}

const FScrollBarStyle& MechKit::ScrollBarStyle()
{
	Init();
	return MechKitPriv::BarStyle();
}

const FEditableTextBoxStyle& MechKit::TextBoxStyle()
{
	Init();
	return MechKitPriv::EditStyle();
}

TSharedRef<SScrollBox> MechKit::ScrollBox()
{
	return SNew(SScrollBox)
		.Style(&ScrollBoxStyle())
		.ScrollBarStyle(&ScrollBarStyle())
		.ScrollBarThickness(FVector2D(3.f, 3.f))
		.ScrollBarPadding(FMargin(10.f, 0.f, 0.f, 0.f));
}

// ================================================================ 动效
TSharedRef<SWidget> MechKit::FadeIn(const TSharedRef<SWidget>& Content, TFunction<double()> StartTime, float Duration, float Offset)
{
	auto Progress = [StartTime, Duration]() { return EaseOut(static_cast<float>((Now() - (StartTime ? StartTime() : 0.0)) / FMath::Max(0.01f, Duration))); };
	return SNew(SBorder)
		.BorderImage(NoBrush())
		.Padding(FMargin(0.f))
		.ColorAndOpacity_Lambda([Progress]() { return FLinearColor(1.f, 1.f, 1.f, Progress()); })
		.RenderTransform_Lambda([Progress, Offset]() { return TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(-Offset * (1.f - Progress()), 0.f))); })
		[
			Content
		];
}
