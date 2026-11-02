// 机体耐久：装甲值（AP，打空即击毁）和失衡槽。
// 每次命中按「冲击值」积累失衡，满了进入失衡：机体僵直一会儿，这期间受到的伤害加成（直击）。一段时间不挨打失衡槽会慢慢回落。
// 只在服务器上结算，数值复制给客户端；命中特效由组播通知各端。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechHealthComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_FiveParams(FOnMechHit, float /*Damage*/, float /*Impact*/, AActor* /*Instigator*/, FVector /*HitLocation*/, bool /*bDirectHit*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMechDied, AActor* /*Killer*/);
DECLARE_MULTICAST_DELEGATE(FOnMechStaggered);

UCLASS(ClassGroup=(Mech), meta=(BlueprintSpawnableComponent))
class UMechHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechHealthComponent();

	/** 中型框架的装甲（轻型 / 重型在 AMechCharacter::ApplyLoadout）。10-30 玩家：TTK 过长，以少打多时来不及先打掉一台 →
	 *  三种框架装甲都 ×0.85（12000→10200，轻 9000→7650，重 15500→13200），整体 TTK 短约 15% */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float MaxAP = 10200.f;

	/** 失衡槽上限（10-29 用户：失衡不要那么容易触发，但惩罚可以高一点 → 1000 改 1400、回落更快，僵直和伤害倍数加大） */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float MaxStagger = 1400.f;

	/** 多久没挨打失衡槽开始回落（秒） */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float StaggerDecayDelay = 1.0f;

	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float StaggerDecayPerSecond = 320.f;

	/** 失衡僵直多久 */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float StaggerDuration = 2.6f;

	/** 失衡期间受到的伤害倍数 */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float DirectHitScale = 1.9f;

	/** 刀积累失衡的倍数：刀的冲击值照样用来算顿帧、击退、闪光，但积累失衡只算这么多
	 *  （不打折时一套四连 1380、重斩一下 950，近身就能把人砍到动不了；现在四连约 760、接重斩约 1280，要再补几枪或配合导弹才失衡） */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float BladeStaggerScale = 0.55f;

	/** 结算一次命中（只在服务器上调用），返回实际伤害；StaggerScale 是这一下积累失衡的倍数。
	 *  大剑霸体重斩期间：伤害 ×MechWeapons::SuperArmorDamage、不积累失衡、冲击（击退 / 顿帧）大减；进入失衡时打断正在出的刀 */
	float ApplyHit(float Damage, float Impact, AActor* Instigator, const FVector& HitLocation, float StaggerScale = 1.f);

	/** 服务器：短僵直（被弹反）——和失衡一样动不了、不能攻击，但不吃「直击」伤害加成、不清失衡槽；已经失衡时不管。
	 *  bSilent（11-01 拼刀）：不放失衡的特效、声音和 1.2 秒的动作，反馈由调用方自己放（僵直只有 0.3 秒，再叠失衡那套就和弹反分不开了） */
	void Stun(float Seconds, bool bSilent = false);
	/** 现在是被弹反的短僵直（不是真失衡） */
	bool IsStunned() const { return bStaggered && bStun; }

	/** 最近一次受击（各端按组播记，本机玩家的受击反馈 / HUD 用）：时间、冲击、是不是直击；
	 *  分档 0 轻（子弹）/ 1 重（冲击 ≥ 300：刀、导弹、霰弹一整发）/ 2 很重（直击、冲击 ≥ 700 或刚被打进失衡） */
	float GetLastHurtTime() const { return LastHurtTime; }
	int32 GetLastHurtTier() const { return LastHurtTier; }

	/** 回合开始时回满 */
	void ResetHealth();
	/** 回复装甲（服务器；霰弹枪吸血用），不超过上限 */
	void Heal(float Amount);
	/** 按配装（框架）设装甲和失衡上限；bRefill 时装甲回满 */
	void SetMaxValues(float InMaxAP, float InMaxStagger, bool bRefill);
	/** 保卫战等级（11-01，MechRun.h）：装甲上限再乘这个（服务器设，复制给各端，换配装后还在）；对战里永远是 1 */
	void SetAPScale(float InScale);
	float GetSinceHit() const { return SinceHit; }

	/** 测试场的木桩：打不死（装甲最低留 1），机体那边停手几秒就回满 */
	bool bNoDeath = false;

	/** 框架制造商特色（10-31，AMechCharacter::ApplyLoadout 按 MechCatalog::FrameSpec 设）：失衡僵直时间倍数（维克托重工 0.65）、现场抢修（新纪建设） */
	float StaggerTimeScale = 1.f;
	bool bFieldRepair = false;
	/** 现场抢修：多久没挨打开始修、每秒修装甲上限的几成、最多修到上限的几成 */
	static constexpr float RepairDelay = 6.f;
	static constexpr float RepairPerSecond = 0.02f;
	static constexpr float RepairCap = 0.5f;
	/** 正在现场抢修（复制：HUD 装甲读数变绿、机体每隔一会儿闪一下绿光，对手也看得到） */
	bool IsRepairing() const { return bRepairing; }

	float GetAP() const { return AP; }
	float GetAPFraction() const { return AP / FMath::Max(1.f, MaxAP); }
	float GetStaggerFraction() const { return Stagger / FMath::Max(1.f, MaxStagger); }
	bool IsStaggered() const { return bStaggered; }
	bool IsDead() const { return bDead; }

	/** 各端都会收到（服务器结算后组播） */
	FOnMechHit OnHit;
	FOnMechDied OnDied;
	FOnMechStaggered OnStaggered;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHit(float Damage, float Impact, AActor* Instigator, FVector_NetQuantize HitLocation, bool bDirectHit);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDied(AActor* Killer);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStaggered();

	UPROPERTY(Replicated)
	float AP = 12000.f;

	UPROPERTY(Replicated)
	float Stagger = 0.f;

	UPROPERTY(Replicated)
	bool bStaggered = false;

	UPROPERTY(Replicated)
	bool bDead = false;

	/** 被弹反的短僵直（bStaggered 同时为真）；客户端动作、HUD 也要知道，复制 */
	UPROPERTY(Replicated)
	bool bStun = false;

	UPROPERTY(Replicated)
	bool bRepairing = false;

	UPROPERTY(ReplicatedUsing = OnRep_APScale)
	float APScale = 1.f;
	UFUNCTION()
	void OnRep_APScale(float OldScale);
	float RepairFlashClock = 0.f;

	float SinceHit = 10.f;
	float StaggerTimer = 0.f;
	float LastHurtTime = -100.f;
	int32 LastHurtTier = 0;
	float HurtImpactSum = 0.f;
	/** 本机玩家挨打时的补充反馈（受击闷响、重一档的闪光），在组播里调 */
	void PlayHurtFeedback(float Damage);
	/** 本机玩家受击音（第一次用时加载）、上次响的时间（子弹太密时限流） */
	UPROPERTY()
	TObjectPtr<class USoundBase> HurtSound;
	float LastHurtSoundTime = -100.f;
};
