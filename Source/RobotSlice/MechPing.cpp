#include "MechPing.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "MechCharacter.h"
#include "MechComms.h"
#include "MechCoreDirector.h"
#include "MechGameState.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"
#include "MechHudTheme.h"
#include "MechMeshComponent.h"

// ---------------------------------------------------------------- 种类表
EMechPing MechPing::WheelItem(int32 Slot)
{
	// 轮盘从正上方顺时针：上「进攻」、下「防守」相对，左右是「集合」「注意」，敌情类在右半边（右手甩得顺）
	static const EMechPing Items[WheelSlots] = {
		EMechPing::Attack,        // 上
		EMechPing::Danger,        // 右上
		EMechPing::Watch,         // 右
		EMechPing::EnemyWasHere,  // 右下
		EMechPing::Defend,        // 下
		EMechPing::Help,          // 左下
		EMechPing::Regroup,       // 左
		EMechPing::Go,            // 左上
	};
	return Items[((Slot % WheelSlots) + WheelSlots) % WheelSlots];
}

const TCHAR* MechPing::Word(EMechPing Kind)
{
	switch (Kind)
	{
	case EMechPing::Go: return TEXT("去这里");
	case EMechPing::Enemy: return TEXT("敌人");
	case EMechPing::Item: return TEXT("物品");
	case EMechPing::Danger: return TEXT("危险");
	case EMechPing::Attack: return TEXT("进攻");
	case EMechPing::Defend: return TEXT("防守");
	case EMechPing::Regroup: return TEXT("集合");
	case EMechPing::Watch: return TEXT("注意");
	case EMechPing::EnemyWasHere: return TEXT("敌人来过");
	case EMechPing::Help: return TEXT("支援");
	default: return TEXT("");
	}
}

const TCHAR* MechPing::Label(EMechPing Kind)
{
	switch (Kind)
	{
	case EMechPing::Go: return TEXT("去这里");
	case EMechPing::Danger: return TEXT("有敌人在这");
	case EMechPing::Attack: return TEXT("进攻这里");
	case EMechPing::Defend: return TEXT("防守这里");
	case EMechPing::Regroup: return TEXT("集合");
	case EMechPing::Watch: return TEXT("注意这里");
	case EMechPing::EnemyWasHere: return TEXT("有敌人来过");
	case EMechPing::Help: return TEXT("需要支援");
	default: return Word(Kind);
	}
}

float MechPing::Life(EMechPing Kind)
{
	// 敌人标记只跟几秒（看到的那一下有用，久了位置就不准了）；危险、注意、支援是一阵子的事；进攻 / 防守 / 集合是计划，留得久一点
	switch (Kind)
	{
	case EMechPing::Enemy: return 6.f;
	case EMechPing::Danger: return 12.f;
	case EMechPing::Help: return 10.f;
	case EMechPing::Watch: return 15.f;
	case EMechPing::EnemyWasHere: return 15.f;
	case EMechPing::Defend: return 25.f;
	default: return 20.f;
	}
}

bool MechPing::IsPlace(EMechPing Kind)
{
	return Kind != EMechPing::Enemy && Kind != EMechPing::Item && Kind != EMechPing::Help;
}

bool MechPing::IsThreat(EMechPing Kind)
{
	return Kind == EMechPing::Enemy || Kind == EMechPing::Danger || Kind == EMechPing::EnemyWasHere;
}

FLinearColor MechPing::Color(EMechPing Kind)
{
	const FMechHudTheme& Th = MechHudTheme::Get();
	switch (Kind)
	{
	case EMechPing::Enemy:
	case EMechPing::Danger:
	case EMechPing::EnemyWasHere:
		return Th.Enemy;
	case EMechPing::Attack:
	case EMechPing::Watch:
		return Th.Warn;
	case EMechPing::Defend:
	case EMechPing::Help:
		return Th.Ally;
	case EMechPing::Item:
		return Th.Gold;
	default:
		return Th.Accent;
	}
}

// ---------------------------------------------------------------- 标点 Actor
AMechPing::AMechPing()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);  // 位置走 Anchor（有目标时各端直接跟目标）
	bAlwaysRelevant = false;
	SetNetUpdateFrequency(10.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMechPing::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechPing, Kind);
	DOREPLIFETIME(AMechPing, Target);
	DOREPLIFETIME(AMechPing, CoreId);
	DOREPLIFETIME(AMechPing, Team);
	DOREPLIFETIME(AMechPing, bSolo);
	DOREPLIFETIME(AMechPing, OwnerPS);
	DOREPLIFETIME(AMechPing, ByName);
	DOREPLIFETIME(AMechPing, BornAt);
	DOREPLIFETIME(AMechPing, Life);
	DOREPLIFETIME(AMechPing, Anchor);
}

bool AMechPing::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	// 只复制给同队（个人死斗只给自己）：敌人那一端根本收不到这个标点。不看距离——标在多远都要看得到
	const APlayerController* PC = Cast<APlayerController>(RealViewer);
	if (!PC)
	{
		return false;
	}
	if (PC == GetOwner() || (OwnerPS && PC->PlayerState == OwnerPS))
	{
		return true;
	}
	return !bSolo && MechComms::TeamOf(PC) == Team;
}

float AMechPing::ServerNow() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
}

float AMechPing::GetAge() const
{
	return ServerNow() - BornAt;
}

bool AMechPing::IsMine(const APlayerController* PC) const
{
	return PC && ((OwnerPS && PC->PlayerState == OwnerPS) || GetOwner() == PC);
}

bool AMechPing::IsVisibleTo(const APlayerController* PC, int32 ViewerTeam) const
{
	if (IsMine(PC))
	{
		return true;
	}
	return !bSolo && ViewerTeam == Team;
}

FVector AMechPing::GetPoint() const
{
	if (CoreId > 0)
	{
		for (TActorIterator<AMechCoreDirector> It(GetWorld()); It; ++It)
		{
			for (const FMechCore& C : It->Cores)
			{
				if (C.Id == CoreId)
				{
					return C.State == EMechCoreState::Carried && C.Carrier ? C.Carrier->GetMechMesh()->VisualPivot() : FVector(C.Location);
				}
			}
		}
		return Anchor;
	}
	if (IsValid(Target))
	{
		if (const AMechCharacter* M = Cast<AMechCharacter>(Target))
		{
			return M->GetMechMesh()->VisualPivot();  // 画面上的机体（联机插值后），和头顶名字、敌我标记一致
		}
		return Target->GetActorLocation();
	}
	return Anchor;
}

void AMechPing::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
	// 到达这一端：本机玩家看得到就响一声、记一行（房主那台服务器上，敌队的标点也会生成，这里按队伍滤掉）
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController() && IsVisibleTo(PC, MechComms::TeamOf(PC)))
			{
				if (UMechCommsComponent* Comms = MechComms::Get(PC))
				{
					Comms->OnPingArrived(this);
				}
			}
		}
	}
}

void AMechPing::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	// 服务器：跟着目标走（Anchor 复制下去，客户端看不到目标时也大致对）；目标没了——敌人被击毁 / 杂兵死了留 1.5 秒，插件舱被拿走 / 核心交掉就撤
	if (CoreId > 0)
	{
		bool bFound = false;
		for (TActorIterator<AMechCoreDirector> It(GetWorld()); It && !bFound; ++It)
		{
			for (const FMechCore& C : It->Cores)
			{
				if (C.Id == CoreId)
				{
					Anchor = C.State == EMechCoreState::Carried && C.Carrier ? C.Carrier->GetActorLocation() : FVector(C.Location);
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			Destroy();
		}
		return;
	}
	if (!Target)
	{
		return;
	}
	bool bGone = !IsValid(Target);
	if (const AMechCharacter* M = Cast<AMechCharacter>(Target))
	{
		bGone = bGone || M->GetHealth()->IsDead();
	}
	else if (const AMechGrunt* G = Cast<AMechGrunt>(Target))
	{
		bGone = bGone || !G->IsAlive();
	}
	if (!bGone)
	{
		Anchor = Target->GetActorLocation();
		return;
	}
	if (Kind == EMechPing::Item)
	{
		Destroy();
		return;
	}
	Target = nullptr;
	const float Left = FMath::Min(Life - GetAge(), 1.5f);
	Life = GetAge() + FMath::Max(Left, 0.1f);
	SetLifeSpan(FMath::Max(Left, 0.1f));
}

AMechPing* AMechPing::PlaceFor(AActor* OwnerActor, APlayerState* PS, const FString& Name, int32 InTeam, EMechPing Kind, const FVector& At, AActor* InTarget, int32 InCoreId)
{
	UWorld* World = OwnerActor ? OwnerActor->GetWorld() : nullptr;
	if (!World || Kind >= EMechPing::Count)
	{
		return nullptr;
	}
	const AMechGameState* GS = World->GetGameState<AMechGameState>();
	AMechPing* P = World->SpawnActorDeferred<AMechPing>(AMechPing::StaticClass(), FTransform(At), OwnerActor, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!P)
	{
		return nullptr;
	}
	P->Kind = Kind;
	P->Target = InTarget;
	P->CoreId = InCoreId;
	P->Team = InTeam;
	P->bSolo = GS && GS->IsSoloDM();
	P->OwnerPS = PS;
	P->ByName = Name;
	P->BornAt = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	P->Life = MechPing::Life(Kind);
	P->Anchor = InTarget ? InTarget->GetActorLocation() : At;
	P->FinishSpawning(FTransform(At));
	P->SetLifeSpan(P->Life);
	UE_LOG(LogTemp, Display, TEXT("[ping] %s（%d 队）标了「%s」%s"), *Name, InTeam, MechPing::Word(Kind),
		InTarget ? *FString::Printf(TEXT("在 %s 上"), *InTarget->GetName()) : InCoreId > 0 ? TEXT("在核心上") : *FString::Printf(TEXT("在 %s"), *At.ToCompactString()));
	return P;
}

AMechPing* AMechPing::Place(APlayerController* By, EMechPing Kind, const FVector& At, AActor* InTarget, int32 InCoreId, bool bReplaceRecent)
{
	if (!By || !By->GetWorld() || Kind >= EMechPing::Count)
	{
		return nullptr;
	}
	UWorld* World = By->GetWorld();
	const AGameStateBase* GS = World->GetGameState();
	const float Now = GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	// 这个人现在的标点（按放下的先后）：双击把刚放的那个换掉；满了最旧的消失
	TArray<AMechPing*> Mine;
	for (TActorIterator<AMechPing> It(World); It; ++It)
	{
		if (It->GetOwner() == By && !It->IsActorBeingDestroyed())
		{
			Mine.Add(*It);
		}
	}
	Mine.Sort([](const AMechPing& A, const AMechPing& B) { return A.BornAt < B.BornAt; });
	if (bReplaceRecent && Mine.Num() > 0 && Now - Mine.Last()->BornAt < 0.8f)
	{
		Mine.Last()->Destroy();
		Mine.Pop();
	}
	while (Mine.Num() >= MechPing::MaxPerPlayer)
	{
		Mine[0]->Destroy();
		Mine.RemoveAt(0);
	}
	const UMechCommsComponent* Comms = MechComms::Get(By);
	// 需要支援：标在自己身上，跟着自己
	if (Kind == EMechPing::Help)
	{
		InTarget = By->GetPawn();
	}
	return PlaceFor(By, By->PlayerState, Comms ? Comms->GetServerName() : FString(), MechComms::TeamOf(By), Kind, At, InTarget, InCoreId);
}
