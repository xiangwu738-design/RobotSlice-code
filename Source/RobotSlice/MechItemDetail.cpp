#include "MechItemDetail.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Input/HittestGrid.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Paths.h"
#include "MechFonts.h"
#include "MechGameMode.h"
#include "MechItemStudio.h"
#include "MechLoadout.h"
#include "MechSettings.h"
#include "MechUI.h"
#include "MechUIKit.h"
#include "MechUITheme.h"
#include "Rendering/DrawElements.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace MechDetailPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的东西撞
{
	const FMechUITheme& Th()
	{
		return MechUITheme::Get();
	}

	const FSlateBrush* Solid()
	{
		static FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	/** 卡片：深色底 + 细描边 */
	const FSlateBrush* Card(const FLinearColor& Outline, float Width = 1.f, const FLinearColor* Fill = nullptr)
	{
		static TMap<uint32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
		const FLinearColor F = Fill ? *Fill : Th().CardFill;
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(Outline.ToFColor(true)), GetTypeHash(F.ToFColor(true))), GetTypeHash(Width));
		TSharedPtr<FSlateRoundedBoxBrush>& B = Cache.FindOrAdd(Key);
		if (!B.IsValid())
		{
			B = MakeShared<FSlateRoundedBoxBrush>(F, 0.f, Outline, Width);  // 11-01 直角（和 MechUIKit 的控件一致）
		}
		return B.Get();
	}

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return MechFonts::Get(Size, bBold);
	}

	TSharedRef<STextBlock> Txt(const TAttribute<FText>& Text, int32 Size, const TAttribute<FSlateColor>& Color)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color);
	}

	TSharedRef<SWidget> Rule(const FLinearColor& Color, float Height = 1.f)
	{
		return SNew(SBox).HeightOverride(Height)[SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(Color)];
	}

	TSharedPtr<FSlateBrush> MakeImageBrush(const FVector2D& Size)
	{
		TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
		B->DrawAs = ESlateBrushDrawType::Image;
		B->ImageSize = Size;
		return B;
	}

	void SetBrushImage(FSlateBrush& B, UObject* Image)
	{
		if (B.GetResourceObject() != Image)
		{
			B.SetResourceObject(Image);
		}
	}

	/** 这一件有没有照片（没有实物模型的摄影棚不拍）。11-01 按「这一件」判断，不按类别：一类里可以有的有照片、有的只有图标 */
	bool HasPhotos(int32 Category, int32 Index)
	{
		if (Category < 0)
		{
			return false;
		}
		// 记下来：卡片的 Visibility 每帧问好几次，HasPhoto 里要按路径找资源（结果运行中不会变）
		static TMap<int32, bool> Known;
		const int32 Key = Category * 1000 + Index;
		if (const bool* Found = Known.Find(Key))
		{
			return *Found;
		}
		return Known.Add(Key, AMechItemStudio::HasPhoto(Category, Index));
	}

	/** 制造商标识（占位：斜体缩写剪影，品牌色版 SVG，Content/Slate/Makers；原件在 docs/世界观/标识/占位）。
	 *  文件不在（例如打包没带上这个目录）返回空，界面退回一小条品牌色 */
	const FSlateBrush* LogoBrush(EMechMaker M, float Height)
	{
		static TMap<int32, TSharedPtr<FSlateBrush>> Cache;
		const int32 Key = static_cast<int32>(M) * 1000 + FMath::RoundToInt(Height);
		if (const TSharedPtr<FSlateBrush>* Found = Cache.Find(Key))
		{
			return Found->Get();
		}
		const MechCatalog::FMaker& Mk = MechCatalog::Maker(M);
		TSharedPtr<FSlateBrush> B;
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Slate/Makers") / FString(Mk.Logo) + TEXT("_brand.svg"));
		if (*Mk.Logo && FPaths::FileExists(Path))
		{
			B = MakeShared<FSlateVectorImageBrush>(Path, FVector2D(Height * Mk.LogoAspect, Height));
		}
		Cache.Add(Key, B);
		return B.Get();
	}

	/** 测试 -MechLoadoutShots 的步骤：哪一类、哪一件、大图显示什么（-1 转台）、文件名 */
	struct FTestStep
	{
		int32 Category;
		int32 Index;
		int32 View;
		const TCHAR* Name;
	};
	/** View：-1 转台、0.. 详细视角、-2 不截图，量实时转台的开销 */
	const FTestStep DefaultSteps[] = {
		{MechLoadout::Shooter, 0, -1, TEXT("1_射击")},
		{MechLoadout::Melee, 3, -1, TEXT("2_近战")},
		{MechLoadout::Shoulder, 3, -1, TEXT("3_肩部")},
		{MechLoadout::Shoulder, 3, 0, TEXT("3b_肩部_挂在肩边")},
		{MechLoadout::Frame, 1, -1, TEXT("4_框架")},
		{MechLoadout::Booster, 2, -1, TEXT("5_推进器")},
		{MechLoadout::Booster, 2, 0, TEXT("5b_推进器_背面点火")},
		// 肩部的地雷（11-01 由战术装备并入）：放在最后，选配那段时间短，截不到也不影响前面
		{MechLoadout::Shoulder, static_cast<int32>(EMechShoulder::Mine), -1, TEXT("6_肩部_地雷")},
		// 最后量开销（7.5 秒）：只在测试场量——选配（-MechLobbyTest）整段只有约 25 秒，AI 选完就关界面
		{MechLoadout::Frame, 1, -2, TEXT("实时转台开销")},
	};
	/** -MechLoadoutShotsWeapons（10-31 武器减面）：13 把武器的转台大图各拍一张（改前改后对比细节） */
	const FTestStep WeaponSteps[] = {
		{MechLoadout::Shooter, 0, -1, TEXT("w00_步枪")}, {MechLoadout::Shooter, 1, -1, TEXT("w01_冲锋枪")}, {MechLoadout::Shooter, 2, -1, TEXT("w02_霰弹枪")},
		{MechLoadout::Shooter, 3, -1, TEXT("w03_充能步枪")}, {MechLoadout::Shooter, 4, -1, TEXT("w04_狙击枪")}, {MechLoadout::Shooter, 5, -1, TEXT("w05_双持手枪")},
		{MechLoadout::Melee, 0, -1, TEXT("w06_双手刀")}, {MechLoadout::Melee, 1, -1, TEXT("w07_快刀")}, {MechLoadout::Melee, 2, -1, TEXT("w08_单手刀")},
		{MechLoadout::Melee, 3, -1, TEXT("w09_大剑")}, {MechLoadout::Melee, 4, -1, TEXT("w10_双刀")}, {MechLoadout::Melee, 5, -1, TEXT("w11_钝器")},
		{MechLoadout::Melee, 6, -1, TEXT("w12_匕首")},
	};
	/** -MechLoadoutShotsDrones（11-01 补模型）：护盾和并入肩部的四件，转台和挂在肩边各拍一张 */
	const FTestStep DroneSteps[] = {
		{MechLoadout::Shoulder, 2, -1, TEXT("d2_护盾")}, {MechLoadout::Shoulder, 2, 0, TEXT("d2b_护盾_挂在肩边")},
		{MechLoadout::Shoulder, 7, -1, TEXT("d7_紧急脱离")}, {MechLoadout::Shoulder, 7, 0, TEXT("d7b_紧急脱离_挂在肩边")},
		{MechLoadout::Shoulder, 8, -1, TEXT("d8_弹射升空")}, {MechLoadout::Shoulder, 8, 0, TEXT("d8b_弹射升空_挂在肩边")},
		{MechLoadout::Shoulder, 9, -1, TEXT("d9_震荡波")}, {MechLoadout::Shoulder, 9, 0, TEXT("d9b_震荡波_挂在肩边")},
		{MechLoadout::Shoulder, 10, -1, TEXT("d10_地雷")}, {MechLoadout::Shoulder, 10, 0, TEXT("d10b_地雷_挂在肩边")},
	};
	TArrayView<const FTestStep> Steps()
	{
		static const bool bDrones = FParse::Param(FCommandLine::Get(), TEXT("MechLoadoutShotsDrones"));
		if (bDrones)
		{
			return TArrayView<const FTestStep>(DroneSteps);
		}
		// -MechLoadoutShotsAll（11-01 重打灯光时用）：有照片的每一类每一件各拍一张转台
		static const bool bAll = FParse::Param(FCommandLine::Get(), TEXT("MechLoadoutShotsAll"));
		if (bAll)
		{
			static TArray<FString> Names;
			static TArray<FTestStep> AllSteps;
			if (AllSteps.IsEmpty())
			{
				static const int32 Cats[] = {MechLoadout::Shooter, MechLoadout::Melee, MechLoadout::Shoulder, MechLoadout::Frame, MechLoadout::Booster};
				Names.Reserve(64);  // 下面存 Names 里字符串的指针，不能扩容搬家
				for (const int32 c : Cats)
				{
					for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
					{
						Names.Add(FString::Printf(TEXT("a%d_%02d"), c, i));
						AllSteps.Add({c, i, -1, *Names.Last()});
					}
				}
			}
			return TArrayView<const FTestStep>(AllSteps);
		}
		static const bool bWeapons = FParse::Param(FCommandLine::Get(), TEXT("MechLoadoutShotsWeapons"));
		return bWeapons ? TArrayView<const FTestStep>(WeaponSteps) : TArrayView<const FTestStep>(DefaultSteps);
	}

	bool bShotsFlag()
	{
		static const bool b = FParse::Param(FCommandLine::Get(), TEXT("MechLoadoutShots"));
		return b;
	}

	/** 这一页的卡片（宿主重建卡片时登记，MechTestSetCards） */
	TArray<TWeakPtr<SWidget>> TestCards;

	/** -MechLoadoutShots：这一页的卡片是不是都在窗口里、点卡片中心能不能点到它（直接查 Slate 的命中网格，和真的鼠标点击走同一套） */
	void CheckCards(const FString& What)
	{
		int32 Total = 0, Inside = 0, Hit = 0;
		FString Bad;
		for (const TWeakPtr<SWidget>& W : TestCards)
		{
			const TSharedPtr<SWidget> Card = W.Pin();
			if (!Card.IsValid())
			{
				continue;
			}
			++Total;
			const TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(Card.ToSharedRef());
			if (!Win.IsValid())
			{
				Bad += FString::Printf(TEXT(" #%d 没在窗口里"), Total);
				continue;
			}
			const FGeometry G = Card->GetTickSpaceGeometry();
			const FSlateRect R = G.GetLayoutBoundingRect();
			const FSlateRect C = Win->GetClientRectInScreen();
			const bool bInside = R.Left >= C.Left - 1.f && R.Top >= C.Top - 1.f && R.Right <= C.Right + 1.f && R.Bottom <= C.Bottom + 1.f;
			Inside += bInside ? 1 : 0;
			const TArray<FWidgetAndPointer> Path = Win->GetHittestGrid().GetBubblePath(R.GetCenter(), 0.f, false);
			const bool bHit = Path.ContainsByPredicate([&Card](const FWidgetAndPointer& P) { return &P.Widget.Get() == Card.Get(); });
			Hit += bHit ? 1 : 0;
			if (!bInside || !bHit)
			{
				Bad += FString::Printf(TEXT(" #%d（%.0f,%.0f–%.0f,%.0f%s%s）"), Total, R.Left - C.Left, R.Top - C.Top, R.Right - C.Left, R.Bottom - C.Top,
					bInside ? TEXT("") : TEXT(" 出了窗口"), bHit ? TEXT("") : TEXT(" 点不到"));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[loadoutshots] %s：卡片 %d 张，%d 张整张在窗口里，%d 张点中心能点到%s%s"), *What, Total, Inside, Hit,
			Bad.IsEmpty() ? TEXT("") : TEXT("；有问题："), *Bad);
	}

	/** -MechLoadoutShots 在测试场里：开局 3 秒后自动打开配装页（拍完由 SMechItemDetail 退出） */
	bool OpenRangeLoadout(float)
	{
		static const double Start = FPlatformTime::Seconds();
		if (!GEngine)
		{
			return true;
		}
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* W = Ctx.World();
			if (!W || !W->IsGameWorld() || !W->GetName().Contains(TEXT("L_Range")))
			{
				continue;
			}
			AMechPlayerController* PC = Cast<AMechPlayerController>(W->GetFirstPlayerController());
			if (!PC || !PC->GetPawn() || W->GetTimeSeconds() < 3.f)
			{
				continue;
			}
			UE_LOG(LogTemp, Display, TEXT("[loadoutshots] 测试场：打开配装页"));
			PC->OpenMenu(EMechMenuPage::Loadout);
			return false;
		}
		return FPlatformTime::Seconds() - Start < 180.0;
	}

	// 不能在全局对象的构造函数里直接读命令行 / 加 ticker：打包版是单个 exe，全局对象在 main 之前构造，
	// 那时命令行还没设，读它直接崩（退出码 777006 CrashDuringStaticInit，连日志都没有）；编辑器里本模块是后加载的 DLL 所以没事。
	// 改成引擎初始化完再注册（10-31 打包时发现）
	FDelayedAutoRegisterHelper RegisterRangeShots(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("MechLoadoutShots")))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OpenRangeLoadout), 0.5f);
		}
	});
}

// ================================================================ 照片
void SMechPhoto::Construct(const FArguments& InArgs)
{
	Size = InArgs._Size;
	BrushA = MechDetailPriv::MakeImageBrush(Size);
	BrushB = MechDetailPriv::MakeImageBrush(Size);
}

void SMechPhoto::SetImages(UObject* A, UObject* B, float InBlend)
{
	MechDetailPriv::SetBrushImage(*BrushA, A);
	MechDetailPriv::SetBrushImage(*BrushB, B);
	Blend = InBlend;
}

int32 SMechPhoto::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), MechDetailPriv::Solid(), ESlateDrawEffect::None,
		MechDetailPriv::Th().PhotoFill * Tint);
	if (BrushA->GetResourceObject())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), BrushA.Get(), ESlateDrawEffect::None, Tint);
	}
	// 转台两帧之间：后一帧按比例盖上去（相邻两帧只差 7.5 度，混起来看着是在转）
	if (Blend > 0.002f && BrushB->GetResourceObject())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), BrushB.Get(), ESlateDrawEffect::None,
			FLinearColor(Tint.R, Tint.G, Tint.B, Tint.A * Blend));
	}
	// 11-01 界面改版：四角折角（取景框的感觉，比一整圈描边轻）；小图、缩略图不画（太小了）
	if (Size.X >= 400.f)
	{
		const FVector2f L = FVector2f(AllottedGeometry.GetLocalSize());
		const FLinearColor Mark = MechDetailPriv::Th().Ink.CopyWithNewOpacity(0.45f) * Tint;
		constexpr float Inset = 10.f, Len = 22.f, T = 1.5f;
		const FVector2f Spots[][2] = {
			{{Inset, Inset}, {Len, T}}, {{Inset, Inset}, {T, Len}},
			{{L.X - Inset - Len, Inset}, {Len, T}}, {{L.X - Inset - T, Inset}, {T, Len}},
			{{Inset, L.Y - Inset - T}, {Len, T}}, {{Inset, L.Y - Inset - Len}, {T, Len}},
			{{L.X - Inset - Len, L.Y - Inset - T}, {Len, T}}, {{L.X - Inset - T, L.Y - Inset - Len}, {T, Len}},
		};
		for (const auto& Spot : Spots)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(Spot[1], FSlateLayoutTransform(Spot[0])), MechDetailPriv::Solid(), ESlateDrawEffect::None, Mark);
		}
	}
	return LayerId + 3;
}

FReply SMechPhoto::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true;
		LastDrag = FPlatformTime::Seconds();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SMechPhoto::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		LastDrag = FPlatformTime::Seconds();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SMechPhoto::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && HasMouseCapture())
	{
		// 往右拖 = 物件跟着手往右转（镜头往左绕 = 方位角变大）；拖满照片宽度转一圈。
		// 10-31 以前这里是减号：注释写的往右转，实际物件往左转（和手反着），是用户说「旋转很奇怪」的原因之一
		Angle += MouseEvent.GetCursorDelta().X / FMath::Max(MyGeometry.Scale, 0.1f) * 360.f / FMath::Max(Size.X, 1.f);
		LastDrag = FPlatformTime::Seconds();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FCursorReply SMechPhoto::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(bDragging ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
}

// ================================================================ 读数图
void SMechRadar::Construct(const FArguments& InArgs)
{
	Size = InArgs._Size;
}

int32 SMechRadar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace MechDetailPriv;
	const int32 N = FMath::Min(Labels.Num(), Values.Num());
	if (N < 3)
	{
		return LayerId;
	}
	const FMechUITheme& T = Th();
	const FVector2f Local = FVector2f(AllottedGeometry.GetLocalSize());
	const FVector2f C = Local * 0.5f;
	const float R = FMath::Min(Local.X, Local.Y) * 0.32f;
	auto Dir = [N](int32 i) {
		const float A = FMath::DegreesToRadians(-90.f + 360.f * i / N);
		return FVector2f(FMath::Cos(A), FMath::Sin(A));
	};
	auto Ring = [&](float Scale, const FLinearColor& Color, float Thick, int32 Layer) {
		TArray<FVector2f> P;
		for (int32 i = 0; i <= N; ++i)
		{
			P.Add(C + Dir(i % N) * R * Scale);
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), P, ESlateDrawEffect::None, Color, true, Thick);
	};
	auto Poly = [&](const TArray<float>& V, const FLinearColor& Color, float Thick, int32 Layer) {
		if (V.Num() < N)
		{
			return;
		}
		TArray<FVector2f> P;
		for (int32 i = 0; i <= N; ++i)
		{
			P.Add(C + Dir(i % N) * R * FMath::Clamp(V[i % N], 0.04f, 1.f));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), P, ESlateDrawEffect::None, Color, true, Thick);
	};
	// 底：两圈、轴线
	Ring(1.f, T.Faint, 1.f, LayerId);
	Ring(0.5f, T.Faint.CopyWithNewOpacity(T.Faint.A * 0.6f), 1.f, LayerId);
	for (int32 i = 0; i < N; ++i)
	{
		TArray<FVector2f> P = {C, C + Dir(i) * R};
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), P, ESlateDrawEffect::None, T.Faint.CopyWithNewOpacity(T.Faint.A * 0.6f), true, 1.f);
	}
	// 对比的那件（已装备）淡一点，这一件实线
	Poly(Compare, T.Dim.CopyWithNewOpacity(0.7f), 1.f, LayerId + 1);
	Poly(Values, T.Accent, 2.f, LayerId + 2);
	// 轴名
	const FSlateFontInfo LabelFont = Font(10);
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	for (int32 i = 0; i < N; ++i)
	{
		// 11-01 轴名只写前两个字：四个字的（失衡上限、空中滑翔）左边压到说明、右边压到红队那一栏；全名就在下面参数行，顺序一样
		const FString S = Labels[i].ToString().Left(2);
		const FVector2f Sz = FVector2f(Measure->Measure(S, LabelFont));
		const FVector2f At = C + Dir(i) * (R + 8.f) + FVector2f(Dir(i).X * Sz.X * 0.5f - Sz.X * 0.5f, Dir(i).Y * Sz.Y * 0.5f - Sz.Y * 0.5f);
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(Sz, FSlateLayoutTransform(At)), S, LabelFont,
			ESlateDrawEffect::None, T.Dim);
	}
	return LayerId + 3;
}

// ================================================================ 详细信息
void SMechItemDetail::Construct(const FArguments& InArgs)
{
	using namespace MechDetailPriv;
	Owner = InArgs._Owner;
	GetCategory = InArgs._GetCategory;
	GetIndex = InArgs._GetIndex;
	GetCompare = InArgs._GetCompare;
	OnTestSelect = InArgs._OnTestSelect;
	TestName = InArgs._TestName;
	PhotoSize = InArgs._PhotoSize;
	InfoWidth = InArgs._InfoWidth;
	PipBrush = MakeImageBrush(FVector2D(240.f, 135.f));
	for (int32 i = 0; i <= AMechItemStudio::MaxViews; ++i)
	{
		StripBrushes.Add(MakeImageBrush(FVector2D(132.f, 74.f)));
	}
	const TAttribute<FText> Status = InArgs._Status;
	const TAttribute<FSlateColor> StatusColor = InArgs._StatusColor;
	GetFrame = InArgs._GetFrame;
	const float RadarSize = FMath::RoundToFloat(FMath::Clamp(InfoWidth * 0.33f, 150.f, 176.f));

	// 11-01 排版：制造商一行（整宽）→ 左边名字、型号、状态、说明，右边读数图（以前读数图和制造商行挤在一起）→ 细线 → 参数（一行一个、等高）→ 同厂加成一块。
	// 去掉了「名称草案」标记（开发时的备注）和最下面几乎看不见的灰字公司介绍
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[MakePhoto()]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[MakeStrip()]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(28.f, 2.f, 0.f, 0.f))
		[
			SNew(SBox)
			.WidthOverride(InfoWidth)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MakeInfo()]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SVerticalBox)
						// 名字（大字）
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Font(MechKit::Light(34))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
							.Text_Lambda([this]() { return Category < 0 ? FText::GetEmpty() : FText::FromString(MechCatalog::Quoted(Category, Index)); })
						]
						// 型号 · 类别全称 · 配装类
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Font(MechKit::Latin(15))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text_Lambda([this]() {
								if (Category < 0)
								{
									return FText::GetEmpty();
								}
								const MechCatalog::FInfo& I = MechCatalog::Info(Category, Index);
								return FText::FromString(FString::Printf(TEXT("%s · %s · %s"), I.Model, I.Kind, MechLoadout::CategoryName(Category)));
							})
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 4.f, 0.f, 0.f))
						[
							SNew(STextBlock).Font(Font(14)).Text(Status).ColorAndOpacity(StatusColor)
							.Visibility_Lambda([Status]() { return Status.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 6.f, 8.f, 0.f))
						[
							SNew(STextBlock)
							.Font(Font(14))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
							.AutoWrapText(true)
							.Text_Lambda([this]() { return Category < 0 ? FText::GetEmpty() : FText::FromString(MechLoadout::Item(Category, Index).Desc); })
						]
					]
					// 读数图（雷达图，前 6 项参数；淡色的是对比那件 = 已装备 / 已选）
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					[
						SAssignNew(Radar, SMechRadar).Size(RadarSize)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 4.f))[MechKit::Hairline(1.4f)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(0)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(1)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(2)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(3)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(4)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(5)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(6)]
				+ SVerticalBox::Slot().AutoHeight()[MakeStatRow(7)]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[MakeBuffBlock()]
			]
		]
	];
}

// ================================================================ 图标、卡片内容、配装槽位（选配和测试场共用）
void SMechItemIcon::Construct(const FArguments& InArgs)
{
	GetCategory = InArgs._GetCategory;
	GetIndex = InArgs._GetIndex;
	bPlain = InArgs._Plain;
}

int32 SMechItemIcon::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace MechDetailPriv;
	const int32 C = GetCategory ? GetCategory() : -1;
	if (C < 0)
	{
		return LayerId;
	}
	const int32 I = GetIndex ? GetIndex() : -1;
	const FMechUITheme& T = Th();
	const FVector2f S = FVector2f(G.GetLocalSize());
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	// 底：和照片幕布一样上亮下暗，不是黑块
	if (!bPlain)
	{
		TArray<FSlateGradientStop> Stops;
		Stops.Add(FSlateGradientStop(FVector2f(0.f, 0.f), FLinearColor(0.021f, 0.028f, 0.04f, Tint.A)));
		Stops.Add(FSlateGradientStop(FVector2f(0.f, S.Y), FLinearColor(0.006f, 0.008f, 0.012f, Tint.A)));
		FSlateDrawElement::MakeGradient(Out, LayerId, G.ToPaintGeometry(), MoveTemp(Stops), Orient_Horizontal);
	}
	const FVector2f Ctr = S * 0.5f;
	const float R = FMath::Min(S.X, S.Y) * 0.3f;
	const float Th1 = FMath::Clamp(R * 0.1f, 1.5f, 4.f);
	auto Col = [&](float A) { return FLinearColor(T.Accent.R, T.Accent.G, T.Accent.B, A * Tint.A); };
	auto Line = [&](TArray<FVector2f> P, float A) {
		FSlateDrawElement::MakeLines(Out, LayerId + 1, G.ToPaintGeometry(), MoveTemp(P), ESlateDrawEffect::None, Col(A), true, Th1);
	};
	auto Ring = [&](float Radius, float A) {
		TArray<FVector2f> P;
		for (int32 i = 0; i <= 40; ++i)
		{
			const float An = 2.f * PI * i / 40.f;
			P.Add(Ctr + FVector2f(FMath::Cos(An), FMath::Sin(An)) * Radius);
		}
		Line(MoveTemp(P), A);
	};
	// 按名字认符号，不按类别和序号：这几件换了类别、排了别的位置都不用改这里。新加的没照片的东西先用默认的菱形，不会是黑块
	const FString Name = I >= 0 ? FString(MechLoadout::Item(C, I).Name) : FString();
	const int32 Glyph = I < 0 ? -1 : Name == TEXT("紧急脱离") ? 0 : Name == TEXT("弹射升空") ? 1 : Name == TEXT("震荡波") ? 2 : Name == TEXT("地雷") ? 3 : 4;
	switch (Glyph)
	{
	case 0:  // 紧急脱离：三道往前冲的箭头，越往前越亮
		for (int32 k = 0; k < 3; ++k)
		{
			const float X = Ctr.X + R * (-0.75f + 0.75f * k);
			Line({FVector2f(X - R * 0.4f, Ctr.Y - R * 0.75f), FVector2f(X + R * 0.4f, Ctr.Y), FVector2f(X - R * 0.4f, Ctr.Y + R * 0.75f)}, 0.35f + 0.325f * k);
		}
		break;
	case 1:  // 弹射升空：地面一条线，上面两道向上的箭头
		Line({FVector2f(Ctr.X - R, Ctr.Y + R), FVector2f(Ctr.X + R, Ctr.Y + R)}, 0.6f);
		for (int32 k = 0; k < 2; ++k)
		{
			const float Y = Ctr.Y + R * (0.35f - 0.8f * k);
			Line({FVector2f(Ctr.X - R * 0.7f, Y + R * 0.3f), FVector2f(Ctr.X, Y - R * 0.4f), FVector2f(Ctr.X + R * 0.7f, Y + R * 0.3f)}, 0.55f + 0.45f * k);
		}
		break;
	case 2:  // 震荡波：中间一点，外面三圈，越往外越淡
		Ring(R * 0.16f, 1.f);
		Ring(R * 0.45f, 0.95f);
		Ring(R * 0.75f, 0.6f);
		Ring(R * 1.05f, 0.3f);
		break;
	case 3:  // 地雷：圆壳 + 中心 + 四根触发针
		Ring(R * 0.72f, 1.f);
		Ring(R * 0.2f, 0.9f);
		for (int32 k = 0; k < 4; ++k)
		{
			const float An = PI * 0.25f + PI * 0.5f * k;
			const FVector2f D(FMath::Cos(An), FMath::Sin(An));
			Line({Ctr + D * R * 0.72f, Ctr + D * R * 1.08f}, 1.f);
		}
		break;
	case 4:  // 默认：菱形 + 中心一点
		Line({FVector2f(Ctr.X, Ctr.Y - R), FVector2f(Ctr.X + R, Ctr.Y), FVector2f(Ctr.X, Ctr.Y + R), FVector2f(Ctr.X - R, Ctr.Y), FVector2f(Ctr.X, Ctr.Y - R)}, 0.9f);
		Ring(R * 0.18f, 1.f);
		break;
	default:
		break;
	}
	return LayerId + 1;
}

TSharedRef<SWidget> MechItemPicture(TFunction<int32()> GetCategory, TFunction<int32()> GetIndex, TFunction<const FSlateBrush*(int32, int32)> Thumb)
{
	using namespace MechDetailPriv;
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFill)
			.Clipping(EWidgetClipping::ClipToBounds)
			.Visibility_Lambda([GetCategory, GetIndex]() { return HasPhotos(GetCategory(), GetIndex()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SBox)
				.WidthOverride(160.f)
				.HeightOverride(100.f)
				[
					SNew(SImage).Image_Lambda([GetCategory, GetIndex, Thumb]() -> const FSlateBrush* { return GetCategory() >= 0 ? Thumb(GetCategory(), GetIndex()) : nullptr; })
				]
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SMechItemIcon)
			.GetCategory(GetCategory)
			.GetIndex(GetIndex)
			.Visibility_Lambda([GetCategory, GetIndex]() { return GetCategory() >= 0 && !HasPhotos(GetCategory(), GetIndex()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		];
}

TSharedRef<SWidget> MechCardBody(int32 InCategory, int32 InIndex, const TSharedRef<SWidget>& Picture, float Width, float Height, TFunction<FText()> TagText, TFunction<FLinearColor()> TagColor,
	TFunction<FLinearColor()> NameColor)
{
	using namespace MechDetailPriv;
	// 框架页是三家并排的窄卡片：企业名已经在分组名牌上，卡片写「轻型 · 斥候」+ 型号（「PHASE-F1「双塔」」一行放不下）
	const bool bFrame = InCategory == MechLoadout::Frame;
	const MechCatalog::FInfo& Info = MechCatalog::Info(InCategory, InIndex);
	const FString CardName = bFrame ? FString::Printf(TEXT("%s · %s"), *MechFrameShortName(InIndex), Info.Title) : FString(MechLoadout::Item(InCategory, InIndex).Name);
	const FString CardModel = bFrame ? FString(Info.Model) : FString::Printf(TEXT("%s「%s」"), Info.Model, Info.Title);
	const bool bWide = Width >= 280.f;
	// 右上角小标签（对面不能用 / 你不能用 / 已装备 / 待做）：压在照片上要看得清，垫一块暗底
	TSharedRef<SWidget> Tag = SNew(SBorder)
		.BorderImage(MechKit::White())
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.72f))
		.Padding(FMargin(6.f, 1.f, 6.f, 2.f))
		.Visibility_Lambda([TagText]() { return TagText().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
		[
			SNew(STextBlock)
			.Font(Font(12))
			.Text_Lambda([TagText]() { return TagText(); })
			.ColorAndOpacity_Lambda([TagColor]() { return FSlateColor(TagColor()); })
		];
	TSharedRef<SWidget> PictureArea = SNew(SOverlay)
		+ SOverlay::Slot()[Picture]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 4.f, 4.f, 0.f))[Tag];
	if (Height < 130.f)
	{
		// 矮卡片（一页件数多、排两行）：照片比例接近 16:10，铺满整张；名字和型号压在照片底边，垫一条暗底
		return SNew(SBox)
			.Padding(FMargin(1.f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[PictureArea]
				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(SBorder)
					.BorderImage(MechKit::White())
					.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.62f))
					.Padding(FMargin(8.f, 1.f, 6.f, 2.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(Font(14))
							.Text(FText::FromString(CardName))
							.ColorAndOpacity_Lambda([NameColor]() { return FSlateColor(NameColor()); })
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(FMargin(8.f, 1.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Font(MechKit::Latin(11))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text(FText::FromString(FString(Info.Model)))
						]
					]
				]
			];
	}
	TSharedRef<SVerticalBox> Words = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(Font(bFrame ? 14 : 15))
			.Text(FText::FromString(CardName))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.ColorAndOpacity_Lambda([NameColor]() { return FSlateColor(NameColor()); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 1.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Font(MechKit::Latin(bFrame ? 11 : 12))
			.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(FText::FromString(CardModel))
		];
	if (bWide)
	{
		// 宽卡片（一页只有三四件）：左图右字，多写前几行参数，宽度才用得上（一句话说明太长，四张并排时放不下，完整的在上面详情里）
		TArray<MechCatalog::FStat> CardStats;
		MechCatalog::Stats(InCategory, InIndex, CardStats);
		int32 Rows = 0;
		for (const MechCatalog::FStat& S : CardStats)
		{
			if (Rows >= 4)
			{
				break;
			}
			if (S.Raw <= 0.f || S.Label.ToString() == TEXT("同厂加成"))
			{
				continue;  // 写「—」的那几行（脱离没有伤害、震荡波没有距离）不写
			}
			Words->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, Rows == 0 ? 7.f : 0.f, 0.f, 0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox).WidthOverride(68.f)[Txt(S.Label, 12, MechKit::Color(&FMechUITheme::Dim))]  // 四个字的行名（地面冲刺）要放下
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(STextBlock)
					.Font(MechKit::Latin(12))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink, 0.85f))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text(FText::FromString(S.Value))
				]
			];
			++Rows;
		}
		return SNew(SBox)
			.Padding(FMargin(1.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox).WidthOverride(FMath::RoundToFloat(Width * 0.36f))[PictureArea]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Top)
				.Padding(FMargin(12.f, 9.f, 10.f, 6.f))
				[
					Words
				]
			];
	}
	return SNew(SBox)
		.Padding(FMargin(1.f))  // 照片铺满到边（只让出 1 像素描边）：以前四周留 8 像素，黑底小图像贴在卡片上的一块
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f)[PictureArea]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(9.f, 6.f, 8.f, 7.f))[Words]
		];
}

TSharedRef<SWidget> MechLoadoutSlot(int32 InCategory, TFunction<int32()> GetIndex, TFunction<const FSlateBrush*(int32, int32)> Thumb, TFunction<bool()> IsCurrent, TFunction<void()> OnClick,
	TFunction<FText()> Note, TFunction<FLinearColor()> NoteColor, float Width)
{
	using namespace MechDetailPriv;
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(72.f)
		[
			SNew(SMechKitButton)
			.Look(EMechKitLook::Chip)
			.Selected_Lambda([IsCurrent]() { return IsCurrent(); })
			.OnClick(OnClick)
			[
				SNew(SBox)
				.Padding(FMargin(1.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(FMath::Min(FMath::RoundToFloat(Width * 0.45f), 112.f))  // 小图保持约 16:10，枪不被裁掉两头
						[
							MechItemPicture([InCategory]() { return InCategory; }, GetIndex, Thumb)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(FMargin(9.f, 0.f, 6.f, 0.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[Txt(FText::FromString(MechLoadout::CategoryName(InCategory)), 12, MechKit::Color(&FMechUITheme::Dim))]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Font(Font(16))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text_Lambda([InCategory, GetIndex]() { return FText::FromString(MechLoadout::Item(InCategory, GetIndex()).Name); })
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Font(Font(11))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text_Lambda([Note]() { return Note(); })
							.ColorAndOpacity_Lambda([NoteColor]() { return FSlateColor(NoteColor()); })
						]
					]
				]
			]
		];
}

float MechCardWidth(int32 Count, float RowWidth, float Gap)
{
	const int32 N = FMath::Max(Count, 1);
	return FMath::FloorToFloat((RowWidth - 2.f - (N - 1) * Gap) / N);  // 留 2 像素：六张 193 宽正好等于 1208 时 SWrapBox 判定放不下，第六张掉到第二行
}

TArrayView<const int32> MechLoadoutCategories()
{
	static const int32 Order[] = {MechLoadout::Shooter, MechLoadout::Melee, MechLoadout::Shoulder, MechLoadout::Frame, MechLoadout::Booster};
	return TArrayView<const int32>(Order);
}

FMechCardLayout MechCardLayout(int32 Count, float RowWidth, float AreaHeight, float Gap)
{
	const int32 N = FMath::Max(Count, 1);
	FMechCardLayout L;
	// 一排放得下（每张至少 130 宽，照片还看得清）：高度跟着宽度走，照片区保持 16:10（再拉高铺满时会把枪的两头裁掉）；58 是名字和型号
	const float OneRow = MechCardWidth(N, RowWidth, Gap);
	if (OneRow >= 130.f)
	{
		L.Width = OneRow;
		L.Height = FMath::Clamp(FMath::RoundToFloat(OneRow * 0.625f) + 58.f, 140.f, FMath::Min(190.f, AreaHeight));
		L.Rows = 1;
		return L;
	}
	// 排两行（再多三行）：高度平分，宽度不要比高度的 2 倍还宽（太扁的话铺满时物件上下被裁掉），所以第一行可能比第二行多几张
	for (int32 Rows = 2; Rows <= 3; ++Rows)
	{
		L.Rows = Rows;
		L.Height = FMath::FloorToFloat((AreaHeight - (Rows - 1) * Gap) / Rows);
		const int32 PerRow = FMath::Max(FMath::CeilToInt(float(N) / Rows), FMath::CeilToInt((RowWidth + Gap) / (L.Height * 2.f + Gap)));
		L.Width = MechCardWidth(PerRow, RowWidth, Gap);
		if (L.Width >= L.Height * 1.3f)
		{
			break;  // 比例还行就不再加行
		}
	}
	return L;
}

void MechTestExpandCards(int32 InCategory, TArray<int32>& Order)
{
	static const int32 Want = []() {
		int32 V = 0;
		FParse::Value(FCommandLine::Get(), TEXT("MechCardCount="), V);
		return V;
	}();
	if (Want > Order.Num() && InCategory == MechLoadout::Shoulder && Order.Num() > 0)
	{
		const int32 Source = Order.Num();
		for (int32 i = Source; i < Want; ++i)
		{
			const int32 Again = Order[i % Source];  // 先拷出来：不能把数组自己的元素传给 Add（扩容时引用就失效了）
			Order.Add(Again);
		}
	}
}

TSharedRef<SWidget> MechMakerGroupHeader(EMechMaker Maker, float Width, float Height)
{
	using namespace MechDetailPriv;
	// 10-31 改成横条（以前是卡片那么高的一格，三家一家一行时排在行首）：三家并排时每家上面一条
	const MechCatalog::FMaker& M = MechCatalog::Maker(Maker);
	const float LogoH = FMath::Clamp(Height - 12.f, 12.f, 18.f);
	const FSlateBrush* Logo = LogoBrush(Maker, LogoH);
	TSharedRef<SWidget> Mark = Logo
		? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(LogoH * M.LogoAspect).HeightOverride(LogoH)[SNew(SImage).Image(Logo)])
		: StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(4.f).HeightOverride(LogoH)[SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(M.Color)]);
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(Height)
		[
			SNew(SOverlay)
			+ SOverlay::Slot().VAlign(VAlign_Bottom)[Rule(M.Color.CopyWithNewOpacity(0.7f), 2.f)]
			+ SOverlay::Slot()
			.Padding(FMargin(2.f, 0.f, 2.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))[Mark]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Txt(FText::FromString(M.Name), 14, MechKit::Color(&FMechUITheme::Ink))]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(10.f, 2.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Font(Font(10))
					.ColorAndOpacity(M.Color.CopyWithNewOpacity(0.85f))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text(FText::FromString(M.Slogan))
				]
			]
		];
}

TSharedRef<SWidget> MechFrameGroups(float Width, float CardHeight, TFunctionRef<TSharedRef<SWidget>(int32, float, float)> MakeCard,
	TArray<TWeakPtr<SWidget>>& OutCards)
{
	// 三家并排：家与家之间 14、卡片之间 6。测试场 1588 宽 → 每家 520、卡片 169；选配 1208 宽 → 每家 393、卡片 127
	constexpr float GroupGap = 14.f;
	constexpr float CardGap = 6.f;
	constexpr float HeaderH = 28.f;
	const float GroupW = FMath::FloorToFloat((Width - 2.f * GroupGap) / 3.f);
	const float CardW = FMath::FloorToFloat((GroupW - 2.f * CardGap) / 3.f);
	TArray<int32> Order;
	MechLoadout::DisplayOrder(MechLoadout::Frame, Order);
	OutCards.Reset();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 m = 0; m < 3; ++m)
	{
		TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
		int32 n = 0;
		for (const int32 i : Order)
		{
			if (MechLoadout::GroupOf(MechLoadout::Frame, i) != m)
			{
				continue;
			}
			const TSharedRef<SWidget> C = MakeCard(i, CardW, CardHeight);
			OutCards.Add(C);
			Cards->AddSlot().AutoWidth().Padding(FMargin(n++ > 0 ? CardGap : 0.f, 0.f, 0.f, 0.f))[C];
		}
		Row->AddSlot()
		.AutoWidth()
		.Padding(FMargin(m > 0 ? GroupGap : 0.f, 0.f, 0.f, 0.f))
		[
			SNew(SBox)
			.WidthOverride(GroupW)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MechMakerGroupHeader(static_cast<EMechMaker>(m), GroupW, HeaderH)]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))[Cards]
			]
		];
	}
	// 只缩不放：窗口比 16:9 窄（逻辑宽度不到 1920）时整块等比缩小，不会挤出屏幕
	return SNew(SScaleBox)
		.Stretch(EStretch::ScaleToFit)
		.StretchDirection(EStretchDirection::DownOnly)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			Row
		];
}

FString MechFrameShortName(int32 FrameIndex)
{
	const FString Name = MechLoadout::Item(MechLoadout::Frame, FrameIndex).Name;
	int32 Space = INDEX_NONE;
	return Name.FindLastChar(TEXT(' '), Space) ? Name.Mid(Space + 1) : Name;
}

void MechTestSetCards(const TArray<TWeakPtr<SWidget>>& Cards)
{
	MechDetailPriv::TestCards = Cards;
}

SMechItemDetail::~SMechItemDetail()
{
	// -MechLoadoutShots：步骤还没走完界面就被关了（选配阶段到时间、AI 都选完）——别卡在那里等，直接退出
	if (MechDetailPriv::bShotsFlag() && TestStep >= 0 && TestStep < MechDetailPriv::Steps().Num() && Owner.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("[loadoutshots] %s：界面在第 %d 步被关掉（选配结束），提前退出"), *TestName, TestStep + 1);
		Owner->ConsoleCommand(TEXT("quit"));
	}
	// 界面关掉：放掉转台和详细视角的贴图（小图留着，下次打开马上有）
	if (Index >= 0 && Owner.IsValid())
	{
		if (AMechItemStudio* S = Owner->GetStudio())
		{
			S->ReleasePreview();
		}
	}
}

AMechItemStudio* SMechItemDetail::Studio() const
{
	AMechPlayerController* PC = Owner.Get();
	return PC ? PC->GetStudio() : nullptr;
}

void SMechItemDetail::Prewarm()
{
	bPrewarmed = true;
	if (AMechItemStudio* S = Studio())
	{
		for (int32 c = 0; c < MechLoadout::NumCategories; ++c)
		{
			for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
			{
				S->GetThumb(c, i);
			}
		}
	}
}

void SMechItemDetail::Refocus(int32 InCategory, int32 InIndex)
{
	Category = InCategory;
	Index = InIndex;
	ViewMode = -1;
	if (Photo.IsValid())
	{
		// 换一件：从小图那个角度开始，自动慢转从 0 慢慢加速（不带上一件的惯性）
		Photo->Angle = Photo->PrevAngle = 0.f;
		Photo->SpinVelocity = 0.f;
	}
	AutoSpin = 0.f;
	MechCatalog::Stats(Category, Index, Stats);
	Stats.RemoveAll([](const MechCatalog::FStat& S) { return S.Label.ToString() == TEXT("同厂加成"); });  // 11-01 单独一块（MakeBuffBlock），不算参数行
	CompareIndex = -2;  // 下面 Tick 里重算对比
	FocusStart = FPlatformTime::Seconds();
	WorstFrame = 0.f;
	bBakeLogged = false;
	if (Radar.IsValid())
	{
		Radar->Labels.Reset();
		Radar->Values.Reset();
		for (const MechCatalog::FStat& S : Stats)
		{
			if (Radar->Labels.Num() < 6)
			{
				Radar->Labels.Add(S.Label);
				Radar->Values.Add(S.Bar);
			}
		}
	}
	if (AMechItemStudio* S = Studio())
	{
		S->SetPreview(Category, Index);
	}
}

void SMechItemDetail::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!GetCategory || !GetIndex)
	{
		return;
	}
	if (!bPrewarmed)
	{
		Prewarm();
	}
	const int32 C = GetCategory();
	const int32 I = GetIndex();
	if (C != Category || I != Index)
	{
		Refocus(C, I);
	}
	const int32 Cmp = GetCompare ? GetCompare() : -1;
	if (Cmp != CompareIndex)
	{
		CompareIndex = Cmp;
		CompareStats.Reset();
		if (Cmp >= 0 && Cmp != Index)
		{
			MechCatalog::Stats(Category, Cmp, CompareStats);
		}
		if (Radar.IsValid())
		{
			Radar->Compare.Reset();
			for (int32 r = 0; r < Radar->Labels.Num(); ++r)
			{
				const MechCatalog::FStat* Found = CompareStats.FindByPredicate([&](const MechCatalog::FStat& S) { return S.Label.EqualTo(Radar->Labels[r]); });
				Radar->Compare.Add(Found ? Found->Bar : 0.f);
			}
			if (CompareStats.Num() == 0)
			{
				Radar->Compare.Reset();
			}
		}
	}
	AMechItemStudio* S = Studio();
	if (!S || !Photo.IsValid())
	{
		return;
	}
	// 转台的角度（10-31 重做，用户：「配装界面旋转很奇怪」）：
	//  拖动时跟手（SMechPhoto::OnMouseMove），这里记角速度；松手后带一点惯性（约 0.17 秒减半）；
	//  停手 2.5 秒后接着自动慢转，速度在 1.5 秒里从 0 加到 20 度/秒（18 秒一圈）。以前停手 1.5 秒直接满速 24 度/秒。测试截图时不转
	const double Now = FPlatformTime::Seconds();
	GeomScale = AllottedGeometry.Scale;
	const float Dt = FMath::Min(InDeltaTime, 0.1f);
	if (ViewMode < 0 && TestStep < 0)
	{
		if (Photo->bDragging)
		{
			const float Inst = Dt > 0.f ? (Photo->Angle - Photo->PrevAngle) / Dt : 0.f;
			Photo->SpinVelocity = FMath::Lerp(Photo->SpinVelocity, FMath::Clamp(Inst, -720.f, 720.f), FMath::Min(1.f, Dt * 20.f));
			AutoSpin = 0.f;
		}
		else
		{
			Photo->Angle += Photo->SpinVelocity * Dt;
			Photo->SpinVelocity *= FMath::Exp(-Dt * 4.f);
			if (FMath::Abs(Photo->SpinVelocity) < 1.f)
			{
				Photo->SpinVelocity = 0.f;
			}
			AutoSpin = Now - Photo->LastDrag > 2.5 ? FMath::Min(1.f, AutoSpin + Dt / 1.5f) : 0.f;
			Photo->Angle += FMath::SmoothStep(0.f, 1.f, AutoSpin) * 20.f * Dt;
		}
		Photo->Angle = FMath::Fmod(Photo->Angle, 3600.f);
	}
	Photo->PrevAngle = Photo->Angle;
	UTextureRenderTarget2D* A = nullptr;
	if (ViewMode >= 0)
	{
		A = S->GetView(ViewMode);
	}
	else if (!bTestLiveOff)
	{
		// 实时拍：按照片在屏幕上的像素大小拍这一帧的角度（只有这一页在显示时才会走到这里）
		const FVector2D Px = PhotoSize * GeomScale;
		A = S->ShootLive(Category, Index, Photo->Angle, FIntPoint(FMath::RoundToInt(Px.X), FMath::RoundToInt(Px.Y)));
	}
	else
	{
		A = S->GetPreview();  // 测试量开销时停拍：一直放最后一张
	}
	if (!A)
	{
		A = S->GetThumb(Category, Index);  // 还没拍好（机体刚换网格要等 2–4 帧姿势）：先放小图
	}
	Photo->SetImages(A, nullptr, 0.f);
	// 肩部里紧急脱离等四件（11-01 并入）没有照片，不留上一件的画中画
	MechDetailPriv::SetBrushImage(*PipBrush, Category == MechLoadout::Shoulder && ViewMode < 0 && MechDetailPriv::HasPhotos(Category, Index) ? S->GetView(0) : nullptr);
	MechDetailPriv::SetBrushImage(*StripBrushes[0], S->GetThumb(Category, Index));
	for (int32 v = 0; v < AMechItemStudio::MaxViews; ++v)
	{
		MechDetailPriv::SetBrushImage(*StripBrushes[v + 1], S->GetView(v));
	}
	// 拍这一件花了多久、期间最长的一帧（日志 [loadoutshots]，看进页面 / 换一件时卡不卡）
	if (!bBakeLogged)
	{
		WorstFrame = FMath::Max(WorstFrame, InDeltaTime);
		if (S->GetPreviewProgress() >= 1.f)
		{
			bBakeLogged = true;
			UE_LOG(LogTemp, Display, TEXT("[loadoutshots] %s %s 照片拍完 %.2f 秒（含贴图流送后补拍），期间最长一帧 %.1f 毫秒"),
				MechLoadout::CategoryName(Category), *MechCatalog::FullName(Category, Index), Now - FocusStart, WorstFrame * 1000.f);
		}
	}
	TestTick(InDeltaTime);
}

TSharedRef<SWidget> SMechItemDetail::MakePhoto()
{
	using namespace MechDetailPriv;
	const FVector2D Pip(FMath::RoundToFloat(PhotoSize.X * 0.3f), FMath::RoundToFloat(PhotoSize.X * 0.3f * 9.f / 16.f));
	return SNew(SBox)
		.WidthOverride(PhotoSize.X)
		.HeightOverride(PhotoSize.Y)
		[
			SNew(SBorder)
			.BorderImage_Lambda([]() { return MechKit::Box(Th().PhotoFill, Th().Ink.CopyWithNewOpacity(0.14f), 1.f); })  // 11-01 很淡的细描边（以前是一圈强调色），重点留给四角折角
			.Padding(FMargin(1.f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SAssignNew(Photo, SMechPhoto).Size(PhotoSize)]
				// 11-01 没照片时铺一层和照片幕布一样的渐变（以前是纯黑）
				+ SOverlay::Slot()
				[
					SNew(SMechItemIcon)
					.GetCategory([this]() { return Category; })
					.GetIndex([]() { return -1; })
					.Visibility_Lambda([this]() { return Category >= 0 && !HasPhotos(Category, Index) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				]
				// 没有照片的一件（肩部的紧急脱离等四件没有实物模型，摄影棚不拍）：照片的位置写名字、类别全称和一句话说明，参数看右边
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(FMargin(48.f, 0.f))
				[
					SNew(SVerticalBox)
					.Visibility_Lambda([this]() { return Category >= 0 && !HasPhotos(Category, Index) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					// 11-01 大符号 + 名字 + 型号（以前这里只有字；说明右边已经写了，这里不再写一遍）
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(0.f, 0.f, 0.f, 6.f))
					[
						SNew(SBox)
						.WidthOverride(FMath::RoundToFloat(PhotoSize.Y * 0.56f))
						.HeightOverride(FMath::RoundToFloat(PhotoSize.Y * 0.56f))
						[
							SNew(SMechItemIcon).Plain(true).GetCategory([this]() { return Category; }).GetIndex([this]() { return Index; })
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						Txt(TAttribute<FText>::CreateLambda([this]() { return FText::FromString(MechLoadout::Item(FMath::Max(Category, 0), FMath::Max(Index, 0)).Name); }), 34, Th().Ink)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
					[
						Txt(TAttribute<FText>::CreateLambda([this]() {
							const MechCatalog::FInfo& I = MechCatalog::Info(FMath::Max(Category, 0), FMath::Max(Index, 0));
							return FText::FromString(FString::Printf(TEXT("%s「%s」%s"), I.Model, I.Title, I.Kind));
						}), 15, MechKit::Color(&FMechUITheme::Dim))
					]
				]
				// 左上：类别 · 现在这张是什么
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				.Padding(FMargin(20.f, 16.f))  // 11-01 让开四角折角
				[
					SNew(STextBlock)
					.Font(Font(12))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
					.Visibility(EVisibility::HitTestInvisible)
					.Text_Lambda([this]() {
						if (Category < 0)
						{
							return FText::GetEmpty();
						}
						if (!HasPhotos(Category, Index))
						{
							return FText::FromString(MechLoadout::CategoryName(Category));
						}
						const FText View = ViewMode < 0 ? INVTEXT("转台") : AMechItemStudio::ViewName(Category, ViewMode);
						return FText::Format(INVTEXT("{0} · {1}"), FText::FromString(MechLoadout::CategoryName(Category)), View);
					})
				]
				// 左下：操作提示
				+ SOverlay::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(20.f, 14.f))  // 11-01 让开四角折角
				[
					SNew(STextBlock)
					.Font(Font(11))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Faint))
					.Visibility_Lambda([this]() { return ViewMode < 0 && HasPhotos(Category, Index) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					.Text(INVTEXT("按住左键左右拖动旋转"))
				]
				// 右下：画中画（肩部：挂在机体左肩外的样子）
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Bottom)
				.Padding(FMargin(8.f))
				[
					SNew(SBox)
					.WidthOverride(Pip.X)
					.HeightOverride(Pip.Y + 18.f)
					.Visibility_Lambda([this]() { return PipBrush->GetResourceObject() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						SNew(SBorder)
						.BorderImage_Lambda([]() { return Card(Th().Accent.CopyWithNewOpacity(0.5f), 1.f, &Th().PhotoFill); })
						.Padding(FMargin(1.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SImage).Image(PipBrush.Get())]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.f, 1.f))[Txt(INVTEXT("挂在左肩外"), 11, MechKit::Color(&FMechUITheme::Dim))]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SMechItemDetail::MakeStrip()
{
	using namespace MechDetailPriv;
	static const FLinearColor Shade(0.f, 0.f, 0.f, 0.55f);
	// 选配页竖向有富余：这一条用满照片宽（小图大一点）；测试场照片已经很高，还是 132 宽
	const float W = FMath::Min(PhotoSize.X <= 800.f ? 184.f : 132.f, FMath::FloorToFloat((PhotoSize.X - 3.f * 8.f) / 4.f));
	const float H = FMath::RoundToFloat(W * 9.f / 16.f);
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 v = -1; v < AMechItemStudio::MaxViews; ++v)
	{
		Row->AddSlot()
		.AutoWidth()
		.Padding(FMargin(0.f, 0.f, v + 1 < AMechItemStudio::MaxViews ? 8.f : 0.f, 0.f))  // 最后一格不留间隔：多 8 像素会把照片撑宽，右边信息栏顶到红队那一栏
		[
			// 11-01 换成 MechUIKit 的卡片：悬停描边变亮（以前没有悬停反馈），选中强调色描边 + 四角折角
			SNew(SMechKitButton)
			.Look(EMechKitLook::Card)
			.Selected_Lambda([this, v]() { return ViewMode == v; })
			.OnClick([this, v]() { ViewMode = v; })
			[
				SNew(SBox)
				.WidthOverride(W)
				.HeightOverride(H)
				[
					SNew(SBorder)
					.BorderImage(MechKit::NoBrush())
					.Padding(FMargin(2.f))
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SImage).Image(StripBrushes[v + 1].Get())]
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(Font(11))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Faint))
							.Text(INVTEXT("拍摄中…"))
							.Visibility_Lambda([this, v]() { return StripBrushes[v + 1]->GetResourceObject() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						]
						+ SOverlay::Slot()
						.VAlign(VAlign_Bottom)
						[
							SNew(SBorder)
							.BorderImage(Solid())
							.BorderBackgroundColor(Shade)
							.Padding(FMargin(6.f, 1.f))
							[
								SNew(STextBlock)
								.Font(Font(11))
								.ColorAndOpacity_Lambda([this, v]() { return FSlateColor(ViewMode == v ? Th().Ink : Th().Dim); })
								.Text_Lambda([this, v]() { return v < 0 ? INVTEXT("转台") : Category < 0 ? FText::GetEmpty() : AMechItemStudio::ViewName(Category, v); })
							]
						]
					]
				]
			]
		];
	}
	// 没有照片的一件：缩略条整条藏起来（占位不收，排版不跳），不然一直显示「拍摄中…」
	Row->SetVisibility(TAttribute<EVisibility>::CreateLambda([this]() { return Category >= 0 && !MechDetailPriv::HasPhotos(Category, Index) ? EVisibility::Hidden : EVisibility::Visible; }));
	return Row;
}

TSharedRef<SWidget> SMechItemDetail::MakeInfo()
{
	using namespace MechDetailPriv;
	auto Info = [this]() -> const MechCatalog::FInfo& { return MechCatalog::Info(FMath::Max(Category, 0), FMath::Max(Index, 0)); };
	return SNew(SVerticalBox)
		// 制造商：标识（占位 SVG；没有就一小条品牌色）+ 名字；右边重量感
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SBox)
				.HeightOverride(20.f)
				.WidthOverride_Lambda([Info]() {
					const MechCatalog::FMaker& M = MechCatalog::Maker(Info().Maker);
					return FOptionalSize(LogoBrush(Info().Maker, 20.f) ? 20.f * M.LogoAspect : 4.f);
				})
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBorder)
						.BorderImage(Solid())
						.BorderBackgroundColor_Lambda([Info]() { return FSlateColor(MechCatalog::Maker(Info().Maker).Color); })
						.Visibility_Lambda([Info]() { return LogoBrush(Info().Maker, 20.f) ? EVisibility::Collapsed : EVisibility::Visible; })
					]
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image_Lambda([Info]() { return LogoBrush(Info().Maker, 20.f); })
						.Visibility_Lambda([Info]() { return LogoBrush(Info().Maker, 20.f) ? EVisibility::Visible : EVisibility::Collapsed; })
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(Font(16))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
				.Text_Lambda([Info]() { return FText::FromString(MechCatalog::Maker(Info().Maker).Name); })
			]
			// 英文名 · 企业理念（用户定的）：一行放不下就省略，不换行
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.f, 2.f, 12.f, 0.f))
			[
				SNew(STextBlock)
				.Font(Font(12))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text_Lambda([Info]() {
					const MechCatalog::FMaker& M = MechCatalog::Maker(Info().Maker);
					return M.bPending ? INVTEXT("名称待定") : FText::FromString(FString::Printf(TEXT("「%s」"), M.Slogan));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderImage_Lambda([]() { return Card(Th().Faint); })
				.Padding(FMargin(8.f, 2.f))
				[
					SNew(STextBlock)
					.Font(Font(12))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
					.Text_Lambda([Info]() { return FText::FromString(FString::Printf(TEXT("重量  %s"), MechCatalog::HeftName(Info().Heft))); })
				]
			]
		];
}

TSharedRef<SWidget> SMechItemDetail::MakeStatRow(int32 Row)
{
	using namespace MechDetailPriv;
	// 11-01 一行一个、等高（24）：字放宽到不用换行（「不用弹药，每发 12 能量」「363 米（200 米外衰减）」约 14 个字），放不下才省略；以前值列 170 宽，长的换两行、后面全错位
	const float LabelW = 76.f;
	const float ValueW = FMath::Clamp(InfoWidth * 0.42f, 190.f, 240.f);
	const float BarW = FMath::Max(80.f, InfoWidth - LabelW - ValueW - 12.f);
	auto Stat = [this, Row]() -> const MechCatalog::FStat* { return Stats.IsValidIndex(Row) ? &Stats[Row] : nullptr; };
	auto CompareBar = [this, Row]() -> float {
		if (!Stats.IsValidIndex(Row))
		{
			return -1.f;
		}
		const MechCatalog::FStat* Found = CompareStats.FindByPredicate([&](const MechCatalog::FStat& S) { return S.Label.EqualTo(Stats[Row].Label); });
		return Found ? Found->Bar : -1.f;
	};
	return SNew(SBox)
		.HeightOverride(24.f)
		.Visibility_Lambda([Stat]() { return Stat() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(LabelW)
				[
					SNew(STextBlock).Font(Font(13)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim)).Text_Lambda([Stat]() { return Stat() ? Stat()->Label : FText::GetEmpty(); })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(BarW)
				.HeightOverride(12.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot().VAlign(VAlign_Center)[SNew(SBox).HeightOverride(3.f)[SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Track))]]
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.HeightOverride(3.f)
						.WidthOverride_Lambda([Stat, BarW]() { return FOptionalSize(Stat() ? BarW * FMath::Clamp(Stat()->Bar, 0.f, 1.f) : 0.f); })
						[
							SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Accent))
						]
					]
					// 已装备那件的位置：一根竖的细刻度
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([CompareBar]() { return CompareBar() >= 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox).WidthOverride_Lambda([CompareBar, BarW]() { return FOptionalSize(FMath::Max(0.f, BarW * CompareBar() - 1.f)); })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(2.f)
							[
								SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Ink, 0.8f))
							]
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(ValueW)
				[
					SNew(STextBlock)
					.Font(MechKit::Latin(14))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([Stat]() { return Stat() ? FText::FromString(Stat()->Value) : FText::GetEmpty(); })
				]
			]
		];
}

TSharedRef<SWidget> SMechItemDetail::MakeBuffBlock()
{
	using namespace MechDetailPriv;
	auto Buff = [this]() { return Category >= 0 ? MechCatalog::OwnBuffText(Category, Index) : nullptr; };
	auto Active = [this, Buff]() {
		return Buff() && GetFrame && GetFrame() >= 0 && MechCatalog::FrameMaker(GetFrame()) == MechCatalog::Info(Category, Index).Maker;
	};
	return SNew(SBorder)
		.Visibility_Lambda([Buff]() { return Buff() ? EVisibility::Visible : EVisibility::Collapsed; })
		.BorderImage_Lambda([Active]() {
			const FMechUITheme& T = Th();
			return Active() ? MechKit::Box(T.Accent.CopyWithNewOpacity(0.1f), T.Accent, 1.f) : MechKit::Box(T.Ink.CopyWithNewOpacity(0.025f), T.Line, 1.f);
		})
		.Padding(FMargin(12.f, 7.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(Font(13))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
					.Text_Lambda([this]() {
						return Category < 0 ? FText::GetEmpty() : FText::FromString(FString::Printf(TEXT("同厂加成 · 配%s的框架"), MechCatalog::Maker(MechCatalog::Info(Category, Index).Maker).Name));
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(Font(15))
					.ColorAndOpacity_Lambda([Active]() { return FSlateColor(Active() ? Th().Ink : Th().Dim); })
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([Buff]() { return Buff() ? FText::FromString(Buff()) : FText::GetEmpty(); })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(10.f, 0.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Font(Font(14, true))
				.ColorAndOpacity_Lambda([Active]() { return FSlateColor(Active() ? Th().Accent : Th().Dim); })
				.Text_Lambda([Active]() { return Active() ? INVTEXT("已生效") : INVTEXT("未生效"); })
			]
		];
}

void SMechItemDetail::TestTick(float DeltaTime)
{
	using namespace MechDetailPriv;
	if (!bShotsFlag() || !OnTestSelect)
	{
		return;
	}
	AMechItemStudio* S = Studio();
	if (!S)
	{
		return;
	}
	TestClock += DeltaTime;
	TestStepClock += DeltaTime;
	const TArrayView<const MechDetailPriv::FTestStep> TestSteps = MechDetailPriv::Steps();
	const int32 NumSteps = TestSteps.Num();
	if (!bTestScaleLogged && GEngine && GEngine->GameViewport)
	{
		// 界面缩放：游戏视口里 Slate 的缩放 = DPI 曲线按视口短边算（Windows 的 180% 被引擎除掉），逻辑尺寸 16:9 下都是 1920×1080
		bTestScaleLogged = true;
		FVector2D Vp;
		GEngine->GameViewport->GetViewportSize(Vp);
		UE_LOG(LogTemp, Display, TEXT("[loadoutshots] %s：视口 %.0f×%.0f 像素，界面缩放 %.3f → 逻辑尺寸 %.0f×%.0f"), *TestName, Vp.X, Vp.Y, GeomScale,
			Vp.X / FMath::Max(GeomScale, 0.01f), Vp.Y / FMath::Max(GeomScale, 0.01f));
	}
	if (!bTestHeightLogged && TestClock > 2.f)
	{
		// 11-01 量每一件的详情多高：选配页的详情是固定高度（换一件时下面的页签、卡片不跳），要放得下最长的那件
		bTestHeightLogged = true;
		const int32 SaveC = Category;
		const int32 SaveI = Index;
		float Worst = 0.f;
		FString WorstName;
		for (const int32 c : MechLoadoutCategories())
		{
			for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
			{
				Refocus(c, i);
				SlatePrepass(GeomScale > 0.f ? GeomScale : 1.f);
				if (GetDesiredSize().Y > Worst)
				{
					Worst = GetDesiredSize().Y;
					WorstName = MechLoadout::Item(c, i).Name;
				}
			}
		}
		Refocus(SaveC, SaveI);
		UE_LOG(LogTemp, Display, TEXT("[loadoutshots] %s：详情最高 %.0f（%s）"), *TestName, Worst, *WorstName);
	}
	if (TestStep < 0)
	{
		// 先等所有小图拍完、贴图流送后的补拍也拍完（进页面时排的队）
		if (TestClock > 4.f)
		{
			TestStep = 0;
			TestStepClock = 0.f;
			bTestShot = false;
			OnTestSelect(TestSteps[0].Category, TestSteps[0].Index);
		}
		return;
	}
	if (TestStep >= NumSteps)
	{
		if (TestStepClock > 1.5f && Owner.IsValid())
		{
			UE_LOG(LogTemp, Display, TEXT("[loadoutshots] %s：%d 张截图拍完，退出"), *TestName, NumSteps);
			TestStep = NumSteps + 1000;
			Owner->ConsoleCommand(TEXT("quit"));
		}
		return;
	}
	const FTestStep& Step = TestSteps[TestStep];
	if (Category != Step.Category || Index != Step.Index)
	{
		if (TestStepClock > 0.5f)
		{
			OnTestSelect(Step.Category, Step.Index);  // 宿主可能被别的事（轮次切换）抢回去了，再点一次
			TestStepClock = 0.f;
		}
		return;
	}
	if (Step.View == -2)
	{
		// 不截图：量实时转台的开销，量完下一步（选配有时限，不量）
		ViewMode = -1;
		if (TestName != TEXT("range") || TestLivePerf(DeltaTime))
		{
			++TestStep;
			TestStepClock = 0.f;
			bTestShot = false;
			if (TestStep < NumSteps)
			{
				OnTestSelect(TestSteps[TestStep].Category, TestSteps[TestStep].Index);
			}
		}
		return;
	}
	ViewMode = Step.View;
	Photo->Angle = 0.f;
	if (!bTestShot)
	{
		// 照片拍完（含补拍）再截；最多等 15 秒
		// 没有照片的一件不用等拍摄
		const bool bReady = !MechDetailPriv::HasPhotos(Category, Index) || (S->GetPreviewProgress() >= 1.f && (Step.View < 0 || S->GetView(Step.View)));
		if ((bReady && TestStepClock > 1.f) || TestStepClock > 15.f)
		{
			const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots/Loadout") / FString::Printf(TEXT("%s_%s.png"), *TestName, Step.Name);
			FScreenshotRequest::RequestScreenshot(File, true, false);
			UE_LOG(LogTemp, Display, TEXT("[loadoutshots] 截图 %s（%s，拍摄进度 %.0f%%）"), *File, *MechCatalog::FullName(Category, Index), S->GetPreviewProgress() * 100.f);
			bTestShot = true;
			TestStepClock = 0.f;
		}
		return;
	}
	if (TestStepClock > 0.4f && !bTestChecked)
	{
		// 检查卡片点不点得到：不能在这里直接查——这是 Slate 绘制过程中（控件的 Tick 在绘制里调），卡片在详细信息下面、这一帧还没画、
		// 命中网格里还没有它们。放到下一帧开头（核心 ticker，Slate 绘制之前）查上一帧完整的命中网格
		bTestChecked = true;
		const FString What = FString::Printf(TEXT("%s %s"), *TestName, Step.Name);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([What](float) {
			CheckCards(What);
			return false;
		}));
	}
	if (TestStepClock > 0.7f)
	{
		bTestChecked = false;
		++TestStep;
		TestStepClock = 0.f;
		bTestShot = false;
		if (TestStep < NumSteps)
		{
			OnTestSelect(TestSteps[TestStep].Category, TestSteps[TestStep].Index);
		}
	}
}

bool SMechItemDetail::TestLivePerf(float DeltaTime)
{
	// 阶段：0 开着拍、预热 1 秒；1 开着拍、量 3 秒；2 停拍、预热 0.5 秒；3 停拍、量 3 秒。转台一直在自动转（开着时每帧真的重拍）
	PerfClock += DeltaTime;
	Photo->Angle += DeltaTime * 20.f;
	const int32 Bucket = PerfPhase == 1 ? 0 : PerfPhase == 3 ? 1 : -1;
	if (Bucket >= 0)
	{
		++PerfN[Bucket];
		PerfFrame[Bucket] += DeltaTime * 1000.0;
		PerfGT[Bucket] += FPlatformTime::ToMilliseconds(GGameThreadTime);
		PerfRT[Bucket] += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		PerfGPU[Bucket] += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		if (Bucket == 0)
		{
			if (AMechItemStudio* S = Studio())
			{
				PerfCall += S->GetLiveCallMs();
			}
		}
	}
	static const float PhaseTime[4] = {1.f, 3.f, 0.5f, 3.f};
	if (PerfClock < PhaseTime[PerfPhase])
	{
		return false;
	}
	PerfClock = 0.f;
	++PerfPhase;
	bTestLiveOff = PerfPhase >= 2;
	if (PerfPhase < 4)
	{
		return false;
	}
	bTestLiveOff = false;
	auto Avg = [this](const double* V, int32 i) { return PerfN[i] > 0 ? V[i] / PerfN[i] : 0.0; };
	const UTextureRenderTarget2D* RT = Studio() ? Studio()->GetPreview() : nullptr;
	UE_LOG(LogTemp, Display, TEXT("[livephoto] %s 实时转台开销（%s，%d×%d）：开着拍 %d 帧 平均一帧 %.2f 毫秒（游戏线程 %.2f 渲染线程 %.2f GPU %.2f），"
		"停拍 %d 帧 平均一帧 %.2f 毫秒（游戏线程 %.2f 渲染线程 %.2f GPU %.2f）→ 每帧多 %.2f 毫秒（游戏线程 +%.2f 渲染线程 +%.2f GPU +%.2f）；捕获调用本身 %.2f 毫秒"),
		*TestName, *MechCatalog::FullName(Category, Index), RT ? RT->SizeX : 0, RT ? RT->SizeY : 0,
		PerfN[0], Avg(PerfFrame, 0), Avg(PerfGT, 0), Avg(PerfRT, 0), Avg(PerfGPU, 0),
		PerfN[1], Avg(PerfFrame, 1), Avg(PerfGT, 1), Avg(PerfRT, 1), Avg(PerfGPU, 1),
		Avg(PerfFrame, 0) - Avg(PerfFrame, 1), Avg(PerfGT, 0) - Avg(PerfGT, 1), Avg(PerfRT, 0) - Avg(PerfRT, 1), Avg(PerfGPU, 0) - Avg(PerfGPU, 1),
		PerfN[0] > 0 ? PerfCall / PerfN[0] : 0.0);
	return true;
}

// ================================================================ 测试场配装页
void SMechLoadoutBrowser::Construct(const FArguments& InArgs)
{
	using namespace MechDetailPriv;
	MechKit::Init();
	Owner = InArgs._Owner;
	OnApply = InArgs._OnApply;
	OnBack = InArgs._OnBack;
	Flat = FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateNoResource())
		.SetPressed(FSlateNoResource())
		.SetDisabled(FSlateNoResource())
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f));
	Edit = Equipped = FMechSettings::Get().Loadout;
	FocusIndex = MechLoadout::Get(Edit, Category);
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(Th().Backdrop.CopyWithNewOpacity(0.9f))]
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			// 顶栏
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(88.f)
				[
					// 11-01：大标题（雅黑 Light）+ 同一行的英文小标签，下面一行说明；以前标题 30 + 说明 15 塞在 88 高里，说明压在底线上
					SNew(SOverlay)
					+ SOverlay::Slot().VAlign(VAlign_Bottom)[MechKit::Hairline(1.4f)]
					+ SOverlay::Slot()
					.Padding(FMargin(36.f, 10.f, 36.f, 12.f))
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[MechKit::Text(INVTEXT("配装"), MechKit::Light(30), MechKit::Color(&FMechUITheme::Ink))]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Bottom)
							.Padding(FMargin(14.f, 0.f, 0.f, 8.f))
							[
								MechKit::Text(INVTEXT("LOADOUT · TEST RANGE"), MechKit::Latin(12, 260), MechKit::Color(&FMechUITheme::Accent))
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 1.f, 0.f, 0.f))
						[
							Txt(INVTEXT("测试场：随便换，点「应用」原地换装（装甲、能量回满）。照片按住左键可以拖着转。"), 14, MechKit::Color(&FMechUITheme::Dim))
						]
					]
				]
			]
			// 详细信息
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(FMargin(36.f, 12.f, 36.f, 0.f))
			[
				SAssignNew(Detail, SMechItemDetail)
				.Owner(Owner)
				.GetCategory([this]() { return Category; })
				.GetIndex([this]() { return FocusIndex; })
				.GetCompare([this]() { return MechLoadout::Get(Equipped, Category); })
				.GetFrame([this]() { return MechLoadout::Get(Edit, MechLoadout::Frame); })
				.PhotoSize(FVector2D(960.f, 540.f))
				.InfoWidth(600.f)
				.Status_Lambda([this]() {
					if (!MechLoadout::Item(Category, FocusIndex).bReady)
					{
						return INVTEXT("还没做好");
					}
					if (MechLoadout::Get(Equipped, Category) == FocusIndex)
					{
						return MechLoadout::Get(Edit, Category) == FocusIndex ? INVTEXT("已装备") : INVTEXT("已装备（这一套里换成了别的）");
					}
					return MechLoadout::Get(Edit, Category) == FocusIndex ? INVTEXT("已选，点「应用」换上") : FText::GetEmpty();
				})
				.StatusColor_Lambda([this]() { return FSlateColor(MechLoadout::Item(Category, FocusIndex).bReady ? Th().Accent : Th().Bad); })
				.OnTestSelect([this](int32 C, int32 I) { Select(C, I); })
				.TestName(TEXT("range"))
			]
			// 页签 + 卡片。逻辑尺寸 1920×1080（16:9 窗口都是这么大，见文件头）里：顶栏 88 + 详细信息 12+622 + 页签 10+40 + 8 + 卡片 + 底栏 96，
			// 卡片区剩 204：其余类一排 170，框架三家并排 28+6+158 = 192（10-31 以前框架三家各一行 = 534，最下面一家和底栏都被挤出屏幕）。
			// 卡片区占满剩下的高度、放不下时可以滚（只在比 16:9 还扁的窗口里才会用到），底栏「应用 / 返回」始终在屏幕里
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.HAlign(HAlign_Center)
			.Padding(FMargin(36.f, 10.f, 36.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(1588.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MakeTabs()]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						SNew(SScrollBox)
						.Style(&MechKit::ScrollBoxStyle())  // 11-01 细滚动条（MechUIKit）
						.ScrollBarStyle(&MechKit::ScrollBarStyle())
						.ScrollBarThickness(FVector2D(3.f, 3.f))
						.ScrollBarVisibility(EVisibility::Collapsed)  // 11-01 一排放得下，不要滚动条；还留着滚动框：比 16:9 窄的窗口会折行，滚轮还能滚
						+ SScrollBox::Slot()
						[
							SAssignNew(Grid, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12.f, 12.f))
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[MakeBottom()]
		]
	];
	RebuildGrid();
}

void SMechLoadoutBrowser::Reset()
{
	Edit = Equipped = FMechSettings::Get().Loadout;
	Category = MechLoadout::Shooter;
	FocusIndex = MechLoadout::Get(Edit, Category);
	RebuildGrid();
	if (Detail.IsValid())
	{
		Detail->Prewarm();
	}
}

void SMechLoadoutBrowser::Select(int32 InCategory, int32 InIndex)
{
	if (Category != InCategory)
	{
		Category = InCategory;
		RebuildGrid();
	}
	FocusIndex = InIndex;
	if (MechLoadout::Item(InCategory, InIndex).bReady)
	{
		MechLoadout::Set(Edit, InCategory, InIndex);
	}
}

const FSlateBrush* SMechLoadoutBrowser::ThumbBrush(int32 InCategory, int32 InIndex)
{
	const int32 K = InCategory * 100 + InIndex;
	if (TSharedPtr<FSlateBrush>* Found = Brushes.Find(K))
	{
		return Found->Get();
	}
	AMechPlayerController* PC = Owner.Get();
	AMechItemStudio* Studio = PC ? PC->GetStudio() : nullptr;
	UTextureRenderTarget2D* RT = Studio ? Studio->GetThumb(InCategory, InIndex) : nullptr;
	if (!RT)
	{
		return nullptr;
	}
	TSharedPtr<FSlateBrush> B = MechDetailPriv::MakeImageBrush(FVector2D(176.f, 110.f));
	B->SetResourceObject(RT);
	Brushes.Add(K, B);
	return B.Get();
}

TSharedRef<SWidget> SMechLoadoutBrowser::MakeTabs()
{
	using namespace MechDetailPriv;
	// 11-01 MechUIKit 的页签；几类、什么顺序在 MechLoadoutCategories（和选配界面同一张表）
	const TArrayView<const int32> Order = MechLoadoutCategories();
	TArray<FText> Labels;
	for (const int32 c : Order)
	{
		Labels.Add(FText::FromString(MechLoadout::CategoryName(c)));
	}
	return MechKit::Tabs(Labels,
		[this, Order]() { return Order.Find(Category); },
		[this, Order](int32 i) { Select(Order[i], MechLoadout::Get(Edit, Order[i])); }, nullptr, 120.f, 16.f);
}

TSharedRef<SWidget> SMechLoadoutBrowser::MakeCard(int32 InCategory, int32 InIndex, float Width, float Height)
{
	using namespace MechDetailPriv;
	const bool bReady = MechLoadout::Item(InCategory, InIndex).bReady;
	// 11-01 卡片内容和选配共用（MechCardBody）：照片铺满到边，宽卡片左图右字
	TSharedRef<SMechKitButton> ItemCard = MechKit::Card(
		MechCardBody(InCategory, InIndex,
			MechItemPicture([InCategory]() { return InCategory; }, [InIndex]() { return InIndex; }, [this](int32 C, int32 I) { return ThumbBrush(C, I); }),
			Width,
			Height,
			[this, InCategory, InIndex, bReady]() { return !bReady ? INVTEXT("待做") : MechLoadout::Get(Equipped, InCategory) == InIndex ? INVTEXT("已装备") : FText::GetEmpty(); },
			[bReady]() { return bReady ? Th().Accent : Th().Bad; },
			[bReady]() { return bReady ? Th().Ink : Th().Dim; }),
		[this, InCategory, InIndex]() { Select(InCategory, InIndex); },
		[this, InIndex]() { FocusIndex = InIndex; },
		TAttribute<bool>::CreateLambda([this, InCategory, InIndex]() { return MechLoadout::Get(Edit, InCategory) == InIndex; }),
		[this, InIndex]() { return FocusIndex == InIndex ? Th().Accent.CopyWithNewOpacity(0.5f) : FLinearColor(0.f, 0.f, 0.f, -1.f); });
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(Height)
		[
			ItemCard
		];
}

void SMechLoadoutBrowser::RebuildGrid()
{
	if (!Grid.IsValid())
	{
		return;
	}
	Grid->ClearChildren();
	TArray<TWeakPtr<SWidget>> Cards;
	if (Category == MechLoadout::Frame)
	{
		// 框架：三家并排一排（MechFrameGroups），占满整行
		Grid->AddSlot().FillLineWhenSizeLessThan(100000.f)
		[
			MechFrameGroups(1588.f, 158.f, [this](int32 i, float W, float H) { return MakeCard(MechLoadout::Frame, i, W, H); }, Cards)
		];
	}
	else
	{
		TArray<int32> Order;
		MechLoadout::DisplayOrder(Category, Order);
		MechTestExpandCards(Category, Order);
		// 11-01 一排用满：张数少就把卡片放宽（以前固定 208 宽，右边空一大块），张数多就缩小或排两行（MechCardLayout）；这里卡片区约 204 高
		const FMechCardLayout L = MechCardLayout(Order.Num(), 1588.f, 204.f, 12.f);
		for (const int32 i : Order)
		{
			const TSharedRef<SWidget> C = MakeCard(Category, i, L.Width, L.Height);
			Cards.Add(C);
			Grid->AddSlot()[C];
		}
	}
	MechTestSetCards(Cards);
}

TSharedRef<SWidget> SMechLoadoutBrowser::MakeBottom()
{
	using namespace MechDetailPriv;
	// 11-01：每类一格带小图（MechLoadoutSlot），现在看的那一类高亮，点了切到那一类；以前只有字，长得像第二排页签
	// 几类就几格，一格宽度按类数算（1550 = 整行 1848 减两个按钮）
	TSharedRef<SHorizontalBox> Cells = SNew(SHorizontalBox);
	const float CellW = MechCardWidth(MechLoadoutCategories().Num(), 1550.f, 8.f);
	bool bFirst = true;
	for (const int32 c : MechLoadoutCategories())
	{
		Cells->AddSlot()
		.AutoWidth()
		.Padding(FMargin(bFirst ? 0.f : 8.f, 0.f, 0.f, 0.f))
		[
			MechLoadoutSlot(c, [this, c]() { return MechLoadout::Get(Edit, c); }, [this](int32 C, int32 I) { return ThumbBrush(C, I); },
				[this, c]() { return Category == c; }, [this, c]() { Select(c, MechLoadout::Get(Edit, c)); },
				[this, c]() { return MechLoadout::Get(Edit, c) == MechLoadout::Get(Equipped, c) ? INVTEXT("已装备") : INVTEXT("点「应用」换上"); },
				[this, c]() { return MechLoadout::Get(Edit, c) == MechLoadout::Get(Equipped, c) ? Th().Dim : Th().Warn; }, CellW)
		];
		bFirst = false;
	}
	return SNew(SBox)
		.HeightOverride(96.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot().VAlign(VAlign_Top)[MechKit::Hairline(1.4f)]
			+ SOverlay::Slot()
			.Padding(FMargin(36.f, 12.f, 36.f, 12.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cells]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(16.f, 0.f, 12.f, 0.f))
				[
					MechKit::Button(INVTEXT("应用"), [this]() {
						Equipped = Edit;
						if (OnApply)
						{
							OnApply(Edit);
						}
					}, true, true, 140.f, 50.f)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MechKit::Button(INVTEXT("返回"), [this]() {
						if (OnBack)
						{
							OnBack();
						}
					}, false, true, 130.f, 50.f)
				]
			]
		];
}
