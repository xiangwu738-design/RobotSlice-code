// 菜单界面（Slate 直接写，不用 UMG 资源）：主菜单（开房 / 加入 / 测试场 / 模型查看 / 设置 / 退出）、暂停、设置、结算。
// 11-01 起地图、模式、人数都在房间里选（MechDraftUI 的房间页，房间流程见 MechRoom.h），主菜单不再有「开始对战 / 联机对战」两页。
// 左边一列内容，底下是一块从左往右淡出的深色面板（右边露出场景和自己的机体）；按钮要做的事都交给 AMechPlayerController。
// 11-01 界面改版：控件全换成 MechUIKit 的（列表项、选项块、分段选择、开关、滑条……颜色跟设置里的菜单配色走），切页淡入；
// 设置页分五个页签（画面 / 声音 / 操作 / 界面 / 按键），按键设置从单独一页变成最后一个页签。

#pragma once

#include "CoreMinimal.h"
#include "MechTypes.h"
#include "MechRules.h"
#include "Scalability.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"

class AMechPlayerController;
class SWidgetSwitcher;

enum class EMechMenuPage : uint8
{
	Main,
	Join,     // 11-01 加入别人的房间：输 IP（原来「联机对战」页的下半截）
	Settings,
	Pause,
	Results,
	Loadout,  // 测试场的自由配装
	Keys      // 按键设置：11-01 起是设置页的「按键」页签，SetPage(Keys) = 打开设置并切到那个页签（这一页本身不再显示）
};

/** 设置页的页签（11-01） */
enum class EMechSettingsTab : uint8
{
	Graphics,   // 画面：窗口、分辨率、垂直同步、帧率上限、画质、3D 渲染精度（这几项要点「应用」）、动态模糊
	Audio,      // 声音：主音量、音乐音量、告警音量、告警语音
	Control,    // 操作：鼠标灵敏度、反转 Y、视野、第一人称
	Interface,  // 界面：菜单配色、战斗 HUD 主题、信息区
	Keys,       // 按键
	Count
};

class SMechMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMechMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AMechPlayerController>, Owner)
		SLATE_ARGUMENT(EMechMenuPage, Page)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetPage(EMechMenuPage InPage);
	EMechMenuPage GetPage() const { return Page; }

	virtual bool SupportsKeyboardFocus() const override { return true; }
	/** 左侧面板宽度的过渡（设置页更宽） */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	/** Esc：子页面回上一页，暂停页回游戏 */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	/** 按键设置页「按任意键」时，键盘和鼠标按键在这里先截下来（不让按钮把空格 / 回车当成点击） */
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	TSharedRef<SWidget> MakeMainPage();
	TSharedRef<SWidget> MakeJoinPage();
	TSharedRef<SWidget> MakeSettingsPage();
	TSharedRef<SWidget> MakePausePage();
	TSharedRef<SWidget> MakeResultsPage();
	/** 11-01 结算页底下的按钮：从房间开的场 = 回房间（房主）/ 退出房间，否则再来一局 / 返回主菜单 */
	TSharedRef<SWidget> MakeResultsButtons();
	TSharedRef<SWidget> MakeLoadoutPage();
	TSharedRef<SWidget> MakeKeysPage();
	/** 设置页的五个页签（11-01） */
	TSharedRef<SWidget> MakeGraphicsTab();
	TSharedRef<SWidget> MakeAudioTab();
	TSharedRef<SWidget> MakeControlTab();
	TSharedRef<SWidget> MakeInterfaceTab();
	TSharedRef<SWidget> MakeKeysTab();
	/** 设置页底栏：左边一行说明 / 提示，右边按钮（画面页签：撤销、应用；按键页签：恢复默认；都有返回） */
	TSharedRef<SWidget> MakeSettingsFooter();
	void SetSettingsTab(int32 Tab);
	/** 按键设置：正在等哪个操作的新键（-1 = 没在等）；收到一个键后改键、提示 */
	void FinishRebind(const FKey& Key);
	/** 正在按键页签等新键 */
	bool IsListening() const;

	/** 画面设置（引擎的 UGameUserSettings）：进设置页时记一份「已应用」的值，和现在的比就知道哪几项改了还没应用 */
	void SnapshotGraphics();
	int32 PendingGraphics() const;
	void ApplyGraphics();
	void RevertGraphics();

	/** 菜单大列表项（主菜单、暂停、结算）；右边的英文小字按名字查表 */
	TSharedRef<SWidget> Button(const FText& Label, TFunction<void()> OnClick, TAttribute<bool> Enabled = true);
	/** 一个可选中的选项块 */
	TSharedRef<SWidget> Toggle(const FText& Label, TFunction<bool()> IsOn, TFunction<void()> OnClick, TAttribute<bool> Enabled = true);
	/** 开关 */
	TSharedRef<SWidget> OnOff(TFunction<bool()> Get, TFunction<void(bool)> Set);
	/** 页头：英文小标签（按标题查表）+ 大标题 + 说明 */
	TSharedRef<SWidget> Header(const FText& Title, const FText& Sub = FText::GetEmpty());
	TSharedRef<SWidget> Label(const FText& Text, int32 Size = 15);
	TSharedRef<SWidget> SettingRow(const FText& Name, TSharedRef<SWidget> Control);
	/** 主菜单左上角的标题块（HX-07） */
	TSharedRef<SWidget> Brand();
	/** 游戏里打开的（暂停、结算、配装、暂停里进的设置）：整屏压暗 */
	bool IsInGame() const;
	void Back();

	TWeakObjectPtr<AMechPlayerController> Owner;
	EMechMenuPage Page = EMechMenuPage::Main;
	/** 设置页返回到哪一页（主菜单还是暂停） */
	EMechMenuPage SettingsReturn = EMechMenuPage::Main;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SBox> ResultsHolder;
	/** 测试场配装页（10-31 整屏，MechItemDetail.h） */
	TSharedPtr<class SMechLoadoutBrowser> LoadoutBrowser;

	FString JoinAddress = TEXT("127.0.0.1");
	/** 配装页上正在挑的那一套（打开时从设置里取） */
	FMechLoadout EditLoadout;
	int32 ListeningKey = -1;
	/** 按键页底下的一行提示（互换了哪两个、这个键不能绑） */
	FText KeyNotice;

	/** 设置页现在是哪个页签（EMechSettingsTab；这次进程里记着上次看的那个） */
	int32 SettingsTab = 0;
	/** 切页 / 切页签的时间（淡入动画从这一刻开始） */
	double PageTime = 0.0;
	double TabTime = 0.0;
	/** 左侧面板现在的宽度（往目标宽度平滑过渡） */
	float PanelWidth = 700.f;

	/** 进设置页时记下的画面设置（「已应用」的值） */
	struct FGraphicsSnap
	{
		int32 Window = 0;
		FIntPoint Resolution = FIntPoint::ZeroValue;
		bool bVSync = false;
		int32 FrameLimit = 0;
		int32 Quality = -1;
		float RenderScale = 0.f;
		bool bValid = false;
	};
	FGraphicsSnap Snap;
	/** 撤销时整套画质档位原样放回去（总档位是「自定义」时只记总档位不够） */
	Scalability::FQualityLevels SnapQuality;
};
