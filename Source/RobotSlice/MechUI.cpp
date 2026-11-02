#include "MechUI.h"

#include "Brushes/SlateColorBrush.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "MechGameMode.h"
#include "MechCharacter.h"
#include "MechFonts.h"
#include "MechItemDetail.h"
#include "MechLoadout.h"
#include "MechGameState.h"
#include "MechHudAlerts.h"
#include "MechHudTheme.h"
#include "MechSettings.h"
#include "MechUIKit.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "MechMaps.h"
#include "MechRules.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace MechMenuPriv  // 11-01 改成具名命名空间：合并编译时匿名命名空间里的 Ink / Font 会和别的文件撞
{
	// 11-01 起跟着设置里的菜单配色走（引用 MechUITheme 那份，换配色马上变）
	const FLinearColor& Ink = MechUITheme::Get().Ink;
	const FLinearColor& Dim = MechUITheme::Get().Dim;
	const FLinearColor& Accent = MechUITheme::Get().Accent;
	const FLinearColor Ally(0.25f, 0.8f, 1.f, 1.f);
	const FLinearColor Enemy(1.f, 0.28f, 0.2f, 1.f);

	const FSlateBrush* Solid()
	{
		static FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return MechFonts::Get(Size, bBold);
	}

	const FIntPoint Resolutions[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {2880, 1620}, {3840, 2160}};
	const int32 FrameLimits[] = {60, 120, 144, 0};

	/** 左侧面板：内容宽度 + 左边距 + 64（设置页内容宽 1060，其他页 540） */
	constexpr float ContentLeft = 96.f;
	constexpr float NarrowContent = 540.f;
	constexpr float WideContent = 1060.f;

	/** 菜单列表项右边那行很淡的英文（悬停时变亮）：按中文名查，查不到不写 */
	FText TagFor(const FText& Label)
	{
		static const TMap<FString, const TCHAR*> Tags = {
			{TEXT("开房"), TEXT("HOST")}, {TEXT("加入"), TEXT("JOIN")}, {TEXT("测试场"), TEXT("RANGE")}, {TEXT("模型查看"), TEXT("VIEWER")},
			{TEXT("设置"), TEXT("SETTINGS")}, {TEXT("退出游戏"), TEXT("QUIT")}, {TEXT("继续"), TEXT("RESUME")}, {TEXT("配装"), TEXT("LOADOUT")},
			{TEXT("返回主菜单"), TEXT("MAIN MENU")}, {TEXT("再来一局"), TEXT("REMATCH")}, {TEXT("返回"), TEXT("BACK")}, {TEXT("打开友伤"), TEXT("FRIENDLY FIRE")}, {TEXT("关闭友伤"), TEXT("FRIENDLY FIRE")}, {TEXT("回房间"), TEXT("LOBBY")},
			{TEXT("退出房间"), TEXT("LEAVE")}, {TEXT("解散房间，回主菜单"), TEXT("CLOSE ROOM")}, {TEXT("开始对战"), TEXT("MATCH")}, {TEXT("联机对战"), TEXT("ONLINE")},
		};
		const TCHAR* const* Found = Tags.Find(Label.ToString());
		return Found ? FText::FromString(*Found) : FText::GetEmpty();
	}

	/** 页头的英文小标签：按标题查，查不到写 HX-07 */
	FText KickerFor(const FText& Title)
	{
		static const TMap<FString, const TCHAR*> Kickers = {
			{TEXT("设置"), TEXT("SETTINGS")}, {TEXT("暂停"), TEXT("PAUSED")}, {TEXT("加入"), TEXT("JOIN ROOM")}, {TEXT("按键设置"), TEXT("CONTROLS")},
			{TEXT("配装"), TEXT("LOADOUT")}, {TEXT("开始对战"), TEXT("MATCH SETUP")}, {TEXT("联机对战"), TEXT("ONLINE")}, {TEXT("房间"), TEXT("LOBBY")},
		};
		const TCHAR* const* Found = Kickers.Find(Title.ToString());
		return FText::FromString(Found ? *Found : TEXT("HX-07"));
	}
}
using namespace MechMenuPriv;

/** 菜单底板（11-01）：游戏里整屏压暗；左侧一块深色面板从左往右淡出（以前是一整块 84% 不透明的灰板 + 4 像素青色竖线，
 *  压在亮背景上发灰、边缘生硬）；上下各一道暗角把场景压下去；左边一根细竖线带一小截强调色、底部一道细横线（底栏在它下面）。
 *  只画不接鼠标 */
class SMechMenuBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechMenuBackdrop) {}
		SLATE_ATTRIBUTE(float, PanelWidth)
		SLATE_ATTRIBUTE(float, Dim)
		SLATE_ATTRIBUTE(bool, ShowPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		PanelWidth = InArgs._PanelWidth;
		DimAmount = InArgs._Dim;
		ShowPanel = InArgs._ShowPanel;
		SetVisibility(EVisibility::HitTestInvisible);
	}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(16.f, 16.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const FMechUITheme& T = MechUITheme::Get();
		const FVector2f S = FVector2f(G.GetLocalSize());
		const FSlateBrush* White = MechKit::White();
		auto Box = [&](int32 Layer, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& C) {
			FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), White, ESlateDrawEffect::None, C);
		};
		auto Grad = [&](int32 Layer, const FVector2f& Pos, const FVector2f& Size, EOrientation O, std::initializer_list<TPair<float, FLinearColor>> Stops) {
			TArray<FSlateGradientStop> Arr;
			for (const TPair<float, FLinearColor>& P : Stops)
			{
				Arr.Add(FSlateGradientStop(O == Orient_Vertical ? FVector2f(P.Key * Size.X, 0.f) : FVector2f(0.f, P.Key * Size.Y), P.Value));
			}
			FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(Arr), O);
		};
		const float DimA = DimAmount.Get();
		if (DimA > 0.f)
		{
			Box(LayerId, FVector2f::ZeroVector, S, FLinearColor(0.f, 0.f, 0.f, DimA));
		}
		if (!ShowPanel.Get())
		{
			return LayerId;
		}
		// 上下暗角：把场景的上下边压暗，菜单像是浮在画面上（电影感）
		const FLinearColor Black(0.f, 0.f, 0.f, 0.f);
		Grad(LayerId + 1, FVector2f::ZeroVector, FVector2f(S.X, S.Y * 0.2f), Orient_Horizontal, {{0.f, Black.CopyWithNewOpacity(0.5f)}, {1.f, Black}});
		Grad(LayerId + 1, FVector2f(0.f, S.Y * 0.68f), FVector2f(S.X, S.Y * 0.32f), Orient_Horizontal, {{0.f, Black}, {1.f, Black.CopyWithNewOpacity(0.62f)}});
		// 左侧面板：到内容右边还很实，过了以后 420 像素内淡到透明
		const float W = PanelWidth.Get();
		const float Fade = 420.f;
		const float Total = W + Fade;
		const FLinearColor P = T.Panel;
		Grad(LayerId + 2, FVector2f::ZeroVector, FVector2f(Total, S.Y), Orient_Vertical,
			{{0.f, P.CopyWithNewOpacity(0.95f)}, {W * 0.6f / Total, P.CopyWithNewOpacity(0.9f)}, {W / Total, P.CopyWithNewOpacity(0.74f)}, {1.f, P.CopyWithNewOpacity(0.f)}});
		// 左边细竖线 + 顶上一小截强调色（和页头小标签同一高度）
		Box(LayerId + 3, FVector2f(56.f, 72.f), FVector2f(1.f, S.Y - 144.f), T.Line);
		Box(LayerId + 4, FVector2f(55.f, 72.f), FVector2f(3.f, 40.f), T.Accent);
		// 底部细横线（底栏的按键提示在它下面），两头各一道短竖线
		const float Y = S.Y - 64.f;
		Box(LayerId + 3, FVector2f(ContentLeft, Y), FVector2f(S.X - 2.f * ContentLeft, 1.f), T.Line);
		Box(LayerId + 3, FVector2f(ContentLeft, Y - 3.f), FVector2f(1.f, 7.f), T.Line.CopyWithNewOpacity(T.Line.A * 2.5f));
		Box(LayerId + 3, FVector2f(S.X - ContentLeft - 1.f, Y - 3.f), FVector2f(1.f, 7.f), T.Line.CopyWithNewOpacity(T.Line.A * 2.5f));
		return LayerId + 4;
	}

private:
	TAttribute<float> PanelWidth;
	TAttribute<float> DimAmount;
	TAttribute<bool> ShowPanel;
};

void SMechMenu::Construct(const FArguments& InArgs)
{
	MechKit::Init();  // 按设置套用菜单配色（第一次建界面时）
	Owner = InArgs._Owner;
	Page = InArgs._Page;
	SettingsTab = FMath::Clamp(SettingsTab, 0, static_cast<int32>(EMechSettingsTab::Count) - 1);
	auto Wide = [this]() { return Page == EMechMenuPage::Settings || Page == EMechMenuPage::Keys; };
	PanelWidth = ContentLeft + (Wide() ? WideContent : NarrowContent) + 64.f;  // 第一次出现不播宽度动画

	ChildSlot
	[
		SNew(SOverlay)
		// 底板：游戏里整屏压暗、左侧淡出面板、细线（测试场配装页是整屏的，那时只压暗）
		+ SOverlay::Slot()
		[
			SNew(SMechMenuBackdrop)
			.PanelWidth_Lambda([this]() { return PanelWidth; })
			.Dim_Lambda([this]() { return IsInGame() ? 0.5f : 0.f; })
			.ShowPanel_Lambda([this]() { return Page != EMechMenuPage::Loadout; })
		]
		// 内容：左边一列（上 72、下 88 留给左上角的强调色刻度和底栏），切页时淡入 + 从左滑进来一点
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(ContentLeft, 72.f, 0.f, 88.f))
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Page == EMechMenuPage::Loadout ? EVisibility::Collapsed : EVisibility::Visible; })
			.WidthOverride_Lambda([Wide]() { return FOptionalSize(Wide() ? WideContent : NarrowContent); })
			[
				MechKit::FadeIn(
					SAssignNew(Switcher, SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return static_cast<int32>(Page); })
					+ SWidgetSwitcher::Slot()[MakeMainPage()]
					+ SWidgetSwitcher::Slot()[MakeJoinPage()]
					+ SWidgetSwitcher::Slot()[MakeSettingsPage()]
					+ SWidgetSwitcher::Slot()[MakePausePage()]
					+ SWidgetSwitcher::Slot()[MakeResultsPage()]
					+ SWidgetSwitcher::Slot()[MakeLoadoutPage()]
					+ SWidgetSwitcher::Slot()[MakeKeysPage()],
					[this]() { return PageTime; })
			]
		]
		// 底栏（底部细横线下面）：左边现在在哪一页，右边 Esc 做什么
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(ContentLeft, 0.f, ContentLeft, 18.f))
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return Page == EMechMenuPage::Loadout ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(MechKit::Latin(12, 180))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim, 0.7f))
				.Text_Lambda([this]() {
					static const TCHAR* Names[] = {TEXT("MAIN"), TEXT("JOIN"), TEXT("SETTINGS"), TEXT("PAUSED"), TEXT("RESULTS"), TEXT("LOADOUT"), TEXT("SETTINGS")};
					const int32 i = static_cast<int32>(Page);
					return FText::FromString(FString::Printf(TEXT("HX-07  /  %s"), i >= 0 && i < UE_ARRAY_COUNT(Names) ? Names[i] : TEXT("")));
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this]() { return Page == EMechMenuPage::Main || Page == EMechMenuPage::Results ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MechKit::KeyCap(INVTEXT("Esc"))]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(10.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Font(MechKit::Font(14))
					.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
					.Text_Lambda([this]() {
						if (IsListening())
						{
							return INVTEXT("取消改键");
						}
						return Page == EMechMenuPage::Pause ? INVTEXT("继续") : INVTEXT("返回");
					})
				]
			]
		]
		// 测试场配装页（10-31，用户：配装照片要详细一点）：整屏，照片 + 详细信息 + 卡片，见 MechItemDetail.cpp
		+ SOverlay::Slot()
		[
			SAssignNew(LoadoutBrowser, SMechLoadoutBrowser)
			.Owner(Owner)
			.Visibility_Lambda([this]() { return Page == EMechMenuPage::Loadout ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnApply([this](const FMechLoadout& L) {
				if (Owner.IsValid())
				{
					Owner->MenuApplyLoadout(L);
					Owner->MenuResume();
				}
			})
			.OnBack([this]() { SetPage(EMechMenuPage::Pause); })
		]
	];
	SetPage(Page);
}

void SMechMenu::SetPage(EMechMenuPage InPage)
{
	// 11-01 按键设置是设置页的一个页签：要打开按键页 = 打开设置、切到「按键」
	if (InPage == EMechMenuPage::Keys)
	{
		InPage = EMechMenuPage::Settings;
		SettingsTab = static_cast<int32>(EMechSettingsTab::Keys);
	}
	const bool bEnterSettings = InPage == EMechMenuPage::Settings && Page != EMechMenuPage::Settings;
	if (Page == EMechMenuPage::Settings && InPage != EMechMenuPage::Settings && PendingGraphics() > 0)
	{
		RevertGraphics();  // 画面设置改了没点「应用」就离开 = 不改（以前这些值会留在内存里，下次谁调 ApplySettings 就突然生效）
	}
	if (InPage != Page || PageTime <= 0.0)
	{
		PageTime = MechKit::Now();  // 切页淡入从这一刻开始
	}
	Page = InPage;
	ListeningKey = -1;
	KeyNotice = FText::GetEmpty();
	if (bEnterSettings || !Snap.bValid)
	{
		SnapshotGraphics();  // 进设置页时记一份「已应用」的画面设置，用来判断哪几项还没应用
	}
	if (Page == EMechMenuPage::Loadout)
	{
		EditLoadout = FMechSettings::Get().Loadout;
		if (LoadoutBrowser.IsValid())
		{
			LoadoutBrowser->Reset();
		}
	}
	if (Page != EMechMenuPage::Results || !ResultsHolder.IsValid())
	{
		return;
	}
	// 结算页每次打开时按 GameState 里的统计重建
	const AMechPlayerController* PC = Owner.Get();
	const AMechGameState* GS = PC && PC->GetWorld() ? PC->GetWorld()->GetGameState<AMechGameState>() : nullptr;
	const int32 MyTeam = PC ? PC->GetMenuTeam() : 0;
	const FString MyName = PC ? PC->GetMenuName() : FString();
	const bool bDM = GS && GS->IsDeathmatch();
	const bool bSolo = GS && GS->IsSoloDM();
	const bool bDraw = GS && GS->MatchWinner < 0;
	bool bWin = GS && GS->MatchWinner >= 0 && GS->MatchWinner == MyTeam;
	TArray<FMechResultRow> Rows;
	if (GS)
	{
		Rows = GS->Results;
	}
	// 个人死斗：按击毁排名次（同击毁阵亡少的在前），自己第几名
	int32 MyRank = 0;
	if (bSolo)
	{
		Rows.Sort([](const FMechResultRow& A, const FMechResultRow& B) {
			return A.Kills != B.Kills ? A.Kills > B.Kills : A.Deaths != B.Deaths ? A.Deaths < B.Deaths : A.Name < B.Name;
		});
		for (const FMechResultRow& R : Rows)
		{
			if (R.Name == MyName)
			{
				MyRank = 1;
				for (const FMechResultRow& O : Rows)
				{
					MyRank += O.Kills > R.Kills ? 1 : 0;
				}
			}
		}
		bWin = GS && !GS->WinnerName.IsEmpty() && GS->WinnerName == MyName;
	}
	else
	{
		Rows.Sort([bDM](const FMechResultRow& A, const FMechResultRow& B) {
			return A.Team != B.Team ? A.Team < B.Team : bDM ? A.Kills > B.Kills : A.Dealt > B.Dealt;
		});
	}
	// 表头：死斗把击毁、阵亡放前面
	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(bSolo ? 1 : 0, 1.f);
	TArray<const TCHAR*> Heads;
	if (bSolo)
	{
		Heads = {TEXT("名次"), TEXT("机体"), TEXT("击毁"), TEXT("阵亡"), TEXT("输出"), TEXT("承受")};
	}
	else if (bDM)
	{
		Heads = {TEXT("机体"), TEXT("击毁"), TEXT("阵亡"), TEXT("输出"), TEXT("承受"), TEXT("子弹 / 导弹 / 刀")};
	}
	else
	{
		Heads = {TEXT("机体"), TEXT("输出"), TEXT("承受"), TEXT("击毁"), TEXT("子弹 / 导弹 / 刀")};
	}
	for (int32 c = 0; c < Heads.Num(); ++c)
	{
		Grid->AddSlot(c, 0).Padding(FMargin(0.f, 0.f, 18.f, 10.f))[Label(FText::FromString(Heads[c]), 14)];
	}
	int32 Row = 1;
	for (const FMechResultRow& R : Rows)
	{
		const bool bMe = R.Name == MyName;
		const FLinearColor C = bSolo ? (bMe ? Accent : Ink) : (R.Team == 0 ? Ally : Enemy);
		auto Cell = [&](int32 Col, const FString& S, const FLinearColor& Color) {
			Grid->AddSlot(Col, Row).Padding(FMargin(0.f, 3.f, 18.f, 3.f))[SNew(STextBlock).Text(FText::FromString(S)).Font(MechKit::Latin(18)).ColorAndOpacity(Color)];
		};
		if (bSolo)
		{
			int32 Rank = 1;
			for (const FMechResultRow& O : Rows)
			{
				Rank += O.Kills > R.Kills ? 1 : 0;
			}
			Cell(0, FString::FromInt(Rank), Rank == 1 ? Accent : Dim);
			Cell(1, R.Name, C);
			Cell(2, FString::FromInt(R.Kills), Ink);
			Cell(3, FString::FromInt(R.Deaths), Ink);
			Cell(4, FString::FromInt(R.Dealt), Dim);
			Cell(5, FString::FromInt(R.Taken), Dim);
		}
		else if (bDM)
		{
			Cell(0, R.Name, C);
			Cell(1, FString::FromInt(R.Kills), Ink);
			Cell(2, FString::FromInt(R.Deaths), Ink);
			Cell(3, FString::FromInt(R.Dealt), Dim);
			Cell(4, FString::FromInt(R.Taken), Dim);
			Cell(5, FString::Printf(TEXT("%d / %d / %d"), R.Bullets, R.Missiles, R.Blades), Dim);
		}
		else
		{
			Cell(0, R.Name, C);
			Cell(1, FString::FromInt(R.Dealt), Ink);
			Cell(2, FString::FromInt(R.Taken), Ink);
			Cell(3, FString::FromInt(R.Kills), Ink);
			Cell(4, FString::Printf(TEXT("%d / %d / %d"), R.Bullets, R.Missiles, R.Blades), Dim);
		}
		++Row;
	}
	// 大字：胜利 / 败北 / 平局；个人死斗写「第 N 名」
	FText Title = bDraw ? INVTEXT("平局") : bWin ? INVTEXT("胜利") : INVTEXT("败北");
	FLinearColor TitleColor = bDraw ? Ink : bWin ? Ally : Enemy;
	FString Score;
	if (bSolo)
	{
		// 自己没上场（观战 / 自动对战）时不写胜负
		Title = MyRank > 0 ? FText::FromString(FString::Printf(TEXT("第 %d 名"), MyRank)) : INVTEXT("比赛结束");
		TitleColor = bWin ? Accent : (bDraw || MyRank <= 1) ? Ink : Enemy;
		Score = GS->WinnerName.IsEmpty() ? FString(TEXT("时间到 · 第一名并列，平局"))
			: FString::Printf(TEXT("%s 胜 · %d 击毁"), *GS->WinnerName, Rows.Num() ? Rows[0].Kills : 0);
	}
	else if (GS)
	{
		Score = FString::Printf(TEXT("蓝 %d  :  %d 红"), GS->TeamScore[0], GS->TeamScore[1]);
	}
	const FString RuleLine = GS ? MechRules::Describe(GS->Rules, GS->bCoreMode) : FString();
	ResultsHolder->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MechKit::Kicker(INVTEXT("MATCH RESULTS"))]  // 11-01 和其他页同一套标题层级
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			SNew(STextBlock).Text(Title).Font(MechKit::Light(68)).ColorAndOpacity(TitleColor)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Score))
			.Font(MechKit::Latin(24))
			.ColorAndOpacity(Ink)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 30.f))[Label(FText::FromString(RuleLine), 14)]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 36.f))[Grid]
		+ SVerticalBox::Slot().AutoHeight()[MakeResultsButtons()]);
}

TSharedRef<SWidget> SMechMenu::MakeResultsButtons()
{
	// 11-01 从房间开的场（主菜单「开房」进来的都是）：结算后回房间——房主带所有人回（服务器换图，客户端自动跟着），
	// 非房主只能等房主或自己退出；结算表到时也会自动回房间。直接进对战图的（测试 / 启动脚本）照旧「再来一局 / 返回主菜单」
	const AMechPlayerController* PC = Owner.Get();
	const bool bHost = PC && PC->CanRestartMatch();
	auto Travelling = [this]() {
		const AMechPlayerController* P = Owner.Get();
		const AMechGameState* GS = P && P->GetWorld() ? P->GetWorld()->GetGameState<AMechGameState>() : nullptr;
		return GS && GS->bRoomTravelling;
	};
	if (PC && PC->IsRoomMatch())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Button(bHost ? INVTEXT("回房间") : INVTEXT("等房主回房间"), [this]() { if (Owner.IsValid()) Owner->MenuBackToRoom(); },
				TAttribute<bool>::CreateLambda([bHost, Travelling]() { return bHost && !Travelling(); }))]
			+ SVerticalBox::Slot().AutoHeight()[Button(bHost ? INVTEXT("解散房间，回主菜单") : INVTEXT("退出房间"), [this]() { if (Owner.IsValid()) Owner->MenuBackToMain(); })]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 0.f))
			[
				SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Dim).AutoWrapText(true).Text_Lambda([bHost, Travelling]() {
					if (Travelling())
					{
						return INVTEXT("正在回房间…");
					}
					return bHost ? INVTEXT("稍后自动回房间；队伍、地图和规则都还在") : INVTEXT("房主回房间时你会跟着回去，队伍还在");
				})
			];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("再来一局"), [this]() { if (Owner.IsValid()) Owner->MenuRestart(); },
			TAttribute<bool>::CreateLambda([this]() { return Owner.IsValid() && Owner->CanRestartMatch(); }))]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("返回主菜单"), [this]() { if (Owner.IsValid()) Owner->MenuBackToMain(); })]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 0.f))
		[
			Label(bHost ? INVTEXT("30 秒后自动开始下一局") : INVTEXT("等主机开始下一局"), 13)
		];
}

FReply SMechMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		Back();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SMechMenu::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// 按键页签在等新键：键盘键都在这里截下（Esc 取消），不往下传给按钮
	if (IsListening())
	{
		const FKey Key = InKeyEvent.GetKey();
		if (Key == EKeys::Escape)
		{
			ListeningKey = -1;
			KeyNotice = INVTEXT("已取消");
		}
		else if (!Key.IsGamepadKey() && !InKeyEvent.IsRepeat())  // 按住回车选中这一行时的自动重复不算新键
		{
			FinishRebind(Key);
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
}

FReply SMechMenu::OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// 等新键时点鼠标 = 绑鼠标键（点进「等键」那一下是松开时才触发的，不会被当成新键）
	if (IsListening())
	{
		FinishRebind(MouseEvent.GetEffectingButton());
		return FReply::Handled();
	}
	return SCompoundWidget::OnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

void SMechMenu::FinishRebind(const FKey& Key)
{
	const EMechKey Action = static_cast<EMechKey>(ListeningKey);
	if (!MechKeys::CanBind(Key))
	{
		KeyNotice = FText::FromString(FString::Printf(TEXT("%s 不能绑（Esc、F1、鼠标滚轮留给别的用途），换一个键，或按 Esc 取消"),
			*MechKeys::KeyName(Key).ToString()));
		return;  // 继续等
	}
	ListeningKey = -1;
	const FKey Old = MechKeys::Key(Action);
	const EMechKey Swapped = MechKeys::Rebind(Action, Key);
	AMechCharacter::ApplyKeyBindings();
	if (Swapped != EMechKey::Count)
	{
		KeyNotice = FText::FromString(FString::Printf(TEXT("%s 原来是「%s」的键，两个已互换：「%s」改用 %s"), *MechKeys::KeyName(Key).ToString(),
			*MechKeys::Label(Swapped).ToString(), *MechKeys::Label(Swapped).ToString(), *MechKeys::KeyName(Old).ToString()));
	}
	else
	{
		KeyNotice = FText::FromString(FString::Printf(TEXT("「%s」改为 %s"), *MechKeys::Label(Action).ToString(), *MechKeys::KeyName(Key).ToString()));
	}
}

void SMechMenu::Back()
{
	switch (Page)
	{
	case EMechMenuPage::Join:
		SetPage(EMechMenuPage::Main);
		break;
	case EMechMenuPage::Keys:
		SetPage(EMechMenuPage::Settings);
		break;
	case EMechMenuPage::Settings:
		SetPage(SettingsReturn);
		break;
	case EMechMenuPage::Loadout:
		SetPage(EMechMenuPage::Pause);
		break;
	case EMechMenuPage::Pause:
		if (Owner.IsValid())
		{
			Owner->MenuResume();
		}
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- 小部件
// 11-01 界面改版：下面几个是各页（包括别的代理加的页）共用的入口，签名不变，外观换成 MechUIKit 的
TSharedRef<SWidget> SMechMenu::Button(const FText& Text, TFunction<void()> OnClick, TAttribute<bool> Enabled)
{
	// 菜单大列表项：一项一行、底下细分隔线，悬停时左边亮一根竖条、字往右滑；右边一行很淡的英文
	return MechKit::MenuItem(Text, OnClick, Enabled, TagFor(Text));
}

TSharedRef<SWidget> SMechMenu::Toggle(const FText& Text, TFunction<bool()> IsOn, TFunction<void()> OnClick, TAttribute<bool> Enabled)
{
	// 以前是白底灰字的按钮（改前截图里最「廉价」的地方），现在是细描边选项块，选中 = 强调色描边 + 淡底
	return SNew(SBox)
		.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			MechKit::Chip(Text, IsOn, OnClick, Enabled)
		];
}

TSharedRef<SWidget> SMechMenu::OnOff(TFunction<bool()> Get, TFunction<void(bool)> Set)
{
	return MechKit::Switch(Get, Set);
}

TSharedRef<SWidget> SMechMenu::Header(const FText& Title, const FText& Sub)
{
	return SNew(SBox)
		.Padding(FMargin(0.f, 0.f, 0.f, 34.f))
		[
			MechKit::PageTitle(KickerFor(Title), Title, Sub)
		];
}

TSharedRef<SWidget> SMechMenu::Label(const FText& Text, int32 Size)
{
	return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim)).AutoWrapText(true);
}

TSharedRef<SWidget> SMechMenu::SettingRow(const FText& Name, TSharedRef<SWidget> Control)
{
	return MechKit::Row(Name, FText::GetEmpty(), Control, 150.f);
}

TSharedRef<SWidget> SMechMenu::Brand()
{
	// 主菜单标题块：英文小标签 → 大号 HX-07（Bahnschrift，和 HUD 的读数同一款字）→ 一行加了字距的副标题
	FSlateFontInfo Tagline = MechKit::Font(17);
	Tagline.LetterSpacing = 160;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MechKit::Kicker(INVTEXT("WORKING TITLE"))]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(-4.f, 6.f, 0.f, 0.f))
		[
			MechKit::Text(INVTEXT("HX-07"), MechKit::Latin(112, 20), MechKit::Color(&FMechUITheme::Ink))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 0.f))
		[
			MechKit::Text(INVTEXT("高机动 · 机体对战"), Tagline, MechKit::Color(&FMechUITheme::Dim))
		];
}

bool SMechMenu::IsInGame() const
{
	return Page == EMechMenuPage::Pause || Page == EMechMenuPage::Results || Page == EMechMenuPage::Loadout ||
		((Page == EMechMenuPage::Settings || Page == EMechMenuPage::Keys) && SettingsReturn == EMechMenuPage::Pause);
}

bool SMechMenu::IsListening() const
{
	return Page == EMechMenuPage::Settings && SettingsTab == static_cast<int32>(EMechSettingsTab::Keys) && ListeningKey >= 0;
}

void SMechMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const bool bWide = Page == EMechMenuPage::Settings || Page == EMechMenuPage::Keys;
	const float Target = ContentLeft + (bWide ? WideContent : NarrowContent) + 64.f;
	PanelWidth += (Target - PanelWidth) * (1.f - FMath::Exp(-FMath::Min(InDeltaTime, 0.1f) * 11.f));
}

void SMechMenu::SetSettingsTab(int32 Tab)
{
	Tab = FMath::Clamp(Tab, 0, static_cast<int32>(EMechSettingsTab::Count) - 1);
	if (Tab != SettingsTab)
	{
		SettingsTab = Tab;
		TabTime = MechKit::Now();
	}
	ListeningKey = -1;
	KeyNotice = FText::GetEmpty();
}

// ---------------------------------------------------------------- 画面设置的「应用」（11-01）
void SMechMenu::SnapshotGraphics()
{
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!S)
	{
		return;
	}
	float Norm = 0.f, Cur = 0.f, Min = 0.f, Max = 0.f;
	S->GetResolutionScaleInformationEx(Norm, Cur, Min, Max);
	Snap.Window = static_cast<int32>(S->GetFullscreenMode());
	Snap.Resolution = S->GetScreenResolution();
	Snap.bVSync = S->IsVSyncEnabled();
	Snap.FrameLimit = FMath::RoundToInt(S->GetFrameRateLimit());
	Snap.Quality = S->GetOverallScalabilityLevel();
	Snap.RenderScale = Cur;
	Snap.bValid = true;
	SnapQuality = S->ScalabilityQuality;
}

int32 SMechMenu::PendingGraphics() const
{
	const UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!S || !Snap.bValid)
	{
		return 0;
	}
	float Norm = 0.f, Cur = 0.f, Min = 0.f, Max = 0.f;
	S->GetResolutionScaleInformationEx(Norm, Cur, Min, Max);
	return (static_cast<int32>(S->GetFullscreenMode()) != Snap.Window ? 1 : 0) + (S->GetScreenResolution() != Snap.Resolution ? 1 : 0) +
		(S->IsVSyncEnabled() != Snap.bVSync ? 1 : 0) + (FMath::RoundToInt(S->GetFrameRateLimit()) != Snap.FrameLimit ? 1 : 0) +
		(S->GetOverallScalabilityLevel() != Snap.Quality ? 1 : 0) + (!FMath::IsNearlyEqual(Cur, Snap.RenderScale, 0.5f) ? 1 : 0);
}

void SMechMenu::ApplyGraphics()
{
	if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		S->ApplySettings(false);
		S->SaveSettings();
	}
	FMechSettings::Get().Save();
	FMechSettings::Get().ApplyAudio(Owner.Get());  // 和以前「应用并保存」一样：引擎应用设置后把主音量再设一次
	SnapshotGraphics();
}

void SMechMenu::RevertGraphics()
{
	// 撤销 = 把进设置页时记下的值放回去（不重新读 ini：别的没点「应用」的改动也在内存里，重读会一起丢掉）
	UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!S || !Snap.bValid)
	{
		return;
	}
	S->SetFullscreenMode(static_cast<EWindowMode::Type>(Snap.Window));
	S->SetScreenResolution(Snap.Resolution);
	S->SetVSyncEnabled(Snap.bVSync);
	S->SetFrameRateLimit(static_cast<float>(Snap.FrameLimit));
	S->ScalabilityQuality = SnapQuality;
	S->SetResolutionScaleValueEx(Snap.RenderScale);
}

// ---------------------------------------------------------------- 各页
TSharedRef<SWidget> SMechMenu::MakeMainPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 84.f))[Brand()]  // 11-01 标题块改版（Brand）
		// 11-01 用户：「只单独保留开房，地图在界面里面选择」——「开始对战 / 联机对战」合成「开房」（总是开着房，朋友随时能进），另留「加入」
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("开房"), [this]() { if (Owner.IsValid()) Owner->MenuOpenRoom(); })]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("加入"), [this]() { SetPage(EMechMenuPage::Join); })]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("测试场"), [this]() { if (Owner.IsValid()) Owner->MenuOpenRange(); })]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("模型查看"), [this]() { if (Owner.IsValid()) Owner->MenuOpenViewer(); })]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("设置"), [this]() {
			SettingsReturn = EMechMenuPage::Main;
			SetPage(EMechMenuPage::Settings);
		})]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("退出游戏"), [this]() { if (Owner.IsValid()) Owner->MenuQuit(); })]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNullWidget::NullWidget];  // 11-01：「Esc 返回上一页」挪到底栏的按键提示
}

TSharedRef<SWidget> SMechMenu::MakeJoinPage()
{
	// 11-01 原来「联机对战」页的下半截：开房那半挪到主菜单「开房」，地图在房间里选
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Header(INVTEXT("加入"), INVTEXT("输入房主的 IP（局域网 / UU 联机房间的虚拟 IP 都行），端口 7777。进去就是房主的房间，地图和规则由房主选"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
		[
			SNew(SBox)
			.WidthOverride(320.f)
			.HAlign(HAlign_Left)
			[
				SNew(SEditableTextBox)
				.Style(&MechKit::TextBoxStyle())  // 11-01 换掉引擎默认的白底输入框
				.Font(MechKit::Latin(19))
				.Text_Lambda([this]() { return FText::FromString(JoinAddress); })
				.OnTextChanged_Lambda([this](const FText& T) { JoinAddress = T.ToString().TrimStartAndEnd(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("加入"), [this]() { if (Owner.IsValid()) Owner->MenuJoin(JoinAddress); })]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("返回"), [this]() { SetPage(EMechMenuPage::Main); })];
}

TSharedRef<SWidget> SMechMenu::MakeSettingsPage()
{
	// 11-01（用户：「设置整理一下，现在有点乱」）：以前十几项混在一列里、最下面的「返回」被挤出屏幕，哪几项要「应用并保存」也说不清。
	// 现在分五个页签，每项一行（左边名字 + 一行小字说明，右边控件）；只有画面页签里「显示」「画质」两组要点「应用」，
	// 其他改了马上生效、马上存盘。没点「应用」就离开设置页 = 撤销那几项（SetPage 里）
	auto Scrolled = [](const TSharedRef<SWidget>& Body) {
		TSharedRef<SScrollBox> Box = MechKit::ScrollBox();
		Box->AddSlot().Padding(FMargin(0.f, 0.f, 18.f, 12.f))[Body];
		return Box;
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MechKit::PageTitle(INVTEXT("SETTINGS"), INVTEXT("设置"))]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 24.f, 0.f, 0.f))
		[
			MechKit::Tabs({INVTEXT("画面"), INVTEXT("声音"), INVTEXT("操作"), INVTEXT("界面"), INVTEXT("按键")},
				[this]() { return SettingsTab; }, [this](int32 i) { SetSettingsTab(i); }, nullptr, 112.f)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
		[
			// 切页签：内容淡入 + 轻轻滑进来
			MechKit::FadeIn(
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return SettingsTab; })
				+ SWidgetSwitcher::Slot()[Scrolled(MakeGraphicsTab())]
				+ SWidgetSwitcher::Slot()[Scrolled(MakeAudioTab())]
				+ SWidgetSwitcher::Slot()[Scrolled(MakeControlTab())]
				+ SWidgetSwitcher::Slot()[Scrolled(MakeInterfaceTab())]
				+ SWidgetSwitcher::Slot()[MakeKeysTab()],
				[this]() { return FMath::Max(TabTime, PageTime); }, 0.2f, 10.f)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 14.f, 0.f, 0.f))[MakeSettingsFooter()];
}

TSharedRef<SWidget> SMechMenu::MakeGraphicsTab()
{
	auto GUS = []() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; };
	auto CurScale = [GUS]() {
		float Norm = 0.f, Cur = -1.f, Lo = 0.f, Hi = 0.f;
		if (GUS())
		{
			GUS()->GetResolutionScaleInformationEx(Norm, Cur, Lo, Hi);
		}
		return Cur;
	};
	auto CycleRes = [GUS](int32 Step) {
		UGameUserSettings* S = GUS();
		if (!S)
		{
			return;
		}
		const FIntPoint Desk = S->GetDesktopResolution();
		TArray<FIntPoint> List;
		for (const FIntPoint& R : Resolutions)
		{
			if (R.X <= Desk.X && R.Y <= Desk.Y)
			{
				List.Add(R);
			}
		}
		if (List.IsEmpty())
		{
			return;
		}
		const int32 Cur = List.IndexOfByKey(S->GetScreenResolution());
		S->SetScreenResolution(List[((Cur == INDEX_NONE ? 0 : Cur) + Step + List.Num()) % List.Num()]);
	};
	// 3D 渲染精度：3D 画面按这个比例渲染再用 TSR 放大到窗口大小，界面文字始终按原生分辨率画；自动 = 3D 内部约 810p（AMechPlayerController::UpdateRenderScale）
	static const float Scales[] = {0.f, 67.f, 83.f, 100.f};
	constexpr float NameW = 340.f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))[MechKit::Section(INVTEXT("显示"), INVTEXT("改完点右下角「应用」生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("窗口"), INVTEXT("无边框 = 铺满屏幕的窗口，切出去最快"),
				MechKit::Segmented({INVTEXT("全屏"), INVTEXT("无边框"), INVTEXT("窗口")},
					[GUS]() { return GUS() ? static_cast<int32>(GUS()->GetFullscreenMode()) : -1; },
					[GUS](int32 i) { if (GUS()) GUS()->SetFullscreenMode(static_cast<EWindowMode::Type>(i)); }),
				NameW, [this, GUS]() { return GUS() && static_cast<int32>(GUS()->GetFullscreenMode()) != Snap.Window; })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("分辨率"),
				TAttribute<FText>::CreateLambda([GUS]() {
					return GUS() && GUS()->GetFullscreenMode() == EWindowMode::WindowedFullscreen ? INVTEXT("无边框时跟随桌面分辨率，这一项不起作用") : INVTEXT("全屏和窗口用这个大小");
				}),
				MechKit::Stepper(TAttribute<FText>::CreateLambda([GUS]() {
					const FIntPoint R = GUS() ? GUS()->GetScreenResolution() : FIntPoint::ZeroValue;
					return FText::FromString(FString::Printf(TEXT("%d × %d"), R.X, R.Y));
				}), CycleRes, 300.f),
				NameW, [this, GUS]() { return GUS() && GUS()->GetScreenResolution() != Snap.Resolution; })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("垂直同步"), INVTEXT("画面不撕裂，但会多一点输入延迟"),
				MechKit::Switch([GUS]() { return GUS() && GUS()->IsVSyncEnabled(); }, [GUS](bool b) { if (GUS()) GUS()->SetVSyncEnabled(b); }),
				NameW, [this, GUS]() { return GUS() && GUS()->IsVSyncEnabled() != Snap.bVSync; })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("帧率上限"), FText::GetEmpty(),
				MechKit::Segmented({INVTEXT("60"), INVTEXT("120"), INVTEXT("144"), INVTEXT("不限")},
					[GUS]() {
						const int32 L = GUS() ? FMath::RoundToInt(GUS()->GetFrameRateLimit()) : -1;
						for (int32 i = 0; i < UE_ARRAY_COUNT(FrameLimits); ++i)
						{
							if (FrameLimits[i] == L)
							{
								return i;
							}
						}
						return -1;
					},
					[GUS](int32 i) { if (GUS()) GUS()->SetFrameRateLimit(static_cast<float>(FrameLimits[i])); }),
				NameW, [this, GUS]() { return GUS() && FMath::RoundToInt(GUS()->GetFrameRateLimit()) != Snap.FrameLimit; })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))[MechKit::Section(INVTEXT("画质"), INVTEXT("改完点右下角「应用」生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("画质"), INVTEXT("整体档位：阴影、光照、反射、特效一起调"),
				MechKit::Segmented({INVTEXT("低"), INVTEXT("中"), INVTEXT("高"), INVTEXT("史诗")},
					[GUS]() { return GUS() ? GUS()->GetOverallScalabilityLevel() : -1; },
					[GUS](int32 i) { if (GUS()) GUS()->SetOverallScalabilityLevel(i); }),
				NameW, [this, GUS]() { return GUS() && GUS()->GetOverallScalabilityLevel() != Snap.Quality; })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("3D 渲染精度"), INVTEXT("3D 画面按这个比例渲染再放大，界面文字不受影响。自动 ≈ 内部 810p"),
				MechKit::Segmented({INVTEXT("自动"), INVTEXT("67%"), INVTEXT("83%"), INVTEXT("100%")},
					[CurScale]() {
						for (int32 i = 0; i < UE_ARRAY_COUNT(Scales); ++i)
						{
							if (FMath::IsNearlyEqual(CurScale(), Scales[i], 0.5f))
							{
								return i;
							}
						}
						return -1;
					},
					[GUS](int32 i) { if (GUS()) GUS()->SetResolutionScaleValueEx(Scales[i]); }),
				NameW, [this, CurScale]() { return !FMath::IsNearlyEqual(CurScale(), Snap.RenderScale, 0.5f); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))[MechKit::Section(INVTEXT("效果"), INVTEXT("马上生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("动态模糊"), INVTEXT("快速转镜头、冲刺时的拖影"),
				MechKit::Switch([]() { return FMechSettings::Get().bMotionBlur; }, [](bool b) {
					FMechSettings::Get().bMotionBlur = b;
					FMechSettings::Get().Save();
				}),
				NameW)
		];
}

TSharedRef<SWidget> SMechMenu::MakeAudioTab()
{
	auto Percent = [](float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.f))); };
	auto SaveNow = []() { FMechSettings::Get().Save(); };  // 滑条松手时存（拖的过程中不每帧写 ini）
	constexpr float NameW = 340.f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))[MechKit::Section(INVTEXT("音量"), INVTEXT("马上生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("主音量"), INVTEXT("所有声音"),
				MechKit::Slider([]() { return FMechSettings::Get().MasterVolume; },
					[this](float V) {
						FMechSettings::Get().MasterVolume = V;
						FMechSettings::Get().ApplyAudio(Owner.Get());
					},
					0.f, 1.f, 0.01f, Percent, SaveNow),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			// 11-01 新加：背景音乐（MechMusic 每帧读，拖的时候马上变）和结算场景的配乐（MechResultsStage）都乘它
			MechKit::Row(INVTEXT("音乐音量"), INVTEXT("菜单、房间、对战和结算的背景音乐，再乘主音量"),
				MechKit::Slider([]() { return FMechSettings::Get().MusicVolume; }, [](float V) { FMechSettings::Get().MusicVolume = V; },
					0.f, 1.f, 0.01f, Percent, SaveNow),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("告警音量"), INVTEXT("被锁定、导弹来袭、低装甲等提示音和语音，再乘主音量；拖动时响一声试听"),
				MechKit::Slider([]() { return FMechSettings::Get().AlertVolume; },
					[this](float V) {
						FMechSettings::Get().AlertVolume = V;
						UMechHudAlerts::PreviewVolume(Owner.Get());  // 拖的时候按新音量响一下锁定告警（限流），听得出多大声
					},
					0.f, 1.f, 0.01f, Percent, SaveNow),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 30.f, 0.f, 6.f))[MechKit::Section(INVTEXT("语音"), INVTEXT("点一下试听一句"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			// 告警语音（10-31）：第一项「跟随机体」= 用 HUD 主题的默认（新纪 冷静女声、维克托 机械合成、相位 企业客服），手动点别的就固定用那种
			MechKit::Row(INVTEXT("告警语音"), INVTEXT("「跟随机体」= 用机体那家厂商的界面默认的语音"),
				MechKit::Segmented({INVTEXT("跟随机体"), INVTEXT("关"), INVTEXT("冷静女声"), INVTEXT("机械合成"), INVTEXT("企业客服")},
					[]() { return FMechSettings::Get().bAlertVoiceSet ? FMechSettings::Get().AlertVoice + 1 : 0; },
					[this](int32 i) {
						FMechSettings& S = FMechSettings::Get();
						S.bAlertVoiceSet = i > 0;
						if (i > 0)
						{
							S.AlertVoice = i - 1;
						}
						S.Save();
						UMechHudAlerts::PreviewVoice(Owner.Get(), i > 0 ? i - 1 : UMechHudAlerts::VoiceStyle());
					}),
				NameW)
		];
}

namespace MechSettingsUI
{
	/** 输入数字的框：打字时显示打的内容，回车 / 失焦才解析、写入（解析不了就还原） */
	TSharedRef<SWidget> NumberBox(TFunction<float()> Get, TFunction<void(float)> Set)
	{
		TSharedRef<FString> Buf = MakeShared<FString>();
		TSharedRef<bool> bEditing = MakeShared<bool>(false);
		return SNew(SBox)
			.WidthOverride(150.f)
			[
				SNew(SEditableTextBox)
				.Style(&MechKit::TextBoxStyle())
				.Font(MechKit::Latin(19))
				.Text_Lambda([Get, Buf, bEditing]() { return FText::FromString(*bEditing ? *Buf : FString::Printf(TEXT("%.4g"), Get())); })
				.OnTextChanged_Lambda([Buf, bEditing](const FText& T) { *Buf = T.ToString(); *bEditing = true; })
				.OnTextCommitted_Lambda([Set, Buf, bEditing](const FText& T, ETextCommit::Type)
				{
					const FString Str = T.ToString().TrimStartAndEnd();
					if (Str.IsNumeric())
					{
						Set(FCString::Atof(*Str));
					}
					*bEditing = false;
				})
			];
	}
}

TSharedRef<SWidget> SMechMenu::MakeControlTab()
{
	auto SaveNow = []() { FMechSettings::Get().Save(); };
	constexpr float NameW = 340.f;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))[MechKit::Section(INVTEXT("鼠标与镜头"), INVTEXT("马上生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("鼠标灵敏度"), INVTEXT("和 CS2 一样：每个鼠标计数转 灵敏度 x 0.022 度，直接输入数字（CS2 的数字原样搬过来）"),
				MechSettingsUI::NumberBox([]() { return FMechSettings::Get().MouseSensitivity; }, [](float V) { FMechSettings::Get().MouseSensitivity = FMath::Clamp(V, 0.01f, 100.f); FMechSettings::Get().Save(); }),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("瞄准灵敏度"), INVTEXT("右键瞄准时用这个数（和上面同一种数字，独立设置，改鼠标灵敏度不跟着变）"),
				MechSettingsUI::NumberBox([]() { return FMechSettings::Get().AimSensitivity; }, [](float V) { FMechSettings::Get().AimSensitivity = FMath::Clamp(V, 0.01f, 100.f); FMechSettings::Get().Save(); }),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("开镜灵敏度"), INVTEXT("狙击枪开镜时用这个数（独立设置，改鼠标灵敏度不跟着变）"),
				MechSettingsUI::NumberBox([]() { return FMechSettings::Get().ScopeSensitivity; }, [](float V) { FMechSettings::Get().ScopeSensitivity = FMath::Clamp(V, 0.01f, 100.f); FMechSettings::Get().Save(); }),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("反转 Y 轴"), INVTEXT("鼠标往前推 = 镜头往下看"),
				MechKit::Switch([]() { return FMechSettings::Get().bInvertY; }, [](bool b) {
					FMechSettings::Get().bInvertY = b;
					FMechSettings::Get().Save();
				}),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("视野"), INVTEXT("越大看到的越多，画面边缘拉伸也越明显"),
				MechKit::Slider([]() { return FMechSettings::Get().FieldOfView; }, [](float V) { FMechSettings::Get().FieldOfView = V; },
					70.f, 110.f, 1.f, [](float V) { return FText::FromString(FString::Printf(TEXT("%d°"), FMath::RoundToInt(V))); }, SaveNow),
				NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			// 第一人称在出生 / 重生时读（AMechCharacter），游戏里按键切换也会记下来
			MechKit::Row(INVTEXT("第一人称"),
				TAttribute<FText>::CreateLambda([]() {
					return FText::Format(INVTEXT("出生和重生时用机体自己的眼睛看；游戏里按 {0} 随时切换"), MechKeys::KeyName(MechKeys::Key(EMechKey::FirstPerson)));
				}),
				MechKit::Switch([]() { return FMechSettings::Get().bFirstPerson; }, [](bool b) {
					FMechSettings::Get().bFirstPerson = b;
					FMechSettings::Get().Save();
				}),
				NameW)
		];
}

TSharedRef<SWidget> SMechMenu::MakeInterfaceTab()
{
	constexpr float NameW = 300.f;
	// 菜单配色（11-01 用户：「配色也可以在设置里面改」）：五套，点了马上换（MechKit::ApplyPalette），存进设置
	TSharedRef<SHorizontalBox> Palettes = SNew(SHorizontalBox);
	for (int32 i = 0; i < MechUITheme::NumPresets; ++i)
	{
		const FMechUITheme P = MechUITheme::Preset(i);
		Palettes->AddSlot()
		.AutoWidth()
		.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			MechKit::ChipWith(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
				[
					SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)[SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor(P.Accent)]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(P.Name))
					.Font(MechKit::Font(15))
					.ColorAndOpacity_Lambda([i]() { return FSlateColor(FMechSettings::Get().MenuPalette == i ? MechKit::Th().Ink : MechKit::Th().Dim); })
				],
				[i]() { return FMechSettings::Get().MenuPalette == i; },
				[i]() {
					FMechSettings::Get().MenuPalette = i;
					FMechSettings::Get().Save();
					MechKit::ApplyPalette(i);
				})
		];
	}
	// 战斗 HUD 主题（11-01）：-1 跟随机体（原来的做法），0–3 固定一套（MechHudTheme::Resolve 里读，命令行 -MechHudTheme= 仍然优先）
	TSharedRef<SHorizontalBox> Huds = SNew(SHorizontalBox);
	static const TCHAR* HudNames[] = {TEXT("跟随机体"), TEXT("联赛标准"), TEXT("新纪建设"), TEXT("维克托重工"), TEXT("相位电子")};
	for (int32 i = -1; i < 4; ++i)
	{
		TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
		if (i >= 0)
		{
			// 那套主题的主色做色块
			Content->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(10.f)
				.HeightOverride(10.f)
				[
					SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor(MechHudTheme::Preset(static_cast<EMechHudThemeId>(i)).Accent.CopyWithNewOpacity(1.f))
				]
			];
		}
		Content->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(HudNames[i + 1]))
			.Font(MechKit::Font(15))
			.ColorAndOpacity_Lambda([i]() { return FSlateColor(FMechSettings::Get().HudTheme == i ? MechKit::Th().Ink : MechKit::Th().Dim); })
		];
		Huds->AddSlot()
		.AutoWidth()
		.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			MechKit::ChipWith(Content, [i]() { return FMechSettings::Get().HudTheme == i; }, [i]() {
				FMechSettings::Get().HudTheme = i;
				FMechSettings::Get().Save();
			})
		];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 18.f, 0.f, 6.f))[MechKit::Section(INVTEXT("菜单"), INVTEXT("马上生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("菜单配色"), INVTEXT("菜单、房间、选配界面的强调色"), Palettes, NameW)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 30.f, 0.f, 6.f))[MechKit::Section(INVTEXT("战斗界面"), INVTEXT("马上生效"))]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("HUD 主题"), INVTEXT("「跟随机体」= 装哪家的框架就用哪家的界面；选了别的就一直用那套，观战时也是"), Huds, NameW)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MechKit::Row(INVTEXT("信息区"), INVTEXT("右上角的击毁、队友技能、拿插件、标点等消息"),
				MechKit::Switch([]() { return FMechSettings::Get().bShowFeed; }, [](bool b) {
					FMechSettings::Get().bShowFeed = b;
					FMechSettings::Get().Save();
				}),
				NameW)
		];
}

TSharedRef<SWidget> SMechMenu::MakeSettingsFooter()
{
	auto IsTab = [this](EMechSettingsTab T) { return SettingsTab == static_cast<int32>(T); };
	auto Show = [](bool b) { return b ? EVisibility::Visible : EVisibility::Collapsed; };
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 24.f, 0.f))
		[
			SNew(STextBlock)
			.Font(MechKit::Font(14))
			.AutoWrapText(true)
			.Text_Lambda([this, IsTab]() {
				if (IsTab(EMechSettingsTab::Graphics))
				{
					const int32 N = PendingGraphics();
					return N > 0 ? FText::Format(INVTEXT("有 {0} 项改了还没应用；不点「应用」就离开会撤销"), N)
								 : INVTEXT("显示、画质这几项要点「应用」才生效；其他设置改了马上生效、自动保存");
				}
				if (IsTab(EMechSettingsTab::Keys))
				{
					return KeyNotice.IsEmpty() ? INVTEXT("改键马上生效并保存") : KeyNotice;
				}
				return INVTEXT("改了马上生效，自动保存");
			})
			.ColorAndOpacity_Lambda([this, IsTab]() {
				const FMechUITheme& T = MechKit::Th();
				if (IsTab(EMechSettingsTab::Graphics) && PendingGraphics() > 0)
				{
					return FSlateColor(T.Warn);
				}
				return FSlateColor(IsTab(EMechSettingsTab::Keys) && !KeyNotice.IsEmpty() ? T.Accent : T.Dim);
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
		[
			SNew(SBox)
			.Visibility_Lambda([this, IsTab, Show]() { return Show(IsTab(EMechSettingsTab::Graphics) && PendingGraphics() > 0); })
			[
				MechKit::Button(INVTEXT("撤销"), [this]() { RevertGraphics(); })
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
		[
			SNew(SBox)
			.Visibility_Lambda([IsTab, Show]() { return Show(IsTab(EMechSettingsTab::Graphics)); })
			[
				MechKit::Button(INVTEXT("应用"), [this]() { ApplyGraphics(); }, true, TAttribute<bool>::CreateLambda([this]() { return PendingGraphics() > 0; }), 140.f)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
		[
			SNew(SBox)
			.Visibility_Lambda([IsTab, Show]() { return Show(IsTab(EMechSettingsTab::Keys)); })
			[
				MechKit::Button(INVTEXT("恢复默认"), [this]() {
					ListeningKey = -1;
					MechKeys::ResetAll();
					AMechCharacter::ApplyKeyBindings();
					KeyNotice = INVTEXT("已恢复默认键位");
				})
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MechKit::Button(INVTEXT("返回"), [this]() { SetPage(SettingsReturn); }, false, true, 140.f)
		];
}

TSharedRef<SWidget> SMechMenu::MakePausePage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Header(INVTEXT("暂停"), INVTEXT("联机时游戏不会停"))]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("继续"), [this]() { if (Owner.IsValid()) Owner->MenuResume(); })]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Owner.IsValid() && Owner->IsRangeMode() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Button(INVTEXT("配装"), [this]() { SetPage(EMechMenuPage::Loadout); })
			]
		]
		// 11-01 房主对局中也能带大家回房间（以前只有结算页有，暂停里只有「返回主菜单」，那样会解散房间）；任何阶段都能用
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Owner.IsValid() && Owner->IsRoomMatch() && Owner->CanRestartMatch() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Button(INVTEXT("回房间"), [this]() { if (Owner.IsValid()) Owner->MenuBackToRoom(); })
			]
		]
		// 11-02 训练场：房主随时开关友伤（房间页也能开，开局时带过来）
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Owner.IsValid() && Owner->CanToggleFriendlyFire() && !Owner->IsFriendlyFire() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Button(INVTEXT("打开友伤"), [this]() { if (Owner.IsValid()) Owner->MenuSetFriendlyFire(true); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return Owner.IsValid() && Owner->CanToggleFriendlyFire() && Owner->IsFriendlyFire() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Button(INVTEXT("关闭友伤"), [this]() { if (Owner.IsValid()) Owner->MenuSetFriendlyFire(false); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("设置"), [this]() {
			SettingsReturn = EMechMenuPage::Pause;
			SetPage(EMechMenuPage::Settings);
		})]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("返回主菜单"), [this]() { if (Owner.IsValid()) Owner->MenuBackToMain(); })]
		+ SVerticalBox::Slot().AutoHeight()[Button(INVTEXT("退出游戏"), [this]() { if (Owner.IsValid()) Owner->MenuQuit(); })];
}

TSharedRef<SWidget> SMechMenu::MakeResultsPage()
{
	return SAssignNew(ResultsHolder, SBox);
}

TSharedRef<SWidget> SMechMenu::MakeKeysPage()
{
	// 11-01 按键设置挪进设置页的「按键」页签（MakeKeysTab）；这一格只为了 WidgetIndex = 页面编号对得上，不会显示（SetPage(Keys) 会换成设置页）
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SMechMenu::MakeKeysTab()
{
	// 每行「操作名 + 键帽」，点一下进入「按任意键」，下一次按下的键盘键 / 鼠标键就是新键（OnPreviewKeyDown / OnPreviewMouseButtonDown）。
	// 11-01 分组成两栏（以前一长列要滚）：左 移动 + 视角与信息，右 战斗 + 其他；表里没列到的操作（以后新加的）自动放进「其他」
	struct FGroup
	{
		FText Name;
		TArray<EMechKey> Keys;
	};
	TArray<FGroup> Groups = {
		{INVTEXT("移动"), {EMechKey::MoveForward, EMechKey::MoveBack, EMechKey::MoveLeft, EMechKey::MoveRight, EMechKey::Jump, EMechKey::Boost, EMechKey::QuickBoost}},
		{INVTEXT("视角与信息"), {EMechKey::FirstPerson, EMechKey::Map, EMechKey::Scoreboard}},
		{INVTEXT("战斗"), {EMechKey::Stance, EMechKey::Fire, EMechKey::Alt, EMechKey::Shoulder, EMechKey::Reload}},
		{INVTEXT("其他"), {EMechKey::ThrowCore, EMechKey::Ping}},
	};
	for (int32 k = 0; k < MechKeys::Num; ++k)
	{
		const EMechKey Action = static_cast<EMechKey>(k);
		if (!Groups.ContainsByPredicate([Action](const FGroup& G) { return G.Keys.Contains(Action); }))
		{
			Groups.Last().Keys.Add(Action);
		}
	}
	auto KeyRow = [this](EMechKey Action) {
		const int32 i = static_cast<int32>(Action);
		TSharedRef<SMechKitButton> B = SNew(SMechKitButton)
			.Look(EMechKitLook::Row)
			.Selected_Lambda([this, i]() { return ListeningKey == i; })
			.OnClick([this, i]() {
				ListeningKey = ListeningKey == i ? -1 : i;  // 再点一下同一行 = 取消
				KeyNotice = ListeningKey >= 0 ? FText::Format(INVTEXT("「{0}」：按新键（键盘或鼠标键），Esc 取消"), MechKeys::Label(static_cast<EMechKey>(i))) : FText::GetEmpty();
			});
		B->SetContent(
			SNew(SBox)
			.HeightOverride(46.f)
			.Padding(FMargin(16.f, 0.f, 8.f, 0.f))
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					MechKit::Text(MechKeys::Label(Action), MechKit::Font(16), MechKit::Color(&FMechUITheme::Ink, 0.9f))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MechKit::KeyCap(
						TAttribute<FText>::CreateLambda([this, i, Action]() { return ListeningKey == i ? INVTEXT("按任意键…") : MechKeys::KeyName(MechKeys::Key(Action)); }),
						[this, i, Action]() { return ListeningKey == i ? 2 : MechKeys::Key(Action) == MechKeys::Default(Action) ? 0 : 1; }, 128.f)
				]
			]);
		return B;
	};
	auto Column = [&Groups, &KeyRow](std::initializer_list<int32> Which) {
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		bool bFirst = true;
		for (const int32 g : Which)
		{
			Col->AddSlot().AutoHeight().Padding(FMargin(0.f, bFirst ? 14.f : 28.f, 0.f, 6.f))[MechKit::Section(Groups[g].Name)];
			bFirst = false;
			for (const EMechKey K : Groups[g].Keys)
			{
				Col->AddSlot().AutoHeight()[KeyRow(K)];
			}
		}
		return Col;
	};
	TSharedRef<SScrollBox> Scroll = MechKit::ScrollBox();
	Scroll->AddSlot()
	.Padding(FMargin(0.f, 0.f, 18.f, 12.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[Column({0, 1})]
		+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(40.f)]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Column({2, 3})]
	];
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 14.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Font(MechKit::Font(13))
			.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
			.AutoWrapText(true)
			.Text(INVTEXT("点一行再按新键（键盘或鼠标键），Esc 取消；和别的操作撞键时两个互换。改过的键标成提醒色。手柄按键固定不变"))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[Scroll];
}

TSharedRef<SWidget> SMechMenu::MakeLoadoutPage()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Header(INVTEXT("配装"),
			INVTEXT("测试场里随便换，点「应用」马上在原地换装（装甲、能量回满）。灰色的还没做，先选不了。"))];
	for (int32 c = 0; c < MechLoadout::NumCategories; ++c)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
		{
			const MechLoadout::FItem& It = MechLoadout::Item(c, i);
			Row->AddSlot().AutoWidth()[Toggle(FText::FromString(It.Name), [this, c, i]() { return MechLoadout::Get(EditLoadout, c) == i; },
				[this, c, i]() { MechLoadout::Set(EditLoadout, c, i); }, It.bReady)];
		}
		Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))[SettingRow(FText::FromString(MechLoadout::CategoryName(c)), Row)];
		Box->AddSlot().AutoHeight().Padding(FMargin(150.f, 0.f, 0.f, 4.f))
		[
			SNew(STextBlock).Font(Font(13)).ColorAndOpacity(Dim).AutoWrapText(true).Text_Lambda([this, c]() {
				return FText::FromString(MechLoadout::Item(c, MechLoadout::Get(EditLoadout, c)).Desc);
			})
		];
	}
	Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 22.f, 0.f, 0.f))[Button(INVTEXT("应用"), [this]() {
		if (Owner.IsValid())
		{
			Owner->MenuApplyLoadout(EditLoadout);
			Owner->MenuResume();
		}
	})];
	Box->AddSlot().AutoHeight()[Button(INVTEXT("返回"), [this]() { SetPage(EMechMenuPage::Pause); })];
	return Box;
}
