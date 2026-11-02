// 战斗 HUD（代码画，不需要 UI 资源）：科幻简约仪表盘风格，只用细线、等宽数字和一套颜色（青 = 自己的状态，橙 = 提醒，红 = 敌人，蓝 = 队友），
// 不画实心底板、不写提示句子，状态全靠颜色、形状和数字。每帧在 DrawHUD 里按视口像素坐标算好要画的东西，
// 记进 FMechHudFrame，由 Slate 画层 SMechHudOverlay 画出来（线抗锯齿、文字清楚；Canvas 直接画线和三角形不抗锯齿）。
// 准星只留点和刻度；战斗时要盯的数值（AP、能量、弹药 / 装填、导弹冷却、还能闪几次、失衡）收成准星下方一条细条，平时半透明，
// 数值一变或进入冷却时全亮。次要信息在底部仪表盘（速度表、AP / 能量读数、武器读数、姿态）。
// 上方正中比分条，右上信息区（11-01 起：击毁 + 队友技能 / 插件 / 标点，MechHudFeed.cpp），锁定框、敌我标记、屏幕外箭头、受击方向弧、命中提示，
// 中键标点和轮盘（MechHudPing.cpp），按住 Tab 的计分板。
// 机体头顶名字、左上角小地图 / 按住 M 的大地图（场地俯视图开局拍一次）在 MechMinimap.cpp。
// 菜单打开（暂停、结算）或主菜单模式时不画。观战（自动对战）时显示被观战那台的信息。
// 10-31 起颜色、线宽、条形厚度、字体、数字风格、装饰按 HUD 主题走（MechHudTheme.h）：本机框架是哪家造的就用哪家的界面
// （新纪建设 / 维克托重工 / 相位电子），观战用联赛标准（就是原来这套青色）。同一套布局，只换参数和少量专属装饰。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MechTypes.h"
#include "MechHUD.generated.h"

class AMechCharacter;
class UFont;
class USoundBase;
struct FMechHudFrame;
struct FCanvasUVTri;
class SMechHudOverlay;
struct FSlateBrush;
struct FMechMapIcon;
class UMechHudAlerts;

UCLASS()
class AMechHUD : public AHUD
{
	GENERATED_BODY()

	/** 战机式告警（MechHudAlerts.cpp）借这里的画图小工具 */
	friend class UMechHudAlerts;

public:
	AMechHUD();

	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 这一帧要画的东西（Slate 画层照着画）和画层本身 */
	TSharedPtr<FMechHudFrame> HudFrame;
	TSharedPtr<SMechHudOverlay> HudOverlay;
	/** 记一个矩形 / 一条线（和 AHUD 的同名，HUD 里原来的调用不用改，画到 Slate 画层上） */
	void DrawRect(FLinearColor Color, float X, float Y, float W, float H);
	void DrawLine(float X0, float Y0, float X1, float Y1, FLinearColor Color, float Thick = 1.f);
	/** 记一串连着的线（圆弧） */
	void Polyline(const TArray<FVector2f>& Points, const FLinearColor& Color, float Thick);
	/** 记一组三角形（每个顶点自己的颜色，狙击镜压暗、箭头用） */
	void AddTris(const TArray<FCanvasUVTri>& Tris);

	AMechCharacter* GetFocusMech() const;

	// 画图小工具（坐标、尺寸都已乘 UI 缩放；字号按 1080p 写）
	void Text(const FString& S, float X, float Y, float Size, const FLinearColor& Color, float AlignX = 0.f);
	FVector2D Measure(const FString& S, float Size);
	/** HUD 字号（按 1080p 写）换成当前分辨率下的 Slate 字号；大字用粗体 */
	FSlateFontInfo HudFont(float Size) const;
	void Panel(float X, float Y, float W, float H, float Alpha = 1.f);
	void Bar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, const FLinearColor& Back = FLinearColor(0.02f, 0.03f, 0.04f, 0.6f));
	/** 分段条：每段之间留一道缝 */
	void SegBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Fill, int32 Segments);
	void Box(float X, float Y, float W, float H, const FLinearColor& Color, float Thick);
	void Brackets(float CX, float CY, float Half, float Len, const FLinearColor& Color, float Thick);
	/** 圆弧：角度从正上方顺时针（弧度） */
	void Arc(float CX, float CY, float R, float From, float To, const FLinearColor& Color, float Thick);
	/** 抗锯齿的线（斜线用；横平竖直的线 DrawLine 就够了） */
	void SoftLine(float X0, float Y0, float X1, float Y1, const FLinearColor& Color, float Thick);
	void Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);

	// ---------------------------------------------------------------- 主题（10-31，MechHudTheme.h：HUD 跟着本机框架的制造商走）
	/** 每帧按看的机体选主题（自己操控的 = 框架制造商；观战 / 没有机体 = 联赛标准；命令行 -MechHudTheme= 强制），换了记一行日志 */
	void UpdateTheme(AMechCharacter* Mech);
	int32 ThemeShown = -1;
	/** 主要读数：主题要七段数码管、且只含数字和 - : . / % 空格时画七段，否则照常写字（参数和 Text 一样） */
	void Readout(const FString& S, float X, float Y, float Size, const FLinearColor& Color, float AlignX = 0.f);
	FVector2D MeasureReadout(const FString& S, float Size);
	/** 七段数码管（新纪）：Y 是和 Text 一样的文字框上沿 */
	void SegDigits(const FString& S, float X, float Y, float Size, const FLinearColor& Color);
	FVector2D MeasureSeg(const FString& S, float Size);
	static bool CanSegment(const FString& S);
	/** 整数读数：主题要补零（维克托）时补到 Width 位 */
	FString Pad(int32 Value, int32 Width) const;
	/** 黄黑警示斜纹（新纪）：矩形里一道道斜着的黄条，暗底 */
	void Hazard(float X, float Y, float W, float H, float Alpha);
	/** 刻度尺（维克托）：X 起 W 宽，从 Y 往下 N 格短刻度，每 Major 格长一点 */
	void Ruler(float X, float Y, float W, int32 N, int32 Major, const FLinearColor& Color);
	/** 分段条（新纪）：在横条上按 N 格切出缝 */
	void Cuts(float X, float Y, float W, float H, int32 N);
	/** 主题专属装饰：维克托的方位刻度带、相位的四角极简细线框（新纪的警示斜纹画在仪表盘和倒计时横幅里） */
	void DrawDecor(AMechCharacter* Mech);

	void DrawViewer(AMechCharacter* Mech);
	void DrawSelf(AMechCharacter* Mech);
	void DrawAllies(AMechCharacter* Mech);
	/** 准星下方的细条：AP、能量、弹药 / 装填 / 匕首、导弹冷却、还能闪几次、失衡、交芯读条 */
	void DrawStrip(AMechCharacter* Mech);
	/** 底部仪表盘：速度表、AP / 能量读数、武器读数、姿态 */
	void DrawDash(AMechCharacter* Mech);
	/** 肩部的紧急脱离等四件（实现在 MechTacticalHUD.cpp；11-01 并入肩部，冷却和 E 的图标就是肩部那一格，这里不再单独占位）：
	 *  DrawTactical = 震荡波蓄力时准星外一圈；DrawTacticalIcon = 这四件的图标（X、Y 是中心，S 是半径）；DrawTacticalMines = 地雷还有几颗（小格，从 X 往右排） */
	void DrawTactical(AMechCharacter* Mech);
	void DrawTacticalIcon(EMechShoulder Type, float X, float Y, float S, const FLinearColor& C);
	void DrawTacticalMines(AMechCharacter* Mech, float X, float Y, const FLinearColor& On);
	void DrawReticle(AMechCharacter* Mech);
	/** 狙击镜：镜框外压暗、细十字和刻度、距离、还能打几发、上膛进度 */
	void DrawScope(AMechCharacter* Mech);
	/** 伤害飘字 */
	void DrawPopups(AMechCharacter* Mech);
	void DrawLockBox(AMechCharacter* Mech);
	void DrawMarkers(AMechCharacter* Mech);
	/** 插件图标：六边形 + 里面的小图形（每种插件一个颜色和图形，不写字） */
	void PluginIcon(float X, float Y, float R, FName Id, float Alpha, float Thick = 1.5f);
	/** 场上的插件舱：六边形标记 + 距离（屏幕外贴边）；刚拿到插件时准星下方闪一下它的图标 */
	void DrawPods(AMechCharacter* Mech);
	TArray<FName> SeenEffects;
	FName FlashEffect;
	float FlashStart = -100.f;
	void DrawOffscreen(AMechCharacter* Mech);
	void DrawDamage(AMechCharacter* Mech);
	/** 受击方向弧：每个来源一条，各自淡出（11-02） */
	struct FDamageArc
	{
		TWeakObjectPtr<AActor> Source;
		FVector WorldDir = FVector::ForwardVector;  // 从我指向来源的水平方向（来源没了就用它）
		float Time = -100.f;
		int32 Tier = 0;
	};
	TArray<FDamageArc> DamageArcs;
	float LastDamageSeen = -100.f;
	void DrawRound();
	/** 右上信息区（11-01 起接管原来的击毁通报，实现在 MechHudFeed.cpp）：击毁、队友用技能 / 拿插件 / 标点、夺芯、保卫战，
	 *  图标为主、字为辅，新的在上，几秒后淡出；设置里关掉（FMechSettings::bShowFeed）整块不画 */
	void DrawFeed();
	/** 信息区的小图标：武器、技能（MechFeedIcon）；X / Y 是中心，S 是半高 */
	void FeedIcon(uint8 Icon, float X, float Y, float S, const FLinearColor& C);
	// ---------------------------------------------------------------- 标点（11-01，实现在 MechHudPing.cpp）
	/** 自己和队友的标点：图标 + 距离 + 谁标的（小字），屏幕外贴边 + 小箭头；刚放下时一圈扩散，最后一秒淡出 */
	void DrawPings(AMechCharacter* Mech);
	/** 按住标点键时正中的轮盘 */
	void DrawPingWheel();
	/** 标点图标（一种标点一个几何图形；S 是半径） */
	void PingGlyph(uint8 Kind, float X, float Y, float S, const FLinearColor& C, float Thick = 1.5f);
	/** 地图上的标点（GatherMapIcons 调） */
	void GatherPingIcons(AMechCharacter* Mech, TArray<FMechMapIcon>& Out);
	void DrawScoreboard();
	/** 夺芯：核心、回收站标记，带芯 / 阵亡等救的提示 */
	void DrawCore(AMechCharacter* Mech);
	/** 保卫战（10-31，实现在 MechHudDefense.cpp）：波次条、剩余敌人、指挥所血条、准备倒计时、杂兵头顶的红菱形、胜负 */
	void DrawDefense(AMechCharacter* Mech);
	/** 保卫战肉鸽成长（11-01，实现在 MechHudRun.cpp）：等级和经验条、下方的选卡、按住 Tab 看已拿到的卡、导弹锁杂兵的框 */
	void DrawRun(AMechCharacter* Mech);
	/** 蜂群全锁的框（0.2 秒查一次视野） */
	TArray<TWeakObjectPtr<AActor>> RunMarks;
	float RunMarksTime = -100.f;
	TWeakObjectPtr<class AMechDefenseDirector> DefenseDirector;
	// ---------------------------------------------------------------- 死斗（实现在 MechHudModes.cpp）
	/** 上方比分条：团队 = 两队击毁数 + 进度线 + 目标；个人 = 左边自己的击毁和名次、右边第一名（自己领先时是第二名）的击毁 */
	void DrawDMRound();
	/** 阵亡时正中的重生读条、自己的重生保护圈、敌人头顶的保护六边形 */
	void DrawDeathmatch(AMechCharacter* Mech);
	/** 按住 Tab：每人击毁 / 阵亡 / 输出（个人按击毁排名次，团队按队分组） */
	void DrawDMScoreboard();
	/** 第一人称：启动画面（黑屏 → 扫描线 → 自检日志 → 战斗系统上线）、传感器边框、受击雪花 */
	void DrawBoot(AMechCharacter* Mech, float T);
	void DrawVisor(AMechCharacter* Mech);
	/** 启动时各块 HUD 按顺序亮起：所有绘制的透明度都乘它 */
	float HudAlpha = 1.f;
	// ---------------------------------------------------------------- 地图系统和机体名字（实现在 MechMinimap.cpp）
	/** 机体头顶名字：队友始终显示（隔墙小一点、淡一点），敌人只在看得见、150 米内、没被烟挡住时显示；随距离变小变淡，
	 *  开镜时隐藏，压在准星附近时变淡，不画自己 */
	void DrawNames(AMechCharacter* Mech);
	/** 地图：左上角随视角旋转的圆形小地图；按住地图键全屏北向上的大地图（网格坐标、比例尺、队友名字）。所有地图、所有模式都用 */
	void DrawMap(AMechCharacter* Mech);
	void DrawMinimap(AMechCharacter* Mech, const TArray<FMechMapIcon>& Icons, float Alpha);
	void DrawBigMap(AMechCharacter* Mech, const TArray<FMechMapIcon>& Icons, float Alpha);
	/** 测试：-MechMapTest 开打后按时间轮流截小地图 / 大地图（Saved/Screenshots/Map） */
	void MapTestTick();
	float MapTestStart = -1.f;
	int32 MapTestShot = 0;
	/** 开局拍一次场地俯视图（每帧拍一块，拍完在后台线程处理成地图贴图）；菜单 / 选配阶段也照常推进 */
	void UpdateMapCapture();
	/** 地图键（现在是 M；操作代理的按键表做好后只改这里） */
	bool IsMapKeyDown() const;
	/** 记一组带贴图的三角形（每三个顶点一个三角形；颜色乘在贴图上） */
	void AddImageTris(const TSharedPtr<FSlateBrush>& Brush, const TArray<FVector2f>& Pos, const TArray<FVector2f>& UV, const TArray<FLinearColor>& Colors);
	/** 这一帧地图上要画的图标（小地图、大地图共用一份） */
	void GatherMapIcons(AMechCharacter* Mech, TArray<struct FMechMapIcon>& Out);
	void DrawMapIcon(const struct FMechMapIcon& Icon, const FVector2D& P, float ScreenYaw, float Scale, float Alpha, bool bBig);
	/** 敌人在地图上的显示：自己或队友看得见、离队友 35 米内、刚被打中都算「被发现」，之后在最后位置淡出 */
	void UpdateSpotting(AMechCharacter* Mech, int32 MyTeam);
	struct FMapSpot
	{
		FVector Pos = FVector::ZeroVector;
		float Yaw = 0.f;
		float Time = -100.f;
	};
	TMap<TWeakObjectPtr<AMechCharacter>, FMapSpot> MapSpots;
	float MapSpotClock = 0.f;
	/** 名字：每台机体的显示透明度（平滑淡入淡出）和上一次视线检查的结果 */
	struct FNameVis
	{
		float Alpha = 0.f;
		bool bClear = false;
		float NextCheck = 0.f;
	};
	TMap<TWeakObjectPtr<AMechCharacter>, FNameVis> NameVis;
	/** 场地俯视图（开局拍一次） */
	UPROPERTY()
	TObjectPtr<class UMechMapCapture> MapCapture;
	float MapCaptureWait = 0.f;
	/** 大地图展开过渡（0–1） */
	float BigMapBlend = 0.f;
	/** 阶段切换的提示音（菜单开着也要照常判断） */
	void UpdatePhaseSounds();

	UPROPERTY()
	TObjectPtr<USoundBase> LockSound;

	UPROPERTY()
	TObjectPtr<USoundBase> RoundStartSound;

	UPROPERTY()
	TObjectPtr<USoundBase> WinSound;

	UPROPERTY()
	TObjectPtr<USoundBase> LoseSound;

	float UI = 1.f;
	bool bWasLocked = false;
	/** 细条：最后一次有变化（掉血、耗能、开火、冷却中……）的时间，过一会儿没变化就暗下去 */
	float StripHot = -100.f;
	float PrevAPFrac = -1.f;
	float PrevENFrac = -1.f;
	int32 PrevAmmo = -1;
	bool bPrevBlade = false;
	/** AP 条掉血时先留一截浅色，再追上来 */
	float APTrail = 1.f;
	float APTrailHold = 0.f;
	/** 狙击开镜的过渡（0–1） */
	float ScopeBlend = 0.f;
	EMechRoundPhase LastPhase = EMechRoundPhase::Waiting;

	/** 战机式告警（10-31）：被锁定 / 导弹来袭 / 低装甲 / 系统告警的声音和图形，受击抖动、EMP 干扰、上电 / 断电（MechHudAlerts.h） */
	UPROPERTY()
	TObjectPtr<UMechHudAlerts> Alerts;
};
