#include "MechDraftUI.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerState.h"
#include "MechDraft.h"
#include "MechFonts.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechItemDetail.h"
#include "MechItemStudio.h"
#include "MechLoadout.h"
#include "MechMaps.h"
#include "MechRoom.h"
#include "MechSettings.h"
#include "MechUIKit.h"
#include "MechUITheme.h"
#include "MechWeapons.h"
#include "Misc/PackageName.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A = 1.f)
	{
		FLinearColor C(FColor(R, G, B));
		C.A = A;
		return C;
	}
	// 和 HUD 同一套色：青 = 自己 / 选中，橙 = 提醒，红 = 敌人 / 禁用，蓝 = 蓝队。
	// 10-31 起集中在 MechUITheme（以后按框架制造商换主题），这里只是引用
	const FLinearColor& Ink = MechUITheme::Get().Ink;
	const FLinearColor& Dim = MechUITheme::Get().Dim;
	const FLinearColor& Faint = MechUITheme::Get().Faint;
	const FLinearColor& Accent = MechUITheme::Get().Accent;
	const FLinearColor& Track = MechUITheme::Get().Track;
	const FLinearColor& Blue = MechUITheme::Get().Blue;
	const FLinearColor& Red = MechUITheme::Get().Red;
	const FLinearColor& Warn = MechUITheme::Get().Warn;

	FLinearColor TeamColor(int32 Team)
	{
		return Team == 0 ? Blue : Red;
	}

	FText TeamName(int32 Team)
	{
		return Team == 0 ? INVTEXT("蓝队") : INVTEXT("红队");
	}

	const FSlateBrush* Solid()
	{
		static FSlateColorBrush Brush(FLinearColor::White);
		return &Brush;
	}

	/** 卡片：深色底 + 细描边，按状态换描边颜色 */
	const FSlateBrush* Card(const FLinearColor& Outline, float Width = 1.f, const FLinearColor& Fill = FLinearColor(0.012f, 0.018f, 0.024f, 0.9f))
	{
		static TMap<uint32, TSharedPtr<FSlateRoundedBoxBrush>> Cache;
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(Outline.ToFColor(true)), GetTypeHash(Fill.ToFColor(true))), GetTypeHash(Width));
		TSharedPtr<FSlateRoundedBoxBrush>& B = Cache.FindOrAdd(Key);
		if (!B.IsValid())
		{
			B = MakeShared<FSlateRoundedBoxBrush>(Fill, 2.f, Outline, Width);
		}
		return B.Get();
	}

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return MechFonts::Get(Size, bBold);
	}

	/** 数字用 HUD 的 Bahnschrift */
	FSlateFontInfo NumFont(int32 Size)
	{
		return MechFonts::GetCanvas(Size);
	}

	TSharedRef<STextBlock> Txt(const TAttribute<FText>& Text, int32 Size, const TAttribute<FSlateColor>& Color)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size)).ColorAndOpacity(Color);
	}

	/** 一根细横线 */
	TSharedRef<SWidget> Rule(const FLinearColor& Color, float Height = 1.f)
	{
		return SNew(SBox).HeightOverride(Height)[SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(Color)];
	}

	// 涂装色板：主漆 / 副漆 / 点缀色各十种，每排第一个是出厂色
	const FColor PaintPalette[3][10] = {
		{FColor(0x5a, 0x63, 0x6c), FColor(0xd8, 0xd6, 0xcf), FColor(0x2b, 0x2f, 0x33), FColor(0x3f, 0x5a, 0x3a), FColor(0x2f, 0x47, 0x66),
			FColor(0x7a, 0x2e, 0x2a), FColor(0xb8, 0xa2, 0x7a), FColor(0x5c, 0x4a, 0x7a), FColor(0x1f, 0x5c, 0x5a), FColor(0x8a, 0x8f, 0x96)},
		{FColor(0x38, 0x3f, 0x46), FColor(0x1b, 0x1d, 0x20), FColor(0x4a, 0x4f, 0x55), FColor(0x6b, 0x6e, 0x72), FColor(0x2a, 0x33, 0x40),
			FColor(0x3a, 0x2a, 0x28), FColor(0x2e, 0x3a, 0x2c), FColor(0x4d, 0x44, 0x36), FColor(0x26, 0x2c, 0x3a), FColor(0xc9, 0xc5, 0xbb)},
		{FColor(0xcf, 0x5f, 0x1b), FColor(0xe2, 0xb1, 0x3c), FColor(0xc2, 0x3b, 0x3b), FColor(0x3f, 0xa0, 0xd8), FColor(0x5d, 0xca, 0xa5),
			FColor(0x9b, 0x59, 0xd0), FColor(0xe8, 0xe6, 0xdf), FColor(0x7f, 0xbf, 0x3f), FColor(0xe0, 0x6c, 0xa0), FColor(0x2b, 0x2b, 0x2b)},
	};
}

// ---------------------------------------------------------------- 状态
const AMechGameState* SMechDraftScreen::GetGS() const
{
	const AMechPlayerController* PC = Owner.Get();
	return PC && PC->GetWorld() ? PC->GetWorld()->GetGameState<AMechGameState>() : nullptr;
}

bool SMechDraftScreen::IsLobby() const
{
	const AMechGameState* GS = GetGS();
	return GS && GS->Phase == EMechRoundPhase::Lobby;
}

int32 SMechDraftScreen::MySlot() const
{
	const AMechGameState* GS = GetGS();
	const AMechPlayerController* PC = Owner.Get();
	return GS && PC && PC->PlayerState ? GS->Draft.FindPlayer(PC->PlayerState->GetPlayerId()) : INDEX_NONE;
}

int32 SMechDraftScreen::MyTeam() const
{
	const AMechGameState* GS = GetGS();
	const int32 Slot = MySlot();
	if (GS && GS->Draft.Slots.IsValidIndex(Slot))
	{
		return GS->Draft.Slots[Slot].Team;
	}
	const AMechPlayerController* PC = Owner.Get();
	if (GS && PC && PC->PlayerState)
	{
		const int32 Id = PC->PlayerState->GetPlayerId();
		if (const FMechLobbyPlayer* E = GS->Lobby.FindByPredicate([Id](const FMechLobbyPlayer& P) { return P.PlayerId == Id; }))
		{
			return E->Team;
		}
	}
	return INDEX_NONE;
}

bool SMechDraftScreen::IsMyBanTurn() const
{
	const AMechGameState* GS = GetGS();
	const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
	return T && T->bBan && MySlot() != INDEX_NONE && T->Team == MyTeam();
}

bool SMechDraftScreen::IsMyPickTurn() const
{
	const AMechGameState* GS = GetGS();
	const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
	return T && !T->bBan && T->Slot == MySlot() && MySlot() != INDEX_NONE;
}

int32 SMechDraftScreen::SideTeam(int32 Side) const
{
	const int32 Left = FMath::Max(0, MyTeam());
	return Side == 0 ? Left : 1 - Left;
}

FLinearColor SMechDraftScreen::SideColor(int32 Side) const
{
	// 11-02 个人战没有队伍：不用蓝 / 红
	const AMechGameState* GS = GetGS();
	return GS && GS->IsSoloDM() ? MechKit::Th().Ink : TeamColor(SideTeam(Side));
}

bool SMechDraftScreen::HasBanColumn() const
{
	const AMechGameState* GS = GetGS();
	return !GS || !GS->IsSoloDM();
}

bool SMechDraftScreen::IsCompactColumn(int32 Side) const
{
	const AMechGameState* GS = GetGS();
	const int32 Team = SideTeam(Side);
	return GS && GS->TeamCount[FMath::Clamp(Team, 0, 1)] > 6;
}

const FMechDraftSlot* SMechDraftScreen::SlotAt(int32 Team, int32 Row) const
{
	const AMechGameState* GS = GetGS();
	return GS ? GS->Draft.FindSlot(Team, Row) : nullptr;
}

FMechLoadout SMechDraftScreen::MyLoadout() const
{
	const AMechGameState* GS = GetGS();
	const int32 Slot = MySlot();
	return GS && GS->Draft.Slots.IsValidIndex(Slot) && GS->Draft.Slots[Slot].bLocked ? GS->Draft.Slots[Slot].Loadout : Edit;
}

// ---------------------------------------------------------------- 构造
void SMechDraftScreen::Construct(const FArguments& InArgs)
{
	MechKit::Init();  // 11-01 按设置套用菜单配色（直接进房间、没开过主菜单时也要套上）
	Owner = InArgs._Owner;
	Edit = FMechSettings::Get().Loadout;
	PaintEdit = FMechSettings::Get().Paint;
	PreviewBrush = MakeShared<FSlateBrush>();
	PreviewBrush->ImageSize = FVector2D(640.f, 360.f);
	PreviewBrush->DrawAs = ESlateBrushDrawType::Image;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor_Lambda([]() { return FSlateColor(MechKit::Th().Backdrop); })  // 11-01 跟菜单配色（底色带一点强调色的色相）
		]
		+ SOverlay::Slot()
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() {
				const AMechGameState* GS = GetGS();
				return IsLobby() ? 0 : GS && GS->Phase == EMechRoundPhase::Waiting ? 2 : 1;
			})
			+ SWidgetSwitcher::Slot()[MakeLobbyPage()]
			+ SWidgetSwitcher::Slot()[MakeDraftPage()]
			+ SWidgetSwitcher::Slot()[MakeWaitPage()]
		]
	];
	// 11-01 房间图里不会有选配：不建摄影棚、不拍武器图（等待页时照旧建，进选配前先把图拍上）
	const AMechGameState* GS = GetGS();
	if (GS && !MechRoom::IsTraining(GS->Room))
	{
		LastBattleMap = GS->Room.Map;
	}
	if (!GS || !GS->bRoomMode)
	{
		ShowItem(MechLoadout::Shooter, MechLoadout::Get(Edit, MechLoadout::Shooter));
		RebuildGrid();
	}
}

FReply SMechDraftScreen::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && Owner.IsValid())
	{
		Owner->OpenMenu(EMechMenuPage::Pause);
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SMechDraftScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const AMechGameState* GS = GetGS();
	if (!GS || IsLobby() || GS->Phase == EMechRoundPhase::Waiting)
	{
		return;
	}
	// 自己这套里被对面禁掉的换成能用的
	const int32 Team = MyTeam();
	if (Team >= 0)
	{
		const FMechLoadout Clean = MechDraft::Sanitize(GS->Draft, Team, Edit);
		if (FMemory::Memcmp(&Clean, &Edit, sizeof(FMechLoadout)) != 0)
		{
			for (int32 c = 0; c < 32; ++c)  // 底部配装栏要写「原来那件被禁，已换」
			{
				if (MechDraft::IsBannable(c) && MechLoadout::Get(Clean, c) != MechLoadout::Get(Edit, c))
				{
					ReplacedMask |= 1u << c;
				}
			}
			Edit = Clean;
			if (IsMyPickTurn())
			{
				SendPreview();
			}
		}
	}
	// 轮次换了：轮到自己选配就把挑好的这套公开出去；轮到自己禁用就切到射击类
	if (GS->Draft.Turn != LastTurn)
	{
		if (GS->Draft.Turn < LastTurn)  // 又开了一场选配（「再来一局」界面不重建）
		{
			TouchedMask = ReplacedMask = 0;
		}
		LastTurn = GS->Draft.Turn;
		BanCategory = BanIndex = -1;
		if (IsMyPickTurn())
		{
			SendPreview();
		}
		if (IsMyBanTurn() && !MechDraft::IsBannable(Category))
		{
			Category = MechLoadout::Shooter;
			RebuildGrid();
		}
	}
	// 大图贴图（摄影棚第一次拍时才建）
	if (AMechPlayerController* PC = Owner.Get())
	{
		if (AMechItemStudio* Studio = PC->GetStudio())
		{
			UTextureRenderTarget2D* RT = Studio->GetPreview();
			if (RT && PreviewBrush->GetResourceObject() != RT)
			{
				PreviewBrush->SetResourceObject(RT);
			}
		}
	}
}

void SMechDraftScreen::SendPreview()
{
	if (AMechPlayerController* PC = Owner.Get())
	{
		PC->DraftPick(Edit, false);
	}
}

const FSlateBrush* SMechDraftScreen::ThumbBrush(int32 InCategory, int32 Index)
{
	const int32 K = InCategory * 100 + Index;
	if (TSharedPtr<FSlateBrush>* Found = Brushes.Find(K))
	{
		return Found->Get();
	}
	AMechPlayerController* PC = Owner.Get();
	AMechItemStudio* Studio = PC ? PC->GetStudio() : nullptr;
	UTextureRenderTarget2D* RT = Studio ? Studio->GetThumb(InCategory, Index) : nullptr;
	if (!RT)
	{
		return nullptr;
	}
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
	B->SetResourceObject(RT);
	B->ImageSize = FVector2D(208.f, 130.f);
	B->DrawAs = ESlateBrushDrawType::Image;
	Brushes.Add(K, B);
	return B.Get();
}

void SMechDraftScreen::ShowItem(int32 InCategory, int32 Index)
{
	FocusCategory = InCategory;
	FocusIndex = Index;
	if (AMechPlayerController* PC = Owner.Get())
	{
		if (AMechItemStudio* Studio = PC->GetStudio())
		{
			Studio->SetPreview(InCategory, Index);
		}
	}
}

void SMechDraftScreen::ClickItem(int32 InCategory, int32 Index)
{
	ShowItem(InCategory, Index);
	const AMechGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}
	if (IsMyBanTurn())
	{
		if (MechDraft::IsBannable(InCategory) && MechLoadout::Item(InCategory, Index).bReady && !MechDraft::HasBanned(GS->Draft, MyTeam(), InCategory, Index))
		{
			BanCategory = InCategory;
			BanIndex = Index;
		}
		return;
	}
	const int32 Team = FMath::Max(0, MyTeam());
	const int32 Slot = MySlot();
	const bool bLocked = GS->Draft.Slots.IsValidIndex(Slot) && GS->Draft.Slots[Slot].bLocked;
	if (Slot != INDEX_NONE && !bLocked && MechDraft::IsAllowed(GS->Draft, Team, InCategory, Index))
	{
		MechLoadout::Set(Edit, InCategory, Index);
		TouchedMask |= 1u << InCategory;
		ReplacedMask &= ~(1u << InCategory);
		if (IsMyPickTurn())
		{
			SendPreview();
		}
	}
}

void SMechDraftScreen::Confirm()
{
	AMechPlayerController* PC = Owner.Get();
	if (!PC)
	{
		return;
	}
	if (IsMyBanTurn() && BanCategory >= 0)
	{
		PC->DraftBan(BanCategory, BanIndex);
	}
	else if (IsMyPickTurn())
	{
		FMechSettings::Get().Loadout = Edit;  // 下一场默认还是这一套
		FMechSettings::Get().Save();
		PC->DraftPick(Edit, true);
	}
}

// ---------------------------------------------------------------- 选配页
TSharedRef<SWidget> SMechDraftScreen::MakeDraftPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeTopBar()]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(FMargin(28.f, 14.f, 28.f, 10.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(270.f)[MakeTeamColumn(0)]]  // 11-01 300 → 270：字重排成三行后放得下，让给中间的照片
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(FMargin(28.f, 0.f))
			[
				SNew(SVerticalBox)
				// 11-01 详情固定高度（放得下最长的那件，-MechLoadoutShots 日志里有「详情最高」）：以前按内容长，换一件页签和卡片就上下跳
				+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(560.f)[MakePreview()]]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 8.f))[MakeTabs()]
				// 卡片区：逻辑尺寸 1920×1080 里中间一栏 1268 宽、卡片区 240 高（864 - 详情 560 - 页签 64）。
				// 11-01 卡片大小按这一页的件数算（RebuildGrid → MechCardLayout）：少的一排用满，多的（肩部）排两行；框架三家并排一排（28+6+190 = 224 高）
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SScrollBox)
					.Style(&MechKit::ScrollBoxStyle())  // 11-01 细滚动条（MechUIKit）
					.ScrollBarStyle(&MechKit::ScrollBarStyle())
					.ScrollBarThickness(FVector2D(3.f, 3.f))
					// 11-01 不要滚动条（一排放得下）。而且滚动条出现会占掉 5 像素宽，最后一张挤到第二行、又更需要滚动条，一旦出现就下不去；
					// 还留着滚动框：比 16:9 窄的窗口会折行，滚轮还能滚
					.ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()
					[
						SAssignNew(Grid, SWrapBox)
						.UseAllottedSize(true)
						.InnerSlotPadding(FVector2D(10.f, 10.f))
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(270.f)[MakeTeamColumn(1)]]
		]
		+ SVerticalBox::Slot().AutoHeight()[MakeBottomBar()];
}

TSharedRef<SWidget> SMechDraftScreen::MakeTopBar()
{
	auto Remaining = [this]() {
		const AMechGameState* GS = GetGS();
		return GS ? FMath::Max(0.f, GS->GetPhaseRemaining()) : 0.f;
	};
	auto Full = [this]() {
		const AMechGameState* GS = GetGS();
		const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
		return T ? (T->bBan ? MechDraft::BanTime : MechDraft::PickTime) : MechDraft::ReviewTime;
	};
	auto Mine = [this]() { return IsMyBanTurn() || IsMyPickTurn(); };
	// 倒计时：最后 10 秒提醒色、最后 5 秒红（以前只有一档，而且字和底差得不够大）。全部定下后那 5 秒只是等开打，不用催
	auto ClockColor = [this, Remaining](const FLinearColor& Normal) {
		const AMechGameState* GS = GetGS();
		const float R = Remaining();
		return !GS || GS->Draft.bDone ? Normal : R < 5.f ? MechKit::Th().Bad : R < 10.f ? MechKit::Th().Warn : Normal;
	};
	// 11-01 用户：不知道现在轮到谁、后面还有谁。左边标题 + 一句话，中间回合顺序条，右边倒计时；轮到自己时整条顶栏带淡强调色、底边一道实线、前面一个实心「轮到你」
	return SNew(SBox)
		.HeightOverride(92.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(Solid())
				.BorderBackgroundColor(MechKit::Color(&FMechUITheme::Accent, 0.09f))
				.Visibility_Lambda([Mine]() { return Mine() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			]
			+ SOverlay::Slot().VAlign(VAlign_Bottom)[MechKit::Hairline(1.4f)]
			+ SOverlay::Slot()
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox)
				.HeightOverride(2.f)
				.Visibility_Lambda([Mine]() { return Mine() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				[
					SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Accent))
				]
			]
			+ SOverlay::Slot()
			.Padding(FMargin(36.f, 10.f, 36.f, 10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(330.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Bottom)
							[
								SNew(STextBlock)
								.Font(MechKit::Light(30))
								.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
								.Text_Lambda([this]() {
									const AMechGameState* GS = GetGS();
									const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
									return !GS ? FText::GetEmpty() : GS->Draft.bDone ? INVTEXT("就绪") : T && T->bBan ? INVTEXT("禁用") : INVTEXT("选配");
								})
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Bottom)
							.Padding(FMargin(12.f, 0.f, 0.f, 8.f))
							[
								SNew(STextBlock)
								.Font(MechKit::Latin(11, 240))
								.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
								.Text_Lambda([this]() {
									const AMechGameState* GS = GetGS();
									const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
									return !GS ? FText::GetEmpty() : GS->Draft.bDone ? INVTEXT("READY") : T && T->bBan ? INVTEXT("BAN PHASE") : INVTEXT("DRAFT");
								})
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
							[
								SNew(SBorder)
								.BorderImage(Solid())
								.BorderBackgroundColor(MechKit::Color(&FMechUITheme::Accent))
								.Padding(FMargin(8.f, 1.f, 8.f, 2.f))
								.Visibility_Lambda([Mine]() { return Mine() ? EVisibility::Visible : EVisibility::Collapsed; })
								[
									SNew(STextBlock).Font(Font(14, true)).ColorAndOpacity(MechKit::Color(&FMechUITheme::AccentInk)).Text(INVTEXT("轮到你"))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Font(Font(15))
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text_Lambda([this]() {
									const AMechGameState* GS = GetGS();
									const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
									if (!GS)
									{
										return FText::GetEmpty();
									}
									if (GS->Draft.bDone)
									{
										return INVTEXT("全部定下，马上开打");
									}
									if (!T)
									{
										return FText::GetEmpty();
									}
									if (IsMyBanTurn())
									{
										return INVTEXT("禁掉对面一件装备");
									}
									if (IsMyPickTurn())
									{
										return INVTEXT("挑好整套配装后确认");
									}
									if (T->bBan)
									{
										return FText::Format(INVTEXT("{0}正在禁用"), TeamName(T->Team));
									}
									const FString Who = GS->Draft.Slots.IsValidIndex(T->Slot) ? GS->Draft.Slots[T->Slot].Name : FString();
									if (GS->IsSoloDM())
									{
										return FText::Format(INVTEXT("{0} 正在选配"), FText::FromString(Who));  // 11-02 个人战没有队伍
									}
									return FText::Format(INVTEXT("{0} {1} 正在选配"), TeamName(T->Team), FText::FromString(Who));
								})
								.ColorAndOpacity_Lambda([this, Mine]() {
									const AMechGameState* GS = GetGS();
									const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
									return FSlateColor(Mine() ? Ink : T && !GS->IsSoloDM() ? TeamColor(T->Team) : Dim);
								})
							]
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(12.f, 0.f))
				[
					MakeTurnBar()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					[
						SNew(STextBlock)
						.Font(MechKit::Latin(44))
						.Text_Lambda([Remaining]() { return FText::AsNumber(FMath::CeilToInt(Remaining())); })
						.ColorAndOpacity_Lambda([ClockColor]() { return FSlateColor(ClockColor(MechKit::Th().Ink)); })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
					[
						// 底槽 + 剩余时间（从右往左缩）
						SNew(SBox)
						.WidthOverride(170.f)
						.HeightOverride(2.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Ink, 0.12f))]
							+ SOverlay::Slot()
							.HAlign(HAlign_Right)
							[
								SNew(SBox)
								.WidthOverride_Lambda([Remaining, Full]() { return FOptionalSize(170.f * FMath::Clamp(Remaining() / Full(), 0.f, 1.f)); })
								[
									SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor_Lambda([ClockColor]() { return FSlateColor(ClockColor(MechKit::Th().Accent)); })
								]
							]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakeTurnBar()
{
	// 预先建够格子（每队最多 4 台：4 禁 + 8 选 = 12，多留几个），用不到的折叠：不用每轮重建
	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
	for (int32 i = 0; i < 16; ++i)
	{
		Bar->AddSlot().AutoWidth()[MakeTurnCell(i)];
	}
	return Bar;
}

TSharedRef<SWidget> SMechDraftScreen::MakeTurnCell(int32 Cell)
{
	auto Turn = [this, Cell]() -> const FMechDraftTurn* {
		const AMechGameState* GS = GetGS();
		return GS && GS->Draft.Order.IsValidIndex(Cell) ? &GS->Draft.Order[Cell] : nullptr;
	};
	// -1 已完成，0 现在这一轮，1 还没到
	auto State = [this, Cell]() {
		const AMechGameState* GS = GetGS();
		const int32 Now = GS ? GS->Draft.Turn : 0;
		return Cell < Now ? -1 : Cell == Now ? 0 : 1;
	};
	// 禁用和选配之间多留一点空：一眼分成两段
	auto Gap = [this, Cell]() {
		const AMechGameState* GS = GetGS();
		return GS && Cell > 0 && GS->Draft.Order.IsValidIndex(Cell) && GS->Draft.Order[Cell - 1].bBan && !GS->Draft.Order[Cell].bBan ? 12.f : 0.f;
	};
	auto IsMine = [this, Cell, Turn]() {
		const FMechDraftTurn* T = Turn();
		return T && !T->bBan && T->Slot == MySlot() && MySlot() != INDEX_NONE;
	};
	return SNew(SBox)
		.Visibility_Lambda([Turn]() { return Turn() ? EVisibility::Visible : EVisibility::Collapsed; })
		.WidthOverride_Lambda([this, Gap]() {
			const AMechGameState* GS = GetGS();
			const int32 N = GS ? FMath::Max(GS->Draft.Order.Num(), 1) : 1;
			return FOptionalSize(FMath::Min(132.f, FMath::FloorToFloat((1250.f - 12.f) / N)) + Gap());  // 顶栏去掉两边的字，中间约 1250 宽
		})
		.HeightOverride(62.f)
		.Padding_Lambda([Gap]() { return FMargin(Gap(), 0.f, 4.f, 0.f); })
		[
			SNew(SBorder)
			.BorderImage_Lambda([State]() {
				const FMechUITheme& T = MechKit::Th();
				const int32 S = State();
				return S == 0 ? MechKit::Box(T.Accent.CopyWithNewOpacity(0.14f), T.Accent, 1.f)
					: S < 0 ? MechKit::Box(T.Ink.CopyWithNewOpacity(0.04f), T.Line, 1.f) : MechKit::Box(FLinearColor::Transparent, T.Line, 1.f);
			})
			.Padding(FMargin(0.f))
			[
				SNew(SVerticalBox)
				// 上面一条队伍色：不用读字也能看出 A B B A 的顺序
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(3.f)
					[
						SNew(SBorder)
						.BorderImage(Solid())
						.BorderBackgroundColor_Lambda([Turn, State]() {
							const FMechDraftTurn* T = Turn();
							const int32 S = State();
							return FSlateColor(T ? TeamColor(T->Team).CopyWithNewOpacity(S == 0 ? 1.f : S < 0 ? 0.65f : 0.35f) : FLinearColor::Transparent);
						})
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(8.f, 0.f, 4.f, 0.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font(13))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text_Lambda([this, Turn]() {
							const AMechGameState* GS = GetGS();
							const FMechDraftTurn* T = Turn();
							if (!GS || !T)
							{
								return FText::GetEmpty();
							}
							if (T->bBan)
							{
								return MySlot() == INDEX_NONE ? FText::Format(INVTEXT("{0}禁用"), TeamName(T->Team)) : T->Team == MyTeam() ? INVTEXT("我方禁用") : INVTEXT("对面禁用");
							}
							if (T->Slot == MySlot() && MySlot() != INDEX_NONE)
							{
								return INVTEXT("你");
							}
							return GS->Draft.Slots.IsValidIndex(T->Slot) ? FText::FromString(GS->Draft.Slots[T->Slot].Name) : FText::GetEmpty();
						})
						.ColorAndOpacity_Lambda([State, IsMine]() {
							const FMechUITheme& T = MechKit::Th();
							const int32 S = State();
							return FSlateColor(IsMine() ? T.Accent : S == 0 ? T.Ink : S < 0 ? T.Dim : T.Dim.CopyWithNewOpacity(0.7f));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 1.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Font(Font(14))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text_Lambda([this, Cell, Turn, State]() {
							const AMechGameState* GS = GetGS();
							const FMechDraftTurn* T = Turn();
							if (!GS || !T)
							{
								return FText::GetEmpty();
							}
							const int32 S = State();
							if (S == 0)
							{
								return T->bBan ? INVTEXT("禁用中…") : INVTEXT("选配中…");
							}
							if (S > 0)
							{
								return T->bBan ? INVTEXT("待禁") : INVTEXT("待选");
							}
							if (T->bBan)
							{
								// 第几个禁用轮就是 Bans 第几条（每个禁用轮结束追加一条，超时也会自动禁一件）
								int32 Nth = 0;
								for (int32 i = 0; i < Cell; ++i)
								{
									Nth += GS->Draft.Order[i].bBan ? 1 : 0;
								}
								return GS->Draft.Bans.IsValidIndex(Nth)
									? FText::FromString(MechLoadout::Item(GS->Draft.Bans[Nth].Category, GS->Draft.Bans[Nth].Index).Name)
									: FText::GetEmpty();
							}
							return GS->Draft.Slots.IsValidIndex(T->Slot)
								? FText::FromString(MechLoadout::Item(MechLoadout::Shooter, MechLoadout::Get(GS->Draft.Slots[T->Slot].Loadout, MechLoadout::Shooter)).Name)
								: FText::GetEmpty();
						})
						.ColorAndOpacity_Lambda([State]() {
							const FMechUITheme& T = MechKit::Th();
							const int32 S = State();
							return FSlateColor(S == 0 ? T.Accent : S < 0 ? T.Ink : T.Dim.CopyWithNewOpacity(0.55f));
						})
					]
				]
			]
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakeTeamColumn(int32 Side)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			// 队伍色的一小块 + 队名
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(4.f)
				.HeightOverride(22.f)
				[
					SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor_Lambda([this, Side]() { return FSlateColor(SideColor(Side)); })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.Font(Font(21))
				.Text_Lambda([this, Side]() {
					// 11-02 个人战没有队伍：两栏只是选配的先后分组
					const AMechGameState* GS = GetGS();
					return GS && GS->IsSoloDM() ? (SideTeam(Side) == 0 ? INVTEXT("A 组") : INVTEXT("B 组")) : TeamName(SideTeam(Side));
				})
				.ColorAndOpacity_Lambda([this, Side]() { return FSlateColor(SideColor(Side)); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(10.f, 0.f, 0.f, 3.f))
			[
				SNew(STextBlock)
				.Font(Font(13))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
				.Text_Lambda([this, Side]() {
					const AMechGameState* GS = GetGS();
					return GS && GS->IsSoloDM() ? INVTEXT("选配顺序") : Side == 0 && MySlot() != INDEX_NONE ? INVTEXT("你的队") : FText::GetEmpty();
				})
			]
		]
		// 11-01 写清楚谁不能用（以前是「BANS · 禁用」，看不出禁的是谁的）
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 14.f, 0.f, 6.f))
		[
			SNew(STextBlock)
			.Font(Font(13))
			.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
			.Visibility_Lambda([this]() { return HasBanColumn() ? EVisibility::Visible : EVisibility::Collapsed; })
			.Text_Lambda([this, Side]() {
				if (MySlot() == INDEX_NONE)
				{
					return FText::Format(INVTEXT("{0}禁的 · {1}不能用"), TeamName(SideTeam(Side)), TeamName(1 - SideTeam(Side)));
				}
				return Side == 0 ? INVTEXT("我方禁的 · 对面不能用") : INVTEXT("对面禁的 · 你不能用");
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 16.f))
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return HasBanColumn() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))[MakeBanChip(Side, 0)]
			+ SHorizontalBox::Slot().AutoWidth()[MakeBanChip(Side, 1)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[MechKit::Text(INVTEXT("机体"), Font(13), MechKit::Color(&FMechUITheme::Dim))];
	for (int32 Row = 0; Row < MechRoom::MaxMechs; ++Row)  // 11-02 一队的台数由房主定（1v9 也行），用不到的折叠
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[MakeSlotCard(Side, Row)];
	}
	return Box;
}

TSharedRef<SWidget> SMechDraftScreen::MakeBanChip(int32 Side, int32 Which)
{
	// 这队禁的第 Which 件（禁的是对面）
	auto Ban = [this, Side, Which]() -> const FMechDraftBan* {
		const AMechGameState* GS = GetGS();
		if (!GS)
		{
			return nullptr;
		}
		int32 n = 0;
		for (const FMechDraftBan& B : GS->Draft.Bans)
		{
			if (B.Team == SideTeam(Side) && n++ == Which)
			{
				return &B;
			}
		}
		return nullptr;
	};
	// 这一块是不是正在禁的那个位置（这队现在轮到禁用、前面的位置都禁过了）
	auto IsNow = [this, Side, Which]() {
		const AMechGameState* GS = GetGS();
		const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
		if (!T || !T->bBan || T->Team != SideTeam(Side))
		{
			return false;
		}
		return GS->Draft.Bans.FilterByPredicate([this, Side](const FMechDraftBan& B) { return B.Team == SideTeam(Side); }).Num() == Which;
	};
	// 11-01：没实物照片的装备画图标（以前全黑一块）；照片铺满、压暗偏红，下面一条名字。空着的写「还没禁」/「正在禁…」，不是只留个框
	return SNew(SBox)
		.WidthOverride(131.f)
		.HeightOverride(96.f)
		[
			SNew(SBorder)
			.BorderImage_Lambda([Ban, IsNow]() {
				const FMechUITheme& T = MechKit::Th();
				return Ban() ? MechKit::Box(T.CardFill, T.Bad.CopyWithNewOpacity(0.8f), 1.f)
					: IsNow() ? MechKit::Box(T.Accent.CopyWithNewOpacity(0.08f), T.Accent, 1.f) : MechKit::Box(T.Ink.CopyWithNewOpacity(0.02f), T.Line, 1.f);
			})
			.Padding(FMargin(1.f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					.Visibility_Lambda([Ban]() { return Ban() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SNew(SBorder)
						.BorderImage(MechKit::NoBrush())
						.Padding(FMargin(0.f))
						.ColorAndOpacity(FLinearColor(1.f, 0.62f, 0.62f, 0.8f))
						[
							MechItemPicture(
								[Ban]() { const FMechDraftBan* B = Ban(); return B ? B->Category : -1; },
								[Ban]() { const FMechDraftBan* B = Ban(); return B ? B->Index : 0; },
								[this](int32 C, int32 I) { return ThumbBrush(C, I); })
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(7.f, 3.f, 4.f, 4.f))
					[
						SNew(STextBlock)
						.Font(Font(13))
						.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text_Lambda([Ban]() {
							const FMechDraftBan* B = Ban();
							return B ? FText::FromString(MechLoadout::Item(B->Category, B->Index).Name) : FText::GetEmpty();
						})
					]
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(Font(13))
					.Text_Lambda([IsNow]() { return IsNow() ? INVTEXT("正在禁…") : INVTEXT("还没禁"); })
					.ColorAndOpacity_Lambda([IsNow]() { return FSlateColor(IsNow() ? MechKit::Th().Accent : MechKit::Th().Dim.CopyWithNewOpacity(0.7f)); })
					.Visibility_Lambda([Ban]() { return Ban() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				]
			]
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakeSlotCard(int32 Side, int32 Row)
{
	auto Slot = [this, Side, Row]() { return SlotAt(SideTeam(Side), Row); };
	auto IsTurn = [this, Slot]() {
		const AMechGameState* GS = GetGS();
		const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
		const FMechDraftSlot* S = Slot();
		return T && !T->bBan && S && GS->Draft.Slots.IsValidIndex(T->Slot) && &GS->Draft.Slots[T->Slot] == S;
	};
	// 别人的配装：定下了或正在选的才公开
	auto Shown = [Slot, IsTurn]() { const FMechDraftSlot* S = Slot(); return S && (S->bLocked || IsTurn()); };
	// 这台排第几个选配（没选的人那一行写这个，不是只留空框）
	auto PickOrder = [this, Slot]() {
		const AMechGameState* GS = GetGS();
		const FMechDraftSlot* S = Slot();
		int32 N = 0;
		if (GS && S)
		{
			for (const FMechDraftTurn& T : GS->Draft.Order)
			{
				if (!T.bBan)
				{
					++N;
					if (GS->Draft.Slots.IsValidIndex(T.Slot) && &GS->Draft.Slots[T.Slot] == S)
					{
						return N;
					}
				}
			}
		}
		return 0;
	};
	auto Line = [Slot, Shown](int32 A, int32 B, const TCHAR* Empty) {
		const FMechDraftSlot* S = Slot();
		if (!S || !Shown())
		{
			return FText::FromString(Empty);
		}
		if (B < 0)
		{
			return FText::FromString(MechLoadout::Item(A, MechLoadout::Get(S->Loadout, A)).Name);  // 肩部一行只写一样
		}
		return FText::FromString(FString::Printf(TEXT("%s · %s"), MechLoadout::Item(A, MechLoadout::Get(S->Loadout, A)).Name, MechLoadout::Item(B, MechLoadout::Get(S->Loadout, B)).Name));
	};
	// 11-01：左边一道队伍色竖条；正在选的那台强调色描边 + 淡底，定下的实一点，还没轮到的最淡。
	// 字分三行（射击 · 近战 / 框架 · 推进器 / 肩部）：以前四样挤一行，「新纪 中型 · 均衡型 · 钩…」被截断，正好看不到后面两样
	return SNew(SBox)
		.HeightOverride_Lambda([this, Side]() { return FOptionalSize(IsCompactColumn(Side) ? 64.f : 100.f); })  // 四行字（雅黑行高大）；11-02 这队超过 6 台时只留两行，一屏放得下
		.Visibility_Lambda([Slot]() { return Slot() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SBorder)
			.BorderImage_Lambda([IsTurn, Slot]() {
				const FMechUITheme& T = MechKit::Th();
				const FMechDraftSlot* S = Slot();
				return IsTurn() ? MechKit::Box(T.Accent.CopyWithNewOpacity(0.08f), T.Accent, 1.f)
					: S && S->bLocked ? MechKit::Box(T.CardFill, T.Ink.CopyWithNewOpacity(0.2f), 1.f) : MechKit::Box(T.CardFill, T.Line, 1.f);
			})
			.Padding(FMargin(0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(3.f)
					[
						SNew(SBorder).BorderImage(Solid()).BorderBackgroundColor_Lambda([this, Side]() { return FSlateColor(SideColor(Side)); })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.f, 0.f, 0.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(92.f)
					.HeightOverride(58.f)  // 16:10，整把枪都看得到（竖着铺满卡片高度会把两头裁掉）
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[
							MechItemPicture(
								[Shown]() { return Shown() ? int32(MechLoadout::Shooter) : -1; },
								[Slot]() { const FMechDraftSlot* S = Slot(); return S ? MechLoadout::Get(S->Loadout, MechLoadout::Shooter) : 0; },
								[this](int32 C, int32 I) { return ThumbBrush(C, I); })
						]
						// 还没选：图的位置写字
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(Font(13))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim, 0.7f))
							.Text(INVTEXT("待选"))
							.Visibility_Lambda([Shown]() { return Shown() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(10.f, 0.f, 8.f, 0.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							SNew(STextBlock)
							.Font(Font(15))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text_Lambda([this, Slot]() {
								const FMechDraftSlot* S = Slot();
								if (!S)
								{
									return FText::GetEmpty();
								}
								const AMechGameState* GS = GetGS();
								const bool bMe = GS && GS->Draft.Slots.IsValidIndex(MySlot()) && &GS->Draft.Slots[MySlot()] == S;
								return FText::FromString(bMe && S->Name != TEXT("你") ? S->Name + TEXT("（你）") : S->Name);
							})
							.ColorAndOpacity_Lambda([this, Side]() { return FSlateColor(SideColor(Side)); })
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(6.f, 0.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Font(Font(12))
							.Text_Lambda([Slot, IsTurn]() {
								const FMechDraftSlot* S = Slot();
								return IsTurn() ? INVTEXT("选配中") : S && S->bLocked ? INVTEXT("已定") : FText::GetEmpty();
							})
							.ColorAndOpacity_Lambda([IsTurn]() { return FSlateColor(IsTurn() ? MechKit::Th().Accent : MechKit::Th().Dim); })
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.Font(Font(13))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.ColorAndOpacity_Lambda([Shown]() { return FSlateColor(Shown() ? MechKit::Th().Ink : MechKit::Th().Dim); })
						.Text_Lambda([Shown, PickOrder, Line]() {
							return Shown() ? Line(MechLoadout::Shooter, MechLoadout::Melee, TEXT("")) : FText::Format(INVTEXT("第 {0} 个选配"), FText::AsNumber(PickOrder()));
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font(12))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
						.Visibility_Lambda([this, Side]() { return IsCompactColumn(Side) ? EVisibility::Collapsed : EVisibility::Visible; })
						.Text_Lambda([Line]() { return Line(MechLoadout::Frame, MechLoadout::Booster, TEXT("")); })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font(12))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
						.Visibility_Lambda([this, Side]() { return IsCompactColumn(Side) ? EVisibility::Collapsed : EVisibility::Visible; })
						.Text_Lambda([Line]() { return Line(MechLoadout::Shoulder, -1, TEXT("")); })
					]
				]
			]
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakePreview()
{
	// 10-31（用户：配装照片要详细一点，不只枪有照片）：照片 + 详细信息换成 SMechItemDetail（MechItemDetail.cpp）——
	// 五类都有实拍照片（转台可拖动 + 详细视角）、名牌（制造商、型号、名字）、参数条和读数图。中间一栏 1268 宽：照片 760 + 间隔 28 + 信息 480
	return SNew(SMechItemDetail)
		.Owner(Owner)
		.GetCategory([this]() { return FocusCategory; })
		.GetIndex([this]() { return FocusIndex; })
		.GetCompare([this]() { return MySlot() != INDEX_NONE ? MechLoadout::Get(MyLoadout(), FocusCategory) : -1; })
		.GetFrame([this]() { return MySlot() != INDEX_NONE ? MechLoadout::Get(MyLoadout(), MechLoadout::Frame) : -1; })
		.PhotoSize(FVector2D(760.f, 428.f))
		.InfoWidth(480.f)
		.Status_Lambda([this]() {
			const AMechGameState* GS = GetGS();
			if (!MechLoadout::Item(FocusCategory, FocusIndex).bReady)
			{
				return INVTEXT("还没做好");
			}
			if (GS && MyTeam() >= 0 && MechDraft::IsBannedFor(GS->Draft, MyTeam(), FocusCategory, FocusIndex))
			{
				return INVTEXT("你不能用：被对面禁了");
			}
			if (GS && MyTeam() >= 0 && MechDraft::HasBanned(GS->Draft, MyTeam(), FocusCategory, FocusIndex))
			{
				return INVTEXT("对面不能用：你们禁的");
			}
			return MechLoadout::Get(MyLoadout(), FocusCategory) == FocusIndex && MySlot() != INDEX_NONE ? INVTEXT("已选") : FText::GetEmpty();
		})
		.StatusColor_Lambda([this]() {
			const AMechGameState* GS = GetGS();
			const bool bBad = !MechLoadout::Item(FocusCategory, FocusIndex).bReady ||
				(GS && MyTeam() >= 0 && MechDraft::IsBannedFor(GS->Draft, MyTeam(), FocusCategory, FocusIndex));
			return FSlateColor(bBad ? Red : Accent);
		})
		.OnTestSelect([this](int32 C, int32 I) {
			Category = C;
			RebuildGrid();
			ClickItem(C, I);
		})
		.TestName(TEXT("draft"));
}

TSharedRef<SWidget> SMechDraftScreen::MakeTabs()
{
	// 禁用轮里能点的是 MechDraft::IsBannable 的几类。几类、什么顺序在 MechLoadoutCategories（和测试场同一张表）
	// 11-01 换成 MechUIKit 的页签（选中的底下一道强调色从中间长出来，悬停有细线）
	const TArrayView<const int32> Order = MechLoadoutCategories();
	TArray<FText> Labels;
	for (const int32 c : Order)
	{
		Labels.Add(FText::FromString(MechLoadout::CategoryName(c)));
	}
	return MechKit::Tabs(Labels,
		[this, Order]() { return Order.Find(Category); },
		[this, Order](int32 i) {
			const int32 c = Order[i];
			Category = c;
			RebuildGrid();
			ShowItem(c, MechLoadout::Get(Edit, c));
		},
		[this, Order](int32 i) { return !IsMyBanTurn() || MechDraft::IsBannable(Order[i]); }, 120.f, 16.f);
}

TSharedRef<SWidget> SMechDraftScreen::MakeItemCard(int32 InCategory, int32 Index, float Width, float Height)
{
	auto Banned = [this, InCategory, Index]() {
		const AMechGameState* GS = GetGS();
		return GS && MyTeam() >= 0 && MechDraft::IsBannedFor(GS->Draft, MyTeam(), InCategory, Index);
	};
	auto MyBan = [this, InCategory, Index]() {
		const AMechGameState* GS = GetGS();
		return GS && MyTeam() >= 0 && MechDraft::HasBanned(GS->Draft, MyTeam(), InCategory, Index);
	};
	auto Chosen = [this, InCategory, Index]() {
		return IsMyBanTurn() ? (BanCategory == InCategory && BanIndex == Index) : (MySlot() != INDEX_NONE && MechLoadout::Get(MyLoadout(), InCategory) == Index);
	};
	const bool bReady = MechLoadout::Item(InCategory, Index).bReady;
	// 11-01 卡片内容和测试场共用（MechCardBody）。「已禁」「被禁」（用户：看不懂是谁不能用）改成「对面不能用」「你不能用」，压在照片上垫一块暗底
	TSharedRef<SMechKitButton> ItemCard = MechKit::Card(
		MechCardBody(InCategory, Index,
			SNew(SBorder)
			.BorderImage(MechKit::NoBrush())
			.Padding(FMargin(0.f))
			.ColorAndOpacity_Lambda([Banned]() { return Banned() ? FLinearColor(0.55f, 0.35f, 0.35f, 0.65f) : FLinearColor::White; })
			[
				MechItemPicture([InCategory]() { return InCategory; }, [Index]() { return Index; }, [this](int32 C, int32 I) { return ThumbBrush(C, I); })
			],
			Width,
			Height,
			[Banned, MyBan, bReady]() { return !bReady ? INVTEXT("待做") : Banned() ? INVTEXT("你不能用") : MyBan() ? INVTEXT("对面不能用") : FText::GetEmpty(); },
			[Banned, bReady]() { return !bReady ? MechKit::Th().Dim : Banned() ? MechKit::Th().Bad : MechKit::Th().Accent; },
			[Banned, bReady]() { return Banned() || !bReady ? MechKit::Th().Dim : MechKit::Th().Ink; }),
		[this, InCategory, Index]() { ClickItem(InCategory, Index); },
		[this, InCategory, Index]() { ShowItem(InCategory, Index); },
		TAttribute<bool>::CreateLambda(Chosen),
		[this, Banned, MyBan, InCategory, Index]() {
			if (Banned())
			{
				return MechKit::Th().Bad.CopyWithNewOpacity(0.75f);
			}
			if (MyBan())
			{
				return MechKit::Th().Accent.CopyWithNewOpacity(0.6f);
			}
			return FocusCategory == InCategory && FocusIndex == Index ? MechKit::Th().Accent.CopyWithNewOpacity(0.5f) : FLinearColor(0.f, 0.f, 0.f, -1.f);
		});
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(Height)
		[
			ItemCard
		];
}

void SMechDraftScreen::RebuildGrid()
{
	if (!Grid.IsValid())
	{
		return;
	}
	Grid->ClearChildren();
	TArray<TWeakPtr<SWidget>> Cards;
	if (Category == MechLoadout::Frame)
	{
		// 框架：三家并排一排（MechFrameGroups，每家上面一条名牌、下面 轻 / 中 / 重），占满整行；窗口比 16:9 窄时整块等比缩小
		Grid->AddSlot().FillLineWhenSizeLessThan(100000.f)
		[
			MechFrameGroups(1268.f, 190.f, [this](int32 i, float W, float H) { return MakeItemCard(MechLoadout::Frame, i, W, H); }, Cards)
		];
	}
	else
	{
		TArray<int32> Order;
		MechLoadout::DisplayOrder(Category, Order);
		MechTestExpandCards(Category, Order);
		// 11-01 一排用满（用户：卡片行右边空一块）：张数少的卡片放宽、改成左图右字，张数多的（肩部 11 张）排两行（MechCardLayout）
		const FMechCardLayout L = MechCardLayout(Order.Num(), 1268.f, 240.f, 10.f);
		for (const int32 i : Order)
		{
			const TSharedRef<SWidget> C = MakeItemCard(Category, i, L.Width, L.Height);
			Cards.Add(C);
			Grid->AddSlot()[C];
		}
	}
	MechTestSetCards(Cards);
}

TSharedRef<SWidget> SMechDraftScreen::MakeBottomBar()
{
	// 11-01 用户：一眼看出选了什么、哪项还没选。每类一格带小图（MechLoadoutSlot），现在看的那一类高亮，点了跳过去；几类就几格，一格宽度按类数算（1502 = 整行 1848 减确认按钮）。
	// 配装没有「空」（上一局那套直接带过来），所以「还没选」写成「沿用上次」
	TSharedRef<SHorizontalBox> Cells = SNew(SHorizontalBox);
	const float CellW = MechCardWidth(MechLoadoutCategories().Num(), 1502.f, 8.f);
	bool bFirst = true;
	for (const int32 c : MechLoadoutCategories())
	{
		auto Locked = [this]() {
			const AMechGameState* GS = GetGS();
			const int32 Slot = MySlot();
			return GS && GS->Draft.Slots.IsValidIndex(Slot) && GS->Draft.Slots[Slot].bLocked;
		};
		Cells->AddSlot()
		.AutoWidth()
		.Padding(FMargin(bFirst ? 0.f : 8.f, 0.f, 0.f, 0.f))
		[
			MechLoadoutSlot(c, [this, c]() { return MechLoadout::Get(MyLoadout(), c); }, [this](int32 C, int32 I) { return ThumbBrush(C, I); },
				[this, c]() { return Category == c; },
				[this, c]() {
					if (IsMyBanTurn() && !MechDraft::IsBannable(c))
					{
						return;  // 禁用轮这几类的页签是灰的，这里也不跳
					}
					Category = c;
					RebuildGrid();
					ShowItem(c, MechLoadout::Get(Edit, c));
				},
				[this, c, Locked]() {
					if (MySlot() == INDEX_NONE)
					{
						return INVTEXT("观战中");
					}
					return Locked() ? INVTEXT("已定下") : ReplacedMask & (1u << c) ? INVTEXT("原来那件被禁，已换") : TouchedMask & (1u << c) ? INVTEXT("这局选的") : INVTEXT("沿用上次");
				},
				[this, c, Locked]() {
					const FMechUITheme& T = MechKit::Th();
					return ReplacedMask & (1u << c) ? T.Warn : Locked() || TouchedMask & (1u << c) ? T.Accent : T.Dim;
				}, CellW)
		];
		bFirst = false;
	}
	auto CanConfirm = [this]() { return (IsMyBanTurn() && BanCategory >= 0) || IsMyPickTurn(); };
	// 确认按钮：能点时是全屏最强的元素（实心强调色、大字）。等待时写在等谁（以前只写「等待」）。
	// 字放在按钮外面叠上去：按钮禁用时 Slate 会把里面的字压灰，这几个字（「等 蓝2 选配」）是要让人读的
	return SNew(SBox)
		.HeightOverride(100.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot().VAlign(VAlign_Top)[MechKit::Hairline(1.4f)]
			+ SOverlay::Slot()
			.Padding(FMargin(36.f, 14.f, 36.f, 14.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cells]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SMechKitButton)
						.Look(EMechKitLook::Primary)
						.OnClick([this]() { Confirm(); })
						.IsEnabled(TAttribute<bool>::CreateLambda(CanConfirm))
						[
							SNew(SBox).WidthOverride(330.f).HeightOverride(66.f)
						]
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.MaxDesiredWidth(300.f)
						.Visibility(EVisibility::HitTestInvisible)
						[
							SNew(STextBlock)
							.Font(Font(22, true))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.ColorAndOpacity_Lambda([CanConfirm]() { return FSlateColor(CanConfirm() ? MechKit::Th().AccentInk : MechKit::Th().Dim); })
							.Text_Lambda([this]() {
								if (IsMyBanTurn())
								{
									return BanCategory >= 0 ? FText::Format(INVTEXT("禁用 {0}"), FText::FromString(MechLoadout::Item(BanCategory, BanIndex).Name))
															: INVTEXT("点一件对面的装备");
								}
								if (IsMyPickTurn())
								{
									return INVTEXT("确认配装");
								}
								const AMechGameState* GS = GetGS();
								const int32 Slot = MySlot();
								if (GS && GS->Draft.Slots.IsValidIndex(Slot) && GS->Draft.Slots[Slot].bLocked && !GS->Draft.Current())
								{
									return INVTEXT("已定下");
								}
								const FMechDraftTurn* T = GS ? GS->Draft.Current() : nullptr;
								if (!T)
								{
									return Slot == INDEX_NONE ? INVTEXT("观战中") : INVTEXT("已定下");
								}
								if (T->bBan)
								{
									return FText::Format(INVTEXT("等{0}禁用"), TeamName(T->Team));
								}
								return FText::Format(INVTEXT("等 {0} 选配"), GS->Draft.Slots.IsValidIndex(T->Slot) ? FText::FromString(GS->Draft.Slots[T->Slot].Name) : FText::GetEmpty());
							})
						]
					]
				]
			]
		];
}

// ---------------------------------------------------------------- 房间页
// 11-02 用户：「开房界面 UI 还没有统一到整体的 UI」「玩家可以自由在房间里任意一队加入人机或者移除人机」「个人战斗就没有队伍了」。
// 控件全换成 MechUIKit 的（和选配页、设置页一套），一屏放下不滚动（逻辑尺寸 1920×1080）：
//   左边阵容（两队各一列；个人战 / 保卫战 / 训练场不分队，是一个玩家列表），右边上面地图缩略图、中间模式、下面规则，右下角自己的名字和涂装。
// 房主看到按钮，其他人同样的位置只显示房主当前选的结果。每点一下都发给服务器写进 GameState::Room（MechRoom.h）。
namespace MechRoomUI
{
	FText LevelName(int32 Level)
	{
		static const FText Levels[4] = {INVTEXT("新兵"), INVTEXT("普通"), INVTEXT("老练"), INVTEXT("王牌")};
		return Levels[FMath::Clamp(Level, 0, 3)];
	}

	/** 11-01 人机强度：一句话说明这档什么水平（鼠标放到档位名字上看；数值在 MechAIController.cpp 开头的 MechAITier 表） */
	FText LevelDesc(int32 Level)
	{
		static const FText Desc[4] = {
			INVTEXT("新兵：刚上手也能赢。枪常打偏，刀常砍空，反应慢，还会发呆"),
			INVTEXT("普通：轻松的对手。枪法一般，偶尔砍空，很少弹反"),
			INVTEXT("老练：有点难。枪比较准，连招稳，会格挡，偶尔弹反"),
			INVTEXT("王牌：高手水准。枪很准，几乎不失误，会卡点弹反，得认真打")};
		return Desc[FMath::Clamp(Level, 0, 3)];
	}

	/** 地图卡片右下角的小字：这张图能玩什么 */
	FText MapTag(const MechMaps::FMap& M)
	{
		if (M.IsTraining())
		{
			return INVTEXT("自由练习");
		}
		if (M.IsCore())
		{
			return INVTEXT("夺芯");
		}
		FString S;
		if (M.Supports(MechMaps::Elim))
		{
			S += TEXT("淘汰");
		}
		if (M.Supports(MechMaps::DM))
		{
			S += S.IsEmpty() ? TEXT("死斗") : TEXT(" · 死斗");
		}
		if (M.Supports(MechMaps::Defense))
		{
			S += S.IsEmpty() ? TEXT("保卫") : TEXT(" · 保卫");
		}
		return FText::FromString(S);
	}

	/** 这个模式该用哪张图：现在这张支持就不换，不然回到上次打对战的那张，再不然表里第一张支持的 */
	int32 MapForMode(uint8 Flag, int32 Current, int32 LastBattle)
	{
		auto Ok = [Flag](int32 m) {
			const MechMaps::FMap& M = MechMaps::Get(m);
			return Flag == MechMaps::Core ? M.IsCore() : Flag == MechMaps::Training ? M.IsTraining() : !M.IsCore() && !M.IsTraining() && M.Supports(Flag);
		};
		if (Ok(Current))
		{
			return Current;
		}
		if (Ok(LastBattle))
		{
			return LastBattle;
		}
		for (int32 m = 0; m < MechMaps::Num(); ++m)
		{
			if (Ok(m))
			{
				return m;
			}
		}
		return Current;
	}
}

bool SMechDraftScreen::IsRoomHost() const
{
	// 房主 = 开房的那台机器上的玩家（本机有游戏模式；单机也算）。不靠房间名单：从房间开的场进图后名单是空的
	if (bForceGuestView)
	{
		return false;  // -MechRoomTest=Shots：同一台机器上看非房主的样子
	}
	const AMechPlayerController* PC = Owner.Get();
	return PC && PC->CanRestartMatch();
}

void SMechDraftScreen::ChangeRoom(TFunction<void(FMechRoomSettings&)> Change)
{
	const AMechGameState* GS = GetGS();
	AMechPlayerController* PC = Owner.Get();
	if (GS && PC && IsRoomHost() && !GS->bRoomTravelling)
	{
		FMechRoomSettings R = GS->Room;
		Change(R);
		if (!MechMaps::Get(R.Map).IsTraining())
		{
			LastBattleMap = R.Map;
		}
		PC->LobbySetRoom(R);  // 服务器夹到合法范围（地图不支持的模式换掉、超过总台数上限的人机去掉）再写回 GameState
	}
}

bool SMechDraftScreen::IsNoTeams() const
{
	const AMechGameState* GS = GetGS();
	return GS && (MechRoom::IsSolo(GS->Room, GS->bRoomMode, GS->bCoreMode) || (GS->bRoomMode && !MechRoom::UsesTeams(GS->Room)));
}

bool SMechDraftScreen::LobbyRowAt(int32 Team, int32 Row, FLobbyRow& Out) const
{
	const AMechGameState* GS = GetGS();
	if (!GS || Row < 0)
	{
		return false;
	}
	// 真人在前、人机在后；Team = -1 不分队（个人战、保卫战、训练场）
	int32 n = 0;
	for (const FMechLobbyPlayer& P : GS->Lobby)
	{
		if (Team < 0 || P.Team == Team)
		{
			if (n++ == Row)
			{
				Out = FLobbyRow();
				Out.Human = &P;
				return true;
			}
		}
	}
	if (!MechRoom::UsesAI(GS->Room))
	{
		return false;  // 保卫战、训练场没有人机
	}
	int32 Number = 0;
	for (int32 i = 0; i < GS->Room.AIs.Num(); ++i)
	{
		if (Team < 0 || GS->Room.AIs[i].Team == Team)
		{
			++Number;
			if (n++ == Row)
			{
				Out = FLobbyRow();
				Out.AIIndex = i;
				Out.AINumber = Number;
				Out.AILevel = GS->Room.AIs[i].Level;
				return true;
			}
		}
	}
	return false;
}

int32 SMechDraftScreen::LobbyCount(int32 Team) const
{
	const AMechGameState* GS = GetGS();
	return GS ? MechRoom::CountOf(GS->Room, *GS, Team) : 0;
}

const FMechRoomSettings& SMechDraftScreen::RoomNow() const
{
	static const FMechRoomSettings Empty;
	const AMechGameState* GS = GetGS();
	return GS ? GS->Room : Empty;
}

const FSlateBrush* SMechDraftScreen::MapBrush(int32 Index)
{
	if (const TSharedPtr<FSlateBrush>* Found = MapBrushes.Find(Index))
	{
		return Found->Get();
	}
	// /Game/UI/Maps/T_Map_<地图>（tools/ue_import_map_thumbs.py）；没图的地图返回空，界面画占位
	TSharedPtr<FSlateBrush> Brush;
	const FString Short = FPackageName::GetShortName(MechMaps::Get(Index).Path);
	if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/UI/Maps/T_Map_%s.T_Map_%s"), *Short, *Short), nullptr, LOAD_NoWarn))
	{
		MapTextures.Emplace(Tex);
		Brush = MakeShared<FSlateBrush>();
		Brush->SetResourceObject(Tex);
		Brush->ImageSize = FVector2D(640.f, 360.f);
		Brush->DrawAs = ESlateBrushDrawType::Image;
	}
	MapBrushes.Add(Index, Brush);
	return Brush.Get();
}

TSharedRef<SWidget> SMechDraftScreen::MakeMapPicture(TFunction<int32()> GetMap)
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage_Lambda([]() { return MechKit::Box(MechKit::Th().PhotoFill); })
		]
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image_Lambda([this, GetMap]() { return MapBrush(GetMap()); })
			.Visibility_Lambda([this, GetMap]() { return MapBrush(GetMap()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font(14))
			.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
			.Text(INVTEXT("暂无预览"))
			.Visibility_Lambda([this, GetMap]() { return MapBrush(GetMap()) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakeMapCard(int32 Index, float Width, float Height)
{
	auto GetMap = [Index]() { return Index; };
	const MechMaps::FMap& M = MechMaps::Get(Index);
	TSharedRef<SWidget> Content = SNew(SOverlay)
		+ SOverlay::Slot()[MakeMapPicture(GetMap)]
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBorder)
			.BorderImage(MechKit::Box(FLinearColor(0.f, 0.f, 0.f, 0.68f)))
			.Padding(FMargin(10.f, 4.f, 10.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					MechKit::Text(FText::FromString(M.Name), Font(15), MechKit::Color(&FMechUITheme::Ink))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MechKit::Text(MechRoomUI::MapTag(M), Font(12), MechKit::Color(&FMechUITheme::Dim))
				]
			]
		];
	TSharedRef<SMechKitButton> Card = MechKit::Card(Content,
		[this, Index]() { ChangeRoom([Index](FMechRoomSettings& R) { R.Map = Index; }); }, nullptr,
		TAttribute<bool>::CreateLambda([this, Index]() {
			const AMechGameState* GS = GetGS();
			return GS && GS->Room.Map == Index;
		}));
	return SNew(SBox).WidthOverride(Width).HeightOverride(Height)[Card];
}

TSharedRef<SWidget> SMechDraftScreen::MakeRoomRow(const FText& Name, const TAttribute<FText>& Desc, const TSharedRef<SWidget>& HostControl, TFunction<FText()> Value, TFunction<bool()> Visible)
{
	// 和设置页的行同一个样子（整行悬停淡底 + 左边细条 + 底下分隔线），矮一点：一屏要放得下。房主这里是控件，其他人是房主选的结果
	TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(EMechKitLook::Row).Clickable(false);
	B->SetContent(
		SNew(SBox)
		.MinDesiredHeight(46.f)
		.Padding(FMargin(14.f, 3.f, 10.f, 3.f))
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(176.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MechKit::Text(Name, Font(15), MechKit::Color(&FMechUITheme::Ink, 0.92f))]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(Desc)
						.Font(Font(12))
						.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
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
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return IsRoomHost() ? 0 : 1; })
				+ SWidgetSwitcher::Slot().HAlign(HAlign_Right)[HostControl]
				+ SWidgetSwitcher::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(Font(18)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink)).Text_Lambda([Value]() { return Value ? Value() : FText::GetEmpty(); })
				]
			]
		]);
	return SNew(SBox).Visibility_Lambda([Visible]() { return !Visible || Visible() ? EVisibility::Visible : EVisibility::Collapsed; })[B];
}

TSharedRef<SWidget> SMechDraftScreen::MakeRosterRow(int32 Team, int32 Row)
{
	auto Get = [this, Team, Row]() {
		FLobbyRow R;
		LobbyRowAt(Team, Row, R);
		return R;
	};
	auto Human = [Get]() { return Get().Human; };
	auto IsAI = [Get]() { return Get().AIIndex >= 0; };
	auto TeamTint = [Team]() -> FLinearColor {
		return Team == 0 ? MechKit::Th().Blue : Team == 1 ? MechKit::Th().Red : MechKit::Th().Accent;
	};
	auto IsMe = [this, Get]() {
		const FMechLobbyPlayer* H = Get().Human;
		const AMechPlayerController* PC = Owner.Get();
		return H && PC && PC->PlayerState && H->PlayerId == PC->PlayerState->GetPlayerId();
	};
	auto Swatch = [Human](float W, int32 Which) {
		return SNew(SBox)
			.WidthOverride(W)
			.HeightOverride(20.f)
			[
				SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor_Lambda([Human, Which]() {
					const FMechLobbyPlayer* H = Human();
					return FSlateColor(!H ? FLinearColor::Transparent : FLinearColor(Which == 0 ? H->Paint.Primary : Which == 1 ? H->Paint.Secondary : H->Paint.Accent));
				})
			];
	};
	// 人机强度的小切换：‹ 老练 ›，点一下换一档（新加的人机按最后一次选的强度）
	auto Arrow = [this, Get](int32 Dir) {
		TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(EMechKitLook::None).OnClick([this, Get, Dir]() {
			const int32 AI = Get().AIIndex;
			ChangeRoom([AI, Dir](FMechRoomSettings& R) {
				if (R.AIs.IsValidIndex(AI))
				{
					R.AIs[AI].Level = (R.AIs[AI].Level + Dir + 4) % 4;
					R.AILevel = R.AIs[AI].Level;
				}
			});
		});
		const TWeakPtr<SMechKitButton> W = B;
		B->SetContent(
			SNew(SBox)
			.WidthOverride(26.f)
			.HeightOverride(34.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Dir < 0 ? FText::FromString(TEXT("‹")) : FText::FromString(TEXT("›")))
				.Font(Font(19))
				.ColorAndOpacity_Lambda([W]() {
					const TSharedPtr<SMechKitButton> P = W.Pin();
					return FSlateColor(MechKit::Mix(MechKit::Th().Dim, MechKit::Th().Accent, P.IsValid() ? P->GetHover() : 0.f));
				})
			]);
		return B;
	};
	TSharedRef<SMechKitButton> Remove = SNew(SMechKitButton).Look(EMechKitLook::None).OnClick([this, Get]() {
		const int32 AI = Get().AIIndex;
		ChangeRoom([AI](FMechRoomSettings& R) {
			if (R.AIs.IsValidIndex(AI))
			{
				R.AIs.RemoveAt(AI);
			}
		});
	});
	{
		const TWeakPtr<SMechKitButton> W = Remove;
		Remove->SetContent(
			SNew(SBox)
			.WidthOverride(30.f)
			.HeightOverride(34.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("×")))
				.Font(Font(18))
				.ColorAndOpacity_Lambda([W]() {
					const TSharedPtr<SMechKitButton> P = W.Pin();
					return FSlateColor(MechKit::Mix(MechKit::Th().Dim, MechKit::Th().Bad, P.IsValid() ? P->GetHover() : 0.f));
				})
			]);
	}
	return SNew(SBox)
		.HeightOverride(44.f)
		.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
		.Visibility_Lambda([this, Team, Row]() {
			FLobbyRow R;
			return LobbyRowAt(Team, Row, R) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SBorder)
			.BorderImage_Lambda([IsMe]() {
				const FMechUITheme& T = MechKit::Th();
				return IsMe() ? MechKit::Box(T.Accent.CopyWithNewOpacity(0.08f), T.Accent.CopyWithNewOpacity(0.6f), 1.f) : MechKit::Box(T.CardFill, T.Line, 1.f);
			})
			.Padding(FMargin(0.f))
			[
				SNew(SHorizontalBox)
				// 左边一道队伍色（不分队时是强调色）；人机淡一点
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(3.f)
					[
						SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor_Lambda([TeamTint, IsAI]() {
							return FSlateColor(TeamTint().CopyWithNewOpacity(IsAI() ? 0.45f : 1.f));
						})
					]
				]
				// 涂装三色 / 「AI」
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(10.f, 0.f, 10.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(30.f)
					.HAlign(HAlign_Center)
					[
						SNew(SWidgetSwitcher)
						.WidgetIndex_Lambda([IsAI]() { return IsAI() ? 1 : 0; })
						+ SWidgetSwitcher::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()[Swatch(14.f, 0)]
							+ SHorizontalBox::Slot().AutoWidth()[Swatch(10.f, 1)]
							+ SHorizontalBox::Slot().AutoWidth()[Swatch(6.f, 2)]
						]
						+ SWidgetSwitcher::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							MechKit::Text(INVTEXT("AI"), MechKit::Latin(13, 120), MechKit::Color(&FMechUITheme::Dim))
						]
					]
				]
				// 名字
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(Font(16))
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text_Lambda([Get, IsMe]() {
						const FLobbyRow R = Get();
						if (R.Human)
						{
							return FText::FromString(IsMe() && R.Human->Name != TEXT("你") ? R.Human->Name + TEXT("（你）") : R.Human->Name);
						}
						return R.AIIndex >= 0 ? FText::Format(INVTEXT("人机 {0}"), FText::AsNumber(R.AINumber)) : FText::GetEmpty();
					})
					.ColorAndOpacity_Lambda([Get, TeamTint]() {
						const FLobbyRow R = Get();
						return FSlateColor(R.Human ? MechKit::Mix(TeamTint(), MechKit::Th().Ink, 0.35f) : MechKit::Th().Dim);
					})
				]
				// 右边：真人写「房主」；人机：房主有强度切换 + 移除，其他人只看强度
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(8.f, 0.f, 6.f, 0.f))
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([this, IsAI]() { return !IsAI() ? 0 : IsRoomHost() ? 1 : 2; })
					+ SWidgetSwitcher::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(Font(13))
						.ColorAndOpacity(MechKit::Color(&FMechUITheme::Accent))
						.Text_Lambda([Human]() {
							const FMechLobbyPlayer* H = Human();
							return H && H->bHost ? INVTEXT("房主") : FText::GetEmpty();
						})
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()[Arrow(-1)]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(40.f)
							.HAlign(HAlign_Center)
							[
								SNew(STextBlock).Font(Font(14)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink)).Text_Lambda([Get]() { return MechRoomUI::LevelName(Get().AILevel); }).ToolTipText_Lambda([Get]() { return MechRoomUI::LevelDesc(Get().AILevel); })
							]
						]
						+ SHorizontalBox::Slot().AutoWidth()[Arrow(1)]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(2.f, 0.f, 0.f, 0.f))[Remove]
					]
					+ SWidgetSwitcher::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Font(Font(14)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink)).Text_Lambda([Get]() { return MechRoomUI::LevelName(Get().AILevel); }).ToolTipText_Lambda([Get]() { return MechRoomUI::LevelDesc(Get().AILevel); })
					]
				]
			]
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakeAddAIButton(int32 Team)
{
	// 房主才有；总台数到上限（压力测试定的 MechRoom::MaxMechs）就灰掉
	return SNew(SBox)
		.Visibility_Lambda([this]() {
			const AMechGameState* GS = GetGS();
			return IsRoomHost() && (!GS || !GS->bRoomMode || MechRoom::UsesAI(GS->Room)) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			MechKit::Button(INVTEXT("+ 加人机"), [this, Team]() {
				ChangeRoom([Team](FMechRoomSettings& R) {
					FMechRoomAI A;
					A.Team = Team >= 0 ? Team : R.AIs.Num() % 2;  // 个人战没有队伍：平均记在两边，切回团队模式时两队也差不多
					A.Level = R.AILevel;
					R.AIs.Add(A);
				});
			}, false, TAttribute<bool>::CreateLambda([this]() { return LobbyCount(-1) < MechRoom::MaxMechs; }), 0.f, 38.f)
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakeLobbyTeam(int32 Team)
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 Row = 0; Row < MechRoom::MaxMechs; ++Row)
	{
		Rows->AddSlot().AutoHeight()[MakeRosterRow(Team, Row)];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 10.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(4.f)
				.HeightOverride(24.f)
				[
					SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor_Lambda([Team]() { return FSlateColor(TeamColor(Team)); })
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(MechKit::Light(25))
				.Text(TeamName(Team))
				.ColorAndOpacity_Lambda([Team]() { return FSlateColor(TeamColor(Team)); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(10.f, 0.f, 8.f, 4.f))
			[
				SNew(STextBlock)
				.Font(Font(14))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
				.Text_Lambda([this, Team]() { return FText::Format(INVTEXT("{0} 台"), FText::AsNumber(LobbyCount(Team))); })
			]
			// 真人自己选队（房主、其他人都一样）
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MechKit::Chip(Team == 0 ? INVTEXT("加入蓝队") : INVTEXT("加入红队"), [this, Team]() { return MyTeam() == Team; }, [this, Team]() {
					if (AMechPlayerController* PC = Owner.Get())
					{
						PC->LobbySetTeam(Team);
					}
				})
			]
		]
		+ SVerticalBox::Slot().AutoHeight()[Rows]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))[MakeAddAIButton(Team)];
}

TSharedRef<SWidget> SMechDraftScreen::MakeNoTeamPanel()
{
	// 个人战（人人为敌）、保卫战（都在一边）、训练场（自由练习）不分队：一个玩家列表，排两列（每列 5 行，最多 10 台）
	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	for (int32 Row = 0; Row < MechRoom::MaxMechs; ++Row)
	{
		(Row < MechRoom::MaxMechs / 2 ? Left : Right)->AddSlot().AutoHeight()[MakeRosterRow(-1, Row)];
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 10.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(MechKit::Light(25))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink))
				.Text_Lambda([this]() {
					const AMechGameState* GS = GetGS();
					return GS && GS->Room.bDefense ? INVTEXT("一起守") : GS && MechRoom::IsTraining(GS->Room) ? INVTEXT("一起练") : INVTEXT("人人为敌");
				})
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(10.f, 0.f, 0.f, 4.f))
			[
				SNew(STextBlock)
				.Font(Font(14))
				.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
				.Text_Lambda([this]() { return FText::Format(INVTEXT("{0} 台 · 这个模式不分队"), FText::AsNumber(LobbyCount(-1))); })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 8.f, 0.f))[Left]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(8.f, 0.f, 0.f, 0.f))[Right]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
		[
			SNew(SBox).WidthOverride(360.f)[MakeAddAIButton(-1)]
		];
}

TSharedRef<SWidget> SMechDraftScreen::MakePaintRow(int32 Which)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < 10; ++i)
	{
		const FColor C = PaintPalette[Which][i];
		auto Get = [this, Which]() -> FColor& { return Which == 0 ? PaintEdit.Primary : Which == 1 ? PaintEdit.Secondary : PaintEdit.Accent; };
		TSharedRef<SMechKitButton> B = SNew(SMechKitButton).Look(EMechKitLook::None).OnClick([this, Get, C]() {
			Get() = C;
			SaveProfile();
		});
		const TWeakPtr<SMechKitButton> W = B;
		B->SetContent(
			SNew(SBox)
			.WidthOverride(30.f)
			.HeightOverride(30.f)
			[
				SNew(SBorder).BorderImage_Lambda([Get, C, W]() {
					const FMechUITheme& T = MechKit::Th();
					const TSharedPtr<SMechKitButton> P = W.Pin();
					const float H = P.IsValid() ? P->GetHover() : 0.f;
					return Get() == C ? MechKit::Box(FLinearColor(C), T.Ink, 2.f) : MechKit::Box(FLinearColor(C), T.Ink.CopyWithNewOpacity(0.15f + 0.5f * H), 1.f);
				})
			]);
		Row->AddSlot().AutoWidth().Padding(FMargin(0.f, 0.f, 6.f, 0.f))[B];
	}
	return Row;
}

void SMechDraftScreen::SaveProfile()
{
	FMechSettings& S = FMechSettings::Get();
	if (NameBox.IsValid())
	{
		S.PlayerName = NameBox->GetText().ToString().TrimStartAndEnd().Left(16);
	}
	S.Paint = PaintEdit;
	S.Save();
	if (AMechPlayerController* PC = Owner.Get())
	{
		PC->SendProfile();
	}
}

TSharedRef<SWidget> SMechDraftScreen::MakeLobbyPage()
{
	auto Room = [this]() -> const FMechRoomSettings& { return RoomNow(); };
	auto IsRoomMap = [this]() {
		const AMechGameState* GS = GetGS();
		return GS && GS->bRoomMode;
	};
	auto IsCore = [this]() {
		const AMechGameState* GS = GetGS();
		return GS && (GS->bRoomMode ? MechRoom::IsCore(GS->Room) : GS->bCoreMode);
	};
	auto IsDefense = [this]() {
		const AMechGameState* GS = GetGS();
		return GS && GS->bRoomMode && GS->Room.bDefense;
	};
	auto IsTraining = [this]() {
		const AMechGameState* GS = GetGS();
		return GS && GS->bRoomMode && MechRoom::IsTraining(GS->Room);
	};
	auto IsDM = [IsCore, IsDefense, IsTraining, Room]() { return !IsCore() && !IsDefense() && !IsTraining() && Room().Rules.IsDeathmatch(); };
	auto IsSolo = [IsDM, Room]() { return IsDM() && Room().Rules.bSolo; };
	auto IsElim = [IsCore, IsDefense, IsTraining, IsDM]() { return !IsCore() && !IsDefense() && !IsTraining() && !IsDM(); };
	auto Change = [this](TFunction<void(FMechRoomSettings&)> Fn) { ChangeRoom(Fn); };

	// ---------------------------------------------------------------- 模式（地图不支持的模式也能点：顺手换到支持它的那张图，不用先想该选哪张）
	// 表里一张图都没有的模式不显示（11-02 夺芯图撤了）
	TArray<uint8> ModeFlags;
	TArray<FText> ModeNames;
	auto AddMode = [&ModeFlags, &ModeNames](uint8 Flag, const FText& Name) {
		if (MechMaps::AnySupports(Flag))
		{
			ModeFlags.Add(Flag);
			ModeNames.Add(Name);
		}
	};
	AddMode(MechMaps::Elim, INVTEXT("团队淘汰"));
	AddMode(MechMaps::DM, INVTEXT("死斗"));
	AddMode(MechMaps::Core, INVTEXT("夺芯"));
	AddMode(MechMaps::Defense, INVTEXT("保卫战"));
	AddMode(MechMaps::Training, INVTEXT("训练场"));
	auto ModeIndex = [IsCore, IsDefense, IsTraining, IsDM, ModeFlags]() {
		const uint8 Now = IsTraining() ? MechMaps::Training : IsDefense() ? MechMaps::Defense : IsCore() ? MechMaps::Core : IsDM() ? MechMaps::DM : MechMaps::Elim;
		return FMath::Max(0, ModeFlags.IndexOfByKey(Now));
	};
	TSharedRef<SWidget> ModeRow = MechKit::Segmented(ModeNames, ModeIndex, [this, Change, ModeFlags](int32 i) {
		const uint8 Flag = ModeFlags[i];
		const int32 Last = LastBattleMap;
		Change([Flag, Last](FMechRoomSettings& R) {
			R.Map = MechRoomUI::MapForMode(Flag, R.Map, Last);
			R.bDefense = Flag == MechMaps::Defense;
			if (Flag == MechMaps::Elim || Flag == MechMaps::DM)
			{
				R.Rules.Mode = Flag == MechMaps::DM ? EMechMode::Deathmatch : EMechMode::Elimination;
			}
		});
	}, nullptr, 96.f);
	auto ModeHint = [IsCore, IsDefense, IsTraining, IsDM, IsSolo]() {
		if (IsTraining())
		{
			return INVTEXT("训练场：大家自由换配装、打木桩，不计分；房主打开友伤后玩家之间能互相打，阵亡自动重生");
		}
		if (IsCore())
		{
			return INVTEXT("夺芯：抢中央的核心送回自家回收站，阵亡 8 秒后重生");
		}
		if (IsDefense())
		{
			return INVTEXT("保卫战：所有人一起守住指挥所，扛过 10 波敌人；阵亡 10 秒后重生，不分队、没有选配，进图直接开打");
		}
		if (IsSolo())
		{
			return INVTEXT("个人死斗：没有队伍，人人为敌，无限复活，先到目标或时间到时击毁多的赢");
		}
		return IsDM() ? INVTEXT("团队死斗：无限复活，按队伍击毁总数，先到目标或时间到时多的赢") : INVTEXT("团队淘汰：一方全倒输一局，局间不复活");
	};

	// ---------------------------------------------------------------- 地图：房主四张一排的缩略图；其他人一张大图 + 当前结果
	TSharedRef<SWrapBox> MapGrid = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 8.f));
	for (int32 m = 0; m < MechMaps::Num(); ++m)
	{
		MapGrid->AddSlot()[MakeMapCard(m, 264.f, 148.f)];  // 四张一排（右栏 1084 宽）
	}
	TSharedRef<SWidget> MapPanel = SNew(SWidgetSwitcher)
		.WidgetIndex_Lambda([this, IsRoomMap]() { return !IsRoomMap() ? 2 : IsRoomHost() ? 0 : 1; })
		+ SWidgetSwitcher::Slot()[MapGrid]
		+ SWidgetSwitcher::Slot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(540.f)
				.HeightOverride(304.f)
				[
					MakeMapPicture([Room]() { return Room().Map; })
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(28.f, 0.f, 0.f, 0.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MechKit::Kicker(INVTEXT("HOST PICK"))]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(MechKit::Light(40)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink)).Text_Lambda([Room]() { return FText::FromString(MechMaps::Get(Room().Map).Name); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(Font(18)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Accent)).Text_Lambda([this]() {
						const AMechGameState* GS = GetGS();
						return GS ? FText::FromString(MechRoom::DescribeRules(GS->Room)) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
				[
					MechKit::Text(INVTEXT("地图和规则由房主选，你这里只显示当前结果"), Font(14), MechKit::Color(&FMechUITheme::Dim))
				]
			]
		]
		// 对战图里的老式房间（直接 ?Match 进图）：地图已经定了
		+ SWidgetSwitcher::Slot()
		[
			MechKit::Text(INVTEXT("地图已经定了（这个房间是直接开在对战图里的）"), Font(15), MechKit::Color(&FMechUITheme::Dim))
		];

	// ---------------------------------------------------------------- 规则
	TArray<FText> RoundNames;
	for (const int32 N : MechRules::RoundOptions())
	{
		RoundNames.Add(MechRules::RoundsName(N));
	}
	TSharedRef<SWidget> RoundsRow = MechKit::Segmented(RoundNames, [Room]() { return MechRules::RoundOptions().IndexOfByKey(Room().Rules.RoundsToWin); },
		[Change](int32 i) { const int32 N = MechRules::RoundOptions()[i]; Change([N](FMechRoomSettings& R) { R.Rules.RoundsToWin = N; }); }, nullptr, 60.f);
	TSharedRef<SWidget> KindRow = MechKit::Segmented({INVTEXT("团队"), INVTEXT("个人")}, [Room]() { return Room().Rules.bSolo ? 1 : 0; },
		[Change](int32 i) { Change([i](FMechRoomSettings& R) { R.Rules.bSolo = i == 1; }); }, nullptr, 72.f);
	auto Numbers = [](const TArray<int32>& Options) {
		TArray<FText> Out;
		for (const int32 N : Options)
		{
			Out.Add(FText::AsNumber(N));
		}
		return Out;
	};
	TSharedRef<SWidget> TeamTargetRow = MechKit::Segmented(Numbers(MechRules::TeamTargetOptions()), [Room]() { return MechRules::TeamTargetOptions().IndexOfByKey(Room().Rules.TeamTarget); },
		[Change](int32 i) { const int32 N = MechRules::TeamTargetOptions()[i]; Change([N](FMechRoomSettings& R) { R.Rules.TeamTarget = N; }); }, nullptr, 44.f);
	TSharedRef<SWidget> SoloTargetRow = MechKit::Segmented(Numbers(MechRules::SoloTargetOptions()), [Room]() { return MechRules::SoloTargetOptions().IndexOfByKey(Room().Rules.SoloTarget); },
		[Change](int32 i) { const int32 N = MechRules::SoloTargetOptions()[i]; Change([N](FMechRoomSettings& R) { R.Rules.SoloTarget = N; }); }, nullptr, 44.f);
	TArray<FText> TimeNames;
	for (const int32 S : MechRules::TimeOptions())
	{
		TimeNames.Add(FText::FromString(FString::Printf(TEXT("%d 分钟"), S / 60)));
	}
	TSharedRef<SWidget> TimeRow = MechKit::Segmented(TimeNames, [Room]() { return MechRules::TimeOptions().IndexOfByKey(Room().Rules.TimeLimit); },
		[Change](int32 i) { const int32 N = MechRules::TimeOptions()[i]; Change([N](FMechRoomSettings& R) { R.Rules.TimeLimit = N; }); }, nullptr, 56.f);
	auto OnOffText = [](bool bOn) { return bOn ? INVTEXT("开") : INVTEXT("关"); };

	TSharedRef<SVerticalBox> Rules = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[MakeRoomRow(INVTEXT("局数"), FText::GetEmpty(), RoundsRow, [Room]() { return MechRules::RoundsName(Room().Rules.RoundsToWin); }, IsElim)]
		+ SVerticalBox::Slot().AutoHeight()[MakeRoomRow(INVTEXT("计分"), FText::GetEmpty(), KindRow, [Room]() { return Room().Rules.bSolo ? INVTEXT("个人（人人为敌）") : INVTEXT("团队"); }, IsDM)]
		+ SVerticalBox::Slot().AutoHeight()[MakeRoomRow(INVTEXT("目标击毁（队伍）"), FText::GetEmpty(), TeamTargetRow, [Room]() { return FText::AsNumber(Room().Rules.TeamTarget); }, [IsDM, IsSolo]() { return IsDM() && !IsSolo(); })]
		+ SVerticalBox::Slot().AutoHeight()[MakeRoomRow(INVTEXT("目标击毁（个人）"), FText::GetEmpty(), SoloTargetRow, [Room]() { return FText::AsNumber(Room().Rules.SoloTarget); }, IsSolo)]
		+ SVerticalBox::Slot().AutoHeight()[MakeRoomRow(INVTEXT("时间"), FText::GetEmpty(), TimeRow, [Room]() { return FText::FromString(FString::Printf(TEXT("%d 分钟"), Room().Rules.TimeLimit / 60)); }, IsDM)]
		+ SVerticalBox::Slot().AutoHeight()[MakeRoomRow(INVTEXT("禁用阶段"), FText::GetEmpty(),
			MechKit::Switch([Room]() { return Room().bBans; }, [Change](bool b) { Change([b](FMechRoomSettings& R) { R.bBans = b; }); }),
			[Room, OnOffText]() { return OnOffText(Room().bBans); }, [IsSolo, IsDefense, IsTraining]() { return !IsSolo() && !IsDefense() && !IsTraining(); })];
	MakeExtraRoomRows(Rules);

	// ---------------------------------------------------------------- 自己的名字和涂装
	TSharedRef<SWidget> Profile = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))[MechKit::Section(INVTEXT("你自己"), INVTEXT("名字和涂装，每个人自己定"))]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 0.f, 0.f, 12.f))
		[
			SNew(SBox)
			.WidthOverride(360.f)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(NameBox, SEditableTextBox)
				.Style(&MechKit::TextBoxStyle())
				.Font(Font(16))
				.Text(FText::FromString(FMechSettings::Get().PlayerName))
				.HintText(INVTEXT("玩家名称（不填就叫「玩家」）"))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type) { SaveProfile(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[MechKit::Text(INVTEXT("主漆 / 副漆 / 点缀"), Font(13), MechKit::Color(&FMechUITheme::Dim))]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[MakePaintRow(0)]
		+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))[MakePaintRow(1)]
		+ SVerticalBox::Slot().AutoHeight()[MakePaintRow(2)];

	// ---------------------------------------------------------------- 整页
	auto StartBlocked = [this]() {
		const AMechGameState* GS = GetGS();
		return GS ? MechRoom::StartProblem(GS->Room, *GS) : FText::GetEmpty();
	};
	return SNew(SVerticalBox)
		// 顶栏：和选配页同样高、同样的字
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(92.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().VAlign(VAlign_Bottom)[MechKit::Hairline(1.4f)]
				+ SOverlay::Slot()
				.Padding(FMargin(36.f, 10.f, 36.f, 10.f))
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
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[MechKit::Text(INVTEXT("房间"), MechKit::Light(30), MechKit::Color(&FMechUITheme::Ink))]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(FMargin(12.f, 0.f, 0.f, 8.f))[MechKit::Text(INVTEXT("ROOM"), MechKit::Latin(11, 240), MechKit::Color(&FMechUITheme::Dim))]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(FMargin(0.f, 2.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.Font(Font(15))
							.ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink, 0.85f))
							.Text_Lambda([this, IsCore]() {
								const AMechGameState* GS = GetGS();
								if (!GS)
								{
									return FText::GetEmpty();
								}
								const FString Rule = GS->bRoomMode ? MechRoom::Describe(GS->Room) : MechRules::Describe(GS->Room.Rules, IsCore());
								const FString Size = MechRoom::UsesTeams(GS->Room) && !MechRoom::IsSolo(GS->Room, GS->bRoomMode, GS->bCoreMode)
									? FString::Printf(TEXT(" · %d v %d"), MechRoom::CountOf(GS->Room, *GS, 0), MechRoom::CountOf(GS->Room, *GS, 1))
									: FString::Printf(TEXT(" · %d 台"), MechRoom::CountOf(GS->Room, *GS, -1));
								return FText::FromString(Rule + Size);
							})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(Font(15))
						.ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim))
						.Justification(ETextJustify::Right)
						.Text_Lambda([this, IsRoomMap]() {
							const AMechPlayerController* PC = Owner.Get();
							if (IsRoomMap() && IsRoomHost() && PC && PC->GetNetMode() == NM_ListenServer)
							{
								return INVTEXT("朋友在主菜单「加入」里输入你的 IP 就能进来（端口 7777）");
							}
							return IsRoomHost() ? FText::GetEmpty() : INVTEXT("房主在选地图和规则");
						})
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(FMargin(36.f, 16.f, 36.f, 10.f))
		[
			SNew(SHorizontalBox)
			// 左：阵容
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(736.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						SNew(SWidgetSwitcher)
						.WidgetIndex_Lambda([this]() { return IsNoTeams() ? 1 : 0; })
						+ SWidgetSwitcher::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 8.f, 0.f))[MakeLobbyTeam(0)]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(8.f, 0.f, 0.f, 0.f))[MakeLobbyTeam(1)]
						]
						+ SWidgetSwitcher::Slot()[MakeNoTeamPanel()]
					]
					// 一句话：挑战倍数现在给谁（所有人都看得到）/ 总台数
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Font(Font(14))
						.AutoWrapText(true)
						.ColorAndOpacity_Lambda([this]() {
							const AMechGameState* GS = GetGS();
							return FSlateColor(GS && GS->ChallengeTeam >= 0 ? MechKit::Th().Accent : MechKit::Th().Dim);
						})
						.Text_Lambda([this]() {
							const AMechGameState* GS = GetGS();
							if (GS && GS->ChallengeTeam >= 0)
							{
								return FText::FromString(FString::Printf(TEXT("挑战倍数：%s 装甲 ×%.2f、伤害 ×%.2f"), GS->ChallengeTeam == 0 ? TEXT("蓝队") : TEXT("红队"), GS->ChallengeArmor, GS->ChallengeDamage));
							}
							return FText::Format(INVTEXT("场上一共 {0} / {1} 台"), FText::AsNumber(LobbyCount(-1)), FText::AsNumber(MechRoom::MaxMechs));
						})
					]
				]
			]
			// 右：地图、模式、规则、自己
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(FMargin(28.f, 0.f, 0.f, 0.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
				[
					MechKit::Section(INVTEXT("地图"), TAttribute<FText>::CreateLambda([this]() { return IsRoomHost() ? INVTEXT("点一张换图；模式跟着地图走") : FText::GetEmpty(); }))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 14.f))[MapPanel]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return IsRoomHost() ? 0 : 1; })
					+ SWidgetSwitcher::Slot().HAlign(HAlign_Left)[ModeRow]
					+ SWidgetSwitcher::Slot()
					[
						SNew(STextBlock).Font(MechKit::Light(24)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink)).Text_Lambda([ModeIndex, ModeNames]() { return ModeNames[ModeIndex()]; })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 10.f))
				[
					SNew(STextBlock).Font(Font(13)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim)).AutoWrapText(true).Text_Lambda(ModeHint)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[Rules]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(24.f, 0.f, 0.f, 0.f))[SNew(SBox).WidthOverride(366.f)[Profile]]
				]
			]
		]
		// 底栏：左边一句话（开不了的原因），右边退出 + 开始
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(96.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().VAlign(VAlign_Top)[MechKit::Hairline(1.4f)]
				+ SOverlay::Slot()
				.Padding(FMargin(36.f, 0.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(Font(16))
						.Text_Lambda([this, StartBlocked]() {
							const FText Why = StartBlocked();
							if (!Why.IsEmpty())
							{
								return Why;
							}
							return IsRoomHost() ? INVTEXT("选好了就开始；开始后所有人一起进图") : INVTEXT("等房主开始");
						})
						.ColorAndOpacity_Lambda([StartBlocked]() { return FSlateColor(StartBlocked().IsEmpty() ? MechKit::Th().Dim : MechKit::Th().Warn); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 12.f, 0.f))
					[
						MechKit::Button(TAttribute<FText>::CreateLambda([this]() { return IsRoomHost() ? INVTEXT("解散房间") : INVTEXT("退出房间"); }), [this]() {
							if (AMechPlayerController* PC = Owner.Get())
							{
								PC->MenuBackToMain();
							}
						}, false, true, 150.f, 58.f)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() { return IsRoomHost() ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							MechKit::Button(TAttribute<FText>::CreateLambda([this]() {
								const AMechGameState* GS = GetGS();
								return GS && GS->bRoomTravelling ? INVTEXT("正在进入地图…") : INVTEXT("开始");
							}), [this]() {
								SaveProfile();
								if (AMechPlayerController* PC = Owner.Get())
								{
									PC->LobbyStart();  // 房主的选择早就同步到服务器了；这次的选择由服务器存进设置（下次开房按这个起）
								}
							}, true, TAttribute<bool>::CreateLambda([this, StartBlocked]() {
								const AMechGameState* GS = GetGS();
								return GS && !GS->bRoomTravelling && StartBlocked().IsEmpty();
							}), 300.f, 58.f)
						]
					]
				]
			]
		];
}

void SMechDraftScreen::MakeExtraRoomRows(const TSharedRef<SVerticalBox>& Rules)
{
	// 11-02 用户：「你也可以做一个数值权重，可以选择加入，为的就是在人数不平衡时平衡数值。比如 1v5 会给人少的一方推荐的挑战倍数，当然你也可以自定义」
	auto Teams = [this]() {
		const AMechGameState* GS = GetGS();
		return GS && MechRoom::UsesTeams(GS->Room) && !MechRoom::IsSolo(GS->Room, GS->bRoomMode, GS->bCoreMode);
	};
	auto On = [this, Teams]() { return Teams() && RoomNow().bChallenge; };
	auto Recommended = [this]() { return MechRoom::RecommendedChallenge(LobbyCount(0), LobbyCount(1)); };
	auto Few = [this]() { return LobbyCount(0) == LobbyCount(1) ? INVTEXT("两队一样多") : LobbyCount(0) < LobbyCount(1) ? INVTEXT("蓝队") : INVTEXT("红队"); };
	auto Times = [](float V) { return FText::FromString(FString::Printf(TEXT("×%.2f"), V)); };
	Rules->AddSlot().AutoHeight()[MakeRoomRow(INVTEXT("挑战倍数"), FText::GetEmpty(),
		MechKit::Switch([this]() { return RoomNow().bChallenge; }, [this](bool b) { ChangeRoom([b](FMechRoomSettings& R) { R.bChallenge = b; }); }),
		[this]() { return RoomNow().bChallenge ? INVTEXT("开") : INVTEXT("关"); }, Teams)];
	// 用推荐值还是自定义：切到自定义时从推荐值起
	Rules->AddSlot().AutoHeight()[MakeRoomRow(INVTEXT("倍数来源"),
		TAttribute<FText>::CreateLambda([Few, Recommended, Times]() { return FText::Format(INVTEXT("推荐 {0}（给{1}）"), Times(Recommended()), Few()); }),
		MechKit::Segmented({INVTEXT("推荐"), INVTEXT("自定义")}, [this]() { return RoomNow().bChallengeCustom ? 1 : 0; }, [this, Recommended](int32 i) {
			const float V = Recommended();
			ChangeRoom([i, V](FMechRoomSettings& R) {
				if (i == 1 && !R.bChallengeCustom)
				{
					R.ChallengeArmor = V;
					R.ChallengeDamage = V;
				}
				R.bChallengeCustom = i == 1;
			});
		}, nullptr, 64.f),
		[this]() { return RoomNow().bChallengeCustom ? INVTEXT("自定义") : INVTEXT("推荐"); }, On)];
	auto Custom = [this, On]() { return On() && RoomNow().bChallengeCustom; };
	Rules->AddSlot().AutoHeight()[MakeRoomRow(INVTEXT("装甲倍数"), INVTEXT("受到的伤害 ÷ 这个数"),
		MechKit::Slider([this]() { return RoomNow().ChallengeArmor; }, [this](float V) { ChangeRoom([V](FMechRoomSettings& R) { R.ChallengeArmor = V; }); }, 1.f, 6.f, 0.05f, Times, nullptr, 220.f),
		[this, Times]() { return Times(RoomNow().ChallengeArmor); }, Custom)];
	Rules->AddSlot().AutoHeight()[MakeRoomRow(INVTEXT("伤害倍数"), INVTEXT("打出的伤害 × 这个数"),
		MechKit::Slider([this]() { return RoomNow().ChallengeDamage; }, [this](float V) { ChangeRoom([V](FMechRoomSettings& R) { R.ChallengeDamage = V; }); }, 1.f, 6.f, 0.05f, Times, nullptr, 220.f),
		[this, Times]() { return Times(RoomNow().ChallengeDamage); }, Custom)];
	// 11-02 用户：「加入一个训练场联机模式，人们可以在这个训练场开启友伤」
	Rules->AddSlot().AutoHeight()[MakeRoomRow(INVTEXT("友伤"), INVTEXT("玩家之间能互相打"),
		MechKit::Switch([this]() { return RoomNow().bFriendlyFire; }, [this](bool b) { ChangeRoom([b](FMechRoomSettings& R) { R.bFriendlyFire = b; }); }),
		[this]() { return RoomNow().bFriendlyFire ? INVTEXT("开") : INVTEXT("关"); },
		[this]() {
			const AMechGameState* GS = GetGS();
			return GS && GS->bRoomMode && MechRoom::IsTraining(GS->Room);
		})];
}

// ---------------------------------------------------------------- 等待页（从房间开的场，进图后等人）
TSharedRef<SWidget> SMechDraftScreen::MakeWaitPage()
{
	auto Fraction = [this]() {
		const AMechGameState* GS = GetGS();
		return GS && GS->RoomExpected > 0 ? FMath::Clamp(static_cast<float>(GS->RoomArrived) / GS->RoomExpected, 0.f, 1.f) : 0.f;
	};
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(720.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MechKit::Kicker(INVTEXT("LOADING"))]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(MechKit::Light(42)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Ink)).Text_Lambda([this]() {
						const AMechGameState* GS = GetGS();
						return GS ? FText::FromString(MechMaps::Get(GS->Room.Map).Name) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 4.f, 0.f, 16.f))
				[
					SNew(STextBlock).Font(Font(15)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim)).Text_Lambda([this]() {
						const AMechGameState* GS = GetGS();
						return GS ? FText::FromString(MechRoom::DescribeRules(GS->Room)) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.WidthOverride(720.f)
					.HeightOverride(405.f)
					[
						MakeMapPicture([this]() {
							const AMechGameState* GS = GetGS();
							return GS ? GS->Room.Map : 0;
						})
					]
				]
				// 进度：细条 + 人数
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 18.f, 0.f, 8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Bottom)
					[
						MechKit::Text(INVTEXT("等房间里的人进图"), Font(18), MechKit::Color(&FMechUITheme::Ink))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					[
						SNew(STextBlock).Font(MechKit::Latin(24)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Accent)).Text_Lambda([this]() {
							const AMechGameState* GS = GetGS();
							return GS ? FText::FromString(FString::Printf(TEXT("%d / %d"), GS->RoomArrived, GS->RoomExpected)) : FText::GetEmpty();
						})
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(3.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Track))]
						+ SOverlay::Slot()
						.HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride_Lambda([Fraction]() { return FOptionalSize(720.f * Fraction()); })
							[
								SNew(SBorder).BorderImage(MechKit::White()).BorderBackgroundColor(MechKit::Color(&FMechUITheme::Accent))
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
				[
					SNew(STextBlock).Font(Font(14)).ColorAndOpacity(MechKit::Color(&FMechUITheme::Dim)).Text_Lambda([this]() {
						const AMechGameState* GS = GetGS();
						const int32 Left = GS ? FMath::CeilToInt(GS->GetPhaseRemaining()) : 0;
						return FText::FromString(FString::Printf(TEXT("人齐了就开始；最多再等 %d 秒，没赶上的人进来后接管本队人机"), Left));
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(FMargin(0.f, 18.f, 0.f, 0.f))
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return IsRoomHost() ? EVisibility::Visible : EVisibility::Hidden; })
					[
						MechKit::Button(INVTEXT("不等了，先开始"), [this]() {
							if (AMechPlayerController* PC = Owner.Get())
							{
								PC->LobbyStart();  // 服务器上：等人阶段的「开始」= 不等了
							}
						}, false, true, 220.f, 46.f)
					]
				]
			]
		];
}
