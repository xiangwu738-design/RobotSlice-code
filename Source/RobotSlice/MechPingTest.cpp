// -MechPingTest（11-01，标点和信息区）：UMechCommsComponent::TestTick。按键走真的输入路径（往玩家控制器注入中键按下 / 松开），轮盘的鼠标甩动走 AMechCharacter::DoLook。
// 单机（团队淘汰 2v2，"/Game/Maps/L_City?Match?Team=2" -MechNoLobby -MechNoDraft）：开打后
//   1 把一台敌人挪到面前 16 米，点一下 → 敌人标记（跟着它）        2 往前方地面点一下 → 地点「去这里」
//   3 换个方向双击 → 「危险」（第一下放的那个被换掉，信息区不会多一行）
//   4 按住出轮盘、往上甩 → 选中「进攻」截图、松开放下（每人最多 3 个：最早的敌人标记消失）
//   5 准星对着自己的「去这里」再点一下 → 撤掉
//   6 替 AI 队友标一个「集合」（AI 自己不标点，测信息区和到达提示）；让队友放 EMP，再换成紧急脱离、弹射升空、震荡波、地雷各放一次（11-01 并入肩部，都走按 E 的路）、拿一个插件；自己击毁一台敌人
//   7 截图：屏幕上的标点 + 信息区 + 小地图；按住 M 截大地图；关掉信息区再截一张
// 截图 Saved/Screenshots/Ping，日志 [pingtest]（最后一行汇总），约 22 秒退出。
// 联机：房主 "/Game/Maps/L_Defense?Range?Defense?listen" -MechNoLobby -MechPingTest（房主只看：每 2 秒记一行看到几个队友标点、收到几行信息，
//   第一次看到客户端的标点 / 技能时截图，客户端进来后自己也标一个让客户端看），客户端 127.0.0.1 -MechPingTest（标点、轮盘、双击、
//   放肩部装备，记自己收到房主标点和房主那边的信息行），两边都约 40 秒退出。测试场 / 保卫战里大家都是蓝队 = 队友。
// 敌对两端（团队淘汰 "?Match?Team=2?listen" -MechWaitPlayers=2，房主蓝、客户端红）：同一套脚本，日志里「看到对面的标点」应一直是 0。

#include "MechComms.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechEffectComponent.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechPluginPod.h"
#include "MechSettings.h"
#include "MechShoulderComponent.h"

void UMechCommsComponent::TestCheck(const FString& Name, bool bOk, const FString& Detail)
{
	(bOk ? TestPassed : TestFailed)++;
	UE_LOG(LogTemp, Display, TEXT("[pingtest] %s %s：%s"), bOk ? TEXT("通过") : TEXT("没通过"), *Name, *Detail);
}

void UMechCommsComponent::TestShot(const FString& Name)
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots/Ping");
	FScreenshotRequest::RequestScreenshot(Dir / (Name + TEXT(".png")), true, false);
	UE_LOG(LogTemp, Display, TEXT("[pingtest] 截图 %s"), *Name);
}

void UMechCommsComponent::TestTap(int32 Times)
{
	for (int32 i = 0; i < Times; ++i)
	{
		TestKeyQueue.Add(1);
		TestKeyQueue.Add(0);
	}
}

void UMechCommsComponent::TestKey(bool bDown)
{
	if (APlayerController* PC = GetPC())
	{
		// 和真实按键走同一条路：玩家控制器 → 按键状态（MechKeys::IsDown 读的就是它）
		const FInputKeyEventArgs Args(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), MechKeys::Key(EMechKey::Ping),
			bDown ? IE_Pressed : IE_Released, 1.f, false, FPlatformTime::Cycles64());
		PC->InputKey(Args);
	}
}

namespace MechPingTestLocal
{
	void InjectKey(APlayerController* PC, const FKey& Key, bool bDown)
	{
		const FInputKeyEventArgs Args(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Key, bDown ? IE_Pressed : IE_Released, 1.f, false,
			FPlatformTime::Cycles64());
		PC->InputKey(Args);
	}

	/** 这一端看得到的标点，按种类数（Mine：只数自己的 / 只数别人的 / 都数） */
	int32 CountPings(UWorld* World, const APlayerController* PC, int32 Team, int32 Mine, EMechPing Kind = EMechPing::Count)
	{
		int32 N = 0;
		for (TActorIterator<AMechPing> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed() || !It->IsVisibleTo(PC, Team) || (Kind != EMechPing::Count && It->Kind != Kind))
			{
				continue;
			}
			const bool bMine = It->IsMine(PC);
			N += (Mine < 0 || (Mine == 1) == bMine) ? 1 : 0;
		}
		return N;
	}

	/** 这一端所有的标点（不管看不看得到：服务器上什么都有；客户端只收到复制给自己的）里，不是自己的有几个 */
	int32 CountForeign(UWorld* World, const APlayerController* PC, int32 Team, bool bEnemyOnly)
	{
		int32 N = 0;
		for (TActorIterator<AMechPing> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && !It->IsMine(PC) && (!bEnemyOnly || It->Team != Team))
			{
				++N;
			}
		}
		return N;
	}

	FString Describe(const TArray<FMechFeedLine>& L)
	{
		FString S;
		for (const FMechFeedLine& X : L)
		{
			S += FString::Printf(TEXT(" [%d %s%s%s]"), static_cast<int32>(X.E.Kind), *X.E.A, X.E.B.IsEmpty() ? TEXT("") : TEXT("→"), *X.E.B);
		}
		return S;
	}
}

void UMechCommsComponent::TestTick(float DeltaTime)
{
	APlayerController* PC = GetPC();
	UWorld* World = GetWorld();
	AMechCharacter* Me = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	const AMechGameState* GS = World ? World->GetGameState<AMechGameState>() : nullptr;
	if (!PC || !Me || !GS || !PC->PlayerCameraManager)
	{
		return;
	}
	const ENetMode Mode = World->GetNetMode();
	const bool bClient = Mode == NM_Client;
	const bool bHost = Mode == NM_ListenServer;
	// 单机等开打（团队淘汰有开场运镜、倒计时）；测试场 / 保卫战一直是「等待」也能动，有机体就开始
	if (TestClock < 0.f)
	{
		if (!(GS->CanAct() || (GS->bRangeMode && GS->Phase == EMechRoundPhase::Waiting)))
		{
			return;
		}
		TestClock = 0.f;
		UE_LOG(LogTemp, Display, TEXT("[pingtest] 开始（%s），自己 %s 队 %d"), bClient ? TEXT("联机客户端") : bHost ? TEXT("房主") : TEXT("单机"), *Me->DisplayName, Me->GetTeam());
	}
	// 排队的按键一帧处理一个：同一帧里按下又松开，标点的按键判断（每帧看一次键的状态）会漏掉
	if (TestKeyQueue.Num() > 0)
	{
		TestKey(TestKeyQueue[0] != 0);
		TestKeyQueue.RemoveAt(0);
	}
	const float T0 = TestClock;
	TestClock += DeltaTime;
	const float T = TestClock;
	auto At = [&](float S) { return T0 < S && T >= S; };
	auto Between = [&](float A, float B) { return T >= A && T < B; };
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	auto Face = [&](const FVector& P) {
		const FRotator R = (P - Cam).Rotation();
		PC->SetControlRotation(FRotator(FMath::Clamp(R.Pitch, -60.f, 40.f), R.Yaw, 0.f));
	};
	// 按开始时的朝向转（不能按机体现在的朝向：机体跟着镜头转，每帧加一次偏移会一直转圈）
	static float BaseYaw = 0.f;
	if (T0 <= 0.f)
	{
		BaseYaw = PC->GetControlRotation().Yaw;
	}
	auto FaceDir = [&](float YawOff, float Pitch) {
		PC->SetControlRotation(FRotator(Pitch, BaseYaw + YawOff, 0.f));
	};
	const int32 MyTeam = Me->GetTeam();
	if (GetOwner()->HasAuthority())
	{
		Me->GetHealth()->bNoDeath = true;  // 服务器上的自己打不死（客户端改不了，联机时客户端那台照常会死）
	}

	// ================================================================ -MechFeedIcons（11-01 击毁播报的武器剪影）：把所有武器 / 肩部装备 / 符号各放一行击毁，每页 6 行截一张（Saved/Screenshots/FeedIcons），放大看认不认得出
	static const bool bFeedIcons = FParse::Param(FCommandLine::Get(), TEXT("MechFeedIcons"));
	if (bFeedIcons)
	{
		constexpr float Page = 1.6f;
		static int32 LastPage = -1;
		const int32 PageNow = FMath::FloorToInt(T / Page);
		if (PageNow != FMath::RoundToInt(TestStepClock) - 1)
		{
			TestStepClock = PageNow + 1.f;  // 借这个字段记翻到第几页
			for (int32 k = 0; k < 6 && TestStep <= MechFeedIcon::GruntBoom; ++k)
			{
				FMechFeedEvent E;
				E.Kind = EMechFeed::Kill;
				E.Icon = static_cast<uint8>(TestStep++);
				E.A = k == 5 ? Me->DisplayName : FString::Printf(TEXT("蓝%d"), k % 3 + 2);  // 每页最上面一行是自己的（高亮）
				E.TeamA = MyTeam;
				E.B = FString::Printf(TEXT("红%d"), k % 3 + 1);
				E.TeamB = 1 - MyTeam;
				AddLine(E);
				LastPage = PageNow;
			}
		}
		if (PageNow > LastPage)
		{
			// 全放完了（不靠「落在某个时间窗里」判断退出：4K 帧率低会跳过，那次一直没退、多拍了 73 页）
			static bool bQuit = false;
			if (!bQuit)
			{
				bQuit = true;
				UE_LOG(LogTemp, Display, TEXT("[pingtest] 武器剪影截图完成，共 %d 行"), TestStep);
				PC->ConsoleCommand(TEXT("quit"));
			}
		}
		else if (T0 < (PageNow + 0.75f) * Page && T >= (PageNow + 0.75f) * Page)
		{
			int32 VX = 0, VY = 0;
			PC->GetViewportSize(VX, VY);
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/FeedIcons") / FString::Printf(TEXT("%dp_page%d.png"), VY, PageNow), true, false);
		}
		return;
	}

	// ================================================================ 联机：房主只看
	if (bHost)
	{
		static float NextLog = 2.f;
		static bool bShotPing = false, bShotFeed = false, bPlaced = false;
		const int32 Foreign = MechPingTestLocal::CountForeign(World, PC, MyTeam, false);
		const int32 Visible = MechPingTestLocal::CountPings(World, PC, MyTeam, 0);
		if (T >= NextLog)
		{
			NextLog += 2.f;
			UE_LOG(LogTemp, Display, TEXT("[pingtest] 房主 %.0f 秒：看得到别人的标点 %d 个（世界里别人的 %d 个），收到信息 %d 行：%s"), T, Visible, Foreign, DebugFeedReceived, *MechPingTestLocal::Describe(Lines));
		}
		if (!bShotPing && Visible > 0)
		{
			bShotPing = true;
			TestShot(TEXT("net_房主看到队友标点"));
		}
		if (!bShotFeed && DebugFeedReceived > 0 && Lines.ContainsByPredicate([](const FMechFeedLine& L) { return L.E.Kind == EMechFeed::Skill; }))
		{
			bShotFeed = true;
			TestShot(TEXT("net_房主信息区"));
		}
		// 客户端进来以后，房主自己也往前方地面标一个「防守」，让客户端那边检查收没收到
		int32 Humans = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			Humans += It->Get() ? 1 : 0;
		}
		static float JoinedAt = -1.f;
		if (Humans > 1 && JoinedAt < 0.f)
		{
			JoinedAt = T;
		}
		if (!bPlaced && JoinedAt >= 0.f && T > JoinedAt + 12.f)
		{
			bPlaced = true;
			FaceDir(0.f, -12.f);
			AMechPing::Place(PC, EMechPing::Defend, Me->GetActorLocation() + Me->GetActorForwardVector() * 1500.f, nullptr, 0, false);
		}
		// 房主也放一次肩部装备（换成紧急脱离：导弹没锁定按了不算）：客户端的信息区应该有「房主 [图标] 脱离」这一行（技能走服务器 → 同队客户端）
		static bool bEquipped = false, bUsedSkill = false;
		if (!bEquipped && JoinedAt >= 0.f && T > JoinedAt + 15.f)
		{
			bEquipped = true;
			FMechLoadout L = Me->GetLoadout();
			L.Shoulder = EMechShoulder::Burst;
			Me->SetLoadout(L, false);
		}
		if (!bUsedSkill && JoinedAt >= 0.f && T > JoinedAt + 16.f)
		{
			bUsedSkill = true;
			Me->MissilePressedForTest();
		}
		if (T > 180.f || (JoinedAt >= 0.f && T > JoinedAt + 42.f))
		{
			UE_LOG(LogTemp, Display, TEXT("[pingtest] 房主结束：看得到别人的标点（最后）%d，收到信息 %d 行，标点到达 %d 次"), Visible, DebugFeedReceived, DebugArrived);
			PC->ConsoleCommand(TEXT("quit"));
			TestClock = -1000.f;
		}
		return;
	}

	// ================================================================ 联机：客户端
	if (bClient)
	{
		if (Between(2.f, 2.5f))
		{
			FaceDir(0.f, -12.f);
		}
		if (At(2.5f)) TestTap(1);
		if (At(3.8f))
		{
			TestCheck(TEXT("客户端点一下：地点"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Go) == 1, FString::Printf(TEXT("自己的「去这里」%d 个"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Go)));
			TestShot(TEXT("net_客户端_地点"));
		}
		if (Between(4.f, 4.5f))
		{
			FaceDir(30.f, -10.f);
		}
		if (At(4.5f)) TestTap(2);
		if (At(6.f))
		{
			TestCheck(TEXT("客户端双击：危险"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Danger) == 1, FString::Printf(TEXT("自己的标点 %d 个（危险 %d）"),
				MechPingTestLocal::CountPings(World, PC, MyTeam, 1), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Danger)));
		}
		if (Between(6.2f, 6.5f))
		{
			FaceDir(-30.f, -10.f);
		}
		if (At(6.5f)) TestKey(true);
		if (At(7.f))
		{
			float YS, PS;
			MechComms::LookScale(PC, YS, PS);
			Me->DoLook(-8.f / YS, 0.f);  // 往左甩 = 「集合」
		}
		if (At(7.2f))
		{
			TestCheck(TEXT("客户端轮盘"), IsWheelOpen() && MechPing::WheelItem(GetWheelSlot()) == EMechPing::Regroup, FString::Printf(TEXT("开着 %d，选中 %d"), IsWheelOpen(), GetWheelSlot()));
			TestShot(TEXT("net_客户端_轮盘"));
		}
		if (At(7.4f)) TestKey(false);
		if (At(9.f))
		{
			TestCheck(TEXT("客户端轮盘放下：集合"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Regroup) == 1, FString::Printf(TEXT("自己的标点 %d 个"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1)));
			// 放肩部装备（走按键同一条路），房主那边的信息区应该有一行
			Me->MissilePressedForTest();
		}
		if (At(9.4f))
		{
			Me->MissileReleasedForTest();
		}
		if (At(16.f))
		{
			TestShot(TEXT("net_客户端_屏幕"));
		}
		if (At(26.f))
		{
			const int32 Host = MechPingTestLocal::CountPings(World, PC, MyTeam, 0);
			// 敌对两端跑这一项时应该「没通过」：世界里别人的标点也应该是 0（服务器根本没复制过来）
			TestCheck(TEXT("客户端收到房主的标点"), Host >= 1, FString::Printf(TEXT("看得到别人的 %d 个（这一端世界里别人的 %d 个，队伍不同的 %d 个），到达 %d 次；收到信息 %d 行：%s"), Host,
				MechPingTestLocal::CountForeign(World, PC, MyTeam, false), MechPingTestLocal::CountForeign(World, PC, MyTeam, true), DebugArrived,
				DebugFeedReceived, *MechPingTestLocal::Describe(Lines)));
			TestShot(TEXT("net_客户端_房主标点"));
		}
		if (At(30.f))
		{
			// 敌对两端跑这一项时 A 只会是本队的 AI，不会有房主
			const bool bHostSkill = Lines.ContainsByPredicate([&](const FMechFeedLine& L) { return L.E.Kind == EMechFeed::Skill && L.E.TeamA == MyTeam; });
			TestCheck(TEXT("客户端信息区有同队的技能"), bHostSkill, MechPingTestLocal::Describe(Lines));
			UE_LOG(LogTemp, Display, TEXT("[pingtest] 客户端汇总：通过 %d 项，没通过 %d 项"), TestPassed, TestFailed);
			PC->ConsoleCommand(TEXT("quit"));
		}
		return;
	}

	// ================================================================ 单机
	if (At(1.f))
	{
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (*It == Me || It->GetHealth()->IsDead())
			{
				continue;
			}
			if (It->GetTeam() == MyTeam && !TestMate.IsValid())
			{
				TestMate = *It;
				It->GetHealth()->bNoDeath = true;
			}
			else if (It->GetTeam() != MyTeam && !TestEnemy.IsValid())
			{
				TestEnemy = *It;
			}
		}
		TestCheck(TEXT("找到队友和敌人"), TestMate.IsValid() && TestEnemy.IsValid(), FString::Printf(TEXT("队友 %s 敌人 %s"),
			TestMate.IsValid() ? *TestMate->DisplayName : TEXT("没有"), TestEnemy.IsValid() ? *Cast<AMechCharacter>(TestEnemy.Get())->DisplayName : TEXT("没有")));
	}
	AMechCharacter* Enemy = Cast<AMechCharacter>(TestEnemy.Get());
	// ---- 1 敌人：挪到面前 16 米，瞄着点一下
	if (Between(2.f, 2.8f) && Enemy)
	{
		if (At(2.f))
		{
			FVector P = Me->GetActorLocation() + FRotator(0.f, BaseYaw, 0.f).Vector() * 1600.f;
			P.Z = Me->GetActorLocation().Z + 30.f;
			Enemy->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		}
		Face(Enemy->GetMechMesh()->VisualPivot());
	}
	if (At(2.4f)) TestTap(1);
	if (At(3.f))
	{
		AMechPing* Got = nullptr;
		for (TActorIterator<AMechPing> It(World); It; ++It)
		{
			Got = It->IsMine(PC) && It->Kind == EMechPing::Enemy ? *It : Got;
		}
		TestCheck(TEXT("点一下敌人：敌人标记"), Got && Got->Target == Enemy, FString::Printf(TEXT("%s，目标 %s"), Got ? TEXT("有敌人标记") : TEXT("没有"),
			Got && Got->Target ? *Got->Target->GetName() : TEXT("无")));
		TestShot(TEXT("01_敌人标记"));
	}
	// ---- 2 地点：往前方地面点一下
	if (Between(3.3f, 3.9f))
	{
		FaceDir(10.f, -14.f);
	}
	if (At(3.6f)) TestTap(1);
	if (At(4.3f))
	{
		TestCheck(TEXT("点地面：去这里"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Go) == 1, FString::Printf(TEXT("自己的标点 %d 个"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1)));
		for (TActorIterator<AMechPing> It(World); It; ++It)
		{
			if (It->IsMine(PC) && It->Kind == EMechPing::Go)
			{
				TestSpot = It->Anchor;
			}
		}
		TestShot(TEXT("02_地点"));
	}
	// ---- 3 双击：危险（换掉第一下放的「去这里」）
	if (Between(4.6f, 5.4f))
	{
		FaceDir(-28.f, -12.f);
	}
	if (At(5.f)) TestTap(2);
	if (At(6.f))
	{
		TestCheck(TEXT("双击：危险"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Danger) == 1 && MechPingTestLocal::CountPings(World, PC, MyTeam, 1) == 3,
			FString::Printf(TEXT("自己的标点 %d 个（危险 %d、去这里 %d、敌人 %d）"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Danger),
				MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Go), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Enemy)));
	}
	// ---- 4 按住出轮盘、往上甩选「进攻」、松开
	if (Between(6.2f, 6.5f))
	{
		FaceDir(-8.f, -9.f);
	}
	if (At(6.5f)) TestKey(true);
	if (At(6.85f))
	{
		TestCheck(TEXT("按住出轮盘"), IsWheelOpen(), FString::Printf(TEXT("轮盘 %s"), IsWheelOpen() ? TEXT("开着") : TEXT("没开")));
	}
	if (At(6.9f))
	{
		// 轮盘开着时转镜头的输入交给轮盘：往上甩 8 度（按「镜头本来会转几度」换算回原始输入）
		const FRotator Before = PC->GetControlRotation();
		float YS, PS;
		MechComms::LookScale(PC, YS, PS);
		Me->DoLook(0.f, 8.f / PS);
		TestStepClock = FMath::Abs(FRotator::NormalizeAxis(PC->GetControlRotation().Pitch - Before.Pitch));
	}
	if (At(7.1f))
	{
		TestCheck(TEXT("轮盘往上甩：进攻"), GetWheelSlot() == 0 && MechPing::WheelItem(GetWheelSlot()) == EMechPing::Attack,
			FString::Printf(TEXT("选中第 %d 格（%s），光标 %s"), GetWheelSlot(), GetWheelSlot() >= 0 ? MechPing::Label(MechPing::WheelItem(GetWheelSlot())) : TEXT("无"), *GetWheelCursor().ToString()));
		TestShot(TEXT("03_轮盘"));
	}
	if (At(7.4f))
	{
		TestKey(false);
	}
	if (At(8.2f))
	{
		TestCheck(TEXT("轮盘放下 + 每人最多 3 个"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Attack) == 1 && MechPingTestLocal::CountPings(World, PC, MyTeam, 1) == 3 &&
			MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Enemy) == 0, FString::Printf(TEXT("自己的标点 %d 个（进攻 %d、敌人 %d）"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1),
				MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Attack), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Enemy)));
	}
	// ---- 5 对着自己的「去这里」再点一下：撤掉
	if (Between(8.4f, 9.3f) && !TestSpot.IsZero())
	{
		Face(TestSpot);
	}
	if (At(8.9f)) TestTap(1);
	if (At(9.6f))
	{
		TestCheck(TEXT("对着自己的标点点一下：撤掉"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Go) == 0 && MechPingTestLocal::CountPings(World, PC, MyTeam, 1) == 2,
			FString::Printf(TEXT("去这里 %d 个，自己的标点 %d 个"), MechPingTestLocal::CountPings(World, PC, MyTeam, 1, EMechPing::Go), MechPingTestLocal::CountPings(World, PC, MyTeam, 1)));
	}
	// ---- 6 队友：替它标一个「集合」、放 EMP 和地雷、拿一个插件；自己击毁一台敌人
	if (At(10.f) && TestMate.IsValid())
	{
		TestFeedBefore = DebugFeedReceived;
		const int32 ArrivedBefore = DebugArrived;
		AMechPing::PlaceFor(TestMate.Get(), nullptr, TestMate->DisplayName, MyTeam, EMechPing::Regroup,
			Me->GetActorLocation() + Me->GetActorForwardVector() * 1800.f + Me->GetActorRightVector() * 600.f, nullptr, 0);
		TestCheck(TEXT("队友标点到达（响一声、记一行）"), DebugArrived > ArrivedBefore && Lines.Num() > 0 && Lines.Last().E.Kind == EMechFeed::Ping,
			FString::Printf(TEXT("到达 %d → %d，信息 %d 行"), ArrivedBefore, DebugArrived, Lines.Num()));
		FMechLoadout L = TestMate->GetLoadout();
		L.Shoulder = EMechShoulder::Emp;
		TestMate->SetLoadout(L, false);
	}
	if (At(11.5f) && TestMate.IsValid())
	{
		TestMate->MissilePressedForTest();
	}
	if (At(11.8f) && TestMate.IsValid())
	{
		TestMate->MissileReleasedForTest();
	}
	// 11-01 战术装备并入肩部：这四件也一件件放一次（中间隔开，信息区每帧才看一次冷却），按 E 的路；信息区应该各有一行
	{
		static const EMechShoulder Four[] = {EMechShoulder::Burst, EMechShoulder::Launch, EMechShoulder::Shockwave, EMechShoulder::Mine};
		for (int32 k = 0; k < int32(UE_ARRAY_COUNT(Four)); ++k)
		{
			if (At(11.9f + k * 0.25f) && TestMate.IsValid())
			{
				FMechLoadout L = TestMate->GetLoadout();
				L.Shoulder = Four[k];
				TestMate->SetLoadout(L, false);
			}
			if (At(12.0f + k * 0.25f) && TestMate.IsValid())
			{
				TestMate->MissilePressedForTest();
			}
		}
	}
	if (At(12.6f) && TestMate.IsValid())
	{
		const TArray<FName> Ids = MechEffects::PluginIds();
		if (Ids.Num() > 0)
		{
			const FTransform Where(TestMate->GetActorLocation());
			if (AMechPluginPod* Pod = World->SpawnActorDeferred<AMechPluginPod>(AMechPluginPod::StaticClass(), Where, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
			{
				Pod->Plugin = Ids[0];
				Pod->FinishSpawning(Where);
			}
		}
	}
	if (At(12.8f) && TestMate.IsValid())
	{
		TestMate->SetActorLocation(TestMate->GetActorLocation() + FVector(0.f, 0.f, 5.f));  // 万一生成时没触发重叠，挪一下
	}
	if (At(13.4f) && Enemy && !Enemy->GetHealth()->IsDead())
	{
		Enemy->ReceiveHit(1.e7f, 0.f, Me, Enemy->GetActorLocation(), EMechHitKind::Bullet);
	}
	if (At(14.2f))
	{
		int32 Kinds[8] = {};
		for (const FMechFeedLine& Line : Lines)
		{
			++Kinds[FMath::Clamp(static_cast<int32>(Line.E.Kind), 0, 7)];
		}
		TestCheck(TEXT("队友技能进信息区"), Kinds[static_cast<int32>(EMechFeed::Skill)] >= 1, FString::Printf(TEXT("技能 %d 行"), Kinds[static_cast<int32>(EMechFeed::Skill)]));
		FString FourSeen;
		int32 FourCount = 0;
		for (const EMechShoulder S : {EMechShoulder::Burst, EMechShoulder::Launch, EMechShoulder::Shockwave, EMechShoulder::Mine})
		{
			const uint8 Icon = MechFeedIcon::Shoulder0 + static_cast<uint8>(S);
			if (Lines.ContainsByPredicate([Icon](const FMechFeedLine& Line) { return Line.E.Kind == EMechFeed::Skill && Line.E.Icon == Icon; }))
			{
				++FourCount;
				FourSeen += FString::Printf(TEXT("「%s」"), MechFeedIcon::Word(Icon));
			}
		}
		TestCheck(TEXT("紧急脱离等四件进信息区"), FourCount == 4, FString::Printf(TEXT("看到 %d 件：%s"), FourCount, *FourSeen));
		TestCheck(TEXT("队友拿插件进信息区"), Kinds[static_cast<int32>(EMechFeed::Plugin)] >= 1, FString::Printf(TEXT("插件 %d 行"), Kinds[static_cast<int32>(EMechFeed::Plugin)]));
		TestCheck(TEXT("击毁进信息区"), Kinds[static_cast<int32>(EMechFeed::Kill)] >= 1, FString::Printf(TEXT("击毁 %d 行"), Kinds[static_cast<int32>(EMechFeed::Kill)]));
		UE_LOG(LogTemp, Display, TEXT("[pingtest] 信息区现在：%s"), *MechPingTestLocal::Describe(Lines));
		FaceDir(-12.f, -8.f);
	}
	if (At(14.6f))
	{
		TestShot(TEXT("04_标点和信息区"));
	}
	// ---- 7 大地图、关掉信息区
	if (At(15.f)) MechPingTestLocal::InjectKey(PC, MechKeys::Key(EMechKey::Map), true);
	if (At(15.8f)) TestShot(TEXT("05_大地图"));
	if (At(16.1f)) MechPingTestLocal::InjectKey(PC, MechKeys::Key(EMechKey::Map), false);
	if (At(16.6f))
	{
		FMechSettings::Get().bShowFeed = false;  // 只改内存里的，不存盘
	}
	if (At(17.f)) TestShot(TEXT("06_关掉信息区"));
	if (At(17.4f))
	{
		FMechSettings::Get().bShowFeed = true;
	}
	if (At(18.f))
	{
		TestCheck(TEXT("轮盘开着时镜头不转"), TestStepClock < 0.01f, FString::Printf(TEXT("甩轮盘时镜头俯仰变了 %.3f 度"), TestStepClock));
		UE_LOG(LogTemp, Display, TEXT("[pingtest] 汇总：通过 %d 项，没通过 %d 项"), TestPassed, TestFailed);
		PC->ConsoleCommand(TEXT("quit"));
	}
}
