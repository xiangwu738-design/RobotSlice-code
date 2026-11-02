// 保卫战肉鸽成长的规则（11-01，见 MechRun.h）。钩子在 MechCombatComponent / MechHealthComponent / MechMissile / MechGrunt / MechDefense 里都是一两行，
// 规则集中写在这里：哪张卡做什么，一眼能查到。

#include "MechRun.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechDefense.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"
#include "MechMissile.h"
#include "MechMovementComponent.h"
#include "MechCatalog.h"
#include "MechScore.h"
#include "MechShoulderComponent.h"
#include "MechTacticalComponent.h"
#include "MechWeapons.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

using namespace MechRun;

namespace MechRunLocal
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	// 卡的文字（卡名 + 一句规则，要一眼看懂；顺序和 EMechRunCard 一致）
	const FCard Cards[] = {
		{TEXT("flood1"), TEXT("洪流·穿透"), TEXT("步枪"), TEXT("洪流"), TEXT("子弹穿过杂兵继续飞"), 1, 3, EMechRunItem::Rifle},
		{TEXT("flood2"), TEXT("洪流·不断连"), TEXT("步枪"), TEXT("洪流"), TEXT("换目标不清连中层数"), 2, 3, EMechRunItem::Rifle},
		{TEXT("flood3"), TEXT("洪流·追踪弹"), TEXT("步枪"), TEXT("洪流"), TEXT("连中满层时，每发分出一枚追踪弹"), 3, 3, EMechRunItem::Rifle},
		{TEXT("burst1"), TEXT("点射·三连发"), TEXT("步枪"), TEXT("点射"), TEXT("改成三连发，单发更疼"), 1, 3, EMechRunItem::Rifle},
		{TEXT("burst2"), TEXT("点射·全中返弹"), TEXT("步枪"), TEXT("点射"), TEXT("三发全中，返还这组弹药"), 2, 3, EMechRunItem::Rifle},
		{TEXT("burst3"), TEXT("点射·引爆"), TEXT("步枪"), TEXT("点射"), TEXT("三发打中同一目标时引爆"), 3, 3, EMechRunItem::Rifle},
		{TEXT("rift1"), TEXT("地裂·冲击波"), TEXT("大剑"), TEXT("地裂"), TEXT("重斩放出 20 米冲击波"), 1, 3, EMechRunItem::GreatSword},
		{TEXT("rift2"), TEXT("地裂·三叉"), TEXT("大剑"), TEXT("地裂"), TEXT("冲击波分成三道扇形"), 2, 3, EMechRunItem::GreatSword},
		{TEXT("rift3"), TEXT("地裂·余烬"), TEXT("大剑"), TEXT("地裂"), TEXT("冲击波路径留下持续伤害"), 3, 3, EMechRunItem::GreatSword},
		{TEXT("charge1"), TEXT("蓄力·长蓄"), TEXT("大剑"), TEXT("蓄力"), TEXT("重斩可以一直蓄，越久范围越大"), 1, 3, EMechRunItem::GreatSword},
		{TEXT("charge2"), TEXT("蓄力·霸体"), TEXT("大剑"), TEXT("蓄力"), TEXT("蓄力时霸体，受伤减半"), 2, 3, EMechRunItem::GreatSword},
		{TEXT("charge3"), TEXT("蓄力·横贯"), TEXT("大剑"), TEXT("蓄力"), TEXT("蓄满放出横贯全场的一刀"), 3, 3, EMechRunItem::GreatSword},
		{TEXT("swarm1"), TEXT("蜂群·锁杂兵"), TEXT("导弹"), TEXT("蜂群"), TEXT("导弹能锁杂兵"), 1, 3, EMechRunItem::Missiles},
		{TEXT("swarm2"), TEXT("蜂群·分裂"), TEXT("导弹"), TEXT("蜂群"), TEXT("命中后分裂成 3 枚"), 2, 3, EMechRunItem::Missiles},
		{TEXT("swarm3"), TEXT("蜂群·全锁"), TEXT("导弹"), TEXT("蜂群"), TEXT("一次锁定视野内所有敌人"), 3, 3, EMechRunItem::Missiles},
		{TEXT("heavy1"), TEXT("重弹·合弹"), TEXT("导弹"), TEXT("重弹"), TEXT("一轮合成 1 枚大导弹"), 1, 3, EMechRunItem::Missiles},
		{TEXT("heavy2"), TEXT("重弹·震爆"), TEXT("导弹"), TEXT("重弹"), TEXT("爆炸范围翻倍，打出失衡"), 2, 3, EMechRunItem::Missiles},
		{TEXT("heavy3"), TEXT("重弹·火场"), TEXT("导弹"), TEXT("重弹"), TEXT("落点留下 3 秒火场"), 3, 3, EMechRunItem::Missiles},
		{TEXT("repair1"), TEXT("抢修·带伤修"), TEXT("新纪框架"), TEXT("抢修"), TEXT("挨打时也继续修甲，只是慢一半"), 1, 2, EMechRunItem::FrameNewEra},
		{TEXT("repair2"), TEXT("抢修·修到满"), TEXT("新纪框架"), TEXT("抢修"), TEXT("能修到满（原来最多修到一半）"), 2, 2, EMechRunItem::FrameNewEra},
		{TEXT("supply1"), TEXT("补给·补弹"), TEXT("维克托框架"), TEXT("补给"), TEXT("击杀补 1/3 弹匣"), 1, 2, EMechRunItem::FrameVictor},
		{TEXT("supply2"), TEXT("补给·减冷却"), TEXT("维克托框架"), TEXT("补给"), TEXT("击杀也缩短肩部冷却"), 2, 2, EMechRunItem::FrameVictor},
		{TEXT("air1"), TEXT("空战·制空"), TEXT("相位框架"), TEXT("空战"), TEXT("离地伤害 +40%，地面 −20%"), 1, 2, EMechRunItem::FramePhase},
		{TEXT("air2"), TEXT("空战·回能"), TEXT("相位框架"), TEXT("空战"), TEXT("空中击杀回一次闪避的能量"), 2, 2, EMechRunItem::FramePhase},
	};
	static_assert(UE_ARRAY_COUNT(Cards) == MechRun::NumCards, "EMechRunCard 加了新卡，这张表也要加");

	bool IsEnemyGrunt(const AActor* A)
	{
		const AMechGrunt* G = Cast<AMechGrunt>(A);
		return G && G->IsAlive();
	}

	bool IsEnemyMech(const AActor* A, int32 Team)
	{
		const AMechCharacter* M = Cast<AMechCharacter>(A);
		return M && M->GetTeam() != Team && !M->GetHealth()->IsDead();
	}

	float AngleBetween(const FVector& Dir, const FVector& To)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dir | To.GetSafeNormal(), -1.f, 1.f)));
	}

	bool HasLine(const UWorld* World, const FVector& From, const AActor* To)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechRunLine), false);
		return !World->LineTraceSingleByObjectType(Hit, From, To->GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic), Q);
	}

	// ---------------------------------------------------------------- 控制台：试打不到的卡
	void Cheat(UWorld* World, const FString& What, int32 Number)
	{
		APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		UMechRunComponent* Run = UMechRunComponent::Of(PC);
		if (!Run)
		{
			UE_LOG(LogTemp, Warning, TEXT("[run] 现在不在保卫战里（没有肉鸽组件）"));
			return;
		}
		Run->ServerCheat(What, Number);
	}

	FAutoConsoleCommandWithWorldAndArgs GMechRunGive(TEXT("mech.Run.Give"),
		TEXT("给一张肉鸽卡（连带它前面的步骤）：mech.Run.Give flood3 / rift1 / charge3 / swarm2 / heavy3 / repair2 / supply1 / air2 …，all = 全部（对应配装才生效），clear = 清空"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Cheat(World, Args.Num() > 0 ? Args[0] : FString(TEXT("list")), 0); }));
	FAutoConsoleCommandWithWorldAndArgs GMechRunXP(TEXT("mech.Run.XP"), TEXT("加经验：mech.Run.XP 200"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Cheat(World, TEXT("xp"), Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 100); }));
	FAutoConsoleCommandWithWorldAndArgs GMechRunList(TEXT("mech.Run.List"), TEXT("日志里列出所有肉鸽卡的代号、拿了没有"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) { Cheat(World, TEXT("list"), 0); }));
}

using namespace MechRunLocal;

const FCard& MechRun::Card(EMechRunCard C)
{
	return Cards[FMath::Clamp(static_cast<int32>(C), 0, NumCards - 1)];
}

EMechRunCard MechRun::FindCard(const FString& Code)
{
	for (int32 i = 0; i < NumCards; ++i)
	{
		if (Code.Equals(Cards[i].Code, ESearchCase::IgnoreCase) || Code.Equals(Cards[i].Name, ESearchCase::IgnoreCase))
		{
			return static_cast<EMechRunCard>(i);
		}
	}
	return EMechRunCard::Num;
}

// ================================================================ 组件
UMechRunComponent::UMechRunComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

void UMechRunComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 组件在玩家控制器上，控制器只复制给本人
	DOREPLIFETIME(UMechRunComponent, Level);
	DOREPLIFETIME(UMechRunComponent, XP);
	DOREPLIFETIME(UMechRunComponent, Owned);
	DOREPLIFETIME(UMechRunComponent, PendingPicks);
	DOREPLIFETIME(UMechRunComponent, Offer);
	DOREPLIFETIME(UMechRunComponent, LockActor);
	DOREPLIFETIME(UMechRunComponent, LockProgress);
}

UMechRunComponent* UMechRunComponent::Of(const APlayerController* PC)
{
	return PC ? PC->FindComponentByClass<UMechRunComponent>() : nullptr;
}

UMechCombatComponent* UMechRunComponent::GetCombat() const
{
	const AMechCharacter* M = BoundMech.Get();
	return M ? M->GetCombat() : nullptr;
}

AMechDefenseDirector* UMechRunComponent::GetDirector() const
{
	if (!CachedDirector.IsValid())
	{
		for (TActorIterator<AMechDefenseDirector> It(GetWorld()); It; ++It)
		{
			CachedDirector = *It;
			break;
		}
	}
	return CachedDirector.Get();
}

bool UMechRunComponent::ItemEquipped(EMechRunItem Item) const
{
	const AMechCharacter* M = BoundMech.Get();
	if (!M)
	{
		return false;
	}
	const FMechLoadout& L = M->GetLoadout();
	switch (Item)
	{
	case EMechRunItem::Rifle:
		return L.Shooter == EMechShooter::Rifle;
	case EMechRunItem::GreatSword:
		return L.Melee == EMechMeleeWeapon::GreatSword;
	case EMechRunItem::Missiles:
		return L.Shoulder == EMechShoulder::Missiles;
	case EMechRunItem::FrameNewEra:
		return M->GetFrameMaker() == EMechMaker::NewEra;
	case EMechRunItem::FrameVictor:
		return M->GetFrameMaker() == EMechMaker::Victor;
	case EMechRunItem::FramePhase:
		return M->GetFrameMaker() == EMechMaker::Phase;
	default:
		return false;
	}
}

bool UMechRunComponent::Active(EMechRunCard C) const
{
	return Has(C) && ItemEquipped(Card(C).Item);
}

void UMechRunComponent::Bind(AMechCharacter* Mech)
{
	if (AMechCharacter* Old = BoundMech.Get())
	{
		Old->SetRun(nullptr);
	}
	BoundMech = Mech;
	if (!Mech)
	{
		return;
	}
	Mech->SetRun(this);
	if (GetOwner()->HasAuthority())
	{
		// 等级的装甲上限跟着人走；重生的新机体换回阵亡前的配装（SpawnRangeMech 生成的是默认配装）
		Mech->GetHealth()->SetAPScale(1.f + LevelArmor * (Level - 1));
		if (bHaveLoadout)
		{
			Mech->SetLoadout(SavedLoadout, true);
		}
		BurstLeft = 0;
		PendingHoming.Reset();
	}
}

void UMechRunComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	AMechCharacter* Mech = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	if (Mech != BoundMech.Get())
	{
		Bind(Mech);
	}
	if (PC && PC->IsLocalController() && Level != SeenLevel)
	{
		// 本机升级提示音（升级那一圈光是组播的，各端都看得到）
		if (Level > SeenLevel)
		{
			if (USoundBase* Ding = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Shoulder/S_artillery_ready")))
			{
				UGameplayStatics::PlaySound2D(this, Ding, 0.55f, 1.35f);
			}
		}
		SeenLevel = Level;
	}
	if (GetOwner()->HasAuthority())
	{
		ServerTick(Mech, DeltaTime);
	}
	static FString NetTestMode;
	static const bool bNetTest = FParse::Value(FCommandLine::Get(), TEXT("MechRunNetTest="), NetTestMode);
	if (bNetTest && PC && PC->IsLocalController())
	{
		MechRunTest::NetTick(this, NetTestMode == TEXT("Host"), DeltaTime);
	}
}

void UMechRunComponent::ServerTick(AMechCharacter* Mech, float DeltaTime)
{
	if (!bXpParsed)
	{
		bXpParsed = true;
		float Scale = 1.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("MechRunXP="), Scale) && Scale > 0.f)
		{
			XpScale = Scale;
			UE_LOG(LogTemp, Display, TEXT("[run] 经验倍率 ×%.1f"), XpScale);
		}
	}
	UMechCombatComponent* Combat = GetCombat();
	if (Mech && !Mech->GetHealth()->IsDead())
	{
		SavedLoadout = Mech->GetLoadout();
		bHaveLoadout = true;
		Mech->GetHealth()->SetAPScale(1.f + LevelArmor * (Level - 1));
	}
	if (Combat)
	{
		Combat->RunShotFlags = Active(EMechRunCard::Flood1) ? 1 : 0;
		if (BurstLeft > 0 && (Combat->IsReloading() || !IsBurstMode()))
		{
			BurstLeft = 0;  // 换弹、换枪打断这一组
		}
	}
	// 出卡：手上这一次选完才出下一次；手上的卡因为换配装不再合格就重抽
	if (PendingPicks > 0)
	{
		for (const uint8 C : Offer)
		{
			if (!Eligible(static_cast<EMechRunCard>(C)))
			{
				Offer.Reset();
				break;
			}
		}
		if (Offer.IsEmpty())
		{
			BuildOffer();
		}
	}
	// 追踪弹（打中时记下的）
	if (Combat && Mech)
	{
		for (const FPendingHoming& P : PendingHoming)
		{
			AActor* T = P.Target.Get();
			if (!T)
			{
				continue;
			}
			const FVector Dir = (T->GetActorLocation() - P.From).GetSafeNormal();
			Combat->AddBullet(P.From, Dir, true, HomingSpeed, HomingLife, P.Damage, P.Impact, 1.8f, false);
			FMechBullet& B = Combat->Bullets.Last();
			B.RunHoming = T;
			B.Turn = HomingTurn;
			B.bRunExtra = true;
			++Debug.HomingSpawned;
			if (AMechDefenseDirector* D = GetDirector())
			{
				D->MulticastRunBullet(Mech, P.From, Dir, T);
			}
		}
	}
	PendingHoming.Reset();
	// 点射：没打全的组不等了
	const float Now = GetWorld()->GetTimeSeconds();
	Groups.RemoveAll([Now](const FBurstGroup& G) { return Now - G.Born > BurstGroupLife; });
	UpdateLock(Mech, DeltaTime);
	UpdateWaves(DeltaTime);
	UpdateZones(DeltaTime);
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("MechRunTest"));
	if (bTest && Mech)
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetOwner()); PC && PC->IsLocalController())
		{
			MechRunTest::Tick(this, Mech, DeltaTime);
		}
	}
}

// ---------------------------------------------------------------- 经验和等级
void UMechRunComponent::AddXP(int32 Amount)
{
	if (!GetOwner()->HasAuthority() || Amount <= 0)
	{
		return;
	}
	XP += FMath::Max(1, FMath::RoundToInt(Amount * XpScale));
	const int32 Before = Level;
	while (XP >= GetNextXP())
	{
		XP -= GetNextXP();
		++Level;
		++PendingPicks;
	}
	if (Level == Before)
	{
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[run] 升到 %d 级（待选 %d 次）"), Level, PendingPicks);
	if (AMechCharacter* M = BoundMech.Get(); M && !M->GetHealth()->IsDead())
	{
		// 升级：装甲上限 +3%、回满（用户定）
		M->GetHealth()->SetAPScale(1.f + LevelArmor * (Level - 1));
		M->GetHealth()->Heal(M->GetHealth()->MaxAP);
		if (AMechDefenseDirector* D = GetDirector())
		{
			D->MulticastRunFx(static_cast<uint8>(EMechRunFxKind::LevelUp), M->GetActorLocation(), FVector::ZeroVector, 0.f, 0.f, 0.f);
		}
	}
}

void UMechRunComponent::OnWaveEnd(int32 Wave)
{
	AddXP(XpWaveBase + XpWavePer * Wave);
}

void UMechRunComponent::OnKill(EMechDefenseKill Kind, AActor* Victim)
{
	++Debug.Kills;
	AddXP(XpKill[FMath::Clamp(static_cast<int32>(Kind), 0, 5)]);
	AMechCharacter* M = BoundMech.Get();
	UMechCombatComponent* Combat = GetCombat();
	if (!M || !Combat)
	{
		return;
	}
	// 维克托 补给 1：击杀补 1/3 弹匣
	if (Active(EMechRunCard::Supply1) && Combat->GetMagazine() > 0)
	{
		const int32 Give = FMath::CeilToInt(Combat->GetMagazine() * SupplyAmmo);
		const int32 Before = Combat->Ammo;
		Combat->Ammo = FMath::Min(Combat->Ammo + Give, Combat->GetMagazine());
		Debug.AmmoGiven += Combat->Ammo - Before;
	}
	// 维克托 补给 2：击杀缩短肩部冷却（导弹在战斗组件，紧急脱离等四件在战术组件，其余在肩部组件）
	if (Active(EMechRunCard::Supply2))
	{
		const EMechShoulder S = M->GetLoadout().Shoulder;
		const float Before = M->GetShoulderTimer();
		if (S == EMechShoulder::Missiles)
		{
			Combat->MissileTimer = FMath::Max(0.f, Combat->MissileTimer - SupplyCooldown);
		}
		else if (MechIsTacticalShoulder(S))
		{
			M->GetTactical()->RunReduceCooldown(SupplyCooldown);
		}
		else
		{
			M->GetShoulder()->RunReduceCooldown(SupplyCooldown);
		}
		Debug.CooldownGiven += Before - M->GetShoulderTimer();
	}
	// 相位 空战 2：空中击杀回一次闪避的能量（客户端差得多了会按 NetEnergy 校正，见 AMechCharacter::Tick）
	if (Active(EMechRunCard::Air2) && M->GetCharacterMovement()->IsFalling())
	{
		UMechMovementComponent* Move = M->GetMechMovement();
		const float Before = Move->Energy;
		Move->Energy = FMath::Min(Move->MaxEnergy, Move->Energy + Move->QuickBoostCost);
		Debug.EnergyGiven += Move->Energy - Before;
	}
}

float UMechRunComponent::OutgoingScale() const
{
	float S = 1.f + LevelDamage * (Level - 1);
	if (Active(EMechRunCard::Air1))
	{
		const AMechCharacter* M = BoundMech.Get();
		S *= M && M->GetCharacterMovement()->IsFalling() ? AirDamage : GroundDamage;
	}
	return S;
}

void UMechRunComponent::TuneRepair(bool& bTime, float& Rate, float& Cap) const
{
	if (Active(EMechRunCard::Repair1) && !bTime)
	{
		bTime = true;
		Rate *= RepairHurtRate;
	}
	if (Active(EMechRunCard::Repair2))
	{
		Cap = RepairFullCap;
	}
}

// ---------------------------------------------------------------- 出卡和选卡
bool UMechRunComponent::Eligible(EMechRunCard C) const
{
	if (Has(C) || !ItemEquipped(Card(C).Item))
	{
		return false;
	}
	// 不锁路径（11-01）：只要上一步拿了就行，同一件武器两条线可以混着拿
	return Card(C).Step == 1 || Has(static_cast<EMechRunCard>(static_cast<int32>(C) - 1));
}

void UMechRunComponent::BuildOffer()
{
	Offer.Reset();
	TArray<uint8> Cands;
	for (int32 i = 0; i < NumCards; ++i)
	{
		if (Eligible(static_cast<EMechRunCard>(i)))
		{
			Cands.Add(static_cast<uint8>(i));
		}
	}
	if (Cands.IsEmpty())
	{
		PendingPicks = 0;  // 没有可选的也正常升级（回血、数值照给）
		return;
	}
	for (int32 i = Cands.Num() - 1; i > 0; --i)
	{
		Cands.Swap(i, FMath::RandRange(0, i));
	}
	// 尽量不同件：先每件一张，不够 3 张再从剩下的补
	bool bSeen[static_cast<int32>(EMechRunItem::Num)] = {};
	for (const uint8 C : Cands)
	{
		const int32 Item = static_cast<int32>(Card(static_cast<EMechRunCard>(C)).Item);
		if (!bSeen[Item] && Offer.Num() < 3)
		{
			bSeen[Item] = true;
			Offer.Add(C);
		}
	}
	for (const uint8 C : Cands)
	{
		if (Offer.Num() >= 3)
		{
			break;
		}
		Offer.AddUnique(C);
	}
}

void UMechRunComponent::LocalInput(APlayerController* PC)
{
	static const FKey Keys[3] = {EKeys::One, EKeys::Two, EKeys::Three};
	const float Now = GetWorld()->GetRealTimeSeconds();
	for (int32 i = 0; i < 3; ++i)
	{
		if (PC->WasInputKeyJustPressed(Keys[i]) && Offer.IsValidIndex(i) && Now - LastPressTime > 0.2f)
		{
			LastPressTime = Now;  // 客户端按键到服务器换下一组卡有来回延迟：连按别把下一组也选掉
			ServerPick(i);
			break;
		}
	}
}

void UMechRunComponent::ServerPick_Implementation(int32 Slot)
{
	if (!Offer.IsValidIndex(Slot))
	{
		return;
	}
	const EMechRunCard C = static_cast<EMechRunCard>(Offer[Slot]);
	const uint32 Old = Owned;
	Owned |= 1u << static_cast<uint32>(C);
	NotePicked(Owned & ~Old);
	PendingPicks = FMath::Max(0, PendingPicks - 1);
	Offer.Reset();
	UE_LOG(LogTemp, Display, TEXT("[run] 选卡：%s（%s）"), Card(C).Name, Card(C).ItemName);
}

void UMechRunComponent::OnRep_Owned(uint32 OldOwned)
{
	NotePicked(Owned & ~OldOwned);
}

void UMechRunComponent::NotePicked(uint32 NewBits)
{
	for (int32 i = 0; i < NumCards; ++i)
	{
		if ((NewBits >> i) & 1u)
		{
			LastPicked = i;
			LastPickedTime = GetWorld()->GetRealTimeSeconds();
		}
	}
}

void UMechRunComponent::ServerCheat_Implementation(const FString& What, int32 Number)
{
	if (What.Equals(TEXT("xp"), ESearchCase::IgnoreCase))
	{
		AddXP(Number);
		return;
	}
	if (What.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		Owned = 0;
		Offer.Reset();
		UE_LOG(LogTemp, Display, TEXT("[run] 清空所有卡"));
		return;
	}
	if (What.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		for (int32 i = 0; i < NumCards; ++i)
		{
			UE_LOG(LogTemp, Display, TEXT("[run] %-8s %s（%s）%s"), Cards[i].Code, Cards[i].Name, Cards[i].ItemName, Has(static_cast<EMechRunCard>(i)) ? TEXT(" 已拿") : TEXT(""));
		}
		UE_LOG(LogTemp, Display, TEXT("[run] 等级 %d 经验 %d/%d 待选 %d"), Level, XP, GetNextXP(), PendingPicks);
		return;
	}
	const uint32 Old = Owned;
	if (What.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		Owned = (1u << NumCards) - 1u;
	}
	else
	{
		const EMechRunCard C = FindCard(What);
		if (C == EMechRunCard::Num)
		{
			UE_LOG(LogTemp, Warning, TEXT("[run] 没有这张卡：%s（mech.Run.List 看代号）"), *What);
			return;
		}
		// 连带前面的步骤
		for (int32 i = static_cast<int32>(C), Step = Card(C).Step; Step >= 1; --i, --Step)
		{
			Owned |= 1u << i;
		}
		UE_LOG(LogTemp, Display, TEXT("[run] 给卡：%s"), Card(C).Name);
	}
	NotePicked(Owned & ~Old);
	Offer.Reset();
}

// ---------------------------------------------------------------- 步枪：洪流、点射
void UMechRunComponent::BeforeBurstShot()
{
	if (BurstLeft > 0)
	{
		return;
	}
	BurstLeft = BurstCount;
	CurrentGroup = NextGroup++;
	++Debug.BurstGroups;
	FBurstGroup& G = Groups.AddDefaulted_GetRef();
	G.Id = CurrentGroup;
	G.Born = GetWorld()->GetTimeSeconds();
}

void UMechRunComponent::AfterBurstShot()
{
	UMechCombatComponent* Combat = GetCombat();
	if (!Combat)
	{
		return;
	}
	--BurstLeft;
	++Debug.BurstShots;
	Combat->FireTimer = BurstLeft > 0 ? BurstGap : BurstRest;
	if (Combat->Ammo <= 0)
	{
		BurstLeft = 0;
	}
}

void UMechRunComponent::TagBullet(FMechBullet& B)
{
	if (IsBurstMode() && CurrentGroup > 0)
	{
		B.RunGroup = CurrentGroup;
		B.Damage *= BurstDamage;
		if (FBurstGroup* G = Groups.FindByPredicate([this](const FBurstGroup& X) { return X.Id == CurrentGroup; }))
		{
			G->ShotDamage = B.Damage;
		}
	}
}

void UMechRunComponent::NotePierce(const FMechBullet& B)
{
	const int32 N = B.Pierced.Num() + 1;  // 这一发已经穿过的 + 正在打的这个
	if (N >= 2)
	{
		++Debug.PierceHits;
	}
	Debug.MaxPierce = FMath::Max(Debug.MaxPierce, N);
}

float UMechRunComponent::OnBulletHitGrunt(AActor* Grunt, FMechBullet& B, float Damage, const FVector& At)
{
	UMechCombatComponent* Combat = GetCombat();
	if (!Combat)
	{
		return Damage;
	}
	// 保卫战里杂兵也算连中（第一版子弹打杂兵不走这条路，连中永远叠不起来）；规则和打机甲一样：换目标清零，洪流 2 不清
	const MechWeapons::FGun& G = Combat->GetGun();
	if (!B.bRunExtra && !B.bKnife && G.StreakBonus > 0.f)
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if ((Combat->RunStreakActor.Get() != Grunt && !KeepsStreak()) || Now - Combat->StreakTime > MechWeapons::StreakWindow)
		{
			Combat->Streak = 0;
		}
		Damage *= 1.f + G.StreakBonus * Combat->Streak;
		Combat->Streak = FMath::Min(Combat->Streak + 1, MechWeapons::StreakMax);
		Combat->RunStreakActor = Grunt;
		Combat->StreakTarget = nullptr;
		Combat->StreakTime = Now;
	}
	AfterBulletHit(Grunt, B, At);
	return Damage;
}

void UMechRunComponent::OnBulletHitMech(AMechCharacter* Victim, FMechBullet& B, const FVector& At)
{
	if (UMechCombatComponent* Combat = GetCombat(); Combat && !B.bRunExtra)
	{
		Combat->RunStreakActor = Victim;
	}
	AfterBulletHit(Victim, B, At);
}

void UMechRunComponent::AfterBulletHit(AActor* Victim, FMechBullet& B, const FVector& At)
{
	if (B.bRunExtra)
	{
		++Debug.HomingHits;
		return;
	}
	UMechCombatComponent* Combat = GetCombat();
	// 洪流 3：满层时每发分出追踪弹，打附近另一个敌人
	if (Combat && Active(EMechRunCard::Flood3) && Combat->Streak >= MechWeapons::StreakMax)
	{
		if (AActor* Other = FindOtherEnemy(Victim, At, HomingRange))
		{
			PendingHoming.Add({At, Other, B.Damage * HomingDamage, B.Impact * HomingDamage});
		}
	}
	CountGroupHit(B, Victim, At);
}

AActor* UMechRunComponent::FindOtherEnemy(const AActor* Except, const FVector& At, float Range) const
{
	AActor* Best = nullptr;
	float BestD = Range;
	auto Try = [&](AActor* A) {
		const float D = FVector::Dist(A->GetActorLocation(), At);
		if (A != Except && D < BestD)
		{
			BestD = D;
			Best = A;
		}
	};
	if (const AMechDefenseDirector* Dir = CachedDirector.Get() ? CachedDirector.Get() : GetDirector())
	{
		for (const TWeakObjectPtr<AMechGrunt>& G : Dir->GetGrunts())
		{
			if (G.IsValid() && G->IsAlive())
			{
				Try(G.Get());
			}
		}
	}
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (IsEnemyMech(*It, 0))
		{
			Try(*It);
		}
	}
	return Best;
}

void UMechRunComponent::CountGroupHit(FMechBullet& B, AActor* Victim, const FVector& At)
{
	if (B.RunGroup <= 0 || B.bGroupCounted)
	{
		return;
	}
	B.bGroupCounted = true;  // 穿透的一发只算一次
	const int32 Id = B.RunGroup;
	const int32 Index = Groups.IndexOfByPredicate([Id](const FBurstGroup& X) { return X.Id == Id; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	FBurstGroup& G = Groups[Index];
	++G.Hits;
	G.bSameTarget &= !G.Victim.IsValid() || G.Victim.Get() == Victim;
	G.Victim = Victim;
	if (G.Hits < BurstCount)
	{
		return;
	}
	const bool bSame = G.bSameTarget;
	const float ShotDamage = G.ShotDamage;
	Groups.RemoveAtSwap(Index);
	UMechCombatComponent* Combat = GetCombat();
	// 点射 2：三发全中返还这组弹药
	if (Combat && Active(EMechRunCard::Burst2))
	{
		Combat->Ammo = FMath::Min(Combat->Ammo + BurstCount, Combat->GetMagazine());
		++Debug.BurstRefunds;
	}
	// 点射 3：三发打中同一目标时引爆
	if (bSame && Active(EMechRunCard::Burst3))
	{
		Detonate(At, ShotDamage);
	}
}

void UMechRunComponent::Detonate(const FVector& At, float ShotDamage)
{
	++Debug.Detonations;
	TArray<AActor*> Enemies;
	EnemiesInBox(At, FVector(DetonateRadius), Enemies);
	for (AActor* E : Enemies)
	{
		if (FVector::Dist(E->GetActorLocation(), At) < DetonateRadius + 300.f)
		{
			HitEnemy(E, ShotDamage * DetonateDamage, DetonateImpact, At, EMechHitKind::Missile);
		}
	}
	if (AMechDefenseDirector* D = GetDirector())
	{
		D->RunBlastFX(At, DetonateRadius, false);
	}
	UE_LOG(LogTemp, Display, TEXT("[run] 点射引爆：半径 %.0f 米，打到 %d 个，每个 %.0f"), DetonateRadius / 100.f, Enemies.Num(), ShotDamage * DetonateDamage);
}

void UMechRunComponent::HitEnemy(AActor* Enemy, float Damage, float Impact, const FVector& At, EMechHitKind Kind, float Stagger)
{
	AMechCharacter* Me = BoundMech.Get();
	if (AMechCharacter* V = Cast<AMechCharacter>(Enemy))
	{
		const bool bDirect = V->GetHealth()->IsStaggered();
		const float Dealt = V->ReceiveHit(Damage, Impact, Me, At, Kind, Stagger);
		if (Dealt > 0.f && Me)
		{
			Me->GetCombat()->ConfirmHit(V, bDirect, Kind, Dealt, V->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		}
	}
	else if (Enemy)
	{
		UGameplayStatics::ApplyDamage(Enemy, Damage, Cast<AController>(GetOwner()), Me, UDamageType::StaticClass());
	}
}

void UMechRunComponent::EnemiesInBox(const FVector& Center, const FVector& HalfExtent, TArray<AActor*>& Out) const
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	TArray<FOverlapResult> Hits;
	GetWorld()->OverlapMultiByObjectType(Hits, Center, FQuat::Identity, Objects, FCollisionShape::MakeBox(HalfExtent));
	for (const FOverlapResult& H : Hits)
	{
		AActor* A = H.GetActor();
		if (A && (IsEnemyGrunt(A) || IsEnemyMech(A, 0)))
		{
			Out.AddUnique(A);
		}
	}
}

float UMechRunComponent::GroundAt(const FVector& P) const
{
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechRunGround), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 800.f), P - FVector(0.f, 0.f, 6000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
	{
		return Hit.ImpactPoint.Z;
	}
	return P.Z;
}

// ---------------------------------------------------------------- 大剑：地裂、蓄力
bool UMechRunComponent::WantsChargeHold() const
{
	return Active(EMechRunCard::Charge1);
}

void UMechRunComponent::ServerChargeBegin_Implementation(AMechCharacter* InTarget)
{
	UMechCombatComponent* Combat = GetCombat();
	if (!Combat || !WantsChargeHold())
	{
		return;
	}
	Combat->Target = InTarget;
	Combat->bRunChargeWanted = true;  // 重斩一开始（StartMove）就停在起手；轻击还没到可取消时刻的话，排队的重斩开始时才停
	Combat->RequestHeavy();
}

void UMechRunComponent::ServerChargeEnd_Implementation()
{
	UMechCombatComponent* Combat = GetCombat();
	if (!Combat)
	{
		return;
	}
	Combat->bRunChargeWanted = false;
	if (Combat->RunChargeHold != 0)
	{
		Combat->RunChargeLevel = FMath::Clamp((GetWorld()->GetTimeSeconds() - Combat->RunChargeT0) / ChargeFull, 0.f, 1.f);
		Combat->RunChargeHold = 0;
		Combat->HeavyTimer = Combat->HeavyCooldown;  // 冷却从砍出去算（蓄的时候不算）
		UE_LOG(LogTemp, Display, TEXT("[run] 蓄力松手：%.0f%%"), Combat->RunChargeLevel * 100.f);
	}
}

void UMechRunComponent::OnStrikeBegin(EMechMelee Move, float& ReachScale, float& DamageScale)
{
	UMechCombatComponent* Combat = GetCombat();
	if (!Combat || Move != EMechMelee::Heavy || !Active(EMechRunCard::Charge1))
	{
		return;
	}
	const float L = Combat->RunChargeLevel;
	ReachScale = FMath::Lerp(1.f, ChargeReach, L);
	DamageScale = FMath::Lerp(1.f, ChargeDamage, L);
}

void UMechRunComponent::OnStrikeEnd(EMechMelee Move, float BaseDamage, float Reach, int32 Hits)
{
	UMechCombatComponent* Combat = GetCombat();
	AMechCharacter* M = BoundMech.Get();
	if (!Combat || !M)
	{
		return;
	}
	Debug.LastStrikeHits = Hits;
	if (Move != EMechMelee::Heavy || Combat->GetBladeType() != EMechMeleeWeapon::GreatSword)
	{
		return;
	}
	const float L = Combat->RunChargeLevel;
	Combat->RunChargeLevel = 0.f;
	Debug.LastCharge = L;
	Debug.LastReach = Reach;
	++CurrentVolley;
	const FVector Fwd = M->GetActorForwardVector().GetSafeNormal2D();
	FVector From = M->GetActorLocation() + Fwd * FMath::Min(Reach, 400.f) * 0.5f;
	From.Z = GroundAt(From);
	AMechDefenseDirector* D = GetDirector();
	if (L > 0.f && D)
	{
		// 蓄力那一刀：地上一圈冲击环，半径就是这一刀的判定范围（越蓄越大，看得出来）
		D->MulticastRunFx(static_cast<uint8>(EMechRunFxKind::Ring), M->GetActorLocation() + Fwd * Reach * 0.55f, FVector::ZeroVector, Reach * 0.65f + 100.f, 0.5f, L >= 1.f ? 1.f : 0.f);
	}
	// 地裂：重斩放冲击波；蓄力的话冲击波也跟着远、跟着疼
	if (Active(EMechRunCard::Rift1))
	{
		const float Length = WaveLength * FMath::Lerp(1.f, ChargeReach, L);
		const float Damage = BaseDamage * WaveDamage * FMath::Lerp(1.f, ChargeDamage, L);
		const bool bStrip = Active(EMechRunCard::Rift3);
		AddWave(From, Fwd, Length, WaveHalfWidth, WaveSpeed, Damage, WaveImpact, false, bStrip);
		if (Active(EMechRunCard::Rift2))
		{
			AddWave(From, Fwd.RotateAngleAxis(WaveFan, FVector::UpVector), Length, WaveHalfWidth, WaveSpeed, Damage, WaveImpact, false, bStrip);
			AddWave(From, Fwd.RotateAngleAxis(-WaveFan, FVector::UpVector), Length, WaveHalfWidth, WaveSpeed, Damage, WaveImpact, false, bStrip);
		}
	}
	// 蓄力 3：蓄满放出横贯全场的一刀（瞄准方向，能穿所有敌人）
	if (L >= 1.f && Active(EMechRunCard::Charge3))
	{
		AddWave(From, Fwd, SlashLength, SlashHalfWidth, SlashSpeed, BaseDamage * ChargeDamage * SlashDamage, SlashImpact, true, false);
		UE_LOG(LogTemp, Display, TEXT("[run] 横贯一刀：%.0f 米，伤害 %.0f"), SlashLength / 100.f, BaseDamage * ChargeDamage * SlashDamage);
	}
}

void UMechRunComponent::AddWave(const FVector& From, const FVector& Dir, float Length, float HalfWidth, float Speed, float Damage, float Impact, bool bSlash, bool bStrip)
{
	FWave& W = Waves.AddDefaulted_GetRef();
	W.From = From;
	W.Dir = Dir;
	W.Length = Length;
	W.HalfWidth = HalfWidth;
	W.Speed = Speed;
	W.Damage = Damage;
	W.Impact = Impact;
	W.bSlash = bSlash;
	W.bStrip = bStrip;
	W.Volley = CurrentVolley;
	++Debug.Waves;
	UE_LOG(LogTemp, Display, TEXT("[run] 放出%s：长 %.0f 米、半宽 %.1f 米、伤害 %.0f"), bSlash ? TEXT("横贯一刀") : TEXT("冲击波"), Length / 100.f, HalfWidth / 100.f, Damage);
	if (AMechDefenseDirector* D = GetDirector())
	{
		// Vec 传整段位移，不传单位向量：组播参数 FVector_NetQuantize 到厘米取整，单位向量到客户端就废了
		D->MulticastRunFx(static_cast<uint8>(bSlash ? EMechRunFxKind::Slash : EMechRunFxKind::Wave), From, Dir * Length, Length, HalfWidth, Speed);
	}
}

void UMechRunComponent::AddZone(const FVector& A, const FVector& B, float HalfWidth, float Dps, float Seconds)
{
	FZone& Z = Zones.AddDefaulted_GetRef();
	Z.A = A;
	Z.B = B;
	Z.HalfWidth = HalfWidth;
	Z.Dps = Dps;
	Z.EndAt = GetWorld()->GetTimeSeconds() + Seconds;
	++Debug.ZonesSpawned;
}

void UMechRunComponent::UpdateWaves(float DeltaTime)
{
	for (int32 i = Waves.Num() - 1; i >= 0; --i)
	{
		const float Prev = Waves[i].Done;
		Waves[i].Done = FMath::Min(Waves[i].Length, Prev + Waves[i].Speed * DeltaTime);
		const FWave W = Waves[i];  // 拷一份：打死敌人会走到升级、击杀的钩子，别持有数组里的引用
		const float Step = FMath::Max(W.HalfWidth, 100.f);
		TArray<TWeakObjectPtr<AActor>> Hit = VolleyHits.FindOrAdd(W.Volley);
		for (float D = Prev;; D += Step)
		{
			const float At = FMath::Min(D, W.Done);
			FVector P = W.From + W.Dir * At;
			P.Z = GroundAt(P);
			// 贴着地往前走：离地 WaveHalfHeight 以内打得到（低飞的自爆无人机打得到，高处的机枪无人机打不到）；横贯一刀什么高度都打
			const float HalfH = W.bSlash ? 2500.f : WaveHalfHeight;
			TArray<AActor*> Enemies;
			EnemiesInBox(P + FVector(0.f, 0.f, HalfH * 0.4f), FVector(W.HalfWidth, W.HalfWidth, HalfH), Enemies);
			for (AActor* E : Enemies)
			{
				if (Hit.Contains(E))
				{
					continue;
				}
				Hit.Add(E);
				++(W.bSlash ? Debug.SlashHits : Debug.WaveHits);
				HitEnemy(E, W.Damage, W.Impact, E->GetActorLocation(), EMechHitKind::Blade, 0.5f);
			}
			if (At >= W.Done)
			{
				break;
			}
		}
		VolleyHits.FindOrAdd(W.Volley) = MoveTemp(Hit);
		if (W.Done >= W.Length)
		{
			if (W.bStrip)
			{
				// 地裂 3：冲击波走完，路径上留一段持续伤害
				FVector A = W.From;
				FVector B = W.From + W.Dir * W.Length;
				A.Z = GroundAt(A);
				B.Z = GroundAt(B);
				AddZone(A, B, StripHalfWidth, StripDps, StripTime);
				if (AMechDefenseDirector* Dir = GetDirector())
				{
					Dir->MulticastRunFx(static_cast<uint8>(EMechRunFxKind::Strip), A, B - A, StripHalfWidth, StripTime, 0.f);
				}
			}
			Waves.RemoveAtSwap(i);
		}
	}
	if (Waves.IsEmpty() && !VolleyHits.IsEmpty())
	{
		VolleyHits.Reset();
	}
}

void UMechRunComponent::UpdateZones(float DeltaTime)
{
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 i = Zones.Num() - 1; i >= 0; --i)
	{
		if (Now >= Zones[i].EndAt)
		{
			Zones.RemoveAtSwap(i);
			continue;
		}
		Zones[i].Clock += DeltaTime;
		if (Zones[i].Clock < ZoneTick)
		{
			continue;
		}
		Zones[i].Clock -= ZoneTick;
		const FZone Z = Zones[i];
		const FVector Mid = (Z.A + Z.B) * 0.5f;
		const FVector Half = FVector(FMath::Abs(Z.B.X - Z.A.X) * 0.5f + Z.HalfWidth, FMath::Abs(Z.B.Y - Z.A.Y) * 0.5f + Z.HalfWidth, 900.f);
		TArray<AActor*> Enemies;
		EnemiesInBox(Mid + FVector(0.f, 0.f, 500.f), Half, Enemies);
		for (AActor* E : Enemies)
		{
			const FVector P = E->GetActorLocation();
			const FVector OnSeg = FMath::ClosestPointOnSegment(FVector(P.X, P.Y, 0.f), FVector(Z.A.X, Z.A.Y, 0.f), FVector(Z.B.X, Z.B.Y, 0.f));
			// 地面的东西：高处的机枪无人机烧不到
			if (FVector::Dist2D(P, OnSeg) <= Z.HalfWidth + 80.f && P.Z - Mid.Z < 1000.f)
			{
				++Debug.ZoneHits;
				HitEnemy(E, Z.Dps * ZoneTick, 0.f, P, EMechHitKind::Missile, 0.f);
			}
		}
	}
}

void UMechRunComponent::DebugClearZones()
{
	Zones.Reset();
	Waves.Reset();
}

// ---------------------------------------------------------------- 导弹：蜂群、重弹
void UMechRunComponent::UpdateLock(AMechCharacter* Mech, float DeltaTime)
{
	if (!Mech || Mech->GetHealth()->IsDead() || !Active(EMechRunCard::Swarm1) || Mech->IsEmped())
	{
		LockActor = nullptr;
		LockProgress = 0.f;
		return;
	}
	const FVector ViewLoc = Mech->GetActorLocation() + FVector(0.f, 0.f, 120.f);
	const FVector ViewDir = Mech->GetControlRotation().Vector();
	AActor* Keep = LockActor;
	if (Keep && (!IsEnemyGrunt(Keep) || AngleBetween(ViewDir, Keep->GetActorLocation() - ViewLoc) > LockKeep || !HasLine(GetWorld(), ViewLoc, Keep)))
	{
		Keep = nullptr;
	}
	AActor* Best = Keep;
	float BestAngle = Keep ? AngleBetween(ViewDir, Keep->GetActorLocation() - ViewLoc) * 0.6f : LockCone;
	if (const AMechDefenseDirector* D = GetDirector())
	{
		for (const TWeakObjectPtr<AMechGrunt>& G : D->GetGrunts())
		{
			if (!G.IsValid() || !G->IsAlive() || G.Get() == Keep || FVector::Dist(G->GetActorLocation(), ViewLoc) > LockRange)
			{
				continue;
			}
			const float A = AngleBetween(ViewDir, G->GetActorLocation() - ViewLoc);
			if (A < BestAngle && HasLine(GetWorld(), ViewLoc, G.Get()))
			{
				Best = G.Get();
				BestAngle = A;
			}
		}
	}
	if (Best != LockActor)
	{
		LockActor = Best;
		LockProgress = 0.f;
	}
	if (LockActor)
	{
		const UMechCombatComponent* Combat = Mech->GetCombat();
		LockProgress = FMath::Min(1.f, LockProgress + DeltaTime / FMath::Max(0.05f, Combat->MissileLockTime * Combat->FrameLockScale));
	}
}

void UMechRunComponent::CollectAllInView(const FVector& ViewLoc, const FVector& ViewDir, TArray<AActor*>& Out) const
{
	UWorld* World = GetWorld();
	auto Try = [&](AActor* A) {
		const FVector To = A->GetActorLocation() - ViewLoc;
		if (To.SizeSquared() < FMath::Square(LockRange) && AngleBetween(ViewDir, To) < AllCone && HasLine(World, ViewLoc, A))
		{
			Out.Add(A);
		}
	};
	for (TActorIterator<AMechGrunt> It(World); It; ++It)
	{
		if (It->IsAlive() && !It->IsHidden())
		{
			Try(*It);
		}
	}
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		if (IsEnemyMech(*It, 0))
		{
			Try(*It);
		}
	}
}

void UMechRunComponent::OnFireMissiles(int32& Pending, bool& bLocked)
{
	UMechCombatComponent* Combat = GetCombat();
	AMechCharacter* M = BoundMech.Get();
	if (!Combat || !M || M->GetLoadout().Shoulder != EMechShoulder::Missiles)
	{
		return;
	}
	SalvoTargets.Reset();
	Combat->RunSalvo = 0;
	const FVector ViewLoc = M->GetActorLocation() + FVector(0.f, 0.f, 120.f);
	const FVector ViewDir = M->GetControlRotation().Vector();
	if (Active(EMechRunCard::Swarm3))
	{
		// 蜂群 3：一次锁定视野内所有敌人（不用瞄住等锁定）；离准星近的排前面，敌人比一轮的枚数多就多打（有上限）
		TArray<AActor*> All;
		CollectAllInView(ViewLoc, ViewDir, All);
		All.Sort([&](const AActor& A, const AActor& B) { return AngleBetween(ViewDir, A.GetActorLocation() - ViewLoc) < AngleBetween(ViewDir, B.GetActorLocation() - ViewLoc); });
		for (AActor* A : All)
		{
			SalvoTargets.Add(A);
		}
		Debug.SalvoTargets = All.Num();
		UE_LOG(LogTemp, Display, TEXT("[run] 全锁：视野内 %d 个敌人"), All.Num());
	}
	else if (Active(EMechRunCard::Swarm1) && !bLocked && LockActor && LockProgress >= 1.f)
	{
		SalvoTargets.Add(LockActor.Get());
		Debug.SalvoTargets = 1;
	}
	int32 Count = Pending;
	if (Active(EMechRunCard::Heavy1))
	{
		Count = 1;  // 重弹 1：一轮合成一枚（威力在 ModifyMissile）
	}
	else if (SalvoTargets.Num() > Count)
	{
		Count = FMath::Min(SalvoTargets.Num(), AllMax);
	}
	Pending = Count;
	Combat->RunSalvo = Count;
	Debug.SalvoMissiles = Count;
}

void UMechRunComponent::ModifyMissile(int32 Index, AActor*& OutRunTarget, AMechCharacter*& OutMechTarget, float& Damage, float& Impact, float& Radius, float& Scale, float& Stagger)
{
	UMechCombatComponent* Combat = GetCombat();
	if (!Combat)
	{
		return;
	}
	if (!SalvoTargets.IsEmpty())
	{
		AActor* T = SalvoTargets[Index % SalvoTargets.Num()].Get();
		if (AMechCharacter* TM = Cast<AMechCharacter>(T); TM && IsEnemyMech(TM, 0))
		{
			OutMechTarget = TM;
		}
		else if (IsEnemyGrunt(T))
		{
			OutRunTarget = T;
			OutMechTarget = nullptr;
		}
	}
	if (Active(EMechRunCard::Heavy1))
	{
		Damage *= Combat->BaseSalvo() * BigDamage;
		Impact *= BigImpact;
		Radius *= BigRadius;
		Scale = BigScale;
		if (Active(EMechRunCard::Heavy2))
		{
			Radius *= ShockRadius;
			Stagger = ShockStagger;
		}
	}
	else if (Active(EMechRunCard::Heavy2))
	{
		Radius *= ShockRadius;
		Stagger = ShockStagger;
	}
	Debug.LastMissileRadius = Radius;
	Debug.LastMissileDamage = Damage;
}

void UMechRunComponent::OnMissileExplode(AMechMissile* Missile, const FVector& At)
{
	if (!Missile || Missile->bRunChild)
	{
		return;  // 分裂出来的小导弹不再分裂、不留火场
	}
	AMechDefenseDirector* D = GetDirector();
	const float Radius = Missile->GetRadius();
	if (Active(EMechRunCard::Heavy1) && D)
	{
		D->RunBlastFX(At, Radius, true);  // 大导弹的爆炸画面按半径放大
	}
	// 重弹 2：打出失衡。机甲在爆炸里按失衡倍数打满（AMechMissile::RunStagger）；杂兵没有失衡条，瘫痪几秒
	if (Active(EMechRunCard::Heavy2))
	{
		if (!Active(EMechRunCard::Heavy1) && D)
		{
			D->RunBlastFX(At, Radius, true);
		}
		TArray<AActor*> Enemies;
		EnemiesInBox(At, FVector(Radius), Enemies);
		for (AActor* E : Enemies)
		{
			if (AMechGrunt* G = Cast<AMechGrunt>(E))
			{
				G->RunStun(ShockStun);
				++Debug.Stunned;
			}
		}
	}
	// 重弹 3：落点留 3 秒火场
	if (Active(EMechRunCard::Heavy3))
	{
		const FVector Ground(At.X, At.Y, GroundAt(At));
		const float R = Radius * FireRadius;
		AddZone(Ground, Ground, R, FireDps, FireTime);
		if (D)
		{
			D->MulticastRunFx(static_cast<uint8>(EMechRunFxKind::Fire), Ground, FVector::ZeroVector, R, FireTime, 0.f);
		}
		UE_LOG(LogTemp, Display, TEXT("[run] 火场：半径 %.1f 米，%.0f 秒，每秒 %.0f"), R / 100.f, FireTime, FireDps);
	}
	// 蜂群 2：命中后分裂成 3 枚，分头追附近的敌人（敌人不够就同一个多打）
	if (Active(EMechRunCard::Swarm2))
	{
		AMechCharacter* Me = BoundMech.Get();
		if (!Me)
		{
			return;
		}
		TArray<AActor*> Near;
		EnemiesInBox(At, FVector(SplitRange), Near);
		Near.Sort([&At](const AActor& A, const AActor& B) { return FVector::DistSquared(A.GetActorLocation(), At) < FVector::DistSquared(B.GetActorLocation(), At); });
		for (int32 i = 0; i < SplitCount; ++i)
		{
			AActor* T = Near.Num() ? Near[i % Near.Num()] : nullptr;
			const float Yaw = FMath::FRandRange(0.f, 360.f) + i * 120.f;
			const FVector Dir = (FRotator(0.f, Yaw, 0.f).Vector() + FVector(0.f, 0.f, 0.9f)).GetSafeNormal();
			const FTransform Xf(Dir.Rotation(), At + Dir * 80.f, FVector(SplitScale));
			AMechMissile* Child = GetWorld()->SpawnActorDeferred<AMechMissile>(AMechMissile::StaticClass(), Xf, Me, Me, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Child)
			{
				continue;
			}
			AMechCharacter* TM = Cast<AMechCharacter>(T);
			Child->Init(Me, TM, Missile->GetDamage() * SplitDamage, Missile->GetImpact() * SplitDamage, Missile->GetRadius() * SplitRadius);
			Child->RunTarget = TM ? nullptr : T;
			Child->bRunChild = true;
			Child->RunLife = T ? 3.f : 1.5f;
			Child->SetInstigator(nullptr);  // 不从肩上的发射管出（AMechMissile::BeginPlay 按 Instigator 算画面偏移）
			Child->FinishSpawning(Xf);
			Child->Launch(Dir * 2200.f);
			++Debug.Splits;
		}
		UE_LOG(LogTemp, Display, TEXT("[run] 分裂：%d 枚，附近敌人 %d 个"), SplitCount, Near.Num());
	}
}

// ---------------------------------------------------------------- 测试接口
void UMechRunComponent::DebugReset(uint32 InCards, int32 InLevel)
{
	Owned = InCards;
	Level = InLevel;
	XP = 0;
	PendingPicks = 0;
	Offer.Reset();
	Debug = FMechRunDebug();
	Groups.Reset();
	BurstLeft = 0;
	PendingHoming.Reset();
}

void UMechRunComponent::DebugResetCombat()
{
	if (UMechCombatComponent* C = GetCombat())
	{
		C->Ammo = C->GetMagazine();
		C->ReloadTimer = 0.f;
		C->MissileTimer = 0.f;
		C->PendingMissiles = 0;
		C->RunSalvo = 0;
		C->HeavyTimer = 0.f;
		C->Streak = 0;
		C->FireTimer = 0.f;
		C->bRunChargeWanted = false;
		C->RunChargeLevel = 0.f;
		C->bRunChargePressed = false;
		C->EndMelee();
	}
	LockActor = nullptr;
	LockProgress = 0.f;
}

void UMechRunComponent::DebugForceOffer()
{
	PendingPicks = FMath::Max(PendingPicks, 1);
	BuildOffer();
}

void UMechRunComponent::DebugSetAmmo(int32 Ammo)
{
	if (UMechCombatComponent* C = GetCombat())
	{
		C->Ammo = Ammo;
		C->ReloadTimer = 0.f;
	}
}

void UMechRunComponent::DebugSetMissileTimer(float Seconds)
{
	if (UMechCombatComponent* C = GetCombat())
	{
		C->MissileTimer = Seconds;
	}
}

void UMechRunComponent::DebugSetStreak(int32 N)
{
	if (UMechCombatComponent* C = GetCombat())
	{
		C->Streak = N;
		C->StreakTime = GetWorld()->GetTimeSeconds();
	}
}

void UMechRunComponent::AddVisualHoming(AMechCharacter* Mech, const FVector& From, const FVector& Dir, AActor* Target)
{
	if (UMechCombatComponent* C = Mech ? Mech->GetCombat() : nullptr)
	{
		C->AddBullet(From, Dir, false, HomingSpeed, HomingLife, 0.f, 0.f, 1.8f, false);
		FMechBullet& B = C->Bullets.Last();
		B.RunHoming = Target;
		B.Turn = HomingTurn;
		B.bRunExtra = true;
	}
}

// ================================================================ 画面（各端本地）
AMechRunFx::AMechRunFx()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	PlaneMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane"));
	SphereMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
	CylinderMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	RingMat = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_ShockRing"));
	BeamMat = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_MechBeam"));
	EarthFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_01/Particles/Ex_02_Earth_Explosion/Exp_01"));
	ConeMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone"));
	SparkFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	EmberFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01"));
	WaveSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small"));
	SlashSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_fire"));
	ChargeFullSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_ready"));
	LevelSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_ready"));
}

AMechRunFx* AMechRunFx::Get(UWorld* World)
{
	static TWeakObjectPtr<AMechRunFx> Cached;
	if (!World)
	{
		return nullptr;
	}
	if (Cached.IsValid() && Cached->GetWorld() == World)
	{
		return Cached.Get();
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Cached = World->SpawnActor<AMechRunFx>(AMechRunFx::StaticClass(), FTransform::Identity, P);
	return Cached.Get();
}

float AMechRunFx::GroundAt(const FVector& P) const
{
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechRunFxGround), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 800.f), P - FVector(0.f, 0.f, 6000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
	{
		return Hit.ImpactPoint.Z;
	}
	return P.Z;
}

AMechRunFx::FPatch& AMechRunFx::NewPatch(UStaticMesh* Mesh, UMaterialInterface* Mat)
{
	FPatch* Free = Patches.FindByPredicate([Mesh](const FPatch& X) { return X.bFree && X.Comp && X.Comp->GetStaticMesh() == Mesh; });
	if (!Free)
	{
		FPatch New;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		New.Comp = C;
		New.MID = Mat ? UMaterialInstanceDynamic::Create(Mat, this) : nullptr;
		if (New.MID)
		{
			C->SetMaterial(0, New.MID);
		}
		Free = &Patches.Add_GetRef(New);
	}
	Free->bFree = false;
	Free->Born = GetWorld()->GetTimeSeconds();
	Free->NextSpark = 0.f;
	Free->Comp->SetVisibility(true);
	return *Free;
}

UStaticMeshComponent* AMechRunFx::MakeGlowMesh(UStaticMesh* Mesh)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->bAffectDistanceFieldLighting = false;
	C->bAffectDynamicIndirectLighting = false;
	C->SetUsingAbsoluteLocation(true);
	C->SetUsingAbsoluteRotation(true);
	C->SetUsingAbsoluteScale(true);
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	return C;
}

void AMechRunFx::AddTongue(FPatch& P, const FVector& At)
{
	if (!ConeMesh || !BeamMat)
	{
		return;
	}
	if (!P.TongueMID)
	{
		P.TongueMID = UMaterialInstanceDynamic::Create(BeamMat, this);
		// 亮度压到看得出橙色（这个材质带白天补偿，1 以上就发白，第一版白得像路锥）
		P.TongueMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.3f, 0.05f));
		P.TongueMID->SetScalarParameterValue(TEXT("Intensity"), 0.32f);
	}
	UStaticMeshComponent* C = MakeGlowMesh(ConeMesh);
	C->SetMaterial(0, P.TongueMID);
	C->SetWorldLocation(At);
	P.Tongues.Add(C);
}

void AMechRunFx::Play(EMechRunFxKind Kind, const FVector& At, const FVector& Vec, float A, float B, float C)
{
	const float Now = GetWorld()->GetTimeSeconds();
	++DebugPlayed;
	switch (Kind)
	{
	case EMechRunFxKind::Wave:
	case EMechRunFxKind::Slash:
	{
		const bool bSlash = Kind == EMechRunFxKind::Slash;
		FCrest& R = Crests.AddDefaulted_GetRef();
		R.From = At;
		R.Dir = Vec.GetSafeNormal();
		R.Length = A;
		R.HalfWidth = B;
		R.Speed = C;
		R.bSlash = bSlash;
		// 浪头：贴地往前走的一道发光的墙（压扁的球 + 光束材质，中间亮边缘软）
		// 亮度参考充能步枪的光束（芯 3、光晕 0.3）：这个材质带白天补偿，给大了直接变白，看不出形状
		if (SphereMesh && BeamMat && !bSlash)  // 横贯不要浪头：又高又宽，贴脸就是一大团白；只留长光和一路的土
		{
			UStaticMeshComponent* Comp = MakeGlowMesh(SphereMesh);
			R.MID = UMaterialInstanceDynamic::Create(BeamMat, this);
			R.MID->SetVectorParameterValue(TEXT("Color"), bSlash ? FLinearColor(1.f, 0.72f, 0.3f) : FLinearColor(1.f, 0.42f, 0.12f));
			R.MID->SetScalarParameterValue(TEXT("Intensity"), bSlash ? 2.5f : 1.5f);
			Comp->SetMaterial(0, R.MID);
			R.Comp = Comp;
		}
		if (CylinderMesh && BeamMat)
		{
			// 地上一条光：地裂是跟着浪头长出来的裂缝，横贯是一下拉满全长、0.7 秒内收细
			FBeam& L = Beams.AddDefaulted_GetRef();
			UStaticMeshComponent* Comp = MakeGlowMesh(CylinderMesh);
			Comp->SetWorldRotation(FRotationMatrix::MakeFromZ(R.Dir).ToQuat());
			L.MID = UMaterialInstanceDynamic::Create(BeamMat, this);
			L.MID->SetVectorParameterValue(TEXT("Color"), bSlash ? FLinearColor(1.f, 0.75f, 0.4f) : FLinearColor(1.f, 0.4f, 0.1f));
			L.MID->SetScalarParameterValue(TEXT("Intensity"), bSlash ? 3.f : 2.f);
			Comp->SetMaterial(0, L.MID);
			L.Comp = Comp;
			L.Born = Now;
			L.Life = bSlash ? 0.7f : A / FMath::Max(C, 1.f) + 0.45f;
			// 长光是顺着视线的：从身后看正好对着圆柱的端面，粗了贴脸就是一个白盘子（第一版）。放细、贴地、从前面一点开始
			L.Radius = bSlash ? 0.7f : 0.45f;
			L.Length = bSlash ? A - 800.f : A;
			L.Strength = bSlash ? 2.5f : 2.f;
			L.From = At + R.Dir * (bSlash ? 800.f : 0.f) + FVector(0.f, 0.f, bSlash ? 70.f : 40.f);
			L.Dir = R.Dir;
			L.Grow = bSlash ? 0.f : C;
		}
		if (USoundBase* S = bSlash ? SlashSound.Get() : WaveSound.Get())
		{
			UGameplayStatics::PlaySoundAtLocation(this, S, At, FRotator::ZeroRotator, bSlash ? 1.f : 0.8f, bSlash ? 0.6f : 0.75f);
		}
		break;
	}
	case EMechRunFxKind::Strip:
	{
		// 地裂路径：地上一长条暗红的光面 + 一路烧着的小火（面片是椭圆，贴不准坡面，火苗把不平的地方盖掉）
		const float Length = Vec.Size();
		const FVector Dir = Vec.GetSafeNormal();
		const FVector Mid = At + Vec * 0.5f + FVector(0.f, 0.f, 20.f);
		FPatch& P = NewPatch(PlaneMesh, RingMat);
		P.Life = B;
		P.Strength = 1.6f;
		P.Style = 2.f;
		P.Width = 0.03f;
		P.bShrink = false;
		P.TongueHeight = 150.f;
		P.TongueWidth = 1.f;
		P.Comp->SetWorldLocationAndRotation(Mid, FRotator(0.f, Dir.Rotation().Yaw, 0.f));
		P.Comp->SetWorldScale3D(FVector((Length + A * 2.f) / 100.f, A * 2.f / 100.f, 1.f));
		if (P.MID)
		{
			P.MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.28f, 0.06f));
		}
		const int32 N = FMath::Clamp(FMath::CeilToInt(Length / 420.f), 1, 10);
		for (int32 i = 0; i < N; ++i)
		{
			FVector Q = At + Vec * ((i + 0.5f) / N) + Dir.RotateAngleAxis(90.f, FVector::UpVector) * FMath::FRandRange(-A * 0.4f, A * 0.4f);
			Q.Z = GroundAt(Q);
			AddTongue(P, Q);
		}
		break;
	}
	case EMechRunFxKind::Fire:
	{
		// 重弹火场：地上一圈（圈里的亮面随时间往里收，看得出还能烧多久）+ 一圈火
		FPatch& P = NewPatch(PlaneMesh, RingMat);
		P.Life = B;
		P.Strength = 1.8f;
		P.Style = 2.f;
		P.Width = 0.03f;
		P.bShrink = true;
		P.TongueHeight = 280.f;
		P.TongueWidth = 1.7f;
		P.Comp->SetWorldLocationAndRotation(At + FVector(0.f, 0.f, 20.f), FRotator::ZeroRotator);
		P.Comp->SetWorldScale3D(FVector(A * 2.f / 100.f, A * 2.f / 100.f, 1.f));
		if (P.MID)
		{
			P.MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.35f, 0.06f));
		}
		for (int32 i = 0; i < 7; ++i)
		{
			const float Ang = i * 2.f * PI / 6.f;
			FVector Q = i == 6 ? At : At + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * A * 0.6f;
			Q.Z = GroundAt(Q);
			AddTongue(P, Q);
		}
		break;
	}
	case EMechRunFxKind::Ring:
	case EMechRunFxKind::LevelUp:
	{
		const bool bLevel = Kind == EMechRunFxKind::LevelUp;
		const float Radius = bLevel ? 420.f : A;
		FVector G = At;
		G.Z = GroundAt(At);
		FPatch& P = NewPatch(PlaneMesh, RingMat);
		P.Life = bLevel ? 0.7f : B;
		P.Strength = bLevel ? 1.4f : 2.5f;
		P.Style = 0.f;
		P.Width = bLevel ? 0.05f : 0.1f;
		P.bShrink = false;
		P.Comp->SetWorldLocationAndRotation(G + FVector(0.f, 0.f, 25.f), FRotator::ZeroRotator);
		P.Comp->SetWorldScale3D(FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, 1.f));
		if (P.MID)
		{
			P.MID->SetVectorParameterValue(TEXT("Color"), bLevel ? FLinearColor(0.35f, 0.95f, 1.f) : C > 0.5f ? FLinearColor(1.f, 0.78f, 0.3f) : FLinearColor(1.f, 0.45f, 0.14f));
		}
		break;
	}
	}
}

void AMechRunFx::ChargeTick(AMechCharacter* Mech, float Level, float Reach)
{
	if (!Mech)
	{
		return;
	}
	FChargeVis* V = Charges.FindByPredicate([Mech](const FChargeVis& X) { return X.Mech.Get() == Mech; });
	if (!V)
	{
		V = Charges.FindByPredicate([](const FChargeVis& X) { return !X.Mech.IsValid(); });
		if (!V)
		{
			V = &Charges.AddDefaulted_GetRef();
		}
		V->Mech = Mech;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const float Dt = FMath::Min(Now - V->LastTick, 0.1f);
	V->LastTick = Now;
	const bool bFull = Level >= 1.f;
	if (!V->Comp && PlaneMesh && RingMat)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(PlaneMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		V->MID = UMaterialInstanceDynamic::Create(RingMat, this);
		C->SetMaterial(0, V->MID);
		V->Comp = C;
	}
	if (V->Comp)
	{
		// 脚下的圈：外圈就是蓄满时够得着的范围，里面的亮面按蓄力涨，涨满变金色
		const float Radius = Reach * ChargeReach * 1.2f;
		FVector P = Mech->GetActorLocation();
		P.Z = GroundAt(P) + 25.f;
		V->Comp->SetVisibility(true);
		V->Comp->SetWorldLocationAndRotation(P, FRotator::ZeroRotator);
		V->Comp->SetWorldScale3D(FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, 1.f));
		if (V->MID)
		{
			V->MID->SetVectorParameterValue(TEXT("Color"), bFull ? FLinearColor(1.f, 0.78f, 0.3f) : FLinearColor(1.f, 0.42f, 0.12f));
			// 亮度压低：亮面是铺满一大圈地的，给高了整片地变成一块黄板（第一版就是）
			V->MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(FMath::Clamp(Level, 0.02f, 1.f), 0.03f, bFull ? 1.1f + 0.4f * FMath::Sin(Now * 12.f) : 0.8f, 2.f));
		}
	}
	// 机体隔一会儿闪一下橙光（越蓄闪得越快越亮），蓄满时金色、再响一声
	V->FlashClock -= Dt;
	if (V->FlashClock <= 0.f)
	{
		V->FlashClock = FMath::Lerp(0.3f, 0.12f, Level);
		Mech->FlashBody(bFull ? FLinearColor(1.f, 0.8f, 0.35f) : FLinearColor(1.f, 0.45f, 0.12f), 0.35f + 0.65f * Level);
	}
	if (bFull && !V->bFull && ChargeFullSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ChargeFullSound, Mech->GetActorLocation(), FRotator::ZeroRotator, 0.8f, 1.2f);
	}
	V->bFull = bFull;
}

void AMechRunFx::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const float Now = GetWorld()->GetTimeSeconds();
	// ---- 地面面片
	for (FPatch& P : Patches)
	{
		if (P.bFree || !P.Comp)
		{
			continue;
		}
		const float T = (Now - P.Born) / FMath::Max(P.Life, 0.01f);
		if (T >= 1.f)
		{
			P.bFree = true;
			P.Comp->SetVisibility(false);
			for (const TObjectPtr<UStaticMeshComponent>& F : P.Tongues)
			{
				if (F)
				{
					F->DestroyComponent();
				}
			}
			P.Tongues.Reset();
			P.TongueMID = nullptr;
			continue;
		}
		// 火舌：每根按自己的相位抖动高矮，快烧完时一起矮下去；隔一会儿冒一把火星（Course_04 的命中火花，没有长亮的灯）
		if (P.Tongues.Num() > 0)
		{
			const float Sink = FMath::Min(1.f, (1.f - T) * 4.f);
			for (int32 k = 0; k < P.Tongues.Num(); ++k)
			{
				if (UStaticMeshComponent* F = P.Tongues[k])
				{
					const float Phase = k * 2.4f;
					const float H = P.TongueHeight * Sink * (0.75f + 0.25f * FMath::Sin(Now * 11.f + Phase) + 0.12f * FMath::Sin(Now * 23.f + Phase * 2.f));
					const FVector Base = F->GetComponentLocation();
					const float W = P.TongueWidth * (0.85f + 0.15f * FMath::Sin(Now * 7.f + Phase));
					F->SetWorldScale3D(FVector(W, W, FMath::Max(H, 1.f) / 100.f));
				}
			}
			if (Now >= P.NextSpark && SparkFX)
			{
				P.NextSpark = Now + 0.28f;
				if (UStaticMeshComponent* F = P.Tongues[FMath::RandHelper(P.Tongues.Num())])
				{
					UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, F->GetComponentLocation() + FVector(0.f, 0.f, 40.f), FRotator(90.f, 0.f, 0.f), FVector(0.6f), true, true, ENCPoolMethod::AutoRelease);
				}
			}
		}
		if (!P.MID)
		{
			continue;
		}
		const float Fade = FMath::Min(1.f, (1.f - T) * 5.f);
		if (P.Style > 1.5f)
		{
			P.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(P.bShrink ? FMath::Max(1.f - T, 0.02f) : 1.f, P.Width, P.Strength * Fade * (0.85f + 0.15f * FMath::Sin(Now * 9.f)), 2.f));
		}
		else
		{
			P.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(T, P.Width, P.Strength, 0.f));
		}
	}
	// ---- 冲击波的浪头
	for (int32 i = Crests.Num() - 1; i >= 0; --i)
	{
		FCrest& R = Crests[i];
		R.Done = FMath::Min(R.Length, R.Done + R.Speed * DeltaTime);
		FVector P = R.From + R.Dir * R.Done;
		P.Z = GroundAt(P);
		if (R.Comp)
		{
			const float Height = R.bSlash ? 800.f : 300.f;
			R.Comp->SetWorldLocationAndRotation(P + FVector(0.f, 0.f, Height * 0.2f), R.Dir.Rotation());
			R.Comp->SetWorldScale3D(FVector(R.bSlash ? 1.4f : 0.9f, R.HalfWidth * 2.f / 100.f, Height / 100.f));
			if (R.MID)
			{
				const float K = FMath::Clamp((R.Length - R.Done) / FMath::Max(R.Length * 0.15f, 1.f), 0.f, 1.f);
				R.MID->SetScalarParameterValue(TEXT("Intensity"), (R.bSlash ? 2.5f : 1.5f) * K);
			}
		}
		// 一路炸开土块（地裂）/ 灰尘（横贯，走得太快，隔远一点才放）
		// 土块特效（Earth Exp_01）是一发很大的爆炸：带火柱和径向扭曲，贴着镜头放整个画面糊成一片彩虹（看截图才发现）。
		// 缩小，并且离出手的地方远一点才开始放
		const float Spacing = R.bSlash ? 1500.f : 520.f;
		if (R.LastBurst <= 0.f)
		{
			R.LastBurst = R.bSlash ? 1500.f : 700.f;
		}
		while (R.LastBurst <= R.Done)
		{
			FVector Q = R.From + R.Dir * R.LastBurst;
			Q.Z = GroundAt(Q);
			R.LastBurst += Spacing;
			if (UNiagaraSystem* FX = EarthFX.Get())
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, Q, FRotator::ZeroRotator, FVector(R.bSlash ? 0.45f : 0.3f), true, true, ENCPoolMethod::AutoRelease);
			}
			if (UNiagaraSystem* FX = EmberFX.Get())
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, Q, FRotator::ZeroRotator, FVector(R.bSlash ? 0.8f : 0.5f), true, true, ENCPoolMethod::AutoRelease);
			}
		}
		if (R.Done >= R.Length)
		{
			if (R.Comp)
			{
				R.Comp->DestroyComponent();
			}
			Crests.RemoveAtSwap(i);
		}
	}
	// ---- 横贯一刀的长光
	for (int32 i = Beams.Num() - 1; i >= 0; --i)
	{
		FBeam& L = Beams[i];
		const float T = (Now - L.Born) / FMath::Max(L.Life, 0.01f);
		if (T >= 1.f || !L.Comp)
		{
			if (L.Comp)
			{
				L.Comp->DestroyComponent();
			}
			Beams.RemoveAtSwap(i);
			continue;
		}
		const float R = L.Radius * (1.f - T) * (1.f - T);
		// 裂缝跟着浪头长；横贯一下就是全长
		const float Len = L.Grow > 0.f ? FMath::Min(L.Length, (Now - L.Born) * L.Grow) : L.Length;
		L.Comp->SetWorldLocation(L.From + L.Dir * Len * 0.5f);
		L.Comp->SetWorldScale3D(FVector(FMath::Max(R, 0.02f), FMath::Max(R, 0.02f), FMath::Max(Len, 1.f) / 100.f));
		if (L.MID)
		{
			L.MID->SetScalarParameterValue(TEXT("Intensity"), L.Strength * (1.f - T));
		}
	}
	// ---- 蓄力的圈：战斗组件不再调了就藏起来
	for (FChargeVis& V : Charges)
	{
		if (V.Comp && V.Comp->IsVisible() && Now - V.LastTick > 0.15f)
		{
			V.Comp->SetVisibility(false);
			V.bFull = false;
		}
	}
}
