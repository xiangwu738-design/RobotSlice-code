// 配装照片和详细信息（10-31 用户：「配装界面显示配装照片要详细一点，不是只有枪械有照片」）：
//  SMechItemDetail —— 选中的那一件：大照片（摄影棚拍好的转台，自动慢转、按住左键左右拖动转）+ 详细视角缩略条（侧面 / 特写 / 挂在肩边……，
//    点一下大图换成那一张）+ 右边的名牌（制造商、型号、名字、类别、重量感）、一句话特色和参数（条 + 数字，带和已装备那件的对比刻度）、
//    名牌右边的读数图（雷达图）。选配整屏（MechDraftUI）和测试场配装页都用它。
//  SMechLoadoutBrowser —— 测试场 Esc → 配装：整屏，分类页签 + 卡片 + 上面的详细信息 + 底栏「应用 / 返回」（SMechMenu 里 Loadout 页）。
// 小图、详细视角是 AMechItemStudio 拍一次缓存的贴图；大图的转台 10-31 起是实时拍（显示期间每帧按当前角度拍一张，AMechItemStudio::ShootLive）。
// 配色全从 MechUITheme 取。
// 界面大小：游戏里 Slate 的缩放 = 引擎 DPI 曲线按窗口短边算（1080 → 1.0，1620 → 1.5，720 → 0.667，Windows 的 180% 会被引擎除掉），
// 所以 16:9 窗口不管多大，界面的逻辑尺寸都是 1920×1080——布局按 1920×1080 一屏放下来设计。
// 测试 -MechLoadoutShots：选配整屏 / 测试场配装页各自把五类依次选中、等照片拍完后截图到 Saved/Screenshots/Loadout，拍完退出
// （测试场里开局自动打开配装页；选配要和 -MechLobbyTest 一起用）；每张截图后检查这一页的卡片是不是都在窗口里、点得到，
// 框架那一步之后量实时转台的开销（开 / 关各 3 秒对比），日志 [loadoutshots] / [livephoto]。

#pragma once

#include "CoreMinimal.h"
#include "MechCatalog.h"
#include "MechTypes.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class AMechPlayerController;
class AMechItemStudio;
class SWrapBox;
struct FSlateBrush;

/** 没有实物模型的一件（肩部的紧急脱离等四件）的图标（11-01：以前这种地方是黑块）：深色底 + 强调色线条画的符号。Category < 0 什么都不画 */
class SMechItemIcon : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechItemIcon) : _Plain(false) {}
		SLATE_ARGUMENT(TFunction<int32()>, GetCategory)
		SLATE_ARGUMENT(TFunction<int32()>, GetIndex)
		/** 不画底（大图位置：底铺满整张，符号单独放在中间）。Index 传 -1 就是只画底 */
		SLATE_ARGUMENT(bool, Plain)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(64.f, 64.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TFunction<int32()> GetCategory;
	TFunction<int32()> GetIndex;
	bool bPlain = false;
};

/** 这一件用什么当图：有照片的给 ThumbBrush 取到的贴图（选配、测试场各自缓存），没有的（肩部的紧急脱离等四件）画图标。Category / Index 是函数：禁用小块、配装槽位的内容会变 */
TSharedRef<SWidget> MechItemPicture(TFunction<int32()> GetCategory, TFunction<int32()> GetIndex, TFunction<const FSlateBrush*(int32, int32)> Thumb);

/** 卡片内容（选配、测试场共用）：平时上图下字；宽卡片（Width >= 280，一页只有三四件时）左图右字、多写几行参数；矮卡片（Height < 130，一页件数多、排两行时）照片铺满、名字压在照片底边。TagText 非空时右上角一个小标签 */
TSharedRef<SWidget> MechCardBody(int32 InCategory, int32 InIndex, const TSharedRef<SWidget>& Picture, float Width, float Height, TFunction<FText()> TagText, TFunction<FLinearColor()> TagColor,
	TFunction<FLinearColor()> NameColor);

/** 底部配装栏的一格（选配、测试场共用）：小图 + 类别 + 名字 + 一行状态；现在看的那一类高亮，点了跳到那一类 */
TSharedRef<SWidget> MechLoadoutSlot(int32 InCategory, TFunction<int32()> GetIndex, TFunction<const FSlateBrush*(int32, int32)> Thumb, TFunction<bool()> IsCurrent, TFunction<void()> OnClick,
	TFunction<FText()> Note, TFunction<FLinearColor()> NoteColor, float Width);

/** 一行放 Count 个时每个多宽（用满 RowWidth） */
float MechCardWidth(int32 Count, float RowWidth, float Gap);

/** 页签和底部配装栏显示哪几类、什么顺序（选配、测试场共用）：增减一类只改这一处 */
TArrayView<const int32> MechLoadoutCategories();

/** 一页 Count 张卡片怎么排（选配、测试场共用），放进 RowWidth × AreaHeight、不用滚：张数少一排放下（用满宽度）；放不下时排两行（再多三行） */
struct FMechCardLayout
{
	float Width = 0.f;
	float Height = 0.f;
	int32 Rows = 1;
};
FMechCardLayout MechCardLayout(int32 Count, float RowWidth, float AreaHeight, float Gap);

/** 测试开关 -MechCardCount=N：肩部页的卡片循环凑够 N 张，提前看件数变多时排得下不下 */
void MechTestExpandCards(int32 InCategory, TArray<int32>& Order);

/** 框架卡片按企业分组时每组上面那一条（10-31 阵营机体：九种框架 = 三家 × 轻中重）：标识（没有就一条品牌色）+ 名字 + 理念，底下一根品牌色细线。
 *  选配整屏和测试场配装页共用 */
TSharedRef<SWidget> MechMakerGroupHeader(EMechMaker Maker, float Width, float Height);

/** 框架页的卡片区（10-31 用户：「因为篇幅大小框架下面选不到」——以前每家一行、三行叠起来比屏幕还高，最下面一家和底栏被挤出屏幕）：
 *  三家并排成一排，每家上面一条名牌，下面 轻 / 中 / 重 三张窄卡片；整排宽 Width，卡片高 CardHeight。
 *  卡片由宿主画（选配、测试场的状态不一样）：MakeCard(框架编号, 卡片宽, 卡片高)。外面包一层只缩不放的 SScaleBox，窗口比 16:9 窄时整块等比缩小。
 *  OutCards：九张卡片（测试检查点不点得到用） */
TSharedRef<SWidget> MechFrameGroups(float Width, float CardHeight, TFunctionRef<TSharedRef<SWidget>(int32, float, float)> MakeCard,
	TArray<TWeakPtr<SWidget>>& OutCards);

/** 框架卡片上的短名：「新纪 轻型」在分组里只写「轻型」 */
FString MechFrameShortName(int32 FrameIndex);

/** -MechLoadoutShots：宿主每次重建卡片时登记这一页的卡片，测试每张截图后检查是不是都在窗口里、点卡片中心能点到它 */
void MechTestSetCards(const TArray<TWeakPtr<SWidget>>& Cards);

/** 照片：画一张或两张（混合）贴图；按住左键左右拖动转台 */
class SMechPhoto : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechPhoto) : _Size(640.f, 360.f) {}
		SLATE_ARGUMENT(FVector2D, Size)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return Size; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	/** 这一帧画哪两张、B 盖在 A 上的不透明度 */
	void SetImages(UObject* A, UObject* B, float Blend);
	/** 转台角度（度，变大 = 物件往右转）；拖动时改它：往右拖 = 物件跟着手往右转，拖满照片宽度转一圈 */
	float Angle = 0.f;
	bool bDragging = false;
	/** 最近一次拖动的时间（停手一会儿后才接着自动转） */
	double LastDrag = -100.0;
	/** 拖动时的角速度（度/秒，SMechItemDetail::Tick 按每帧角度变化平滑），松手后按它带一点惯性 */
	float SpinVelocity = 0.f;
	/** 上一帧的角度（算角速度用） */
	float PrevAngle = 0.f;

private:
	FVector2D Size = FVector2D(640.f, 360.f);
	TSharedPtr<FSlateBrush> BrushA;
	TSharedPtr<FSlateBrush> BrushB;
	float Blend = 0.f;
};

/** 读数图：每项参数一根轴（0–1），这件实线、对比的那件虚一点 */
class SMechRadar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechRadar) : _Size(180.f) {}
		SLATE_ARGUMENT(float, Size)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Size); }

	TArray<FText> Labels;
	TArray<float> Values;
	TArray<float> Compare;

private:
	float Size = 180.f;
};

/** 选中那一件的照片 + 详细信息 */
class SMechItemDetail : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMechItemDetail)
		: _PhotoSize(760.f, 428.f)
		, _InfoWidth(480.f)
		{}
		SLATE_ARGUMENT(TWeakObjectPtr<AMechPlayerController>, Owner)
		/** 现在看哪一类、哪一件 */
		SLATE_ARGUMENT(TFunction<int32()>, GetCategory)
		SLATE_ARGUMENT(TFunction<int32()>, GetIndex)
		/** 和哪一件比（这一类里已装备 / 已选的那件，-1 不比） */
		SLATE_ARGUMENT(TFunction<int32()>, GetCompare)
		/** 现在这一套用的哪个框架（判断同厂加成生没生效；-1 不知道） */
		SLATE_ARGUMENT(TFunction<int32()>, GetFrame)
		/** 名牌下面一行状态（已选 / 被禁 / 待做……）和颜色 */
		SLATE_ATTRIBUTE(FText, Status)
		SLATE_ATTRIBUTE(FSlateColor, StatusColor)
		SLATE_ARGUMENT(FVector2D, PhotoSize)
		SLATE_ARGUMENT(float, InfoWidth)
		/** -MechLoadoutShots：测试要选中某一类的某一件时调（宿主切页签、点卡片）；截图文件名前缀 */
		SLATE_ARGUMENT(TFunction<void(int32, int32)>, OnTestSelect)
		SLATE_ARGUMENT(FString, TestName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMechItemDetail() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** 摄影棚里所有小图先排上队（进页面时调，分帧拍，翻页签时不用再等） */
	void Prewarm();

private:
	AMechItemStudio* Studio() const;
	void Refocus(int32 InCategory, int32 InIndex);
	TSharedRef<SWidget> MakePhoto();
	TSharedRef<SWidget> MakeStrip();
	TSharedRef<SWidget> MakeInfo();
	TSharedRef<SWidget> MakeStatRow(int32 Row);
	/** 同厂加成单独一块（11-01：以前塞在参数列里换三行）：配哪家框架、加成是什么，现在的框架同厂时高亮并写「已生效」 */
	TSharedRef<SWidget> MakeBuffBlock();
	void TestTick(float DeltaTime);
	/** 测试：量实时转台的开销（开着拍 3 秒、停 3 秒，比平均帧时间 / 游戏线程 / 渲染线程 / GPU）；量完返回 true */
	bool TestLivePerf(float DeltaTime);

	TWeakObjectPtr<AMechPlayerController> Owner;
	TFunction<int32()> GetCategory;
	TFunction<int32()> GetIndex;
	TFunction<int32()> GetCompare;
	TFunction<int32()> GetFrame;
	TFunction<void(int32, int32)> OnTestSelect;
	FString TestName;
	FVector2D PhotoSize;
	float InfoWidth = 480.f;

	TSharedPtr<SMechPhoto> Photo;
	TSharedPtr<SMechRadar> Radar;
	/** 右下角画中画（肩部：挂在肩边那张） */
	TSharedPtr<FSlateBrush> PipBrush;
	/** 缩略条：0 = 转台（用小图），1.. = 详细视角 */
	TArray<TSharedPtr<FSlateBrush>> StripBrushes;

	int32 Category = -1;
	int32 Index = -1;
	int32 CompareIndex = -1;
	/** 大图显示什么：-1 转台，0.. 详细视角 */
	int32 ViewMode = -1;
	/** 这一件和对比那件的参数（换一件时重算） */
	TArray<MechCatalog::FStat> Stats;
	TArray<MechCatalog::FStat> CompareStats;
	bool bPrewarmed = false;
	/** 这一件开始拍的时间、拍的过程中最长的一帧（测试日志看卡不卡） */
	double FocusStart = 0.0;
	float WorstFrame = 0.f;
	bool bBakeLogged = false;
	/** 自动慢转的油门（0–1）：停手一会儿后从 0 慢慢加到 1，不是一下子满速 */
	float AutoSpin = 0.f;
	/** 这一帧的界面缩放（照片像素大小 = PhotoSize × 它；测试日志记一次） */
	float GeomScale = 1.f;

	// ---- 测试（-MechLoadoutShots）
	int32 TestStep = -1;
	float TestClock = 0.f;
	float TestStepClock = 0.f;
	bool bTestShot = false;
	bool bTestChecked = false;
	bool bTestScaleLogged = false;
	bool bTestHeightLogged = false;
	/** 量实时转台开销：阶段（0 开着预热、1 开着量、2 关掉预热、3 关掉量）、累计 */
	bool bTestLiveOff = false;
	int32 PerfPhase = 0;
	float PerfClock = 0.f;
	int32 PerfN[2] = {0, 0};
	double PerfFrame[2] = {0.0, 0.0};
	double PerfGT[2] = {0.0, 0.0};
	double PerfRT[2] = {0.0, 0.0};
	double PerfGPU[2] = {0.0, 0.0};
	double PerfCall = 0.0;
};

/** 测试场配装页（整屏） */
class SMechLoadoutBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMechLoadoutBrowser) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AMechPlayerController>, Owner)
		SLATE_ARGUMENT(TFunction<void(const FMechLoadout&)>, OnApply)
		SLATE_ARGUMENT(TFunction<void()>, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** 每次打开配装页时：从设置里取最后一套、回到射击类 */
	void Reset();

private:
	void RebuildGrid();
	void Select(int32 InCategory, int32 InIndex);
	TSharedRef<SWidget> MakeTabs();
	/** 卡片：默认 208×170；框架页是分组里的窄卡片（只写「轻型」+ 型号「名字」，小图保持比例） */
	TSharedRef<SWidget> MakeCard(int32 InCategory, int32 InIndex, float Width = 208.f, float Height = 170.f);
	TSharedRef<SWidget> MakeBottom();
	const FSlateBrush* ThumbBrush(int32 InCategory, int32 InIndex);

	TWeakObjectPtr<AMechPlayerController> Owner;
	TFunction<void(const FMechLoadout&)> OnApply;
	TFunction<void()> OnBack;
	TSharedPtr<SWrapBox> Grid;
	TSharedPtr<SMechItemDetail> Detail;
	TMap<int32, TSharedPtr<FSlateBrush>> Brushes;
	FMechLoadout Edit;
	/** 现在身上装的（应用过的那一套），卡片上标「已装备」、参数对比用 */
	FMechLoadout Equipped;
	int32 Category = 2;
	int32 FocusIndex = 0;
	FButtonStyle Flat;
};
