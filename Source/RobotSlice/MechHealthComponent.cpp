#include "MechHealthComponent.h"

#include "Net/UnrealNetwork.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechEffectComponent.h"
#include "MechGameState.h"
#include "MechWeapons.h"
#include "MechLoadout.h"
#include "MechRun.h"

UMechHealthComponent::UMechHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMechHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetHealth();
}

void UMechHealthComponent::SetMaxValues(float InMaxAP, float InMaxStagger, bool bRefill)
{
	MaxAP = InMaxAP * APScale;
	MaxStagger = InMaxStagger;
	if (bRefill)
	{
		ResetHealth();
	}
	else
	{
		AP = FMath::Min(AP, MaxAP);
		Stagger = FMath::Min(Stagger, MaxStagger);
	}
}

void UMechHealthComponent::SetAPScale(float InScale)
{
	if (!FMath::IsNearlyEqual(APScale, InScale))
	{
		MaxAP *= InScale / APScale;
		APScale = InScale;
	}
}

void UMechHealthComponent::OnRep_APScale(float OldScale)
{
	MaxAP *= APScale / FMath::Max(OldScale, 0.01f);
}

void UMechHealthComponent::ResetHealth()
{
	AP = MaxAP;
	Stagger = 0.f;
	bStaggered = false;
	bStun = false;
	bDead = false;
	SinceHit = 10.f;
	StaggerTimer = 0.f;
}

void UMechHealthComponent::Heal(float Amount)
{
	if (!bDead && GetOwner()->HasAuthority())
	{
		AP = FMath::Min(MaxAP, AP + Amount);
	}
}

void UMechHealthComponent::Stun(float Seconds, bool bSilent)
{
	if (bDead || !GetOwner()->HasAuthority() || (bStaggered && !bStun))
	{
		return;  // 已经真失衡了：不用再僵直，也别把失衡缩短
	}
	bStaggered = true;
	bStun = true;
	StaggerTimer = FMath::Max(StaggerTimer, Seconds);
	if (AMechCharacter* Self = Cast<AMechCharacter>(GetOwner()))
	{
		if (UMechCombatComponent* Combat = Self->GetCombat())
		{
			Combat->InterruptMelee();
		}
	}
	if (!bSilent)
	{
		MulticastStaggered();  // 动作、失衡特效和声音照失衡放一遍（短）
	}
}

float UMechHealthComponent::ApplyHit(float Damage, float Impact, AActor* Instigator, const FVector& HitLocation, float StaggerScale)
{
	if (bDead || !GetOwner()->HasAuthority())
	{
		return 0.f;
	}
	AMechCharacter* Self = Cast<AMechCharacter>(GetOwner());
	// 插件效果：攻击方的伤害 / 失衡倍数，受击方的承受倍数（割裂的层数也在承受倍数里）
	if (Self)
	{
		Damage *= Self->EffectScale(EMechStat::DamageTaken);
		StaggerScale *= Self->EffectScale(EMechStat::StaggerTaken);
	}
	const AController* ByController = Cast<AController>(Instigator);
	// 11-02 挑战倍数（人数不等时加强人少的一方，房主在房间里开，AMechGameState::ChallengeTeam）：只在这里结算（服务器）。
	// 「装甲 ×k」做成受到的伤害和失衡 ÷ k，血条比例不变，HUD、结算都不用改
	const AMechGameState* ChallengeGS = GetWorld() ? GetWorld()->GetGameState<AMechGameState>() : nullptr;
	const int32 ChallengeTeam = ChallengeGS ? ChallengeGS->ChallengeTeam : -1;
	if (Self && ChallengeTeam >= 0 && Self->GetTeam() == ChallengeTeam)
	{
		Damage /= ChallengeGS->ChallengeArmor;
		StaggerScale /= ChallengeGS->ChallengeArmor;
	}
	if (const AMechCharacter* By = ByController ? Cast<AMechCharacter>(ByController->GetPawn()) : Cast<AMechCharacter>(Instigator))
	{
		Damage *= By->EffectScale(EMechStat::DamageDealt);
		StaggerScale *= By->EffectScale(EMechStat::StaggerDealt);
		if (ChallengeTeam >= 0 && By->GetTeam() == ChallengeTeam && By != Self)
		{
			Damage *= ChallengeGS->ChallengeDamage;
			StaggerScale *= ChallengeGS->ChallengeDamage;
		}
		// 保卫战（11-01，MechRun.h）：等级、空战的伤害倍数。对战里没有 Run，这里什么都不做
		if (const UMechRunComponent* Run = By->GetRun())
		{
			Damage *= Run->OutgoingScale();
		}
	}
	// 大剑霸体重斩：受到的伤害降低、不积累失衡；冲击（击退、顿帧、受击镜头都按它算）只留四分之一，基本推不动
	if (Self && Self->GetCombat() && Self->GetCombat()->IsSuperArmor())
	{
		// 11-01 肉鸽 蓄力 2：蓄力时受伤减半（RunChargeHold == 2 只有带那张卡才会有）
		Damage *= Self->GetCombat()->GetRunChargeHold() == 2 ? MechRun::ChargeDamageTaken : Self->IsOwnMaker(MechLoadout::Melee) ? MechWeapons::OwnSuperArmorDamage : MechWeapons::SuperArmorDamage;
		StaggerScale = 0.f;
		Impact *= 0.25f;
	}
	const bool bDirect = bStaggered && !bStun;  // 被弹反的短僵直不吃直击加成
	// 木桩按实际伤害算读数（装甲最低留 1，打到底也照算）
	const float Raw = Damage * (bDirect ? DirectHitScale : 1.f);
	const float Dealt = bNoDeath ? Raw : FMath::Min(AP, Raw);
	AP -= Dealt;
	SinceHit = 0.f;
	MulticastHit(Dealt, Impact, Instigator, HitLocation, bDirect);
	if (!bStaggered || bStun)
	{
		Stagger += Impact * StaggerScale;
		if (Stagger >= MaxStagger)
		{
			Stagger = MaxStagger;
			bStaggered = true;
			bStun = false;
			StaggerTimer = StaggerDuration * StaggerTimeScale;
			// 失衡 = 僵直：正在出的刀也断掉（以前失衡了刀照样砍下去）
			if (Self && Self->GetCombat())
			{
				Self->GetCombat()->InterruptMelee();
			}
			MulticastStaggered();
		}
	}
	if (bNoDeath)
	{
		AP = FMath::Max(AP, 1.f);
	}
	if (AP <= 0.f)
	{
		AP = 0.f;
		bDead = true;
		MulticastDied(Instigator);
	}
	return Dealt;
}

void UMechHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 现场抢修的绿光：各端按复制下来的 bRepairing 自己闪（每 1.2 秒一下，很短）
	if (bRepairing && !bDead)
	{
		RepairFlashClock -= DeltaTime;
		if (RepairFlashClock <= 0.f)
		{
			RepairFlashClock = 1.2f;
			if (AMechCharacter* Self = Cast<AMechCharacter>(GetOwner()))
			{
				Self->FlashBody(FLinearColor(0.3f, 1.f, 0.45f), 0.35f);
			}
		}
	}
	if (!GetOwner()->HasAuthority() || bDead)
	{
		return;
	}
	SinceHit += DeltaTime;
	// 现场抢修（新纪建设）：RepairDelay 秒没挨打，每秒回 RepairPerSecond × 上限，最多回到 RepairCap × 上限（高于这条线不修）
	float RepairRate = RepairPerSecond;
	float RepairUpTo = RepairCap;
	bool bRepairTime = SinceHit >= RepairDelay;
	// 保卫战（11-01，MechRun.h）：新纪 抢修路径改这三个。对战里没有 Run，照旧
	if (const AMechCharacter* RunOwner = Cast<AMechCharacter>(GetOwner()); RunOwner && RunOwner->GetRun())
	{
		RunOwner->GetRun()->TuneRepair(bRepairTime, RepairRate, RepairUpTo);
	}
	const float RepairTo = MaxAP * RepairUpTo;
	const bool bRepairNow = bFieldRepair && !bNoDeath && bRepairTime && AP < RepairTo;
	if (bRepairNow)
	{
		AP = FMath::Min(RepairTo, AP + MaxAP * RepairRate * DeltaTime);
	}
	bRepairing = bRepairNow;
	if (bStaggered)
	{
		StaggerTimer -= DeltaTime;
		if (!bStun)
		{
			Stagger = MaxStagger * FMath::Max(0.f, StaggerTimer / (StaggerDuration * StaggerTimeScale));
		}
		if (StaggerTimer <= 0.f)
		{
			if (!bStun)
			{
				Stagger = 0.f;
			}
			bStaggered = false;
			bStun = false;
		}
	}
	if ((!bStaggered || bStun) && SinceHit > StaggerDecayDelay)
	{
		Stagger = FMath::Max(0.f, Stagger - StaggerDecayPerSecond * DeltaTime);
	}
}

void UMechHealthComponent::MulticastHit_Implementation(float Damage, float Impact, AActor* Instigator, FVector_NetQuantize HitLocation, bool bDirectHit)
{
	// 受击分档：同一下（0.1 秒内，比如霰弹的一组弹丸）的冲击合在一起算
	const float Now = GetWorld()->GetTimeSeconds();
	HurtImpactSum = (Now - LastHurtTime < 0.1f ? HurtImpactSum : 0.f) + Impact;
	const int32 Tier = (bDirectHit || HurtImpactSum >= 700.f) ? 2 : HurtImpactSum >= 300.f ? 1 : 0;
	LastHurtTier = Now - LastHurtTime < 0.1f ? FMath::Max(LastHurtTier, Tier) : Tier;
	LastHurtTime = Now;
	OnHit.Broadcast(Damage, Impact, Instigator, HitLocation, bDirectHit);
	PlayHurtFeedback(Damage);
}

void UMechHealthComponent::PlayHurtFeedback(float Damage)
{
	// 10-30 玩家：被击中的反馈再强一点（分档，别太影响瞄准）。机体那边（OnHealthHit）已经有闪光、镜头冲击、上身一抖；
	// 这里给本机玩家补：低沉的受击闷响（只有自己听得到，按档变重）和更亮一档的整机闪光。不加镜头转动，免得准星被晃走
	AMechCharacter* Self = Cast<AMechCharacter>(GetOwner());
	if (!Self || Damage <= 0.f || !Self->IsLocallyControlled() || !Self->IsPlayerControlled())
	{
		return;
	}
	const float Real = GetWorld()->GetRealTimeSeconds();
	if (!HurtSound)
	{
		HurtSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Combat/S_hurt_thud"));
	}
	// 轻（子弹）：很轻的一下、0.15 秒最多一声；重：闷响；很重（直击 / 失衡 / 大冲击）：更低更响
	static const float Volume[3] = {0.16f, 0.45f, 0.75f};
	static const float Pitch[3] = {1.4f, 1.05f, 0.8f};
	static const float Gap[3] = {0.15f, 0.08f, 0.f};
	if (HurtSound && Real - LastHurtSoundTime > Gap[LastHurtTier])
	{
		LastHurtSoundTime = Real;
		UGameplayStatics::PlaySound2D(this, HurtSound, Volume[LastHurtTier], Pitch[LastHurtTier] + FMath::FRandRange(-0.05f, 0.05f));
	}
	if (LastHurtTier >= 1)
	{
		Self->FlashBody(LastHurtTier == 2 ? FLinearColor(1.f, 0.9f, 0.8f) : FLinearColor(1.f, 0.45f, 0.15f), LastHurtTier == 2 ? 1.9f : 1.2f);
	}
}

void UMechHealthComponent::MulticastDied_Implementation(AActor* Killer)
{
	bDead = true;
	OnDied.Broadcast(Killer);
}

void UMechHealthComponent::MulticastStaggered_Implementation()
{
	// 被打进失衡（不算弹反的短僵直）：受击按最重一档
	if (!bStun)
	{
		LastHurtTier = 2;
		LastHurtTime = GetWorld()->GetTimeSeconds();
	}
	OnStaggered.Broadcast();
}

void UMechHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMechHealthComponent, AP);
	DOREPLIFETIME(UMechHealthComponent, Stagger);
	DOREPLIFETIME(UMechHealthComponent, bStaggered);
	DOREPLIFETIME(UMechHealthComponent, bDead);
	DOREPLIFETIME(UMechHealthComponent, bStun);
	DOREPLIFETIME(UMechHealthComponent, bRepairing);
	DOREPLIFETIME(UMechHealthComponent, APScale);
}
