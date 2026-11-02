#include "MechEffectComponent.h"

#include "GameFramework/GameStateBase.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"

namespace MechEffects
{
	// 效果表。插件（插件舱刷出来的，每种一个颜色和图形，最多叠 2 层）：超载、装甲强化、推进增压、快速装填、冲击核心、稳定器；
	// Test 是测试用的（-MechEffectTest 开局给每台挂上，验证同步和各处倍数）
	static const TArray<FDef>& Defs()
	{
		static const TArray<FDef> Table = {
			{TEXT("Test"), 3, FLinearColor(0.36f, 0.79f, 0.65f), {{EMechStat::DamageDealt, 0.1f}, {EMechStat::MoveSpeed, 0.05f}}, false, 0},
			{TEXT("Overdrive"), 2, FLinearColor(FColor(232, 96, 60)), {{EMechStat::DamageDealt, 0.2f}}, true, 1},
			{TEXT("Bulwark"), 2, FLinearColor(FColor(110, 160, 232)), {{EMechStat::DamageTaken, -0.25f}}, true, 2},
			{TEXT("Afterburner"), 2, FLinearColor(FColor(80, 212, 232)), {{EMechStat::MoveSpeed, 0.15f}, {EMechStat::EnergyCost, -0.3f}}, true, 3},
			{TEXT("Quickload"), 2, FLinearColor(FColor(236, 200, 72)), {{EMechStat::ReloadTime, -0.4f}, {EMechStat::MissileCooldown, -0.4f}}, true, 4},
			{TEXT("Impact"), 2, FLinearColor(FColor(222, 92, 182)), {{EMechStat::StaggerDealt, 0.35f}}, true, 5},
			{TEXT("Stabilizer"), 2, FLinearColor(FColor(132, 204, 92)), {{EMechStat::StaggerTaken, -0.35f}, {EMechStat::EnergyRegen, 0.3f}}, true, 6},
			// 被 EMP 打中（肩部武器）：不回能量（推进器失灵），锁定失效由战斗组件查这个效果
			{TEXT("Emp"), 1, FLinearColor(FColor(96, 150, 255)), {{EMechStat::EnergyRegen, -1.f}}, false, 7},
			// 刀的机制（10-30 玩家：每把刀要有自己的特色），层数上限和时长见 MechWeapons.h：
			// 快刀「连斩加速」（挂在自己身上）：每层移速 +2.5%（出刀快多少由战斗组件按层数算）
			{TEXT("Flurry"), 5, FLinearColor(FColor(240, 176, 64)), {{EMechStat::MoveSpeed, 0.025f}}, false, 3},
			// 双刀「割裂」（挂在被砍的人身上）：每层受到的伤害 +5%（所有来源都算，队友能跟着打）
			{TEXT("Rend"), 5, FLinearColor(FColor(214, 48, 48)), {{EMechStat::DamageTaken, 0.05f}}, false, 5},
			// 死斗重生保护（MechRules::SpawnGuardTime 秒，自己打中别人就提前结束）：不掉装甲、不积累失衡
			{TEXT("SpawnGuard"), 1, FLinearColor(FColor(226, 232, 240)), {{EMechStat::DamageTaken, -1.f}, {EMechStat::StaggerTaken, -1.f}}, false, 2},
		};
		return Table;
	}

	const FDef* Find(FName Id)
	{
		return Defs().FindByPredicate([Id](const FDef& D) { return D.Id == Id; });
	}

	TArray<FName> PluginIds()
	{
		TArray<FName> Out;
		for (const FDef& D : Defs())
		{
			if (D.bPlugin)
			{
				Out.Add(D.Id);
			}
		}
		return Out;
	}
}

UMechEffectComponent::UMechEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;  // 只用来到期移除
	SetIsReplicatedByDefault(true);
	for (float& S : Scale)
	{
		S = 1.f;
	}
}

void UMechEffectComponent::BeginPlay()
{
	Super::BeginPlay();
	// 测试：每台机体开局挂两层测试效果，20 秒后到期（看同步、HUD 插件栏、倍数）
	if (GetOwner()->HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("MechEffectTest")))
	{
		AddEffect(TEXT("Test"), 20.f, 2);
		UE_LOG(LogTemp, Display, TEXT("[effect] %s 挂上测试效果：伤害 x%.2f 速度 x%.2f"), *GetOwner()->GetName(), GetScale(EMechStat::DamageDealt), GetScale(EMechStat::MoveSpeed));
	}
}

float UMechEffectComponent::Now() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
}

void UMechEffectComponent::AddEffect(FName Id, float Duration, int32 Stacks)
{
	const MechEffects::FDef* Def = MechEffects::Find(Id);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Def)
	{
		return;
	}
	FMechEffect* E = Effects.FindByPredicate([Id](const FMechEffect& X) { return X.Id == Id; });
	if (!E)
	{
		E = &Effects.AddDefaulted_GetRef();
		E->Id = Id;
		E->Stacks = 0;
	}
	E->Stacks = static_cast<uint8>(FMath::Clamp<int32>(E->Stacks + Stacks, 1, FMath::Max<int32>(1, Def->MaxStacks)));
	E->Duration = FMath::Max(0.f, Duration);
	E->EndTime = Duration > 0.f ? Now() + Duration : -1.f;
	Recompute();
}

void UMechEffectComponent::RemoveEffect(FName Id)
{
	if (GetOwner() && GetOwner()->HasAuthority() && Effects.RemoveAll([Id](const FMechEffect& X) { return X.Id == Id; }) > 0)
	{
		Recompute();
	}
}

void UMechEffectComponent::ClearEffects()
{
	if (GetOwner() && GetOwner()->HasAuthority() && !Effects.IsEmpty())
	{
		Effects.Reset();
		Recompute();
	}
}

void UMechEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!GetOwner() || !GetOwner()->HasAuthority() || Effects.IsEmpty())
	{
		return;
	}
	const float T = Now();
	if (Effects.RemoveAll([T](const FMechEffect& X) { return X.EndTime >= 0.f && T >= X.EndTime; }) > 0)
	{
		Recompute();
	}
}

void UMechEffectComponent::OnRep_Effects()
{
	Recompute();
}

void UMechEffectComponent::Recompute()
{
	for (float& S : Scale)
	{
		S = 1.f;
	}
	for (const FMechEffect& E : Effects)
	{
		if (const MechEffects::FDef* Def = MechEffects::Find(E.Id))
		{
			for (const MechEffects::FMod& M : Def->Mods)
			{
				Scale[static_cast<int32>(M.Stat)] *= FMath::Max(0.f, 1.f + M.PerStack * E.Stacks);
			}
		}
	}
}

void UMechEffectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMechEffectComponent, Effects);
}
