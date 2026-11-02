// 彩蛋的机关（11-02，总说明见 MechEaster.h）：压力板、老街要打破的门、门后的射击按钮。
// 地图里有用户摆的标记（EasterPlate / EasterDoor / EasterButton）就挂在标记上（Linked），标记自己带模型就不另外画；
// 没有模型（空点）或者没有标记，就用引擎自带的方块 / 圆柱画一个占位（和保卫战杂兵、指挥所的占位一样，不是做出来的资源）。
// 门和按钮自己有一个打得中的盒子（WorldDynamic、只查询，标签 MechShootable + MechGrunt：子弹、刀、导弹爆炸都算），
// 比标记的模型往外凸一点，先被打中。门破了：服务器和各端都把标记那个 actor 藏起来、关掉碰撞（地图里的东西本身不复制，各端按 OnRep 自己藏）。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechEasterProp.generated.h"

class AMechEasterDirector;
class UBoxComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

namespace MechEasterProp
{
	constexpr uint8 Plate = 0;
	constexpr uint8 Door = 1;
	constexpr uint8 Button = 2;
}

UCLASS(NotPlaceable)
class AMechEasterProp : public AActor
{
	GENERATED_BODY()

public:
	AMechEasterProp();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 服务器：生成后马上调。Size = 占位 / 打击盒的尺寸（厘米，X 厚 Y 宽 Z 高；压力板是直径 × 直径 × 厚）；
	 *  bPlaceholder = 画占位模型；LinkedTag = 地图里那个标记的标签（各端按位置 + 标签找到它，门破了藏起来） */
	void InitProp(uint8 InKind, const FVector& InSize, bool bInPlaceholder, FName InLinkedTag, AMechEasterDirector* InDirector);

	uint8 GetKind() const { return Kind; }
	bool IsDone() const { return bDone; }
	bool IsArmed() const { return bArmed; }
	/** 服务器：踩下 / 打破 / 打中（只会发生一次） */
	void SetDone(AActor* By);
	/** 服务器：按钮露出来了（门破了以后才能打） */
	void SetArmed(bool bOn);
	/** 压力板：踩上的判定半径（厘米） */
	float Radius = 260.f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_State)
	uint8 Kind = 0;
	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bDone = false;
	UPROPERTY(ReplicatedUsing = OnRep_State)
	bool bArmed = false;
	UPROPERTY(Replicated)
	bool bPlaceholder = true;
	UPROPERTY(Replicated)
	FVector_NetQuantize Size;
	UPROPERTY(Replicated)
	FName LinkedTag;
	UFUNCTION()
	void OnRep_State();
	void Refresh();
	bool bBuilt = false;
	void Build();
	/** 地图里的标记（各端按标签 + 位置自己找） */
	AActor* FindLinked() const;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBreakFX(FVector_NetQuantize At);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> HitBox;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Base;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Glow;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	TWeakObjectPtr<AMechEasterDirector> Director;
	float HP = 700.f;
	float Clock = 0.f;
	float DoneTime = -100.f;
};
