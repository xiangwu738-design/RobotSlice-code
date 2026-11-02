// 插件效果：机体身上挂的一组效果（插件、地图增益……），每个带层数和到期时间。服务器加减，同步给所有人（客户端移动预测和 HUD 都要读）。
// 各处数值查 GetScale(Stat)，没有效果时是 1：伤害 / 失衡（UMechHealthComponent::ApplyHit，攻击方和受击方各乘一次）、
// 移动速度和推进能量消耗 / 回复（UMechMovementComponent）、换弹时间和导弹冷却（UMechCombatComponent）。
// 效果写在 MechEffects 的表里（每层对哪些数值加多少）。插件怎么拿到（开局选、地图拾取、掉落）还没定，接 AddEffect 就行；
// 机体每次生成都是新的，效果跟着清空，要整局保留的由 GameMode 在生成时重新加。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechEffectComponent.generated.h"

UENUM()
enum class EMechStat : uint8
{
	DamageDealt,
	DamageTaken,
	StaggerDealt,
	StaggerTaken,
	MoveSpeed,
	EnergyCost,
	EnergyRegen,
	ReloadTime,
	MissileCooldown,
	Count UMETA(Hidden)
};

USTRUCT()
struct FMechEffect
{
	GENERATED_BODY()

	UPROPERTY()
	FName Id;

	UPROPERTY()
	uint8 Stacks = 1;

	/** 服务器时间；小于 0 = 一直有效 */
	UPROPERTY()
	float EndTime = -1.f;

	/** 总时长（HUD 画剩余时间圈用） */
	UPROPERTY()
	float Duration = 0.f;
};

namespace MechEffects
{
	/** 每层让数值乘 (1 + PerStack)，多个效果相乘 */
	struct FMod
	{
		EMechStat Stat;
		float PerStack;
	};

	struct FDef
	{
		FName Id;
		uint8 MaxStacks;
		/** HUD 图标的颜色（图标只画几何形状，不写字） */
		FLinearColor Color;
		TArray<FMod> Mods;
		/** 插件舱里会刷出来的（测试效果不算） */
		bool bPlugin = false;
		/** HUD 六边形里画的小图形：0 点、1 上三角、2 方块、3 双箭头、4 圆弧、5 叉、6 两道横杠、7 闪电 */
		uint8 Glyph = 0;
	};

	const FDef* Find(FName Id);
	/** 所有插件（插件舱随机抽） */
	TArray<FName> PluginIds();
}

UCLASS(ClassGroup = (Mech))
class UMechEffectComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechEffectComponent();

	virtual void BeginPlay() override;

	/** 服务器：加一个效果（已有就叠层、刷新时间）；Duration <= 0 一直有效 */
	void AddEffect(FName Id, float Duration, int32 Stacks = 1);
	void RemoveEffect(FName Id);
	void ClearEffects();

	float GetScale(EMechStat Stat) const { return Scale[static_cast<int32>(Stat)]; }
	bool HasEffect(FName Id) const { return Effects.ContainsByPredicate([Id](const FMechEffect& E) { return E.Id == Id; }); }
	/** 某个效果现在几层（没有为 0） */
	int32 GetStacks(FName Id) const
	{
		const FMechEffect* E = Effects.FindByPredicate([Id](const FMechEffect& X) { return X.Id == Id; });
		return E ? E->Stacks : 0;
	}
	const TArray<FMechEffect>& GetEffects() const { return Effects; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(ReplicatedUsing = OnRep_Effects)
	TArray<FMechEffect> Effects;

	UFUNCTION()
	void OnRep_Effects();

	/** 按 Effects 重算各数值倍数（服务器改动后、客户端收到同步后） */
	void Recompute();

	float Now() const;

	float Scale[static_cast<int32>(EMechStat::Count)];
};
