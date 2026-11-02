#include "MechCoreDirector.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechAIController.h"
#include "MechCharacter.h"
#include "MechCoreArena.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FLinearColor CoreColor[3] = {FLinearColor(0.15f, 0.75f, 1.f), FLinearColor(1.f, 0.12f, 0.06f), FLinearColor(1.f, 0.85f, 0.45f)};
	const FLinearColor& ColorOf(int32 Team)
	{
		return CoreColor[Team == 0 ? 0 : Team == 1 ? 1 : 2];
	}
	const TCHAR* TeamWord(int32 Team)
	{
		return Team == 0 ? TEXT("蓝队") : TEXT("红队");
	}
}

AMechCoreDirector::AMechCoreDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(20.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMechCoreDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechCoreDirector, Cores);
	DOREPLIFETIME(AMechCoreDirector, Downed);
	DOREPLIFETIME(AMechCoreDirector, ActiveStation);
	DOREPLIFETIME(AMechCoreDirector, NextSwapTime);
	DOREPLIFETIME(AMechCoreDirector, NextNeutralTime);
	DOREPLIFETIME(AMechCoreDirector, Arena);
}

void AMechCoreDirector::BeginPlay()
{
	Super::BeginPlay();
}

float AMechCoreDirector::NetTime() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AMechCoreDirector::Setup(AMechGameMode* InMode, AMechCoreArena* InArena)
{
	Mode = InMode;
	Arena = InArena;
}

void AMechCoreDirector::ResetMatch()
{
	for (FMechCore& C : Cores)
	{
		if (C.Carrier)
		{
			C.Carrier->SetCarryingCore(false);
		}
	}
	Cores.Reset();
	Downed.Reset();
	ActiveStation[0] = ActiveStation[1] = 0;
	bRunning = false;
	StatScore[0] = StatScore[1] = 0;
	StatRescue = StatRecall = StatCapture = StatNeutral = StatThrow = StatCatch = StatSteal = StatDeaths = 0;
	StatCarrySum = StatDownSum = 0.f;
	StatCarryCount = StatDownCount = 0;
}

void AMechCoreDirector::BeginFight()
{
	FightStart = NetTime();
	NextSwapTime = FightStart + StationSwapEvery;
	NextNeutralTime = FightStart + NeutralFirst;
	bSwapWarned = bNeutralWarned = false;
	bRunning = true;
	AIClock = 0.f;
}

void AMechCoreDirector::StopMatch()
{
	bRunning = false;
	for (FMechCore& C : Cores)
	{
		if (C.Carrier)
		{
			C.Carrier->SetCarryingCore(false);
		}
	}
}

FVector AMechCoreDirector::StationLocation(int32 Team, int32 Index) const
{
	return Arena ? Arena->StationLocation(Team, Index) : FVector::ZeroVector;
}

const FMechCore* AMechCoreDirector::CoreCarriedBy(const AMechCharacter* Mech) const
{
	return Cores.FindByPredicate([Mech](const FMechCore& C) { return C.State == EMechCoreState::Carried && C.Carrier == Mech; });
}

const FMechCore* AMechCoreDirector::CoreOfVictim(const AMechCharacter* Victim) const
{
	return Cores.FindByPredicate([Victim](const FMechCore& C) { return C.Victim == Victim; });
}

const FMechDowned* AMechCoreDirector::DownedOf(const AMechCharacter* Mech) const
{
	return Downed.FindByPredicate([Mech](const FMechDowned& D) { return D.Mech == Mech; });
}

AMechCharacter* AMechCoreDirector::HumanMateNear(int32 Team, const FVector& At) const
{
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() == Team && It->IsPlayerControlled() && !It->GetHealth()->IsDead() &&
			FVector::Dist(It->GetActorLocation(), At) < HumanFirstRadius)
		{
			return *It;
		}
	}
	return nullptr;
}

FMechDowned* AMechCoreDirector::FindDowned(const AMechCharacter* Mech)
{
	return Downed.FindByPredicate([Mech](const FMechDowned& D) { return D.Mech == Mech; });
}

void AMechCoreDirector::Feed(const FString& Text, int32 Team) const
{
	if (AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>())
	{
		GS->MulticastFeed(Text, Team);
	}
	UE_LOG(LogTemp, Display, TEXT("[core] %.1f 秒：%s"), NetTime() - FightStart, *Text);
}

// ---------------------------------------------------------------- 阵亡掉芯
void AMechCoreDirector::OnMechDied(AMechCharacter* Victim, AActor* Killer)
{
	if (!bRunning || !Victim)
	{
		return;
	}
	++StatDeaths;
	const FVector At = Victim->GetActorLocation();
	// 身上带着的核心掉在原地（错开一点，免得和它自己的核心叠在一起）
	for (FMechCore& C : Cores)
	{
		if (C.State == EMechCoreState::Carried && C.Carrier == Victim)
		{
			Drop(C, At + FVector(160.f, 0.f, 0.f));
			Feed(FString::Printf(TEXT("%s 带着的核心掉了"), *Victim->DisplayName), Victim->GetTeam());
		}
	}
	FMechDowned& D = Downed.AddDefaulted_GetRef();
	D.Mech = Victim;
	D.Controller = Victim->GetController();
	D.DiedAt = NetTime();
	D.RespawnAt = D.DiedAt + NormalRespawn;
}

FMechCore& AMechCoreDirector::SpawnCore(int32 OwnerTeam, AMechCharacter* Victim, const FVector& At)
{
	FMechCore& C = Cores.AddDefaulted_GetRef();
	C.Id = NextId++;
	C.OwnerTeam = OwnerTeam;
	C.Victim = Victim;
	C.VictimName = Victim ? Victim->DisplayName : FString();
	C.State = EMechCoreState::Ground;
	Drop(C, At);
	return C;
}

void AMechCoreDirector::Drop(FMechCore& C, const FVector& At)
{
	const float Now = NetTime();
	if (C.Carrier)
	{
		C.Carrier->SetCarryingCore(false);
		StatCarrySum += Now - C.PickTime;
		++StatCarryCount;
	}
	// 落到地面上（离地 60 厘米）
	FVector P = At;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechCoreDrop), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, 120.f), At - FVector(0.f, 0.f, 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		P = Hit.ImpactPoint + FVector(0.f, 0.f, 60.f);
	}
	C.State = EMechCoreState::Ground;
	C.Carrier = nullptr;
	C.Grab = 0.f;
	C.Grabber = nullptr;
	C.Location = P;
	C.DropTime = Now;
	C.Channel = 0.f;
	C.Velocity = FVector::ZeroVector;
}

void AMechCoreDirector::PickUp(FMechCore& C, AMechCharacter* By)
{
	const float Now = NetTime();
	C.State = EMechCoreState::Carried;
	C.Carrier = By;
	C.Channel = 0.f;
	C.Grab = 0.f;
	C.Grabber = nullptr;
	C.PickTime = Now;
	C.Pulse = By->GetActorLocation();
	C.PulseTime = Now;
	By->SetCarryingCore(true);
	Feed(C.OwnerTeam < 0 ? FString::Printf(TEXT("%s 拿到了无主核心"), *By->DisplayName)
		: FString::Printf(TEXT("%s 带走了 %s 的核心"), *By->DisplayName, *C.VictimName), By->GetTeam());
}

void AMechCoreDirector::Resolve(int32 Index, float RespawnAt)
{
	FMechCore& C = Cores[Index];
	if (C.Carrier)
	{
		C.Carrier->SetCarryingCore(false);
		StatCarrySum += NetTime() - C.PickTime;
		++StatCarryCount;
	}
	if (FMechDowned* D = FindDowned(C.Victim))
	{
		D->RespawnAt = RespawnAt;
	}
	Smoothed.Remove(C.Id);
	Cores.RemoveAt(Index);
}

void AMechCoreDirector::Score(int32 Index, int32 Team)
{
	AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const FMechCore C = Cores[Index];
	const float Now = NetTime();
	++StatScore[Team];
	if (C.OwnerTeam < 0)
	{
		++StatNeutral;
	}
	else
	{
		++StatCapture;
	}
	Resolve(Index, Now);
	NextNeutralTime = Now + NeutralAfterScore;
	bNeutralWarned = false;
	if (!GS)
	{
		return;
	}
	++GS->TeamScore[Team];
	if (Mode.IsValid())
	{
		Mode->RecordCoreScore(C.Carrier);  // 个人交芯数（结算场景的铭牌 / MVP）
	}
	Feed(FString::Printf(TEXT("%s 交芯成功，%s 得分（蓝 %d : %d 红）"), C.Carrier ? *C.Carrier->DisplayName : TEXT("?"), TeamWord(Team),
		GS->TeamScore[0], GS->TeamScore[1]), Team);
	if ((GS->TeamScore[Team] >= GS->CoreTarget || GS->bOvertime) && Mode.IsValid())
	{
		Mode->EndCoreMatch(Team);
	}
}

void AMechCoreDirector::ThrowCore(AMechCharacter* Mech, const FVector& Aim)
{
	if (!bRunning || !Mech || Mech->GetHealth()->IsDead())
	{
		return;
	}
	for (FMechCore& C : Cores)
	{
		if (C.State != EMechCoreState::Carried || C.Carrier != Mech)
		{
			continue;
		}
		FVector Dir = Aim.GetSafeNormal();
		if (Dir.Z < 0.2f)
		{
			Dir = (Dir.GetSafeNormal2D() + FVector(0.f, 0.f, 0.2f)).GetSafeNormal();  // 平着或往下扔也抬一点，免得直接砸地上
		}
		const float Now = NetTime();
		Mech->SetCarryingCore(false);
		StatCarrySum += Now - C.PickTime;
		++StatCarryCount;
		++StatThrow;
		C.State = EMechCoreState::Thrown;
		C.Carrier = nullptr;
		C.Thrower = Mech;
		C.ThrowTime = Now;
		C.Channel = 0.f;
		C.Location = Mech->GetActorLocation() + FVector(0.f, 0.f, 200.f) + Dir.GetSafeNormal2D() * 140.f;
		C.Velocity = Dir * 1900.f + Mech->GetVelocity() * 0.4f;
		return;
	}
}

// ---------------------------------------------------------------- 每帧（服务器）
void AMechCoreDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && bRunning && Arena)
	{
		UpdateCores(DeltaSeconds);
		UpdateTimers();
		UpdateRespawns();
		AIClock -= DeltaSeconds;
		if (AIClock <= 0.f)
		{
			AIClock = 0.5f;
			AssignAI();
		}
	}
	UpdateVisuals(DeltaSeconds);
}

void AMechCoreDirector::UpdateCores(float DeltaSeconds)
{
	const float Now = NetTime();
	TArray<AMechCharacter*> Alive;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetHealth()->IsDead() && It->GetTeam() >= 0 && It->GetTeam() < 2 && !It->IsTrainingDummy())
		{
			Alive.Add(*It);
		}
	}
	for (int32 i = Cores.Num() - 1; i >= 0; --i)
	{
		FMechCore& C = Cores[i];
		if (C.State == EMechCoreState::Carried)
		{
			if (!IsValid(C.Carrier) || C.Carrier->GetHealth()->IsDead())
			{
				Drop(C, C.Location);
				continue;
			}
			C.Location = C.Carrier->GetActorLocation() + FVector(0.f, 0.f, 230.f);
			if (Now - C.PulseTime >= PulseEvery)
			{
				C.Pulse = C.Location;
				C.PulseTime = Now;
			}
			// 交芯：站在自家开启的回收站里读条，被打到失衡就断
			const int32 Team = C.Carrier->GetTeam();
			const FVector Station = ActiveStationLocation(Team);
			const FVector P = C.Carrier->GetActorLocation();
			const bool bIn = FVector::Dist2D(P, Station) < AMechCoreArena::StationRadius && FMath::Abs(P.Z - Station.Z) < 450.f;
			if (bIn && !C.Carrier->GetHealth()->IsStaggered())
			{
				C.Channel += DeltaSeconds / ChannelTime;
				if (C.Channel >= 1.f)
				{
					Score(i, Team);
				}
			}
			else
			{
				C.Channel = 0.f;
			}
			continue;
		}
		if (C.State == EMechCoreState::Thrown)
		{
			C.Velocity.Z += GetWorld()->GetGravityZ() * DeltaSeconds;
			const FVector From = C.Location;
			const FVector To = From + C.Velocity * DeltaSeconds;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(MechCoreThrow), false);
			if (GetWorld()->LineTraceSingleByObjectType(Hit, From, To, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
			{
				Drop(C, Hit.ImpactPoint + Hit.ImpactNormal * 40.f);
			}
			else
			{
				C.Location = To;
			}
			if (C.Location.Z < Arena->KillZ())
			{
				// 扔出了场地：回到铸造台
				++StatRecall;
				Drop(C, Arena->FoundryLocation() + FVector(0.f, 0.f, 100.f));
			}
		}
		// 掉在地上 15 秒没人捡：回到中央（本来就在中央的不动）
		if (C.State == EMechCoreState::Ground && Now - C.DropTime >= RecallTime &&
			FVector::Dist2D(C.Location, Arena->FoundryLocation()) > 300.f)
		{
			Feed(TEXT("核心回到中央"), -1);
			++StatRecall;
			Drop(C, Arena->FoundryLocation() + FVector(0.f, 0.f, 100.f));
			continue;
		}
		// 站在核心旁边读条：自家的 = 救人（0.8 秒），别人的 / 无主的 = 捡起（1.2 秒），被打到失衡就断；
		// 两队的人都在旁边时队友优先（敌人得先把他打断）。空中接住马上拿到（手上已经有一颗的、刚把它扔出去的不算）
		AMechCharacter* Rescuer = nullptr;
		float RescuerD = PickRadius;
		AMechCharacter* Best = nullptr;
		float BestD = PickRadius;
		for (AMechCharacter* M : Alive)
		{
			const float D = FVector::Dist(M->GetActorLocation() - FVector(0.f, 0.f, 70.f), C.Location);
			if (D >= PickRadius || M->GetHealth()->IsStaggered())
			{
				continue;
			}
			if (C.OwnerTeam >= 0 && M->GetTeam() == C.OwnerTeam)
			{
				if (D < RescuerD || M == C.Grabber)
				{
					Rescuer = M;
					RescuerD = M == C.Grabber ? -1.f : D;
				}
				continue;
			}
			if (CoreCarriedBy(M) || (C.Thrower == M && Now - C.ThrowTime < ThrowCooldown))
			{
				continue;
			}
			// 本队真人玩家就在附近：机器人不捡地上的核心，让给玩家（空中接住不受限，玩家扔给它是故意的）
			if (C.State == EMechCoreState::Ground && !M->IsPlayerControlled() && HumanMateNear(M->GetTeam(), C.Location))
			{
				continue;
			}
			if (D < BestD || M == C.Grabber)
			{
				Best = M;
				BestD = M == C.Grabber ? -1.f : D;
			}
		}
		if (C.State == EMechCoreState::Thrown && !Rescuer)
		{
			if (Best)
			{
				const bool bMate = C.Thrower.IsValid() && C.Thrower->GetTeam() == Best->GetTeam();
				bMate ? ++StatCatch : ++StatSteal;
				PickUp(C, Best);
			}
			continue;
		}
		AMechCharacter* Who = Rescuer ? Rescuer : Best;
		if (Who != C.Grabber)
		{
			C.Grabber = Who;
			C.Grab = 0.f;
		}
		if (!Who)
		{
			continue;
		}
		C.Grab += DeltaSeconds / (Who == Rescuer ? RescueTime : GrabTime);
		if (C.Grab < 1.f)
		{
			continue;
		}
		if (Who == Rescuer)
		{
			++StatRescue;
			if (Mode.IsValid())
			{
				Mode->RecordRescue(Rescuer, true);  // 11-01 积分制：捡回队友的核心 = 救人（他马上重生）
			}
			Feed(FString::Printf(TEXT("%s 救回了 %s"), *Rescuer->DisplayName, *C.VictimName), Rescuer->GetTeam());
			Resolve(i, Now);
			continue;
		}
		PickUp(C, Best);
	}
}

void AMechCoreDirector::UpdateTimers()
{
	const float Now = NetTime();
	if (Now - LastCarrierLog >= 10.f)
	{
		LastCarrierLog = Now;
		for (const FMechCore& C : Cores)
		{
			if (C.State != EMechCoreState::Carried || !C.Carrier)
			{
				continue;
			}
			const FVector P = C.Carrier->GetActorLocation() - Arena->Center();
			const int32 Team = C.Carrier->GetTeam();
			const AMechAIController* AI = Cast<AMechAIController>(C.Carrier->GetController());
			UE_LOG(LogTemp, Display, TEXT("[core] 带芯 %s 在 (%.0f, %.0f, %.0f) 速度 %.0f，离%s交芯点 %.0f 米，已带 %.0f 秒 %s"), *C.Carrier->DisplayName, P.X, P.Y, P.Z,
				C.Carrier->GetVelocity().Size2D(), *Arena->StationName(ActiveStation[Team]), FVector::Dist(C.Carrier->GetActorLocation(), ActiveStationLocation(Team)) / 100.f,
				Now - C.PickTime, AI ? *AI->CoreDebug() : TEXT("（玩家）"));
		}
	}
	if (Now >= NextSwapTime)
	{
		ActiveStation[0] = 1 - ActiveStation[0];
		ActiveStation[1] = 1 - ActiveStation[1];
		NextSwapTime += StationSwapEvery;
		bSwapWarned = false;
		Feed(FString::Printf(TEXT("回收站切换到%s"), *Arena->StationName(ActiveStation[0])), -1);
	}
	else if (!bSwapWarned && NextSwapTime - Now <= 10.f)
	{
		bSwapWarned = true;
		Feed(TEXT("回收站 10 秒后切换"), -1);
	}
	if (Cores.Num() == 0)
	{
		if (Now >= NextNeutralTime)
		{
			SpawnCore(-1, nullptr, Arena->FoundryLocation() + FVector(0.f, 0.f, 100.f));
			Feed(TEXT("中央铸造台产出了核心"), -1);
		}
		else if (!bNeutralWarned && NextNeutralTime - Now <= 5.f)
		{
			bNeutralWarned = true;
			Feed(TEXT("核心 5 秒后在中央产出"), -1);
		}
	}
}

void AMechCoreDirector::UpdateRespawns()
{
	const float Now = NetTime();
	for (int32 i = Downed.Num() - 1; i >= 0; --i)
	{
		FMechDowned& D = Downed[i];
		if (!IsValid(D.Mech))
		{
			Downed.RemoveAt(i);
			continue;
		}
		// 保险：核心不见了却还在等，就按正常时间重生
		if (D.RespawnAt < 0.f && !CoreOfVictim(D.Mech))
		{
			D.RespawnAt = FMath::Max(D.DiedAt + NormalRespawn, Now);
		}
		if (D.RespawnAt >= 0.f && Now >= D.RespawnAt)
		{
			AMechCharacter* Old = D.Mech;
			AController* Controller = D.Controller.Get();
			StatDownSum += Now - D.DiedAt;
			++StatDownCount;
			Downed.RemoveAt(i);
			if (Mode.IsValid())
			{
				Mode->RespawnMech(Old, Controller);
			}
		}
	}
}

// ---------------------------------------------------------------- AI 任务：救人 > 拦截（追 / 守站）> 捡芯 > 护送 > 抢无主核心
void AMechCoreDirector::AssignAI()
{
	using EGoal = AMechAIController::ECoreGoal;
	const float Now = NetTime();
	TArray<AMechCharacter*> Alive;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->GetHealth()->IsDead() && It->GetTeam() >= 0 && It->GetTeam() < 2)
		{
			Alive.Add(*It);
		}
	}
	for (int32 Team = 0; Team < 2; ++Team)
	{
		TArray<AMechCharacter*> Bots;
		for (AMechCharacter* M : Alive)
		{
			if (M->GetTeam() == Team && Cast<AMechAIController>(M->GetController()))
			{
				Bots.Add(M);
			}
		}
		if (!Bots.Num())
		{
			continue;
		}
		struct FTask
		{
			EGoal Goal;
			FVector Loc;
			AMechCharacter* Mech;
			float Priority;
			int32 Cap;
		};
		TArray<FTask> Tasks;
		for (const FMechCore& C : Cores)
		{
			const bool bOurs = C.OwnerTeam == Team;
			if (C.State == EMechCoreState::Carried && C.Carrier)
			{
				if (C.Carrier->GetTeam() == Team)
				{
					Tasks.Add({EGoal::Escort, C.Carrier->GetActorLocation(), C.Carrier, 3.f, 2});
				}
				else
				{
					Tasks.Add({EGoal::Intercept, C.Carrier->GetActorLocation(), C.Carrier, bOurs ? 4.5f : 4.f, 1});
					Tasks.Add({EGoal::Guard, ActiveStationLocation(C.Carrier->GetTeam()), C.Carrier, bOurs ? 4.2f : 3.8f, 1});
				}
			}
			else if (bOurs)
			{
				Tasks.Add({EGoal::Rescue, FVector(C.Location), nullptr, 5.f, 1});
			}
			else if (AMechCharacter* Human = C.State == EMechCoreState::Ground ? HumanMateNear(Team, C.Location) : nullptr)
			{
				// 本队真人就在核心附近：让他捡，机器人去护着他
				Tasks.Add({EGoal::Escort, Human->GetActorLocation(), Human, 3.6f, 2});
			}
			else
			{
				Tasks.Add({EGoal::Fetch, FVector(C.Location), nullptr, C.OwnerTeam < 0 ? 3.6f : 3.5f, C.OwnerTeam < 0 ? 2 : 1});
			}
		}
		// 核心快出了先往铸造台走（大场地从出生点推进过去要 25 秒左右）
		if (Cores.Num() == 0 && NextNeutralTime - Now < (Arena->Extent().X > 30000.f ? 25.f : 8.f))
		{
			Tasks.Add({EGoal::Foundry, Arena->FoundryLocation(), nullptr, 2.f, 2});
		}
		Tasks.Sort([](const FTask& A, const FTask& B) { return A.Priority > B.Priority; });
		// 抢芯 / 救人 / 去铸造台只派这个距离内的（小场地 90 米，近的去、其他人照常打）；
		// 大场地（自然地形 800 米长，出生点离中央就有 380 米）按场地长度放宽，不然没人去拿核心
		const float FetchRange = Arena->Extent().X > 30000.f ? Arena->Extent().X * 1.5f : 9000.f;

		TSet<AMechCharacter*> Used;
		for (AMechCharacter* Bot : Bots)
		{
			AMechAIController* AI = Cast<AMechAIController>(Bot->GetController());
			AI->CoreArena = Arena;
			AI->CoreGoal = EGoal::None;
			AI->CoreGoalMech = nullptr;
			const FMechCore* Held = CoreCarriedBy(Bot);
			if (!Held)
			{
				continue;
			}
			Used.Add(Bot);
			AI->CoreGoal = EGoal::Deliver;
			AI->CoreGoalLocation = ActiveStationLocation(Team);
			// 被追近了、身边有离敌人更远的队友：传给他
			AMechCharacter* Foe = nullptr;
			float FoeD = 1800.f;
			for (AMechCharacter* M : Alive)
			{
				const float D = FVector::Dist(M->GetActorLocation(), Bot->GetActorLocation());
				if (M->GetTeam() != Team && D < FoeD)
				{
					Foe = M;
					FoeD = D;
				}
			}
			if (Foe && Now - Held->PickTime > 1.5f && Held->Channel <= 0.f)
			{
				for (AMechCharacter* Mate : Alive)
				{
					const FVector To = Mate->GetActorLocation() - Bot->GetActorLocation();
					if (Mate == Bot || Mate->GetTeam() != Team || To.Size() > 2400.f || To.Size() < 600.f || FMath::Abs(To.Z) > 300.f ||
						FVector::Dist(Mate->GetActorLocation(), Foe->GetActorLocation()) < FoeD + 800.f)
					{
						continue;
					}
					// 按距离算仰角（初速 19 米/秒），落点在队友身上
					const float Range = To.Size2D();
					const float Sin2 = FMath::Clamp(Range * 980.f / (1900.f * 1900.f), 0.f, 1.f);
					const float Pitch = 0.5f * FMath::Asin(Sin2);
					const FVector Aim = To.GetSafeNormal2D() * FMath::Cos(Pitch) + FVector(0.f, 0.f, FMath::Sin(Pitch));
					UE_LOG(LogTemp, Display, TEXT("[core] %s 被 %s 追到 %.0f 米，把核心扔给 %s"), *Bot->DisplayName, *Foe->DisplayName, FoeD / 100.f, *Mate->DisplayName);
					ThrowCore(Bot, Aim);
					break;
				}
			}
		}
		for (const FTask& T : Tasks)
		{
			for (int32 k = 0; k < T.Cap; ++k)
			{
				AMechCharacter* Best = nullptr;
				float BestD = (T.Goal == EGoal::Rescue || T.Goal == EGoal::Fetch || T.Goal == EGoal::Foundry) ? FetchRange : 1e9f;
				for (AMechCharacter* Bot : Bots)
				{
					if (Used.Contains(Bot) || Bot == T.Mech)
					{
						continue;
					}
					const float D = FVector::Dist(Bot->GetActorLocation(), T.Loc);
					if (D < BestD)
					{
						BestD = D;
						Best = Bot;
					}
				}
				if (!Best)
				{
					break;
				}
				Used.Add(Best);
				AMechAIController* AI = Cast<AMechAIController>(Best->GetController());
				AI->CoreGoal = T.Goal;
				AI->CoreGoalLocation = T.Loc;
				AI->CoreGoalMech = T.Mech;
			}
		}
	}
}

// ---------------------------------------------------------------- 各端：画核心（晶体 + 笼子 + 光环 + 光柱）和回收站（地上的圈）
void AMechCoreDirector::UpdateVisuals(float DeltaSeconds)
{
	if (!Arena)
	{
		return;
	}
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/Materials/MI_Tracer.MI_Tracer"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	auto Make = [this, Glow](UStaticMesh* Mesh, UMaterialInstanceDynamic*& OutMat) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetupAttachment(RootComponent);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->RegisterComponent();
		OutMat = Glow ? UMaterialInstanceDynamic::Create(Glow, this) : nullptr;
		if (OutMat)
		{
			C->SetMaterial(0, OutMat);
		}
		return C;
	};
	const float T = NetTime();

	// 回收站：开着的亮、关着的暗；10 秒内要切换时，下一个站一闪一闪
	for (int32 i = 0; i < 4; ++i)
	{
		if (!Discs[i])
		{
			UMaterialInstanceDynamic* M = nullptr;
			Discs[i] = Make(Cylinder, M);
			DiscMats[i] = M;
			Discs[i]->SetWorldLocation(StationLocation(i / 2, i % 2) + FVector(0.f, 0.f, 3.f));
			Discs[i]->SetWorldScale3D(FVector(AMechCoreArena::StationRadius / 50.f, AMechCoreArena::StationRadius / 50.f, 0.04f));
		}
		const int32 Team = i / 2;
		const bool bActive = ActiveStation[Team] == i % 2;
		const bool bNext = !bActive && NextSwapTime - T < 10.f && NextSwapTime > T;
		if (DiscMats[i])
		{
			DiscMats[i]->SetVectorParameterValue(TEXT("Emissive"), ColorOf(Team));
			DiscMats[i]->SetScalarParameterValue(TEXT("EmissiveStrength"), bActive ? 3000.f : bNext && FMath::Fmod(T, 0.6f) < 0.3f ? 1500.f : 40.f);
		}
	}

	// 核心：竖着的菱形晶体（立方体立在一个角上）自转，外面一个线框笼子反着转，再绕两道倾斜的光环
	constexpr int32 RingSegs = 20;
	constexpr int32 FramePieces = 12 + RingSegs * 2;
	while (Orbs.Num() < Cores.Num())
	{
		UMaterialInstanceDynamic* M = nullptr;
		Orbs.Add(Make(Cube, M));
		OrbMats.Add(M);
		UMaterialInstanceDynamic* Unused = nullptr;
		UStaticMeshComponent* Beam = Make(Cylinder, Unused);
		if (Beam && M)
		{
			Beam->SetMaterial(0, M);
		}
		Beams.Add(Beam);
		UInstancedStaticMeshComponent* Frame = NewObject<UInstancedStaticMeshComponent>(this);
		Frame->SetStaticMesh(Cube);
		Frame->SetMobility(EComponentMobility::Movable);
		Frame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Frame->SetCastShadow(false);
		Frame->SetupAttachment(RootComponent);
		Frame->SetUsingAbsoluteLocation(true);
		Frame->SetUsingAbsoluteRotation(true);
		Frame->SetUsingAbsoluteScale(true);
		Frame->RegisterComponent();
		Frame->SetWorldTransform(FTransform::Identity);
		for (int32 k = 0; k < FramePieces; ++k)
		{
			Frame->AddInstance(FTransform::Identity, true);
		}
		UMaterialInstanceDynamic* FrameMat = Glow ? UMaterialInstanceDynamic::Create(Glow, this) : nullptr;
		if (FrameMat)
		{
			Frame->SetMaterial(0, FrameMat);
		}
		Frames.Add(Frame);
		FrameMats.Add(FrameMat);
	}
	for (int32 i = 0; i < Orbs.Num(); ++i)
	{
		const bool bUsed = Cores.IsValidIndex(i);
		Orbs[i]->SetVisibility(bUsed);
		Frames[i]->SetVisibility(bUsed);
		Beams[i]->SetVisibility(bUsed && Cores[i].State == EMechCoreState::Ground);
		if (!bUsed)
		{
			continue;
		}
		const FMechCore& C = Cores[i];
		FVector Want = C.Location;
		if (C.State == EMechCoreState::Carried && C.Carrier)
		{
			Want = C.Carrier->GetActorLocation() + FVector(0.f, 0.f, 230.f);
		}
		else if (C.State == EMechCoreState::Ground)
		{
			Want.Z += 40.f + FMath::Sin(T * 2.4f + C.Id) * 18.f;
		}
		FVector& S = Smoothed.FindOrAdd(C.Id, Want);
		S = (C.State == EMechCoreState::Carried || FVector::Dist(S, Want) > 1500.f) ? Want : FMath::VInterpTo(S, Want, DeltaSeconds, 14.f);
		const float Sc = C.State == EMechCoreState::Carried ? 0.6f : 1.f;
		const float Spin = T * 70.f + C.Id * 40.f;
		// 晶体：立方体立在一个角上，绕竖轴转
		Orbs[i]->SetWorldLocation(S);
		Orbs[i]->SetWorldRotation(FRotator(35.264f, Spin, 45.f));
		Orbs[i]->SetWorldScale3D(FVector(0.32f * Sc));
		if (OrbMats[i])
		{
			OrbMats[i]->SetVectorParameterValue(TEXT("Emissive"), ColorOf(C.OwnerTeam));
			OrbMats[i]->SetScalarParameterValue(TEXT("EmissiveStrength"), 1100.f + 350.f * FMath::Sin(T * 6.f));  // 白天曝光高要亮一些，太亮会糊成一大团
		}
		// 笼子（立方体线框，12 条棱）反着转；两道光环倾斜着各自转
		TArray<FTransform> Pieces;
		Pieces.Reserve(FramePieces);
		const FQuat Cage = FRotator(35.264f, -Spin * 0.6f, 45.f).Quaternion();
		const float Side = 82.f * Sc;
		const float Edge = 3.5f * Sc;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				const float A = (Corner & 1 ? 0.5f : -0.5f) * Side;
				const float B = (Corner & 2 ? 0.5f : -0.5f) * Side;
				const FVector Offset = Axis == 0 ? FVector(0.f, A, B) : Axis == 1 ? FVector(A, 0.f, B) : FVector(A, B, 0.f);
				const FVector Size = Axis == 0 ? FVector(Side + Edge, Edge, Edge) : Axis == 1 ? FVector(Edge, Side + Edge, Edge) : FVector(Edge, Edge, Side + Edge);
				Pieces.Add(FTransform(Cage, S + Cage.RotateVector(Offset), Size / 100.f));
			}
		}
		const float Radius = 72.f * Sc;
		const float SegLen = 2.f * PI * Radius / RingSegs * 1.08f;
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const FQuat Orient = FQuat(FVector::UpVector, FMath::DegreesToRadians(Ring == 0 ? 20.f : 110.f)) *
				FQuat(FVector::RightVector, FMath::DegreesToRadians(Ring == 0 ? 68.f : -62.f)) *
				FQuat(FVector::UpVector, FMath::DegreesToRadians(Spin * (Ring == 0 ? 1.4f : -1.1f)));
			for (int32 k = 0; k < RingSegs; ++k)
			{
				const float Angle = 2.f * PI * k / RingSegs;
				const FQuat Seg = Orient * FQuat(FVector::UpVector, Angle + PI * 0.5f);
				Pieces.Add(FTransform(Seg, S + Orient.RotateVector(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius),
					FVector(SegLen, 2.5f * Sc, 2.5f * Sc) / 100.f));
			}
		}
		Frames[i]->BatchUpdateInstancesTransforms(0, Pieces, true, true, true);
		if (FrameMats[i])
		{
			FrameMats[i]->SetVectorParameterValue(TEXT("Emissive"), ColorOf(C.OwnerTeam));
			FrameMats[i]->SetScalarParameterValue(TEXT("EmissiveStrength"), 380.f);
		}
		Beams[i]->SetWorldLocation(S + FVector(0.f, 0.f, 2000.f));
		Beams[i]->SetWorldScale3D(FVector(0.12f, 0.12f, 40.f));
	}
}

void AMechCoreDirector::LogSummary() const
{
	const float Len = NetTime() - FightStart;
	UE_LOG(LogTemp, Display, TEXT("[core] 结算：用时 %.0f 秒，得分 蓝 %d : %d 红，阵亡 %d，核心回到中央 %d 次，扔芯 %d（接住 %d、被抢 %d），平均带芯 %.1f 秒（%d 次），平均阵亡到重生 %.1f 秒（%d 次）"),
		Len, StatScore[0], StatScore[1], StatDeaths, StatRecall, StatThrow, StatCatch, StatSteal,
		StatCarryCount ? StatCarrySum / StatCarryCount : 0.f, StatCarryCount, StatDownCount ? StatDownSum / StatDownCount : 0.f, StatDownCount);
}
