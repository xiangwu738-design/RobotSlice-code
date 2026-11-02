// 房间和禁用 / 选配的整屏界面（Slate 直接写）。和 HUD 同一套风格：深色底、青色细线、等宽数字；武器卡片和大图是摄影棚
// （AMechItemStudio）实拍的模型。房间页：两队名单（自己选队）、玩家名称、涂装；房主选地图（只在房间图里）、模式、规则、每队台数、
// AI 强度、禁用开关后开始。11-01 房主的选择每改一次就发给服务器写进 GameState::Room，房主看到的是选项按钮，
// 其他人只看到房主当前选的结果（用户：「除了房主以外其他人看到的都只是当前的结果」）。
// 等待页（11-01）：从房间开的场进图后、等房间里的人都进来那几秒。
// 选配页：左栏自己队、右栏对面（观战时左蓝右红），每队的禁用和每台的配装；中间大图 + 分类卡片；底栏自己这套配装和确认按钮。
// 按钮做的事都交给 AMechPlayerController（发给服务器），界面只照着 GameState 里的 Draft / Lobby / Room 画。

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.h"
#include "Styling/SlateTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class AMechPlayerController;
class AMechGameState;
class SWrapBox;
class SEditableTextBox;
struct FSlateBrush;
struct FMechDraftSlot;
struct FMechLobbyPlayer;
struct FMechRoomSettings;
class SVerticalBox;
class UTexture2D;

class SMechDraftScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMechDraftScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AMechPlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	/** Esc：暂停菜单 */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	/** 测试（-MechRoomTest=Shots）：房主这台也按非房主的样子画 */
	static inline bool bForceGuestView = false;

private:
	const AMechGameState* GetGS() const;
	bool IsLobby() const;
	int32 MySlot() const;
	int32 MyTeam() const;
	bool IsMyBanTurn() const;
	bool IsMyPickTurn() const;
	/** 左栏（0）、右栏（1）是哪队：自己的队在左，观战时蓝队在左 */
	int32 SideTeam(int32 Side) const;
	/** 某队队内第 Row 台的位置（没有返回空） */
	const FMechDraftSlot* SlotAt(int32 Team, int32 Row) const;
	/** 自己现在的配装：定下了就是定下的那套，没定就是正在挑的 */
	FMechLoadout MyLoadout() const;
	/** 11-02 这一栏的颜色（个人战没有队伍，不用蓝 / 红）、这个模式有没有禁用（个人战没有）、这队超过 6 台时卡片缩成两行 */
	FLinearColor SideColor(int32 Side) const;
	bool HasBanColumn() const;
	bool IsCompactColumn(int32 Side) const;

	TSharedRef<SWidget> MakeDraftPage();
	TSharedRef<SWidget> MakeTopBar();
	/** 11-01 回合顺序条：Draft.Order 每轮一格（蛇形 A B B A A B），写谁、选了什么；已完成 / 当前 / 待选三种样子 */
	TSharedRef<SWidget> MakeTurnBar();
	TSharedRef<SWidget> MakeTurnCell(int32 Cell);
	TSharedRef<SWidget> MakeTeamColumn(int32 Side);
	TSharedRef<SWidget> MakeSlotCard(int32 Side, int32 Row);
	TSharedRef<SWidget> MakeBanChip(int32 Side, int32 Which);
	TSharedRef<SWidget> MakePreview();
	TSharedRef<SWidget> MakeTabs();
	/** 卡片：宽度由 RebuildGrid 按这一页的件数算（用满 1268 宽）；框架页是三家并排的窄卡片 */
	TSharedRef<SWidget> MakeItemCard(int32 InCategory, int32 Index, float Width, float Height);
	TSharedRef<SWidget> MakeBottomBar();
	void RebuildGrid();
	void ShowItem(int32 InCategory, int32 Index);
	void ClickItem(int32 InCategory, int32 Index);
	void Confirm();
	void SendPreview();
	const FSlateBrush* ThumbBrush(int32 InCategory, int32 Index);

	TSharedRef<SWidget> MakeLobbyPage();
	/** 一队：队名、台数、加入这队、名单（真人在前、人机在后）、加人机 */
	TSharedRef<SWidget> MakeLobbyTeam(int32 Team);
	/** 11-02 个人战 / 保卫战 / 训练场不分队：一个玩家列表 */
	TSharedRef<SWidget> MakeNoTeamPanel();
	/** 名单的一行；Team = -1 不分队。提前建好、用不到的折叠，不用每次重建 */
	TSharedRef<SWidget> MakeRosterRow(int32 Team, int32 Row);
	TSharedRef<SWidget> MakeAddAIButton(int32 Team);
	/** 规则的一行：房主这里是控件，其他人是房主选的结果 */
	TSharedRef<SWidget> MakeRoomRow(const FText& Name, const TAttribute<FText>& Desc, const TSharedRef<SWidget>& HostControl, TFunction<FText()> Value, TFunction<bool()> Visible);
	/** 第二批的规则行（挑战倍数、友伤） */
	void MakeExtraRoomRows(const TSharedRef<SVerticalBox>& Rules);
	/** 地图缩略图（/Game/UI/Maps/T_Map_<地图>，没图的画占位）；地图卡片 = 缩略图 + 名字 */
	TSharedRef<SWidget> MakeMapPicture(TFunction<int32()> GetMap);
	TSharedRef<SWidget> MakeMapCard(int32 Index, float Width, float Height);
	const FSlateBrush* MapBrush(int32 Index);
	/** 11-01 从房间开的场进图后等人：「等待玩家进图 N / M」，房主可以点「不等了」 */
	TSharedRef<SWidget> MakeWaitPage();
	/** 自己是不是房主（开房的那台机器上的玩家） */
	bool IsRoomHost() const;
	TSharedRef<SWidget> MakePaintRow(int32 Which);
	void SaveProfile();
	/** 房主改房间的选择：拿现在的选择改一下发给服务器 */
	void ChangeRoom(TFunction<void(FMechRoomSettings&)> Change);
	const FMechRoomSettings& RoomNow() const;
	/** 现在的模式不分队 */
	bool IsNoTeams() const;
	/** 名单第 Row 行是谁：真人（Human）或人机（AIIndex = Room.AIs 的下标） */
	struct FLobbyRow
	{
		const FMechLobbyPlayer* Human = nullptr;
		int32 AIIndex = -1;
		int32 AINumber = 0;
		int32 AILevel = 1;
	};
	bool LobbyRowAt(int32 Team, int32 Row, FLobbyRow& Out) const;
	/** 现在一队几台（真人 + 人机）；Team = -1 全部 */
	int32 LobbyCount(int32 Team) const;

	TWeakObjectPtr<AMechPlayerController> Owner;
	TSharedPtr<SWrapBox> Grid;
	TSharedPtr<SEditableTextBox> NameBox;
	/** 中间卡片现在显示哪一类 */
	int32 Category = 2;
	/** 大图上显示的那件 */
	int32 FocusCategory = 2;
	int32 FocusIndex = 0;
	/** 自己这一套（轮到之前就能先挑好，轮到时发给服务器公开） */
	FMechLoadout Edit;
	/** 禁用轮：挑中的那件 */
	int32 BanCategory = -1;
	int32 BanIndex = -1;
	int32 LastTurn = -1;
	/** 11-01 底部配装栏的状态：这局动过哪几类（没动过的写「沿用上次」——配装没有「空」，出厂就是上一局那套），哪几类被对面禁了、自动换过。按类别下标的位 */
	uint32 TouchedMask = 0;
	uint32 ReplacedMask = 0;
	TMap<int32, TSharedPtr<FSlateBrush>> Brushes;
	TSharedPtr<FSlateBrush> PreviewBrush;
	// 11-01 人机名单、禁用开关不再存在房主本地界面里（以前按「开始」才发，别人看不到），都在 GameState::Room
	FMechPaint PaintEdit;
	/** 地图缩略图的笔刷（空 = 这张图没有缩略图）和贴图（强引用住，换页不被回收） */
	TMap<int32, TSharedPtr<FSlateBrush>> MapBrushes;
	TArray<TStrongObjectPtr<UTexture2D>> MapTextures;
	/** 最近选过的对战地图：从训练场切回别的模式时回到这张 */
	int32 LastBattleMap = 0;
};
