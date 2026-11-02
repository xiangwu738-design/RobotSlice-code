// 菜单控件套件（11-01 界面改版，用户：「优化一下整体 UI 审美，现在的还是有点廉价」）。
// 改前截图（docs/界面改版_20261101/改前）里廉价感主要来自：引擎默认的滑条 / 数字框 / 输入框 / 滚动条，白底灰字的切换钮，
// 所有字一个字体一个字重、层级只靠字号，没有悬停和切页的过渡，半透明面板压在亮背景上发灰、设置项超出面板。这里把通用的东西统一画：
//   字体层级：英文小标签（Bahnschrift 加字距、强调色）→ 大标题（雅黑 Light）→ 正文（雅黑）→ 说明（暗色小字）；数字一律 Bahnschrift
//   颜色：深色底、很淡的细线、只有一个强调色（MechUITheme，设置「界面」页可换五套）
//   控件：自己画的按钮（菜单列表项 / 描边 / 实心）、选项块、分段选择、开关、滑条、页签、整行悬停的设置行、按键帽、卡片；
//         滚动条和输入框换成同一套样式
//   动效：悬停 / 按下 / 选中每帧平滑过渡（约 0.1 秒），按钮按下时轻微压暗；切页淡入 + 轻微位移（FadeIn）
// 颜色在画的时候从 MechUITheme 读（文字用 TAttribute 每帧取），换配色不用重建界面；滚动条、输入框的样式缓存了笔刷，ApplyPalette 会原地刷新。
// 界面大小按逻辑 1920×1080 设计（16:9 窗口的 DPI 缩放见 MechItemDetail.h 文件头），4K 180% 下布局一样、像素更细。
// 以后各页（房间、加入……）都用这里的控件，不要再用引擎默认样式。

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "MechUITheme.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class SScrollBox;
class STextBlock;

/** 可点控件的画法 */
enum class EMechKitLook : uint8
{
	None,      // 不画底（内容自己画，只用悬停 / 按下值）
	MenuItem,  // 主菜单 / 暂停的大列表项：悬停时左边亮一根竖条、底色从左往右淡出、右边出箭头
	Action,    // 普通按钮：细描边，悬停时描边变强调色、四角出折角
	Primary,   // 主按钮：实心强调色、深色字
	Chip,      // 选项块（地图、模式、配色）：选中时强调色描边 + 淡底
	Segment,   // 分段选择里的一格（外框由 MechKit::Segmented 画）：选中时淡底 + 底下一道强调色
	Tab,       // 页签：选中时底下一道强调色（从中间长出来）
	Row,       // 设置行 / 按键行：整行悬停淡底 + 左边一根强调色细条，底下一道分隔线
	Card,      // 卡片：深色底 + 细描边，选中时强调色描边 + 四角折角
	Switch,    // 开关：左边一个滑块轨道
};

/** 菜单音效的种类（11-01）：声音是 /Game/Audio/UI/S_ui_*（SourceAssets/sfx_ui 合成，tools/ue_import_ui_sfx.py 导入） */
enum class EMechUISound : uint8
{
	Hover,    // 悬停：很轻的 tick（限流）
	Click,    // 普通点击（页签、选项块、卡片、开关……）
	Confirm,  // 确认（开始、应用、主按钮），更厚
	Back,     // 返回 / 取消 / 撤销
	Slider,   // 滑条松手
	Count,
	Auto,     // 按钮自己按 Look 选：主按钮 = Confirm，其余 = Click
};

/** 套件里所有可点的东西：自己处理鼠标（按下记住、松开时还在上面才算点击，和 SButton 一样）；悬停 / 按下 / 选中三个值每帧平滑过渡 */
class SMechKitButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMechKitButton)
		: _Look(EMechKitLook::Action)
		, _Selected(false)
		, _Clickable(true)
		, _ClickSound(EMechUISound::Auto)
		{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_ARGUMENT(EMechKitLook, Look)
		SLATE_ATTRIBUTE(bool, Selected)
		/** false：只要悬停效果、不接点击（设置行：点击交给行里的控件） */
		SLATE_ARGUMENT(bool, Clickable)
		SLATE_ARGUMENT(TFunction<void()>, OnClick)
		/** 鼠标移上来时（选配卡片：悬停就换大图） */
		SLATE_ARGUMENT(TFunction<void()>, OnHover)
		/** 卡片的描边颜色（被禁 = 红……）；不给、或者返回透明度 < 0 的颜色，就按悬停 / 选中自己算 */
		SLATE_ARGUMENT(TFunction<FLinearColor()>, Outline)
		/** 点击音；Auto = 按 Look 选 */
		SLATE_ARGUMENT(EMechUISound, ClickSound)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetContent(const TSharedRef<SWidget>& InContent);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	/** 键盘 / 手柄导航（11-01：换掉 SButton 后要自己接，不然菜单只能用鼠标）：能拿焦点，导航选到时显示成悬停，确认键松开时算点击 */
	virtual bool SupportsKeyboardFocus() const override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** 0–1，平滑过的 */
	float GetHover() const { return HoverT; }
	float GetPress() const { return PressT; }
	float GetSelect() const { return SelectT; }
	bool IsOn() const { return Selected.Get(); }

private:
	EMechKitLook Look = EMechKitLook::Action;
	TAttribute<bool> Selected;
	bool bClickable = true;
	TFunction<void()> OnClick;
	TFunction<void()> OnHover;
	TFunction<FLinearColor()> Outline;
	EMechUISound ClickSound = EMechUISound::Click;
	bool bPressed = false;
	bool bNavFocus = false;
	float HoverT = 0.f;
	float PressT = 0.f;
	float SelectT = 0.f;
};

/** 滑条：细轨道 + 强调色填充 + 竖条滑块；拖动、点哪跳哪、滚轮一格一步。松手（或滚轮）时调 OnCommit（存盘用，拖的过程中不每帧写 ini） */
class SMechKitSlider : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechKitSlider)
		: _Min(0.f)
		, _Max(1.f)
		, _Step(0.01f)
		, _Width(340.f)
		{}
		SLATE_ARGUMENT(TFunction<float()>, Get)
		SLATE_ARGUMENT(TFunction<void(float)>, Set)
		SLATE_ARGUMENT(TFunction<void()>, OnCommit)
		SLATE_ARGUMENT(float, Min)
		SLATE_ARGUMENT(float, Max)
		SLATE_ARGUMENT(float, Step)
		SLATE_ARGUMENT(float, Width)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Width, 30.f); }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	/** 键盘 / 手柄：导航选到后左右键一步一步调 */
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	/** 鼠标的横坐标 → 值（按 Step 取整） */
	void SetFromX(const FGeometry& G, const FVector2D& ScreenPos);
	float Fraction() const;
	TFunction<float()> Get;
	TFunction<void(float)> Set;
	TFunction<void()> OnCommit;
	float Min = 0.f;
	float Max = 1.f;
	float Step = 0.01f;
	float Width = 340.f;
	bool bDragging = false;
	bool bNavFocus = false;
	float HoverT = 0.f;
};

namespace MechKit
{
	// ---------------------------------------------------------------- 基础
	/** 第一次建界面时调：按设置套用配色（可重复调，只做一次） */
	void Init();
	/** 换配色（设置「界面」页）：拷预设进 MechUITheme，再刷新滚动条 / 输入框这些缓存了颜色的样式 */
	void ApplyPalette(int32 Index);

	/** 放一下菜单音效（11-01）。悬停音限流（0.09 秒内不重放；刚放过点击音 0.25 秒内不放，页面重建后鼠标落在新按钮上不会再 tick 一下）；
	 *  声音第一次用时才加载并强引用住。-nosound / 没有世界时只计数不放。 */
	void PlayUISound(EMechUISound Kind);
	/** 测试用：这个种类从开始到现在触发了几次（不管有没有真的放出声） */
	int32 UISoundCount(EMechUISound Kind);
	/** 按按钮上的字挑点击音：返回 / 取消 / 撤销 / 退出 = Back，开始 / 开房 / 应用 / 确定 / 再来一局 = Confirm，其余 Default */
	EMechUISound SoundForLabel(const FString& Label, EMechUISound Default = EMechUISound::Click);

	const FMechUITheme& Th();
	FSlateFontInfo Font(float Size, bool bBold = false);
	FSlateFontInfo Light(float Size);
	FSlateFontInfo Latin(float Size, int32 LetterSpacing = 0);
	/** 跟着配色实时变的颜色（每帧从 MechUITheme 取） */
	TAttribute<FSlateColor> Color(FLinearColor FMechUITheme::*Member, float Alpha = 1.f);
	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float T);

	const FSlateBrush* White();
	const FSlateBrush* NoBrush();
	/** 直角矩形笔刷：底色 + 描边（按颜色缓存；在 BorderImage_Lambda 里每帧调也不会一直新建） */
	const FSlateBrush* Box(const FLinearColor& Fill, const FLinearColor& Line = FLinearColor::Transparent, float LineWidth = 0.f);

	/** 0–1 的缓出 */
	float EaseOut(float T);

	// ---------------------------------------------------------------- 文字
	TSharedRef<STextBlock> Text(const TAttribute<FText>& InText, const FSlateFontInfo& InFont, const TAttribute<FSlateColor>& InColor);
	/** 英文小标签：前面一小段强调色短线 + 加字距的 Bahnschrift（「── SETTINGS」） */
	TSharedRef<SWidget> Kicker(const FText& Label);
	/** 页头：小标签 + 大标题 + 一行说明（说明可空） */
	TSharedRef<SWidget> PageTitle(const FText& KickerText, const FText& Title, const TAttribute<FText>& Sub = FText::GetEmpty());
	/** 横细线 */
	TSharedRef<SWidget> Hairline(float Alpha = 1.f);
	/** 小节标题（设置页里分组）：标题 + 右边一句小字说明 */
	TSharedRef<SWidget> Section(const FText& Title, const TAttribute<FText>& Note = FText::GetEmpty());
	/** 键帽（按键提示、改键页右边）：State 0 默认、1 改过（提醒色）、2 正在等新键（强调色、闪） */
	TSharedRef<SWidget> KeyCap(const TAttribute<FText>& Key, TFunction<int32()> State = nullptr, float MinWidth = 44.f);

	// ---------------------------------------------------------------- 可点的
	/** 菜单大列表项（主菜单、暂停）；Tag 是右边一行很淡的英文（悬停时变亮），可空 */
	TSharedRef<SWidget> MenuItem(const FText& Label, TFunction<void()> OnClick, TAttribute<bool> Enabled = true, const FText& Tag = FText::GetEmpty());
	/** 按钮：描边或实心（主按钮）；Width 0 = 按字宽 */
	TSharedRef<SWidget> Button(const TAttribute<FText>& Label, TFunction<void()> OnClick, bool bPrimary = false, TAttribute<bool> Enabled = true,
		float Width = 0.f, float Height = 46.f);
	/** 选项块：一个可选中的小块（IsOn 时高亮） */
	TSharedRef<SWidget> Chip(const TAttribute<FText>& Label, TFunction<bool()> IsOn, TFunction<void()> OnClick, TAttribute<bool> Enabled = true);
	/** 选项块，内容自己给（配色色块等） */
	TSharedRef<SWidget> ChipWith(const TSharedRef<SWidget>& Content, TFunction<bool()> IsOn, TFunction<void()> OnClick, TAttribute<bool> Enabled = true);
	/** 分段选择：一排连在一起的选项，Get 返回选中的序号（-1 都不选）；Enabled(i) 可空 */
	TSharedRef<SWidget> Segmented(const TArray<FText>& Options, TFunction<int32()> Get, TFunction<void(int32)> Set,
		TFunction<bool(int32)> Enabled = nullptr, float CellMinWidth = 64.f);
	/** 开关（带「开 / 关」字） */
	TSharedRef<SWidget> Switch(TFunction<bool()> Get, TFunction<void(bool)> Set);
	/** 滑条 + 右边读数 */
	TSharedRef<SWidget> Slider(TFunction<float()> Get, TFunction<void(float)> Set, float Min, float Max, float Step, TFunction<FText(float)> Format,
		TFunction<void()> OnCommit = nullptr, float Width = 320.f);
	/** 左右切换：‹ 当前值 ›（分辨率） */
	TSharedRef<SWidget> Stepper(const TAttribute<FText>& Value, TFunction<void(int32)> OnStep, float Width = 300.f);
	/** 页签：一排，选中的底下一道强调色 */
	TSharedRef<SWidget> Tabs(const TArray<FText>& Labels, TFunction<int32()> Get, TFunction<void(int32)> Set, TFunction<bool(int32)> Enabled = nullptr,
		float TabWidth = 0.f, float FontSize = 17.f);
	/** 设置行：左边名字 + 一行小字说明，右边控件；整行悬停高亮。Pending 为真时名字后面标「未应用」（画面设置） */
	TSharedRef<SWidget> Row(const FText& Name, const TAttribute<FText>& Desc, const TSharedRef<SWidget>& Control, float NameWidth = 360.f,
		TFunction<bool()> Pending = nullptr);
	/** 卡片（选配 / 配装的武器卡片）：Selected 时强调色描边 + 四角折角；Outline 可覆盖描边色（被禁 = 红） */
	TSharedRef<SMechKitButton> Card(const TSharedRef<SWidget>& Content, TFunction<void()> OnClick, TFunction<void()> OnHover, TAttribute<bool> Selected,
		TFunction<FLinearColor()> Outline = nullptr);

	// ---------------------------------------------------------------- 引擎控件的样式
	const FScrollBoxStyle& ScrollBoxStyle();
	const FScrollBarStyle& ScrollBarStyle();
	/** 输入框（玩家名称、IP）：深色底 + 细描边，获得焦点时描边变强调色 */
	const FEditableTextBoxStyle& TextBoxStyle();
	/** 换好样式的滚动框（细滚动条、没有上下阴影） */
	TSharedRef<SScrollBox> ScrollBox();

	// ---------------------------------------------------------------- 动效
	/** 切页淡入：StartTime 变了就从透明 + 往左偏 Offset 开始，Duration 秒内淡入并滑到原位 */
	TSharedRef<SWidget> FadeIn(const TSharedRef<SWidget>& Content, TFunction<double()> StartTime, float Duration = 0.24f, float Offset = 14.f);
	/** 现在的界面时间（Slate 的，暂停时也走） */
	double Now();
}
