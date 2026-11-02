#include "MechFeed.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "MechCharacter.h"
#include "MechComms.h"
#include "MechCoreDirector.h"
#include "MechDefense.h"
#include "MechGameState.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"

const TCHAR* MechFeedIcon::Word(uint8 Icon)
{
	static const TCHAR* Shoulders[] = {TEXT("导弹"), TEXT("火炮"), TEXT("护盾"), TEXT("钩爪"), TEXT("捕获网"), TEXT("烟雾弹"), TEXT("EMP"), TEXT("脱离"), TEXT("弹射"), TEXT("震荡波"), TEXT("地雷")};
	if (Icon >= Shoulder0 && Icon < Shoulder0 + UE_ARRAY_COUNT(Shoulders))
	{
		return Shoulders[Icon - Shoulder0];
	}
	return TEXT("");
}

bool UMechFeedSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMechFeedSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMechFeedSubsystem, STATGROUP_Tickables);
}

void UMechFeedSubsystem::Send(const FMechFeedEvent& E, int32 OnlyTeam, const AController* Except)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		UMechCommsComponent* C = PC && PC != Except ? MechComms::Get(PC) : nullptr;
		if (!C || (OnlyTeam >= 0 && C->GetServerTeam() != OnlyTeam))
		{
			continue;
		}
		C->ClientFeed(E);
		++Count;
	}
	++DebugSent[FMath::Clamp(static_cast<int32>(E.Kind), 0, 7)];
	UE_LOG(LogTemp, Display, TEXT("[feed] 种类 %d 图标 %d「%s」→「%s」%s %d（发给 %d 人，%s）"), static_cast<int32>(E.Kind), E.Icon, *E.A, *E.B,
		*E.Plugin.ToString(), E.Num, Count, OnlyTeam >= 0 ? *FString::Printf(TEXT("只给 %d 队"), OnlyTeam) : TEXT("所有人"));
}

void UMechFeedSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;  // 事件只在服务器上产生
	}
	Clock += DeltaTime;
	const AMechGameState* GS = World->GetGameState<AMechGameState>();
	if (!GS || GS->bMenuMode)
	{
		return;
	}
	// 能行动的阶段：开打；测试场 / 保卫战一直是「等待」，也能放技能（和 AMechCharacter::CanAct 一样）
	const bool bFight = GS->Phase == EMechRoundPhase::Waiting || GS->CanAct();
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M->IsTrainingDummy() || M->IsViewerMode())
		{
			continue;
		}
		FTrack& T = Tracks.FindOrAdd(M);
		const float Sh = M->GetShoulderTimer();
		if (!T.bBound)
		{
			// 第一次看到这台：接上阵亡事件（击毁那一行在这里发），记下现在的冷却
			T.bBound = true;
			T.Shoulder = Sh;
			T.Since = Clock;
			TWeakObjectPtr<AMechCharacter> Weak(M);
			M->GetHealth()->OnDied.AddWeakLambda(this, [this, Weak](AActor* Killer) {
				if (Weak.IsValid())
				{
					OnMechDied(Weak.Get(), Killer);
				}
			});
			continue;
		}
		// 冷却突然变长 = 刚用了一次（不改技能代码；冷却缩短 / 重置不会误报）。刚生成的头一秒、不在交战阶段不算
		const bool bLive = bFight && Clock - T.Since > 1.f && !M->GetHealth()->IsDead();
		if (bLive && Sh > T.Shoulder + 0.5f)
		{
			const uint8 Icon = MechFeedIcon::Shoulder0 + static_cast<uint8>(M->GetLoadout().Shoulder);
			if (Clock - T.ShoulderSent > 8.f || Icon != T.ShoulderSentIcon)
			{
				T.ShoulderSent = Clock;
				T.ShoulderSentIcon = Icon;
				FMechFeedEvent E;
				E.Kind = EMechFeed::Skill;
				E.Icon = Icon;
				E.A = M->DisplayName;
				E.TeamA = M->GetTeam();
				Send(E, M->GetTeam(), M->GetController());
			}
		}
		T.Shoulder = Sh;
	}
	for (auto It = Tracks.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	PollCores();
	PollDefense();
}

void UMechFeedSubsystem::NoteHit(AMechCharacter* Victim, AActor* Attacker, EMechHitKind Kind)
{
	if (Victim)
	{
		FTrack& T = Tracks.FindOrAdd(Victim);
		T.LastKind = Kind;
		T.bHit = true;
	}
}

void UMechFeedSubsystem::OnPlugin(AMechCharacter* Mech, FName Plugin)
{
	if (!Mech)
	{
		return;
	}
	FMechFeedEvent E;
	E.Kind = EMechFeed::Plugin;
	E.Plugin = Plugin;
	E.A = Mech->DisplayName;
	E.TeamA = Mech->GetTeam();
	Send(E, Mech->GetTeam(), Mech->GetController());
}

void UMechFeedSubsystem::OnMechDied(AMechCharacter* Victim, AActor* Killer)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !Victim)
	{
		return;
	}
	AMechCharacter* KM = Cast<AMechCharacter>(Killer);
	if (!KM)
	{
		if (const AController* C = Cast<AController>(Killer))
		{
			KM = Cast<AMechCharacter>(C->GetPawn());
		}
	}
	const AMechGrunt* Grunt = Cast<AMechGrunt>(Killer);
	const FTrack* VT = Tracks.Find(Victim);
	const EMechHitKind Kind = VT && VT->bHit ? VT->LastKind : EMechHitKind::Missile;
	FMechFeedEvent E;
	E.Kind = EMechFeed::Kill;
	E.B = Victim->DisplayName;
	E.TeamB = Victim->GetTeam();
	if (KM && KM != Victim)
	{
		E.A = KM->DisplayName;
		E.TeamA = KM->GetTeam();
		const FMechLoadout& L = KM->GetLoadout();
		if (Kind == EMechHitKind::Bullet)
		{
			E.Icon = MechFeedIcon::Shooter0 + static_cast<uint8>(L.Shooter);
		}
		else if (Kind == EMechHitKind::Blade)
		{
			E.Icon = MechFeedIcon::Melee0 + static_cast<uint8>(L.Melee);
		}
		else
		{
			// 爆炸类（导弹、炮弹、EMP、钩爪、网、地雷、震荡波都记成这一类）：按击毁者的配装猜。
			// 11-01 战术装备并入肩部后一人只带一件，不用再按冷却猜是哪件：带的这件能打人就是它，不能打人（护盾、烟雾弹、紧急脱离、弹射升空）写「爆炸」
			const EMechShoulder S = L.Shoulder;
			const bool bShoulderHurts = S != EMechShoulder::Shield && S != EMechShoulder::Smoke && S != EMechShoulder::Burst && S != EMechShoulder::Launch;
			E.Icon = bShoulderHurts ? MechFeedIcon::Shoulder0 + static_cast<uint8>(S) : MechFeedIcon::Blast;
		}
	}
	else if (Grunt)
	{
		// 保卫战：杂兵打死的（杂兵都是红队）
		E.A = AMechGrunt::KindName(Grunt->GetKind());
		E.TeamA = 1;
		E.Icon = Grunt->GetKind() == EMechGrunt::Gunner ? MechFeedIcon::GruntGun : Grunt->GetKind() == EMechGrunt::Kamikaze ? MechFeedIcon::GruntBoom : MechFeedIcon::GruntShell;
	}
	else
	{
		E.Icon = MechFeedIcon::Field;  // 场地：掉出地图、被自己的爆炸炸死……（没有击毁者，那一行只写被击毁的）
	}
	Send(E, -1, nullptr);
}

void UMechFeedSubsystem::PollCores()
{
	AMechCoreDirector* Dir = nullptr;
	for (TActorIterator<AMechCoreDirector> It(GetWorld()); It; ++It)
	{
		Dir = *It;
		break;
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!Dir || !GS)
	{
		return;
	}
	TSet<int32> Present;
	for (const FMechCore& C : Dir->Cores)
	{
		Present.Add(C.Id);
		FCoreSeen& S = CoreSeen.FindOrAdd(C.Id);
		const bool bCarried = C.State == EMechCoreState::Carried && C.Carrier;
		if (bCarried && (!S.bCarried || S.Carrier.Get() != C.Carrier))
		{
			FMechFeedEvent E;
			E.Kind = EMechFeed::CorePick;
			E.A = C.Carrier->DisplayName;
			E.TeamA = C.Carrier->GetTeam();
			E.TeamB = C.OwnerTeam;
			Send(E, -1, nullptr);
		}
		S.bCarried = bCarried;
		S.Carrier = bCarried ? C.Carrier.Get() : nullptr;
	}
	// 核心没了：带着它的人那队分数涨了 = 交芯成功（送回核心）
	for (auto It = CoreSeen.CreateIterator(); It; ++It)
	{
		if (Present.Contains(It.Key()))
		{
			continue;
		}
		const FCoreSeen S = It.Value();
		It.RemoveCurrent();
		const AMechCharacter* By = S.Carrier.Get();
		const int32 Team = By ? By->GetTeam() : -1;
		if (S.bCarried && By && Team >= 0 && Team < 2 && GS->TeamScore[Team] > ScoreSeen[Team])
		{
			FMechFeedEvent E;
			E.Kind = EMechFeed::CoreScore;
			E.A = By->DisplayName;
			E.TeamA = Team;
			E.Num = GS->TeamScore[Team];
			Send(E, -1, nullptr);
		}
	}
	ScoreSeen[0] = GS->TeamScore[0];
	ScoreSeen[1] = GS->TeamScore[1];
}

void UMechFeedSubsystem::PollDefense()
{
	AMechDefenseDirector* D = nullptr;
	for (TActorIterator<AMechDefenseDirector> It(GetWorld()); It; ++It)
	{
		D = *It;
		break;
	}
	if (!D)
	{
		return;
	}
	const bool bFightNow = D->Phase == EMechDefensePhase::Fight;
	if (bFightNow && !bWaveFightSeen && D->Wave > 0)
	{
		FMechFeedEvent E;
		E.Kind = EMechFeed::Wave;
		E.Num = D->Wave;
		Send(E, -1, nullptr);
	}
	bWaveFightSeen = bFightNow;
	// 指挥所被打：12 秒最多说一次（HUD 上指挥所血条本来就在闪，这里只是提醒一下在挨打）
	if (D->Core)
	{
		const float HP = D->Core->HP;
		if (PostHPSeen >= 0.f && HP < PostHPSeen - 1.f && Clock - PostSent > 12.f && !D->Core->IsDestroyed())
		{
			PostSent = Clock;
			FMechFeedEvent E;
			E.Kind = EMechFeed::PostHit;
			E.Num = FMath::RoundToInt(D->Core->GetHPFraction() * 100.f);
			Send(E, -1, nullptr);
		}
		PostHPSeen = HP;
	}
}

void MechFeed::NoteHit(AMechCharacter* Victim, AActor* Attacker, EMechHitKind Kind)
{
	UWorld* World = Victim ? Victim->GetWorld() : nullptr;
	if (UMechFeedSubsystem* S = World ? World->GetSubsystem<UMechFeedSubsystem>() : nullptr)
	{
		S->NoteHit(Victim, Attacker, Kind);
	}
}

void MechFeed::OnPlugin(AMechCharacter* Mech, FName Plugin)
{
	UWorld* World = Mech ? Mech->GetWorld() : nullptr;
	if (UMechFeedSubsystem* S = World ? World->GetSubsystem<UMechFeedSubsystem>() : nullptr)
	{
		S->OnPlugin(Mech, Plugin);
	}
}
