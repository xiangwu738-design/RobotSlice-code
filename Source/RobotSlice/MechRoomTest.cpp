// 房间流程联机测试（-MechRoomTest=Host / Client / Shots；本机双开见 tools/run_room_test.sh）。
// 房主用 "/Game/Maps/L_Street?Room?listen" 启动，客户端用 "127.0.0.1" 启动。走一遍用户要的流程：
//   开房 → 客户端进房间、自己换到蓝队、看到房主选的地图 → 客户端偷偷发一次「改设置」（服务器应不认）→ 房主换两次地图（客户端跟着变）→
//   房主开始 → 两边一起进对战图（等人页）→ 选配（各自定一套）→ 开打 3 秒后房主把对面打爆 → 结算 → 房主点「回房间」→
//   两边回到同一个房间（客户端还在蓝队、地图、规则、人机名单都没变）→ 客户端退出 → 房主退出。
// 换图时控制器 / 世界都会重建，所以用全局 ticker（和 -MechHostAfterJoinTest 一样）每次现找当前世界。
// 截图 Saved/Screenshots/Room/<host|client>_*.png；日志 [roomtest]，最后一行「[roomtest] 房主 / 客户端 通过」或「失败：原因」。
// -MechRoomTestMap=N：开打的地图（MechMaps 表的序号，默认 0 街道；房间里先切到 1、3 再切回 N）。
// -MechRoomTestKind=（只给房主）：Elim（默认，2 真人 v 5 人机，五个强度各不同）/ DMTeam / DMSolo / Core / Defense / Training（第二批）。
// -MechRoomTestAlone（只给房主）：不等客户端（一个人走完流程）；-MechRoomTestBig：对面 9 台人机（看 1v9 的选配页、比分条、结算）。
// -MechRoomTestMidJoin（只给房主；tools/run_room_test.sh <地图> midjoin）：测从房间开的场里「中途加入接管 AI、掉线交 AI」和「结算表到时自动回房间」：
//   房主不等人直接开始，开打后所有机体先打不死；等一个普通客户端（127.0.0.1 -MechQuitAfter=25）中途连进来接管 AI、到点退出，
//   确认它那台交回 AI 后把对面打爆，不点按钮，等结算表到时自动回房间。
// -MechRoomTest=Shots（单进程，tools/run_room_shots.sh）：把房间页各种状态（1v5、1v9、个人战、保卫战、训练场、非房主、五套配色）拍一遍到 Saved/Screenshots/RoomUI/<窗口高>/。

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MechAIController.h"
#include "GameFramework/PlayerState.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechDefense.h"
#include "MechDraft.h"
#include "MechDraftUI.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechLoadout.h"
#include "MechMaps.h"
#include "MechRoom.h"
#include "MechSettings.h"
#include "MechUIKit.h"
#include "MechUITheme.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace MechRoomTest
{
	bool bHost = false;
	int32 FinalMap = 0;
	double Start = 0.0;
	/** 0 第一次在房间、1 在对战图、2 回到房间 */
	int32 Stage = 0;
	/** 这一段开始的时间（秒，平台时间） */
	double StageAt = 0.0;
	int32 Step = 0;
	double StepAt = 0.0;
	int32 SeenMap = -1;
	double ShotAt = -1.0;
	FString ShotName;
	bool bShotWait = false, bShotDraft = false, bShotFight = false, bShotResults = false, bKilled = false, bSentBack = false, bInRoom = false;
	double FightAt = -1.0, OverAt = -1.0;
	int32 LastTurn = -1;
	/** 房主自己定的地图：房间里地图变成别的值 = 非房主的改动生效了（失败） */
	int32 HostMap = -1;
	TArray<FString> Notes;
	FString Kind = TEXT("Elim");
	/** 不等客户端（-MechRoomTestAlone、中途加入那轮） */
	bool bAlone = false;
	/** 对面 9 台人机（-MechRoomTestBig） */
	bool bBig = false;
	// -MechRoomTestMidJoin
	bool bMidJoin = false;
	TWeakObjectPtr<AMechCharacter> JoinMech;
	bool bSawJoin = false, bSawLeave = false;
	double LeaveAt = -1.0;
	/** 房主开始时的人机名单：回房间后核对 */
	TArray<FMechRoomAI> StartAIs;

	void Shot(const FString& Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Room") / FString::Printf(TEXT("%s_%s.png"), bHost ? TEXT("host") : TEXT("client"), *Name);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[roomtest] 截图 %s"), *Path);
	}

	void Finish(bool bOk, const FString& Why)
	{
		UE_LOG(LogTemp, Display, TEXT("[roomtest] %s %s%s"), bHost ? TEXT("房主") : TEXT("客户端"), bOk ? TEXT("通过") : TEXT("失败："), *Why);
		for (const FString& N : Notes)
		{
			UE_LOG(LogTemp, Display, TEXT("[roomtest]   %s"), *N);
		}
		FPlatformMisc::RequestExit(false);
	}

	UWorld* FindWorld()
	{
		for (const FWorldContext& WC : GEngine->GetWorldContexts())
		{
			if (WC.WorldType == EWorldType::Game && WC.World())
			{
				return WC.World();
			}
		}
		return nullptr;
	}

	/** 客户端在房间里换到哪队：保卫战、训练场里机体都在 0 队，让客户端待在红队，查回房间后还在红队 */
	int32 WantTeam()
	{
		return Kind == TEXT("Defense") || Kind == TEXT("Training") ? 1 : 0;
	}

	const FMechLobbyPlayer* FindLobby(const AMechGameState* GS, bool bWantHost)
	{
		return GS->Lobby.FindByPredicate([bWantHost](const FMechLobbyPlayer& P) { return P.bHost == bWantHost; });
	}

	/** 这一轮测的房间设置：地图 + 模式 + 人机名单 */
	FMechRoomSettings MakeRoom(const AMechGameState* GS, int32 Map)
	{
		FMechRoomSettings R = GS->Room;
		R.Map = Map;
		R.bDefense = false;
		R.bBans = false;
		R.AILevel = 2;
		R.Rules.Mode = EMechMode::Elimination;
		R.Rules.RoundsToWin = 1;
		R.Rules.bSolo = false;
		R.AIs.Reset();
		auto AddAI = [&R](int32 Team, int32 Level) {
			FMechRoomAI A;
			A.Team = Team;
			A.Level = Level;
			R.AIs.Add(A);
		};
		if (Kind == TEXT("DMTeam"))
		{
			R.Rules.Mode = EMechMode::Deathmatch;
			R.Rules.TeamTarget = 1;
			AddAI(1, 1);
			AddAI(1, 1);
		}
		else if (Kind == TEXT("DMSolo"))
		{
			R.Rules.Mode = EMechMode::Deathmatch;
			R.Rules.bSolo = true;
			R.Rules.SoloTarget = 1;
			AddAI(0, 1);
			AddAI(1, 2);
			AddAI(0, 3);
		}
		else if (Kind == TEXT("Core"))
		{
			UE_CLOG(!MechMaps::AnySupports(MechMaps::Core), LogTemp, Warning, TEXT("[roomtest] 地图表里没有夺芯图（11-02 撤了），这一轮打的不是夺芯"));
			R.Map = MechRoom::FirstMapFor(MechMaps::Core);
			AddAI(1, 1);
			AddAI(1, 1);
		}
		else if (Kind == TEXT("Defense"))
		{
			R.Map = MechRoom::FirstMapFor(MechMaps::Defense);
			R.bDefense = true;
		}
		else if (Kind == TEXT("Training"))
		{
			R.Map = MechRoom::FirstMapFor(MechMaps::Training);
			R.bFriendlyFire = false;  // 进图后先关着打一次、再打开（TrainingStep）
		}
		else if (Kind == TEXT("Challenge"))
		{
			R.bChallenge = true;
			for (int32 i = 0; i < 5; ++i)
			{
				AddAI(1, 1);
			}
		}
		else if (bBig)
		{
			for (int32 i = 0; i < 9; ++i)
			{
				AddAI(1, i % 4);
			}
		}
		else
		{
			// 团队淘汰：真人都在蓝队，对面 5 台人机，四档强度都有（用户：「你要是想 1v5 也是可以的」）
			for (const int32 Level : {0, 1, 2, 3, 1})
			{
				AddAI(1, Level);
			}
		}
		return R;
	}

	/** 选配：轮到自己禁就禁第一件能禁的，轮到自己选就定一套默认的（被禁的换掉） */
	void DraftStep(AMechPlayerController* PC, const AMechGameState* GS)
	{
		const FMechDraft& D = GS->Draft;
		const FMechDraftTurn* T = D.Current();
		const int32 Mine = PC->PlayerState ? D.FindPlayer(PC->PlayerState->GetPlayerId()) : INDEX_NONE;
		if (!T || !D.Slots.IsValidIndex(Mine) || D.Turn == LastTurn)
		{
			return;
		}
		const int32 Team = D.Slots[Mine].Team;
		if (T->bBan && T->Team == Team)
		{
			for (int32 i = 0; i < MechLoadout::NumItems(MechLoadout::Shooter); ++i)
			{
				if (!MechDraft::HasBanned(D, Team, MechLoadout::Shooter, i))
				{
					PC->DraftBan(MechLoadout::Shooter, i);
					break;
				}
			}
			LastTurn = D.Turn;
		}
		else if (!T->bBan && T->Slot == Mine)
		{
			PC->DraftPick(MechDraft::Sanitize(D, Team, FMechLoadout()), true);
			Notes.Add(FString::Printf(TEXT("选配：%s 定下一套（第 %d 轮）"), *D.Slots[Mine].Name, D.Turn));
			LastTurn = D.Turn;
		}
	}

	void HostRoom(AMechPlayerController* PC, const AMechGameState* GS, double Now)
	{
		if (HostMap >= 0 && GS->Room.Map != HostMap)
		{
			Finish(false, FString::Printf(TEXT("房间地图变成了 %d（房主定的是 %d）：非房主的改动生效了"), GS->Room.Map, HostMap));
			return;
		}
		auto SetMap = [PC, GS](int32 Map) {
			const FMechRoomSettings R = MakeRoom(GS, Map);
			HostMap = R.Map;
			PC->LobbySetRoom(R);
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主换地图 → %d %s"), HostMap, MechMaps::Get(HostMap).Name);
		};
		const double T = Now - StepAt;
		if (bAlone)
		{
			// 不等人：选好就开始，客户端（中途加入那轮）等开打后才连进来
			if (Step == 0 && T > 2.0)
			{
				SetMap(FinalMap);
				Step = 1;
				StepAt = Now;
			}
			else if (Step == 1 && T > 2.0)
			{
				StartAIs = GS->Room.AIs;
				PC->LobbyStart();
				Step = 2;
				StepAt = Now;
			}
			return;
		}
		switch (Step)
		{
		case 0:
			if (T > 2.0)
			{
				SetMap(1 % MechMaps::Num());
				Step = 1;
				StepAt = Now;
			}
			break;
		case 1:  // 等客户端进房间（它进来后自己换到蓝队）
			if (GS->Lobby.Num() >= 2 && T > 1.0)
			{
				Notes.Add(FString::Printf(TEXT("客户端进了房间（%.0f 秒）"), Now - Start));
				Step = 2;
				StepAt = Now;
			}
			else if (T > 150.0)
			{
				Finish(false, TEXT("150 秒内客户端没进房间"));
			}
			break;
		case 2:
			if (T > 7.0)  // 客户端换队、偷改设置都发生在进来后 2–5 秒
			{
				Shot(TEXT("room_1"));
				SetMap(3 % MechMaps::Num());
				Step = 3;
				StepAt = Now;
			}
			break;
		case 3:
			if (T > 4.0)
			{
				Shot(TEXT("room_2"));
				SetMap(FinalMap);
				Step = 4;
				StepAt = Now;
			}
			break;
		case 4:
			if (T > 4.0)
			{
				Shot(TEXT("room_3"));
				const FMechLobbyPlayer* Client = FindLobby(GS, false);
				Notes.Add(FString::Printf(TEXT("开始前：%s，人机 %d 台，客户端 %s 在%s队"), *MechRoom::Describe(GS->Room), GS->Room.AIs.Num(), Client ? *Client->Name : TEXT("不在"),
					Client && Client->Team == 0 ? TEXT("蓝") : TEXT("红")));
				if (!Client || Client->Team != WantTeam())
				{
					Finish(false, TEXT("客户端没换到该去的队"));
					return;
				}
				StartAIs = GS->Room.AIs;
				PC->LobbyStart();
				Step = 5;
				StepAt = Now;
			}
			break;
		default:
			if (T > 40.0)
			{
				Finish(false, TEXT("点了开始 40 秒还没换图"));
			}
			break;
		}
	}

	void ClientRoom(AMechPlayerController* PC, const AMechGameState* GS, double Now)
	{
		if (GS->Room.Map != SeenMap)
		{
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 客户端看到房主的地图 → %d %s（%s）"), GS->Room.Map, MechMaps::Get(GS->Room.Map).Name, *MechRoom::Describe(GS->Room));
			Notes.Add(FString::Printf(TEXT("看到地图 %s（进房间后 %.1f 秒）"), MechMaps::Get(GS->Room.Map).Name, Now - StageAt));
			SeenMap = GS->Room.Map;
			ShotAt = Now + 1.5;
			ShotName = FString::Printf(TEXT("room_map%d"), SeenMap);
		}
		const double T = Now - StepAt;
		if (Step == 0 && T > 2.0)
		{
			PC->LobbySetTeam(WantTeam());
			Step = 1;
			StepAt = Now;
		}
		else if (Step == 1 && T > 2.0)
		{
			// 非房主偷改：服务器应该不认（房主那边盯着地图有没有变成 2）
			FMechRoomSettings R = GS->Room;
			R.Map = (R.Map + 1) % MechMaps::Num();
			R.Rules.RoundsToWin = 4;
			PC->LobbySetRoom(R);
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 客户端试着改设置（换地图、七局四胜）"));
			Step = 2;
			StepAt = Now;
		}
		else if (Step == 2 && T > 3.0)
		{
			const bool bIgnored = GS->Room.Rules.RoundsToWin != 4;
			Notes.Add(bIgnored ? TEXT("非房主改设置：被服务器忽略（对）") : TEXT("非房主改设置：生效了（错）"));
			if (!bIgnored)
			{
				Finish(false, TEXT("非房主能改房间设置"));
				return;
			}
			Step = 3;
		}
	}

	/** -MechRoomTestMidJoin 开打期间：等客户端中途进来接管 AI、退出后机体交回 AI，再把对面打爆 */
	void MidJoinFight(AMechPlayerController* PC, double Now)
	{
		UWorld* World = PC->GetWorld();
		AMechCharacter* Mine = Cast<AMechCharacter>(PC->GetPawn());
		if (bKilled)
		{
			return;
		}
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			It->GetHealth()->bNoDeath = true;  // 比赛别在客户端进来之前就打完
			if (!bSawJoin && *It != Mine && It->IsPlayerControlled())
			{
				bSawJoin = true;
				JoinMech = *It;
				UE_LOG(LogTemp, Display, TEXT("[roomtest] 中途加入：%s 接管了 %d 队的机体"), *It->DisplayName, It->GetTeam());
				Notes.Add(FString::Printf(TEXT("中途加入：%s 接管了 %d 队的机体（开打后 %.0f 秒）"), *It->DisplayName, It->GetTeam(), Now - FightAt));
			}
		}
		if (bSawJoin && !bSawLeave && World->GetNumPlayerControllers() <= 1)
		{
			bSawLeave = true;
			LeaveAt = Now;
			const AMechCharacter* Left = JoinMech.Get();
			const bool bAI = Left && !Left->IsPlayerControlled() && Left->GetController();
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 客户端退出：机体 %s，%s"), Left ? *Left->DisplayName : TEXT("没了"), bAI ? TEXT("交给了 AI") : TEXT("没交给 AI"));
			Notes.Add(FString::Printf(TEXT("客户端退出：机体 %s，%s"), Left ? *Left->DisplayName : TEXT("没了"), bAI ? TEXT("交给了 AI（对）") : TEXT("没交给 AI（错）")));
			if (!bAI)
			{
				Finish(false, TEXT("掉线后机体没交给 AI"));
				return;
			}
		}
		if (!bSawJoin && Now - FightAt > 120.0)
		{
			Finish(false, TEXT("开打 120 秒没有客户端中途加入"));
			return;
		}
		if (bSawLeave && Now - LeaveAt > 3.0)
		{
			bKilled = true;
			const int32 MyTeam = Mine ? Mine->GetTeam() : 0;
			for (TActorIterator<AMechCharacter> It(World); It; ++It)
			{
				if (It->GetTeam() != MyTeam && !It->GetHealth()->IsDead())
				{
					It->GetHealth()->bNoDeath = false;
					It->GetHealth()->ApplyHit(1.0e7f, 0.f, Mine, It->GetActorLocation());
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主打爆对面，等结算表到时自动回房间"));
		}
	}

	/** 挑战倍数（-MechRoomTestKind=Challenge，一个人 1v5）：房主这队是人少的；真打一下核对装甲和伤害的倍数 */
	void CheckChallenge(AMechPlayerController* PC, const AMechGameState* GS)
	{
		AMechCharacter* Mine = Cast<AMechCharacter>(PC->GetPawn());
		AMechCharacter* Foe = nullptr;
		for (TActorIterator<AMechCharacter> It(PC->GetWorld()); It; ++It)
		{
			if (*It != Mine && !It->GetHealth()->IsDead())
			{
				Foe = *It;
				break;
			}
		}
		if (!Mine || !Foe)
		{
			Finish(false, TEXT("挑战倍数：找不到机体"));
			return;
		}
		const float Want = MechRoom::RecommendedChallenge(1, 5);
		const float MyAP = Mine->GetHealth()->GetAP();
		const float FoeAP = Foe->GetHealth()->GetAP();
		Mine->GetHealth()->ApplyHit(1000.f, 0.f, Foe, Mine->GetActorLocation());
		Foe->GetHealth()->ApplyHit(1000.f, 0.f, Mine, Foe->GetActorLocation());
		const float MyLoss = MyAP - Mine->GetHealth()->GetAP();
		const float FoeLoss = FoeAP - Foe->GetHealth()->GetAP();
		const FString Line = FString::Printf(TEXT("挑战倍数：给 %d 队，推荐 ×%.2f，GameState 装甲 ×%.2f 伤害 ×%.2f；同样打 1000：自己掉 %.0f（应 %.0f），对面掉 %.0f（应 %.0f）"),
			GS->ChallengeTeam, Want, GS->ChallengeArmor, GS->ChallengeDamage, MyLoss, 1000.f / Want, FoeLoss, 1000.f * Want);
		UE_LOG(LogTemp, Display, TEXT("[roomtest] %s"), *Line);
		Notes.Add(Line);
		if (GS->ChallengeTeam != Mine->GetTeam() || !FMath::IsNearlyEqual(MyLoss, 1000.f / Want, 5.f) || !FMath::IsNearlyEqual(FoeLoss, 1000.f * Want, 5.f))
		{
			Finish(false, TEXT("挑战倍数没按推荐值生效"));
		}
	}

	/** 训练场（-MechRoomTestKind=Training，要客户端）：友伤关着开枪打不动队友 → 打开后子弹真的掉装甲 → 打爆后自动重生、配装不变 → 回房间 */
	void TrainingStep(AMechPlayerController* PC, AMechGameState* GS, double Now)
	{
		AMechCharacter* Mine = Cast<AMechCharacter>(PC->GetPawn());
		AMechCharacter* Other = nullptr;
		for (FConstPlayerControllerIterator It = PC->GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (It->Get() != PC)
			{
				Other = Cast<AMechCharacter>(It->Get()->GetPawn());
			}
		}
		const double T = Now - StepAt;
		auto AimAt = [PC, Mine](const AMechCharacter* Target) {
			if (Mine && Target)
			{
				PC->SetControlRotation((Target->GetActorLocation() + FVector(0.f, 0.f, 60.f) - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
			}
		};
		static float ApBefore = 0.f;
		static float HurtBefore = 0.f;
		static FMechLoadout LoadoutBefore;
		switch (Step)
		{
		case 0:  // 等客户端进图
			if (Mine && Other && T > 12.0)  // 客户端进图后才把配装发上来（换框架 = 装甲上限变），等它到了再量
			{
				Notes.Add(FString::Printf(TEXT("训练场：两人进图，队伍号 %d / %d，木桩 %s"), Mine->GetTeam(), Other->GetTeam(), TActorIterator<AMechCharacter>(PC->GetWorld()) ? TEXT("有") : TEXT("没有")));
				Shot(TEXT("training"));
				ApBefore = Other->GetHealth()->GetAP();
				HurtBefore = Other->GetHealth()->GetLastHurtTime();
				PC->TestKey(MechKeys::Key(EMechKey::Fire), true);
				Step = 1;
				StepAt = Now;
			}
			else if (T > 120.0)
			{
				Finish(false, TEXT("训练场 120 秒没等到客户端"));
			}
			break;
		case 1:  // 友伤关：开枪 2 秒
			AimAt(Other);
			if (T > 2.0)
			{
				PC->TestKey(MechKeys::Key(EMechKey::Fire), false);
				const float Lost = Other ? ApBefore - Other->GetHealth()->GetAP() : -1.f;
				const bool bHurt = Other && !FMath::IsNearlyEqual(Other->GetHealth()->GetLastHurtTime(), HurtBefore);
				Notes.Add(FString::Printf(TEXT("友伤关：朝队友开枪 2 秒，对方掉 %.0f 装甲、%s受伤（应 0、没有）"), Lost, bHurt ? TEXT("有") : TEXT("没有")));
				if (bHurt || Lost > 0.5f)
				{
					Finish(false, TEXT("友伤关着还能打到队友"));
					return;
				}
				PC->MenuSetFriendlyFire(true);
				Step = 2;
				StepAt = Now;
			}
			break;
		case 2:  // 友伤开：队伍号应该不一样了
			if (T > 1.5 && Mine && Other)
			{
				Notes.Add(FString::Printf(TEXT("友伤开：队伍号 %d / %d，GameState %d"), Mine->GetTeam(), Other->GetTeam(), GS->bFriendlyFire ? 1 : 0));
				if (Mine->GetTeam() == Other->GetTeam() || !GS->bFriendlyFire)
				{
					Finish(false, TEXT("打开友伤后队伍号没变"));
					return;
				}
				ApBefore = Other->GetHealth()->GetAP();
				PC->TestKey(MechKeys::Key(EMechKey::Fire), true);
				Step = 3;
				StepAt = Now;
			}
			break;
		case 3:
			AimAt(Other);
			if (T > 3.0)
			{
				PC->TestKey(MechKeys::Key(EMechKey::Fire), false);
				const float Lost = Other ? ApBefore - Other->GetHealth()->GetAP() : -1.f;
				Notes.Add(FString::Printf(TEXT("友伤开：朝对方开枪 3 秒，对方掉 %.0f 装甲（应 > 0）"), Lost));
				if (Lost <= 0.f)
				{
					Finish(false, TEXT("友伤开着子弹没掉装甲"));
					return;
				}
				Shot(TEXT("training_ff"));
				if (Other)
				{
					LoadoutBefore = Other->GetLoadout();
					Other->GetHealth()->ApplyHit(1.0e7f, 0.f, Mine, Other->GetActorLocation());
				}
				Step = 4;
				StepAt = Now;
			}
			break;
		case 4:  // 阵亡后 5 秒自动重生
			if (T > 2.0 && T < 2.2)
			{
				Shot(TEXT("training_dead"));
			}
			if (Other && !Other->GetHealth()->IsDead())
			{
				const bool bSame = FMemory::Memcmp(&LoadoutBefore, &Other->GetLoadout(), sizeof(FMechLoadout)) == 0;
				Notes.Add(FString::Printf(TEXT("阵亡 %.1f 秒后自动重生，配装%s"), T, bSame ? TEXT("没变") : TEXT("变了（错）")));
				Step = 5;
				StepAt = Now;
			}
			else if (T > 15.0)
			{
				Finish(false, TEXT("训练场阵亡 15 秒没重生"));
			}
			break;
		case 5:
			if (T > 2.0)
			{
				UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主点「回房间」"));
				PC->MenuBackToRoom();
				Step = 6;
			}
			break;
		default:
			break;
		}
	}

	/** 开打时核对阵容：两队台数、每台人机的强度（11-01 人机强度：直接问 AI 控制器，AIAimErrorScale 现在跟着目标速度变）、个人战每台队伍号都不一样 */
	void CheckRoster(AMechPlayerController* PC, const AMechGameState* GS)
	{
		TSet<int32> Teams;
		int32 Count[2] = {0, 0};
		int32 Level[4] = {0, 0, 0, 0};
		int32 Mechs = 0, Humans = 0;
		for (TActorIterator<AMechCharacter> It(PC->GetWorld()); It; ++It)
		{
			if (It->IsTrainingDummy())
			{
				continue;
			}
			++Mechs;
			Teams.Add(It->GetTeam());
			if (It->GetTeam() >= 0 && It->GetTeam() <= 1)
			{
				++Count[It->GetTeam()];
			}
			if (It->IsPlayerControlled())
			{
				++Humans;
			}
			else
			{
				if (const AMechAIController* AI = Cast<AMechAIController>(It->GetController()))
				{
					++Level[FMath::Clamp(AI->GetTierLevel(), 0, 3)];
				}
			}
		}
		const FString Line = FString::Printf(TEXT("阵容：%d 台（真人 %d），蓝 %d 红 %d，比分条 %d / %d，不同队伍号 %d 个；人机强度 新兵 %d 普通 %d 老练 %d 王牌 %d"),
			Mechs, Humans, Count[0], Count[1], GS->TeamCount[0], GS->TeamCount[1], Teams.Num(), Level[0], Level[1], Level[2], Level[3]);
		UE_LOG(LogTemp, Display, TEXT("[roomtest] %s"), *Line);
		Notes.Add(Line);
		if (bHost && Kind == TEXT("DMSolo") && Teams.Num() != Mechs)
		{
			Finish(false, TEXT("个人战有两台机体队伍号相同"));
		}
	}

	void MatchStep(AMechPlayerController* PC, AMechGameState* GS, double Now)
	{
		if (GS->Phase == EMechRoundPhase::Waiting && !bShotWait)
		{
			bShotWait = true;
			ShotAt = Now + 0.6;
			ShotName = TEXT("wait");
			Notes.Add(FString::Printf(TEXT("进对战图 %s（开始后 %.0f 秒），等人 %d / %d"), *PC->GetWorld()->GetMapName(), Now - StageAt, GS->RoomArrived, GS->RoomExpected));
		}
		if (GS->Phase == EMechRoundPhase::Lobby)
		{
			Finish(false, TEXT("从房间开的场又进了房间阶段"));
			return;
		}
		// 保卫战、训练场没有回合阶段：进图几秒后结束这一轮（保卫战打爆指挥所，等 12 秒自动回房间）
		if (GS->bRangeMode)
		{
			if (bHost && Kind == TEXT("Training"))
			{
				TrainingStep(PC, GS, Now);
				return;
			}
			if (bHost && !bKilled && Now - StageAt > 12.0 && GS->bRoomMatch)
			{
				bKilled = true;
				Shot(TEXT("range"));
				if (Kind == TEXT("Defense"))
				{
					for (TActorIterator<AMechDefenseDirector> It(PC->GetWorld()); It; ++It)
					{
						if (It->Core)
						{
							It->Core->ApplyEnemyDamage(1.0e9f);
							UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主打爆指挥所，等自动回房间"));
						}
					}
				}
				else
				{
					UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主点「回房间」"));
					PC->MenuBackToRoom();
				}
			}
			return;
		}
		if (GS->Phase == EMechRoundPhase::Draft)
		{
			if (!bShotDraft)
			{
				bShotDraft = true;
				ShotAt = Now + 1.5;
				ShotName = TEXT("draft");
				Notes.Add(FString::Printf(TEXT("进选配：蓝 %d 红 %d，%s"), GS->TeamCount[0], GS->TeamCount[1], *MechRoom::Describe(GS->Room)));
			}
			DraftStep(PC, GS);
		}
		if (GS->Phase == EMechRoundPhase::Fight)
		{
			if (FightAt < 0.0)
			{
				FightAt = Now;
				Notes.Add(TEXT("开打"));
				UE_LOG(LogTemp, Display, TEXT("[roomtest] 开打"));  // run_room_test.sh midjoin 看到这行才启动客户端
				if (!bMidJoin)
				{
					CheckRoster(PC, GS);
				}
				if (bHost && Kind == TEXT("Challenge"))
				{
					CheckChallenge(PC, GS);
				}
			}
			if (!bShotFight && Now - FightAt > 2.0)
			{
				bShotFight = true;
				Shot(TEXT("fight"));
			}
			if (bMidJoin)
			{
				MidJoinFight(PC, Now);
			}
			else if (bHost && !bKilled && Now - FightAt > 3.0)
			{
				bKilled = true;
				if (Kind == TEXT("Core"))
				{
					if (AMechGameMode* GM = PC->GetWorld()->GetAuthGameMode<AMechGameMode>())
					{
						GM->EndCoreMatch(0);  // 夺芯：抢核心太久，直接判蓝队赢（测的是从房间开、结束后能回房间）
						UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主判夺芯结束"));
					}
				}
				else
				{
					// 房主（服务器）把对面全打爆，比赛马上结束
					AMechCharacter* Mine = Cast<AMechCharacter>(PC->GetPawn());
					const int32 MyTeam = Mine ? Mine->GetTeam() : 0;
					int32 N = 0;
					for (TActorIterator<AMechCharacter> It(PC->GetWorld()); It; ++It)
					{
						if (It->GetTeam() != MyTeam && !It->GetHealth()->IsDead() && !It->IsTrainingDummy())
						{
							It->GetHealth()->ApplyHit(1.0e7f, 0.f, Mine, It->GetActorLocation());
							++N;
						}
					}
					UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主打爆对面 %d 台"), N);
				}
			}
		}
		if (GS->Phase == EMechRoundPhase::MatchOver)
		{
			if (OverAt < 0.0)
			{
				OverAt = Now;
				Notes.Add(FString::Printf(TEXT("比赛结束（胜方 %d），结算页按钮：%s"), GS->MatchWinner, PC->IsRoomMatch() ? TEXT("回房间") : TEXT("再来一局（错）")));
			}
			if (!bShotResults && (PC->IsMenuOpen() || Now - OverAt > 16.0) && Now - OverAt > 3.0)
			{
				bShotResults = true;
				ShotAt = Now + 1.0;
				ShotName = TEXT("results");
			}
			if (bHost && !bMidJoin && bShotResults && !bSentBack && Now - OverAt > 4.0 && ShotAt < 0.0)  // 中途加入那轮不点，测到时自动回
			{
				// 等客户端也拍完结算页再回（客户端开表的时间和房主差不多）
				if (Now - OverAt > 18.0)
				{
					bSentBack = true;
					UE_LOG(LogTemp, Display, TEXT("[roomtest] 房主点「回房间」"));
					PC->MenuBackToRoom();
				}
			}
		}
	}

	bool SameAIs(const TArray<FMechRoomAI>& A, const TArray<FMechRoomAI>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Team != B[i].Team || A[i].Level != B[i].Level)
			{
				return false;
			}
		}
		return true;
	}

	void BackInRoom(AMechPlayerController* PC, const AMechGameState* GS, double Now)
	{
		const double T = Now - StageAt;
		if (bAlone)
		{
			// 没点「回房间」也回来了 = 结算表到时自动回（Big 那轮是点的）；客户端早退了，只看房主自己的选择还在
			if (T > 3.0)
			{
				Shot(TEXT("room_back"));
				Notes.Add(FString::Printf(TEXT("回到房间（结算后 %.0f 秒）：%s，人机 %d 台"), OverAt > 0.0 ? StageAt - OverAt : -1.0, *MechRoom::Describe(GS->Room), GS->Room.AIs.Num()));
				const bool bOk = (!bMidJoin || (bSawJoin && bSawLeave)) && GS->Room.Map == HostMap && SameAIs(GS->Room.AIs, StartAIs);
				Finish(bOk, bOk ? FString() : TEXT("回房间后选择不对，或没测到中途加入 / 掉线"));
			}
			return;
		}
		if (bHost)
		{
			if (Step == 0 && GS->Lobby.Num() >= 2 && T > 2.0)
			{
				Step = 1;
				StepAt = Now;
				Notes.Add(FString::Printf(TEXT("回到房间，客户端也回来了（%.0f 秒）"), T));
			}
			else if (Step == 0 && T > 120.0)
			{
				Finish(false, TEXT("回房间 120 秒客户端没回来"));
			}
			else if (Step == 1 && Now - StepAt > 3.0)
			{
				Shot(TEXT("room_back"));
				const FMechLobbyPlayer* Client = FindLobby(GS, false);
				const bool bOk = GS->Room.Map == HostMap && SameAIs(GS->Room.AIs, StartAIs) && Client && Client->Team == WantTeam();
				Notes.Add(FString::Printf(TEXT("房间状态：%s，人机 %d 台（开始时 %d 台），客户端在%s队"), *MechRoom::Describe(GS->Room), GS->Room.AIs.Num(), StartAIs.Num(),
					Client && Client->Team == 0 ? TEXT("蓝") : TEXT("红")));
				if (!bOk)
				{
					Finish(false, TEXT("回房间后选择 / 人机 / 队伍不对"));
					return;
				}
				Step = 2;
				StepAt = Now;
			}
			else if (Step == 2 && (GS->Lobby.Num() < 2 || Now - StepAt > 30.0))
			{
				Finish(true, TEXT(""));
			}
			return;
		}
		if (Step == 0 && T > 3.0)
		{
			Shot(TEXT("room_back"));
			const int32 Id = PC->PlayerState ? PC->PlayerState->GetPlayerId() : -1;
			const FMechLobbyPlayer* Me = GS->Lobby.FindByPredicate([Id](const FMechLobbyPlayer& P) { return P.PlayerId == Id; });
			Notes.Add(FString::Printf(TEXT("回到房间：%s，人机 %d 台，我在%s队"), *MechRoom::Describe(GS->Room), GS->Room.AIs.Num(), Me && Me->Team == 0 ? TEXT("蓝") : TEXT("红")));
			Step = 1;
			StepAt = Now;
			if (!Me || Me->Team != WantTeam() || GS->Room.Map != SeenMap)
			{
				Finish(false, TEXT("回房间后队伍 / 地图不对"));
			}
		}
		else if (Step == 1 && Now - StepAt > 2.0)
		{
			Finish(true, TEXT(""));
		}
	}

	// ---------------------------------------------------------------- 房间页截图（-MechRoomTest=Shots）
	struct FShotState
	{
		FString Name;
		TFunction<void(FMechRoomSettings&)> Set;
		bool bGuest = false;
		int32 Palette = 0;
		/** 名单里假装有几个别的真人（看满员的样子） */
		int32 FakeHumans = 0;
	};
	TArray<FShotState> ShotStates;
	int32 ShotIndex = -1;
	double ShotStepAt = 0.0;
	bool bShotTaken = false;

	void AddAIs(FMechRoomSettings& R, int32 Team, int32 N)
	{
		for (int32 i = 0; i < N; ++i)
		{
			FMechRoomAI A;
			A.Team = Team;
			A.Level = i % 4;
			R.AIs.Add(A);
		}
	}

	void BuildShotStates()
	{
		auto Elim = [](FMechRoomSettings& R, int32 Map, int32 Blue, int32 Red) {
			R.Map = Map;
			R.bDefense = false;
			R.Rules.Mode = EMechMode::Elimination;
			R.bFriendlyFire = false;
			R.AIs.Reset();
			AddAIs(R, 0, Blue);
			AddAIs(R, 1, Red);
		};
		auto Solo = [](FMechRoomSettings& R) {
			R.Map = 0;
			R.bDefense = false;
			R.Rules.Mode = EMechMode::Deathmatch;
			R.Rules.bSolo = true;
			R.AIs.Reset();
			AddAIs(R, 0, 2);
			AddAIs(R, 1, 2);
		};
		ShotStates.Add({TEXT("01_房主_2v2"), [Elim](FMechRoomSettings& R) { Elim(R, 0, 1, 2); }});
		ShotStates.Add({TEXT("02_房主_1v5"), [Elim](FMechRoomSettings& R) { Elim(R, 3, 0, 5); }});
		ShotStates.Add({TEXT("03_房主_1v9"), [Elim](FMechRoomSettings& R) { Elim(R, 1, 0, 9); }});
		ShotStates.Add({TEXT("04_房主_四真人_3v3"), [Elim](FMechRoomSettings& R) { Elim(R, 3, 0, 2); }, false, 0, 3});
		ShotStates.Add({TEXT("05_房主_团队死斗"), [](FMechRoomSettings& R) { R.Map = MechRoom::FirstMapFor(MechMaps::Defense); R.bDefense = false; R.Rules.Mode = EMechMode::Deathmatch; R.Rules.bSolo = false; R.AIs.Reset(); AddAIs(R, 0, 1); AddAIs(R, 1, 2); }});
		ShotStates.Add({TEXT("06_房主_个人死斗"), Solo});
		if (MechMaps::AnySupports(MechMaps::Core))  // 11-02 夺芯图撤出地图表：没有就不拍这张
		{
			ShotStates.Add({TEXT("07_房主_夺芯"), [Elim](FMechRoomSettings& R) { Elim(R, MechRoom::FirstMapFor(MechMaps::Core), 1, 2); }});
		}
		ShotStates.Add({TEXT("08_房主_保卫战"), [](FMechRoomSettings& R) { R.Map = MechRoom::FirstMapFor(MechMaps::Defense); R.bDefense = true; R.Rules.Mode = EMechMode::Elimination; }, false, 0, 2});
		ShotStates.Add({TEXT("09_房主_训练场"), [](FMechRoomSettings& R) { R.Map = MechRoom::FirstMapFor(MechMaps::Training); R.bDefense = false; R.Rules.Mode = EMechMode::Elimination; }, false, 0, 2});
		auto Challenge = [Elim](FMechRoomSettings& R, bool bCustom) {
			Elim(R, 3, 0, 5);
			R.bChallenge = true;
			R.bChallengeCustom = bCustom;
			R.ChallengeArmor = 2.f;
			R.ChallengeDamage = 3.5f;
		};
		ShotStates.Add({TEXT("10a_房主_挑战倍数_推荐"), [Challenge](FMechRoomSettings& R) { Challenge(R, false); }});
		ShotStates.Add({TEXT("10b_房主_挑战倍数_自定义"), [Challenge](FMechRoomSettings& R) { Challenge(R, true); }});
		ShotStates.Add({TEXT("10c_房主_团队死斗_挑战倍数_自定义"), [Challenge](FMechRoomSettings& R) { Challenge(R, true); R.Rules.Mode = EMechMode::Deathmatch; R.Rules.bSolo = false; }});
		ShotStates.Add({TEXT("10d_房主_训练场_友伤开"), [](FMechRoomSettings& R) { R.Map = MechRoom::FirstMapFor(MechMaps::Training); R.bDefense = false; R.bChallenge = false; R.bFriendlyFire = true; R.Rules.Mode = EMechMode::Elimination; }, false, 0, 2});
		ShotStates.Add({TEXT("14_房客_挑战倍数"), [Challenge](FMechRoomSettings& R) { Challenge(R, false); }, true, 0, 1});
		ShotStates.Add({TEXT("11_房客_1v5"), [Elim](FMechRoomSettings& R) { Elim(R, 3, 0, 5); R.bChallenge = false; }, true, 0, 1});
		ShotStates.Add({TEXT("12_房客_个人死斗"), Solo, true, 0, 1});
		ShotStates.Add({TEXT("13_房客_训练场"), [](FMechRoomSettings& R) { R.Map = MechRoom::FirstMapFor(MechMaps::Training); R.bDefense = false; R.Rules.Mode = EMechMode::Elimination; }, true, 0, 1});
		for (int32 p = 1; p < MechUITheme::NumPresets; ++p)
		{
			ShotStates.Add({FString::Printf(TEXT("2%d_配色%d_房主"), p, p), [Elim](FMechRoomSettings& R) { Elim(R, 3, 1, 4); }, false, p, 1});
			ShotStates.Add({FString::Printf(TEXT("3%d_配色%d_房客"), p, p), [Elim](FMechRoomSettings& R) { Elim(R, 3, 1, 4); }, true, p, 1});
		}
	}

	void ShotsTick(AMechPlayerController* PC, AMechGameState* GS, double Now)
	{
		if (ShotStates.IsEmpty())
		{
			BuildShotStates();
			ShotStepAt = Now + 6.0;  // 等贴图加载
			return;
		}
		if (Now < ShotStepAt)
		{
			return;
		}
		if (ShotIndex >= 0 && !bShotTaken)
		{
			FVector2D Size(0.f, 0.f);
			if (GEngine->GameViewport)
			{
				GEngine->GameViewport->GetViewportSize(Size);
			}
			const FString Path = FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/RoomUI/%d/%s.png"), FMath::RoundToInt(Size.Y), *ShotStates[ShotIndex].Name);
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 截图 %s"), *Path);
			bShotTaken = true;
			ShotStepAt = Now + 0.6;
			return;
		}
		++ShotIndex;
		bShotTaken = false;
		if (!ShotStates.IsValidIndex(ShotIndex))
		{
			UE_LOG(LogTemp, Display, TEXT("[roomtest] 截图 通过：%d 张"), ShotStates.Num());
			FPlatformMisc::RequestExit(false);
			return;
		}
		const FShotState& St = ShotStates[ShotIndex];
		SMechDraftScreen::bForceGuestView = St.bGuest;
		MechKit::ApplyPalette(St.Palette);
		// 假装名单里还有别的真人（只改本机这份，下次 RefreshLobby 才会刷掉；房间里没人进出就不会刷）。要在 LobbySetRoom 之前：它按名单算挑战倍数
		static const TCHAR* Names[] = {TEXT("小满"), TEXT("阿鱼"), TEXT("北风")};
		while (GS->Lobby.Num() > 1)
		{
			GS->Lobby.Pop();
		}
		for (int32 i = 0; i < St.FakeHumans; ++i)
		{
			FMechLobbyPlayer P;
			P.PlayerId = 100 + i;
			P.Name = Names[i % 3];
			P.Team = St.bGuest ? 0 : (i == 0 ? 0 : 1);
			P.Paint = GS->Lobby.Num() ? GS->Lobby[0].Paint : FMechPaint();
			GS->Lobby.Add(P);
		}
		FMechRoomSettings R = GS->Room;
		St.Set(R);
		PC->LobbySetRoom(R);
		ShotStepAt = Now + 1.0;
	}

	bool Tick(float)
	{
		static const bool bShots = FString(FCommandLine::Get()).Contains(TEXT("-MechRoomTest=Shots"));
		const double Now = FPlatformTime::Seconds();
		if (Start == 0.0)
		{
			Start = StageAt = StepAt = Now;
		}
		if (Now - Start > 600.0)
		{
			Finish(false, FString::Printf(TEXT("10 分钟没走完（阶段 %d 第 %d 步）"), Stage, Step));
			return false;
		}
		UWorld* World = FindWorld();
		AMechPlayerController* PC = World ? Cast<AMechPlayerController>(World->GetFirstPlayerController()) : nullptr;
		AMechGameState* GS = World ? World->GetGameState<AMechGameState>() : nullptr;
		if (!PC || !GS || !PC->PlayerState)
		{
			return true;
		}
		if (bShots)
		{
			if (GS->bRoomMode)
			{
				ShotsTick(PC, GS, Now);
			}
			return true;
		}
		// 客户端：没连上之前的本地世界（默认地图单机）不算；连上过又掉回单机 20 秒 = 房主那边已经结束了，别占着锁空等
		static bool bWasClient = false;
		static double NotClientSince = -1.0;
		if (!bHost && World->GetNetMode() != NM_Client)
		{
			if (bWasClient)
			{
				NotClientSince = NotClientSince < 0.0 ? Now : NotClientSince;
				if (Now - NotClientSince > 20.0)
				{
					Finish(false, TEXT("和房主断开了（房主那边的测试可能已经结束）"));
					return false;
				}
			}
			return true;
		}
		bWasClient = bWasClient || !bHost;
		NotClientSince = -1.0;
		if (bHost && World->GetNetMode() == NM_Client)
		{
			return true;
		}
		if (ShotAt > 0.0 && Now >= ShotAt)
		{
			Shot(ShotName);
			ShotAt = -1.0;
		}
		// 阶段切换：房间图 → 对战图 → 房间图（第一次进房间图时从这一刻起算，客户端连上之前的时间不算）
		if (Stage == 0 && GS->bRoomMode && !bInRoom)
		{
			bInRoom = true;
			StageAt = StepAt = Now;
			Notes.Add(FString::Printf(TEXT("进房间图（启动后 %.0f 秒）"), Now - Start));
		}
		if (Stage == 0 && GS->bRoomMatch)
		{
			Stage = 1;
			StageAt = Now;
			Step = 0;
		}
		else if (Stage == 1 && GS->bRoomMode)
		{
			Stage = 2;
			StageAt = StepAt = Now;
			Step = 0;
		}
		if (Stage == 0 && GS->bRoomMode)
		{
			if (bHost)
			{
				HostRoom(PC, GS, Now);
			}
			else
			{
				ClientRoom(PC, GS, Now);
			}
		}
		else if (Stage == 1 && GS->bRoomMatch)
		{
			MatchStep(PC, GS, Now);
		}
		else if (Stage == 2 && GS->bRoomMode)
		{
			BackInRoom(PC, GS, Now);
		}
		return true;
	}

	// 全局对象构造里不能读命令行（打包版 main 之前执行会崩），引擎初始化完再注册
	FDelayedAutoRegisterHelper Register(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		FString Role;
		if (!FParse::Value(FCommandLine::Get(), TEXT("MechRoomTest="), Role))
		{
			return;
		}
		bHost = Role.Equals(TEXT("Host"), ESearchCase::IgnoreCase);
		bMidJoin = bHost && FParse::Param(FCommandLine::Get(), TEXT("MechRoomTestMidJoin"));
		bBig = bHost && FParse::Param(FCommandLine::Get(), TEXT("MechRoomTestBig"));
		bAlone = bMidJoin || (bHost && FParse::Param(FCommandLine::Get(), TEXT("MechRoomTestAlone")));
		FParse::Value(FCommandLine::Get(), TEXT("MechRoomTestKind="), Kind);
		FParse::Value(FCommandLine::Get(), TEXT("MechRoomTestMap="), FinalMap);
		FinalMap = FMath::Clamp(FinalMap, 0, MechMaps::Num() - 1);
		UE_LOG(LogTemp, Display, TEXT("[roomtest] %s，%s，开打地图 %d %s"), bHost ? TEXT("房主") : TEXT("客户端"), *Kind, FinalMap, MechMaps::Get(FinalMap).Name);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.1f);
	});
}
