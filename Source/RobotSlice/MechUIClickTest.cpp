// 菜单点击测试（11-01）：走 Slate 真正的鼠标事件把菜单点一遍。MechUIKit 的按钮替掉了 SButton，房间流程以前的测试又都是直接调控制器函数，
// 没人真的点过界面，所以这里按「屏幕上的字」找控件、算出中心、发鼠标移动 / 按下 / 松开（和真人点击走同一条路，点不到就是真的点不到）。
// 路径：主菜单 → 加入页 → 设置五个页签（配色、音乐音量滑条、HUD 主题、画面设置的未应用 / 撤销 / 应用、改键进等待再取消）→ 开房 → 房间里改选项 →
//   开始 → 禁用 / 选配 → 开局后暂停页「回房间」→ 回到房间。每一项日志 [uiclick] 记通过 / 失败，最后一行总结；截图到 Saved/Screenshots/UIClick。
// 启动：RobotSlice.uproject "/Game/Maps/L_Street" -game -UIClickTest -RenderOffscreen -windowed -ForceRes -ResX=1920 -ResY=1080
// 开关不能以 -Mech 开头（那样不进主菜单）。会改写设置（滑条松手、配色、开始时存房间选择），跑之前备份 GameUserSettings.ini，跑完还原。
// 事件放在 FTSTicker 里发（不在控件的 Tick 里：那是 Slate 绘制的过程中，发事件会重入）。

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechRoom.h"
#include "MechSettings.h"
#include "MechUI.h"
#include "MechUIKit.h"
#include "MechMaps.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace MechUIClickTestPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的小函数撞
{
	struct FStep
	{
		FString Name;
		float Timeout;
		/** 每帧调，返回 true 这一步结束（超时算失败，接着走下一步） */
		TFunction<bool()> Run;
	};
	TArray<FStep> Steps;
	int32 Cur = 0;
	float StepClock = 0.f;
	float Settle = 0.f;
	int32 Passed = 0;
	int32 Failed = 0;
	bool bDone = false;
	/** 选配那一步的小状态：截过图没有、上次动手的时间 */
	bool bDraftShot = false;
	int32 DraftShotWait = 0;
	bool bBanReadyShot = false;
	int32 AIsBefore = 0;
	bool bPickShot = false;
	float ActClock = 0.f;

	UWorld* GameWorld()
	{
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World() && Ctx.World()->IsGameWorld())
				{
					return Ctx.World();
				}
			}
		}
		return nullptr;
	}

	AMechPlayerController* PC()
	{
		UWorld* W = GameWorld();
		return W ? Cast<AMechPlayerController>(W->GetFirstPlayerController()) : nullptr;
	}

	AMechGameState* GS()
	{
		UWorld* W = GameWorld();
		return W ? W->GetGameState<AMechGameState>() : nullptr;
	}

	void Check(bool bOk, const FString& What)
	{
		(bOk ? Passed : Failed)++;
		UE_LOG(LogTemp, Display, TEXT("[uiclick] %s：%s"), bOk ? TEXT("通过") : TEXT("失败"), *What);
	}

	void Shot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/UIClick") / FString::Printf(TEXT("%s.png"), Name), true, false);
	}

	/** 走一遍现在显示着的控件（藏起来的跳过；页面切换器只走当前页） */
	void Walk(const TSharedRef<SWidget>& W, TFunctionRef<void(const TSharedRef<SWidget>&)> Visit)
	{
		if (!W->GetVisibility().IsVisible())
		{
			return;
		}
		Visit(W);
		static const FName SwitcherType(TEXT("SWidgetSwitcher"));
		if (W->GetType() == SwitcherType)
		{
			if (const TSharedPtr<SWidget> Active = StaticCastSharedRef<SWidgetSwitcher>(W)->GetActiveWidget())
			{
				Walk(Active.ToSharedRef(), Visit);
			}
			return;
		}
		if (FChildren* Children = W->GetChildren())
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				Walk(Children->GetChildAt(i), Visit);
			}
		}
	}

	void WalkAll(TFunctionRef<void(const TSharedRef<SWidget>&)> Visit)
	{
		for (const TSharedRef<SWindow>& Win : FSlateApplication::Get().GetTopLevelWindows())
		{
			Walk(Win, Visit);
		}
	}

	/** 屏幕上字完全等于 Text 的文字控件（bPrefix：开头是 Text）；有几个取第一个：底栏的 Esc 提示也写着「返回」，排在页面内容后面 */
	TSharedPtr<SWidget> FindText(const FString& Text, bool bPrefix = false)
	{
		static const FName TextType(TEXT("STextBlock"));
		TSharedPtr<SWidget> Found;
		WalkAll([&](const TSharedRef<SWidget>& W) {
			if (W->GetType() == TextType)
			{
				const FString S = StaticCastSharedRef<STextBlock>(W)->GetText().ToString();
				if (!Found.IsValid() && (bPrefix ? S.StartsWith(Text) : S == Text))
				{
					Found = W;
				}
			}
		});
		return Found;
	}

	/** 只找卡片那一行的字（11-01 顶部顺序条、底部配装栏也写武器名，取第一个会点到顺序条）：窗口高度 45%–90% 之间 */
	TSharedPtr<SWidget> FindCardText(const FString& Text)
	{
		static const FName TextType(TEXT("STextBlock"));
		TSharedPtr<SWidget> Found;
		WalkAll([&](const TSharedRef<SWidget>& W) {
			if (W->GetType() == TextType && !Found.IsValid() && StaticCastSharedRef<STextBlock>(W)->GetText().ToString() == Text)
			{
				const FSlateRect R = W->GetTickSpaceGeometry().GetLayoutBoundingRect();
				const TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(W);
				const float H = Win.IsValid() ? Win->GetSizeInScreen().Y : 1.f;
				const float Y = (R.Top + R.Bottom) * 0.5f / H;
				if (Y > 0.45f && Y < 0.9f)
				{
					Found = W;
				}
			}
		});
		return Found;
	}

	FVector2D CenterOf(const TSharedRef<SWidget>& W, float FractionX = 0.5f)
	{
		const FSlateRect R = W->GetTickSpaceGeometry().GetLayoutBoundingRect();
		return FVector2D(R.Left + (R.Right - R.Left) * FractionX, (R.Top + R.Bottom) * 0.5f);
	}

	void MoveTo(const FVector2D& Pos, const TSet<FKey>& Held = TSet<FKey>())
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FVector2D Last = App.GetCursorPos();
		App.SetCursorPos(Pos);  // -RenderOffscreen 下是空平台的光标，不会动用户的真鼠标
		App.ProcessMouseMoveEvent(FPointerEvent(0, Pos, Last, Held, EKeys::Invalid, 0.f, FModifierKeysState()));
	}

	void Press(const FVector2D& Pos)
	{
		TSet<FKey> Left;
		Left.Add(EKeys::LeftMouseButton);
		FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Pos, Pos, Left, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	}

	void Release(const FVector2D& Pos)
	{
		FSlateApplication::Get().ProcessMouseButtonUpEvent(FPointerEvent(0, Pos, Pos, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
	}

	void ClickAt(const FVector2D& Pos)
	{
		MoveTo(Pos);
		Press(Pos);
		Release(Pos);
	}

	/** 点屏幕上这几个字；找不到返回 false */
	bool ClickText(const FString& Text)
	{
		const TSharedPtr<SWidget> W = FindText(Text);
		if (!W.IsValid())
		{
			return false;
		}
		ClickAt(CenterOf(W.ToSharedRef()));
		return true;
	}

	// ---------------------------------------------------------------- 写步骤用的几种
	void Add(const FString& Name, float Timeout, TFunction<bool()> Run)
	{
		Steps.Add({Name, Timeout, MoveTemp(Run)});
	}

	/** 等屏幕上出现这几个字（换页、换图） */
	void WaitText(const FString& Text, float Timeout = 8.f)
	{
		Add(FString::Printf(TEXT("等「%s」"), *Text), Timeout, [Text]() { return FindText(Text).IsValid(); });
	}

	/** 点这几个字，再等 Expect 出现才算通过 */
	void ClickExpect(const FString& Text, const FString& Expect)
	{
		Add(FString::Printf(TEXT("点「%s」→ 出现「%s」"), *Text, *Expect), 6.f, [Text, Expect, bClicked = false]() mutable {
			if (!bClicked)
			{
				if (!ClickText(Text))
				{
					return false;
				}
				bClicked = true;
			}
			return FindText(Expect).IsValid();
		});
	}

	/** 点这几个字，再等条件成立才算通过 */
	void ClickCheck(const FString& Text, const FString& What, TFunction<bool()> Cond)
	{
		Add(FString::Printf(TEXT("点「%s」→ %s"), *Text, *What), 6.f, [Text, Cond, bClicked = false]() mutable {
			if (!bClicked)
			{
				if (!ClickText(Text))
				{
					return false;
				}
				bClicked = true;
			}
			return Cond();
		});
	}

	void AddShot(const TCHAR* Name)
	{
		Add(FString::Printf(TEXT("截图 %s"), Name), 2.f, [Name, Clock = 0.f]() mutable {
			if (Clock <= 0.f)
			{
				Shot(Name);
			}
			Clock += FApp::GetDeltaTime();
			return Clock > 0.4f;  // 等这张存完（截图要到下一帧才拍）
		});
	}

	void Build()
	{
		// ---- 主菜单、加入页
		WaitText(TEXT("开房"), 60.f);
		Add(TEXT("等场景稳定"), 8.f, [Clock = 0.f]() mutable {
			Clock += FApp::GetDeltaTime();
			return Clock > 5.f;
		});
		AddShot(TEXT("01_主菜单"));
		Add(TEXT("鼠标移到「加入」上（悬停）"), 2.f, [Clock = 0.f]() mutable {
			if (const TSharedPtr<SWidget> W = FindText(TEXT("加入")))
			{
				if (Clock <= 0.f)
				{
					MoveTo(CenterOf(W.ToSharedRef()));
				}
				Clock += FApp::GetDeltaTime();
			}
			return Clock > 0.5f;
		});
		AddShot(TEXT("02_主菜单_悬停"));
		ClickExpect(TEXT("加入"), TEXT("JOIN ROOM"));
		AddShot(TEXT("03_加入"));
		ClickExpect(TEXT("返回"), TEXT("开房"));

		// ---- 设置：五个页签各点一下
		ClickExpect(TEXT("设置"), TEXT("SETTINGS"));
		ClickExpect(TEXT("画面"), TEXT("垂直同步"));
		AddShot(TEXT("04_设置_画面"));
		ClickExpect(TEXT("声音"), TEXT("音乐音量"));
		AddShot(TEXT("05_设置_声音"));
		ClickExpect(TEXT("操作"), TEXT("鼠标灵敏度"));
		AddShot(TEXT("06_设置_操作"));
		ClickExpect(TEXT("界面"), TEXT("菜单配色"));
		AddShot(TEXT("07_设置_界面"));
		ClickExpect(TEXT("按键"), TEXT("前进"));
		AddShot(TEXT("08_设置_按键"));

		// 改键：点一行进入等新键，Esc 取消
		ClickExpect(TEXT("前进"), TEXT("按任意键…"));
		Add(TEXT("改键等新键时按 Esc 取消"), 4.f, [bSent = false]() mutable {
			if (!bSent)
			{
				bSent = true;
				FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
			}
			return !FindText(TEXT("按任意键…")).IsValid() && FindText(TEXT("前进")).IsValid();
		});

		// 配色：点琥珀 → 主题真的换了、存进设置；截一张；换回原来的
		ClickExpect(TEXT("界面"), TEXT("菜单配色"));
		ClickCheck(TEXT("琥珀"), TEXT("强调色换成琥珀、设置里 MenuPalette = 1"), []() {
			return FMechSettings::Get().MenuPalette == 1 && MechUITheme::Get().Accent.Equals(MechUITheme::Preset(1).Accent);
		});
		AddShot(TEXT("09_设置_配色琥珀"));
		ClickCheck(TEXT("青色"), TEXT("强调色换回青色"), []() {
			return FMechSettings::Get().MenuPalette == 0 && MechUITheme::Get().Accent.Equals(MechUITheme::Preset(0).Accent);
		});
		// HUD 主题：固定一套、再换回跟随机体（只看设置值；战斗里的效果 MechHudTheme::Resolve 每帧读）
		ClickCheck(TEXT("新纪建设"), TEXT("HudTheme = 1"), []() { return FMechSettings::Get().HudTheme == 1; });
		ClickCheck(TEXT("跟随机体"), TEXT("HudTheme = -1"), []() { return FMechSettings::Get().HudTheme == -1; });

		// 音乐音量滑条：第二根；拖到 60%，松手后值变了、写进 ini
		ClickExpect(TEXT("声音"), TEXT("音乐音量"));
		Add(TEXT("拖音乐音量滑条到 60%"), 6.f, [Stage = 0, Start = 0.f]() mutable {
			static const FName SliderType(TEXT("SMechKitSlider"));
			TArray<TSharedRef<SWidget>> Sliders;
			WalkAll([&](const TSharedRef<SWidget>& W) {
				if (W->GetType() == SliderType)
				{
					Sliders.Add(W);
				}
			});
			if (Sliders.Num() != 3)
			{
				return false;  // 声音页应该有三根：主音量、音乐音量、告警音量
			}
			const TSharedRef<SWidget>& Music = Sliders[1];
			const FVector2D From = CenterOf(Music, 0.2f);
			const FVector2D To = CenterOf(Music, 0.6f);
			TSet<FKey> Left;
			Left.Add(EKeys::LeftMouseButton);
			switch (Stage++)
			{
			case 0:
				Start = FMechSettings::Get().MusicVolume;
				MoveTo(From);
				Press(From);
				return false;
			case 1:
				MoveTo(To, Left);
				return false;
			case 2:
				Release(To);
				return false;
			default:
			{
				float Saved = -1.f;
				GConfig->GetFloat(TEXT("MechSettings"), TEXT("MusicVolume"), Saved, GGameUserSettingsIni);
				const float Now = FMechSettings::Get().MusicVolume;
				UE_LOG(LogTemp, Display, TEXT("[uiclick] 音乐音量 %.2f → %.2f，ini 里 %.2f"), Start, Now, Saved);
				return FMath::IsNearlyEqual(Now, 0.6f, 0.03f) && FMath::IsNearlyEqual(Saved, Now, 0.01f);
			}
			}
		});
		AddShot(TEXT("10_设置_音乐音量拖过"));

		// 画面设置：改了标「未应用」、撤销回去；再改一次点应用，引擎的值变了、标记消失
		ClickExpect(TEXT("画面"), TEXT("垂直同步"));
		ClickExpect(TEXT("120"), TEXT("未应用"));
		AddShot(TEXT("11_设置_未应用"));
		ClickCheck(TEXT("撤销"), TEXT("「未应用」消失"), []() { return !FindText(TEXT("未应用")).IsValid(); });
		ClickExpect(TEXT("144"), TEXT("未应用"));
		ClickCheck(TEXT("应用"), TEXT("「未应用」消失、帧率上限 = 144"), []() {
			const UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr;
			return !FindText(TEXT("未应用")).IsValid() && S && FMath::IsNearlyEqual(S->GetFrameRateLimit(), 144.f);
		});
		ClickExpect(TEXT("返回"), TEXT("开房"));

		// ---- 开房 → 房间里改选项（真实点击，看 GameState::Room 变了没有）
		ClickCheck(TEXT("开房"), TEXT("进了房间图"), []() {
			const AMechGameState* G = GS();
			return G && G->bRoomMode && FindText(TEXT("开始")).IsValid();
		});
		Steps.Last().Timeout = 90.f;
		Add(TEXT("房间页稳定"), 4.f, [Clock = 0.f]() mutable {
			Clock += FApp::GetDeltaTime();
			return Clock > 1.5f;
		});
		AddShot(TEXT("20_房间_默认配色"));
		// 房间页外观没改（旧控件），看新配色放在一起有没有看不清的：五套各一张（只在内存里换）
		for (int32 p = 1; p < MechUITheme::NumPresets; ++p)
		{
			Add(FString::Printf(TEXT("房间页换配色 %d"), p), 1.f, [p]() {
				MechKit::ApplyPalette(p);
				return true;
			});
			static const TCHAR* Names[] = {TEXT(""), TEXT("21_房间_琥珀"), TEXT("22_房间_冷白"), TEXT("23_房间_赤红"), TEXT("24_房间_荧绿")};
			AddShot(Names[p]);
		}
		Add(TEXT("房间页换回设置里的配色"), 1.f, []() {
			MechKit::ApplyPalette(FMechSettings::Get().MenuPalette);
			return true;
		});
		ClickCheck(TEXT("相位电子园区"), TEXT("地图 = 相位电子园区"), []() { return GS() && GS()->Room.Map == MechMaps::FindByMapName(TEXT("L_TechPark")); });
		ClickCheck(TEXT("死斗"), TEXT("模式 = 死斗"), []() { return GS() && GS()->Room.Rules.Mode == EMechMode::Deathmatch; });
		ClickCheck(TEXT("团队淘汰"), TEXT("模式 = 团队淘汰"), []() { return GS() && GS()->Room.Rules.Mode == EMechMode::Elimination; });
		ClickCheck(TEXT("赛博朋克街道"), TEXT("地图 = 街道（序号 0，进图快）"), []() { return GS() && GS()->Room.Map == 0; });
		// 11-02 阵容：加一台人机、改强度、移除
		Add(TEXT("记下人机台数"), 1.f, []() { AIsBefore = GS() ? GS()->Room.AIs.Num() : 0; return true; });
		ClickCheck(TEXT("+ 加人机"), TEXT("多一台人机"), []() { return GS() && GS()->Room.AIs.Num() == AIsBefore + 1; });
		AddShot(TEXT("25a_房间_加了人机"));
		ClickCheck(TEXT("›"), TEXT("第一台人机换了强度"), []() { return GS() && GS()->Room.AIs.Num() > 0 && GS()->Room.AIs[0].Level != 1; });
		ClickCheck(TEXT("×"), TEXT("移除一台人机"), []() { return GS() && GS()->Room.AIs.Num() == AIsBefore; });
		// 11-02 房间页一屏放下，没有滚动了；禁用阶段是开关（字写着现在的状态）
		// 用户存的设置里禁用阶段可能是关的：先归到已知状态
		Add(TEXT("禁用阶段先开、挑战倍数先关、友伤先关"), 3.f, []() {
			AMechGameState* G = GS();
			UWorld* W = GameWorld();
			AMechPlayerController* PC = W ? Cast<AMechPlayerController>(W->GetFirstPlayerController()) : nullptr;
			if (!G || !PC)
			{
				return false;
			}
			FMechRoomSettings R = G->Room;
			R.bBans = true;
			R.bChallenge = false;
			R.bFriendlyFire = false;
			PC->LobbySetRoom(R);
			return G->Room.bBans && !G->Room.bChallenge && !G->Room.bFriendlyFire;
		});
		ClickCheck(TEXT("开"), TEXT("禁用阶段关"), []() { return GS() && !GS()->Room.bBans; });
		ClickCheck(TEXT("关"), TEXT("禁用阶段开"), []() { return GS() && GS()->Room.bBans; });
		ClickCheck(TEXT("关"), TEXT("挑战倍数开"), []() { return GS() && GS()->Room.bChallenge; });
		ClickCheck(TEXT("自定义"), TEXT("倍数自定义"), []() { return GS() && GS()->Room.bChallengeCustom; });
		AddShot(TEXT("25b_房间_挑战倍数自定义"));
		ClickCheck(TEXT("推荐"), TEXT("倍数用推荐"), []() { return GS() && !GS()->Room.bChallengeCustom; });
		ClickCheck(TEXT("加入红队"), TEXT("自己换到红队"), []() { return GS() && GS()->Lobby.Num() > 0 && GS()->Lobby[0].Team == 1; });
		ClickCheck(TEXT("加入蓝队"), TEXT("自己换回蓝队"), []() { return GS() && GS()->Lobby.Num() > 0 && GS()->Lobby[0].Team == 0; });
		ClickCheck(TEXT("训练场"), TEXT("地图 = 训练场"), []() { return GS() && MechRoom::IsTraining(GS()->Room); });
		ClickCheck(TEXT("关"), TEXT("友伤开"), []() { return GS() && GS()->Room.bFriendlyFire; });
		AddShot(TEXT("25c_房间_训练场友伤"));
		ClickCheck(TEXT("赛博朋克街道"), TEXT("地图 = 街道，回到对战"), []() { return GS() && GS()->Room.Map == 0; });
		AddShot(TEXT("25_房间_改过选项"));

		// ---- 开始 → 进图 → 禁用 / 选配（轮到自己时点卡片、点确认）
		ClickCheck(TEXT("开始"), TEXT("换到对战图（从房间开的场）"), []() {
			const AMechGameState* G = GS();
			return G && G->bRoomMatch && !G->bRoomMode;
		});
		Steps.Last().Timeout = 90.f;
		Add(TEXT("禁用 / 选配：轮到自己时点卡片、点确认"), 150.f, []() {
			const AMechGameState* G = GS();
			if (!G || !G->bRoomMatch)
			{
				return false;
			}
			if (G->Phase != EMechRoundPhase::Draft)
			{
				return G->Phase == EMechRoundPhase::Countdown || G->Phase == EMechRoundPhase::Fight;  // AI 替自己选完了也算往下走（但日志里看得出没点到）
			}
			ActClock += FApp::GetDeltaTime();
			if (ActClock < 1.f)
			{
				return false;  // 一秒动一次手，也等上一下生效
			}
			ActClock = 0.f;
			if (!bDraftShot && FindText(TEXT("选配")).IsValid() == false && FindText(TEXT("点一件对面的装备")).IsValid())
			{
				if (++DraftShotWait < 5)
				{
					return false;  // 11-01 等几秒再拍：刚进选配那一两秒小图还没贴图（贴图流送完摄影棚会补拍）
				}
				bDraftShot = true;
				Shot(TEXT("30_禁用"));
				return false;
			}
			if (FindText(TEXT("点一件对面的装备")).IsValid())
			{
				UE_LOG(LogTemp, Display, TEXT("[uiclick] 轮到自己禁用：点卡片"));
				static const TCHAR* Cards[] = {TEXT("霰弹枪"), TEXT("冲锋枪"), TEXT("狙击枪"), TEXT("步枪"), TEXT("双持手枪")};
				static int32 NextCard = 0;
				// 11-01 第二次禁用走肩部页：先点「肩部」页签，再点「地雷」（并入肩部的四件和别的肩部装备一样能禁，页签禁用轮里是亮的）
				static bool bShoulderTab = false;
				if (NextCard == 1 && !bShoulderTab)
				{
					bShoulderTab = true;
					ClickText(TEXT("肩部"));
					return false;
				}
				const TCHAR* const Want = NextCard == 1 ? TEXT("地雷") : Cards[NextCard % UE_ARRAY_COUNT(Cards)];
				++NextCard;
				if (const TSharedPtr<SWidget> Card = FindCardText(Want))  // 每次换一张：已经禁过的点了没反应
				{
					ClickAt(CenterOf(Card.ToSharedRef()));
				}
				return false;
			}
			if (const TSharedPtr<SWidget> Ban = FindText(TEXT("禁用 "), true))
			{
				if (!bBanReadyShot)  // 11-01 确认按钮亮起来（选好了、还没点）的样子拍一张，下一秒再点
				{
					bBanReadyShot = true;
					Shot(TEXT("30b_禁用_选好了"));
					return false;
				}
				static bool bShoulderBanShot = false;
				if (!bShoulderBanShot && FindText(TEXT("禁用 地雷"), true).IsValid())
				{
					bShoulderBanShot = true;
					Shot(TEXT("30c_禁用_肩部地雷"));
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("[uiclick] 点「%s」"), *StaticCastSharedRef<STextBlock>(Ban.ToSharedRef())->GetText().ToString());
				ClickAt(CenterOf(Ban.ToSharedRef()));
				return false;
			}
			if (FindText(TEXT("确认配装")).IsValid())
			{
				if (!bPickShot)  // 截图是下一帧才拍：同一步里接着点确认，拍到的就是确认之后了
				{
					bPickShot = true;
					Shot(TEXT("31_选配"));
					return false;
				}
				UE_LOG(LogTemp, Display, TEXT("[uiclick] 轮到自己选配：点「冲锋枪」再点「确认配装」"));
				if (const TSharedPtr<SWidget> Card = FindCardText(TEXT("冲锋枪")))
				{
					ClickAt(CenterOf(Card.ToSharedRef()));
				}
				ClickText(TEXT("确认配装"));
				return false;
			}
			return false;
		});
		Add(TEXT("选配结束，开局"), 90.f, []() {
			const AMechGameState* G = GS();
			return G && (G->Phase == EMechRoundPhase::Countdown || G->Phase == EMechRoundPhase::Fight);
		});

		// ---- 暂停页「回房间」
		Add(TEXT("打开暂停页"), 10.f, [bOpened = false]() mutable {
			if (AMechPlayerController* P = PC())
			{
				if (!bOpened)
				{
					bOpened = true;
					P->OpenMenu(EMechMenuPage::Pause);  // 这一下直接调（Esc 键在开局运镜时另有用途），下面的按钮是真点
				}
			}
			return FindText(TEXT("回房间")).IsValid();
		});
		AddShot(TEXT("40_暂停_有回房间"));
		ClickCheck(TEXT("回房间"), TEXT("回到房间图"), []() {
			const AMechGameState* G = GS();
			return G && G->bRoomMode && FindText(TEXT("开始")).IsValid();
		});
		Steps.Last().Timeout = 90.f;
		Add(TEXT("回房间后稳定"), 4.f, [Clock = 0.f]() mutable {
			Clock += FApp::GetDeltaTime();
			return Clock > 1.5f;
		});
		AddShot(TEXT("41_回到房间"));
	}

	bool TickTest(float DeltaTime)
	{
		if (bDone)
		{
			return false;
		}
		if (Steps.IsEmpty())
		{
			Build();
			UE_LOG(LogTemp, Display, TEXT("[uiclick] 开始，共 %d 步"), Steps.Num());
		}
		// 每步之间停一下：等上一步的切页画出来、命中网格更新（点的是上一帧画出来的位置）
		if (Settle > 0.f)
		{
			Settle -= DeltaTime;
			return true;
		}
		if (!Steps.IsValidIndex(Cur))
		{
			bDone = true;
			UE_LOG(LogTemp, Display, TEXT("[uiclick] 结束：通过 %d 项，失败 %d 项"), Passed, Failed);
			if (AMechPlayerController* P = PC())
			{
				P->ConsoleCommand(TEXT("quit"));
			}
			return false;
		}
		FStep& S = Steps[Cur];
		StepClock += DeltaTime;
		const bool bOk = S.Run();
		if (bOk || StepClock > S.Timeout)
		{
			if (!S.Name.StartsWith(TEXT("截图")))
			{
				Check(bOk, FString::Printf(TEXT("%s（%.1f 秒）"), *S.Name, StepClock));
			}
			++Cur;
			StepClock = 0.f;
			Settle = 0.3f;
		}
		return true;
	}

	FDelayedAutoRegisterHelper RegisterClickTest(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("UIClickTest")))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickTest));
		}
	});
}
