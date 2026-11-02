// HUD 主题（10-31）：选手是意识体，装进哪家造的机体，就看到哪家的系统界面——HUD 跟着本机框架的制造商走。
// 同一套布局、不同主题参数：颜色、线宽、条形厚度、字体、数字风格、装饰开关都在 FMechHudTheme 里。
// 战斗 HUD（MechHUD.cpp）、告警（MechHudAlerts.cpp）、小地图 / 名字（MechMinimap.cpp）、死斗比分条和计分板（MechHudModes.cpp）
// 原来各自写死的颜色现在都是 MechHudTheme::Get() 里成员的引用，Use() 把另一套拷进同一份对象，各处马上跟着变。
// 四套：
//   联赛标准 STD   —— 原来那套（青色细线），观战 / 备用 / 没有制造商时用
//   新纪建设 NEC   —— 工程机械仪表：琥珀橙、粗线粗条、分段条、七段数码管读数（灭的段留底色）、仪表盘两角黄黑警示斜纹
//   维克托重工 VHI —— 军用多功能显示器：单色荧光绿、模板字体（Stencil）读数、等宽小字、读数补零、方位刻度带、读条下刻度尺、方框仪表盘
//   相位电子 PHASE —— 最克制：白色细线、不铺底、去掉单位和空槽、细体字、屏幕四角一圈极简细线框
// 切换：AMechHUD::DrawHUD 每帧按「看的是不是自己操控的机体 + 它的框架制造商」选（换配装、重生马上生效）；
// 命令行 -MechHudTheme=NEC|VHI|PHASE|STD 强制；11-01 起设置「界面」页也能固定一套（FMechSettings::HudTheme，默认 -1 = 跟随机体），命令行仍然优先。
// 告警语音的默认值也跟主题走（设置里选「跟随机体」时），不覆盖手动选的。

#pragma once

#include "CoreMinimal.h"

enum class EMechMaker : uint8;  // MechCatalog.h
class AMechHUD;
class AMechCharacter;

enum class EMechHudThemeId : uint8
{
	Standard,  // 联赛标准
	NewEra,    // 新纪建设
	Victor,    // 维克托重工
	Phase,     // 相位电子
	Count
};

/** HUD 的字体（中文名字一律退回微软雅黑）。文件在 Windows\Fonts，没有的退回 Bahnschrift */
enum class EMechHudFace : uint8
{
	Bahnschrift,     // 联赛标准：DIN 风格窄体
	SegoeBold,       // 新纪：粗黑体
	Stencil,         // 维克托：模板字体（STENCIL.TTF，随 Office 装的）
	Mono,            // 维克托：Consolas 粗体（等宽，像 MFD 的点阵字）
	SegoeLight,      // 相位：细体
	SegoeSemilight,  // 相位：半细
	Count
};

/** 底部仪表盘底座的样式 */
enum class EMechDashStyle : uint8
{
	Trapezoid,  // 梯形半透明暗底 + 上沿亮线（联赛标准、新纪）
	Frame,      // 方框暗底 + 四角折角 + 上沿一排刻度（维克托 MFD）
	Hairline    // 不铺底，只有一道细线和两端短竖线（相位）
};

struct FMechHudTheme
{
	EMechHudThemeId Id = EMechHudThemeId::Standard;
	/** STD / NEC / VHI / PHASE（日志、截图文件名、命令行用） */
	const TCHAR* Code = TEXT("STD");

	// ---- 颜色（带透明度）
	FLinearColor Ink;       // 正文、读数
	FLinearColor Dim;       // 次要文字
	FLinearColor Faint;     // 很淡的线、空槽
	FLinearColor Accent;    // 主色：自己的状态、条形填充
	FLinearColor Track;     // 条形底槽
	FLinearColor Ally;      // 队友 / 蓝队
	FLinearColor Enemy;     // 敌人 / 红队
	FLinearColor Warn;      // 提醒：装填、过热、低弹、失衡
	FLinearColor Heal;      // 回装甲
	FLinearColor Gold;      // 核心、头部命中、第一名
	FLinearColor Guard;     // 重生保护
	FLinearColor Plate;     // 面板暗底（计分板、模型查看）
	FLinearColor Shade;     // 地图暗底（不透明，用时再乘透明度）
	FLinearColor DashUp;    // 仪表盘底座上沿 / 下沿
	FLinearColor DashDown;
	FLinearColor Caution;   // 告警琥珀（被跟踪、被拖拽、琥珀告警灯）
	FLinearColor Danger;    // 告警红（锁定、导弹来袭、红告警灯）
	FLinearColor LampText;  // 亮着的告警灯上的字
	FLinearColor Glow;      // 上电扫描线、断电那道亮线
	FLinearColor Hazard;    // 警示斜纹的黄

	// ---- 线和条
	/** 所有线宽 ×（AMechHUD::Polyline 里统一乘） */
	float LineScale = 1.f;
	/** 准星下方细条、仪表盘读条的厚度 × */
	float BarScale = 1.f;
	/** 准星下方细条平静 1.2 秒后降到的透明度 */
	float CalmAlpha = 0.5f;
	/** 细条下面压一道暗影的透明度（相位的白色细条在亮地面上会看不清；0 = 不压） */
	float BarShadow = 0.f;

	// ---- 字
	/** 字号 < 20 的（标签、名字、距离）和 ≥ 20 的（读数、比分、倒计时）各用什么字体 */
	EMechHudFace LabelFace = EMechHudFace::Bahnschrift;
	EMechHudFace NumberFace = EMechHudFace::Bahnschrift;
	/** 主要读数（速度、AP / 能量、弹药、肩部冷却、细条中间数字、时间比分、开局倒计时）画成七段数码管 */
	bool bSegmentDigits = false;
	/** 七段：没亮的段留一点底色（像真的数码管） */
	float GhostAlpha = 0.f;
	/** 读数补零（速度 087、弹药 024 /030） */
	bool bPadDigits = false;

	// ---- 版式 / 装饰
	EMechDashStyle Dash = EMechDashStyle::Trapezoid;
	/** 仪表盘上沿两端的五道刻度 */
	bool bDashTicks = true;
	/** km/h、「/最大值」这类次要字 */
	bool bUnits = true;
	/** 插件栏没插件时的三个空槽 */
	bool bEmptySlots = true;
	/** 读数上面的小标签多写几个（SPD / WPN / SHD） */
	bool bExtraLabels = false;
	/** AP / 能量条分段（一格一格） */
	bool bSegmentBars = false;
	/** 新纪：仪表盘两个上角、开局倒计时横幅的黄黑警示斜纹 */
	bool bHazard = false;
	/** 维克托：比分条下方的方位刻度带 */
	bool bHeadingTape = false;
	/** 维克托：AP / 能量条、仪表盘读条下面的刻度尺 */
	bool bRulers = false;
	/** 相位：屏幕四角一圈极简细线框 */
	bool bHairFrame = false;

	// ---- 告警
	/** 告警语音的默认（设置里「跟随机体」时用）：1 冷静女声、2 机械合成、3 企业客服 */
	int32 DefaultVoice = 1;
	/** 告警提示音的音调 / 音量 × */
	float AlertPitch = 1.f;
	float AlertGain = 1.f;
};

namespace MechHudTheme
{
	/** 现在用的那一套（各处保存它成员的引用即可跟着变） */
	const FMechHudTheme& Get();
	/** 四套预设 */
	const FMechHudTheme& Preset(EMechHudThemeId Id);
	/** 换一整套（拷进 Get() 那份对象） */
	void Use(EMechHudThemeId Id);
	EMechHudThemeId Current();
	/** 制造商 → 主题（回收联合还没有，用联赛标准） */
	EMechHudThemeId ForMaker(EMechMaker Maker);
	/** 选哪套：测试覆盖 > 命令行 -MechHudTheme= > 设置里固定的那套 > 自己操控的机体按框架制造商 > 观战 / 没有机体用联赛标准 */
	EMechHudThemeId Resolve(const AMechCharacter* Focus, bool bOwnMech);
	/** 测试（-MechHudThemeShots）逐套切：-1 = 不覆盖 */
	void SetTestOverride(int32 Id);
	const TCHAR* Code(EMechHudThemeId Id);
	/** STD / NEC / VHI / PHASE（不分大小写；也认 Standard / NewEra / Victor / Phase） */
	bool Parse(const FString& S, EMechHudThemeId& Out);
}

/** -MechHudThemeShots（测试场 /Game/Maps/L_Range?Range）：同一状态下四套主题轮流截图到 Saved/Screenshots/HudTheme/<标签>/，
 *  状态依次是 交战中 / 低装甲告警 / 被锁定 / 开镜 / 近战姿态，日志 [hudtheme]，拍完退出。实现在 MechHudThemeTest.cpp，由 AMechHUD::DrawHUD 每帧调 */
namespace MechHudThemeTest
{
	void Tick(AMechHUD& Hud, AMechCharacter* Me, float Dt);
}
