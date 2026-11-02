// 插件舱：对局中途刷在场上的拾取物（死斗刷在场地中央，夺芯刷在铸造台两侧），机体碰到就随机拿到一种插件效果（MechEffects 表里
// 带 bPlugin 的那些），持续 PluginTime 秒，拿到同一种叠层。服务器判定、同步给所有人；外观是转动的方块 + 外圈 + 灯，颜色是待出的插件色。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechPluginPod.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

UCLASS()
class AMechPluginPod : public AActor
{
	GENERATED_BODY()

public:
	AMechPluginPod();

	/** 插件持续时间（秒） */
	static constexpr float PluginTime = 45.f;

	/** 这个舱里是哪种插件（生成时服务器抽好，同步给客户端决定颜色） */
	UPROPERTY(ReplicatedUsing = OnRep_Plugin)
	FName Plugin;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_Plugin();
	void ApplyLook();

	UFUNCTION()
	void OnTouch(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY()
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ring;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	float Clock = 0.f;
	bool bTaken = false;
};
