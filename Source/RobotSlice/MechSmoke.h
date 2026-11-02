// 烟雾弹的干扰烟幕：落点一团贴地的烟云（约 7 米高、20 米宽），持续 20 秒（11-02 起；烟里的敌人每秒掉 20 点血）——头 1 秒从中心往外翻滚涨开，最后 2 秒变淡散掉。
// （10-30 玩家反馈后加强：半径 7.5 → 10 米、9 → 13 秒；穿过烟的导弹永久丢失目标；HUD 不再在烟后的敌人身上画干扰框，改画在准星上）
// 烟是二十多片朝向镜头的烟片（平面网格每帧转过来对着本机镜头；M_SmokePuff：特效包的写实烟雾序列帧，受场景光照，白天夜里都对），
// 各自慢慢转、往外飘、换帧（朝向镜头的贴片组件在 5.8 里贴图坐标传不进材质，整片烟变成均匀的方板，所以不用它）；
// 烟里飘着一闪一闪的金属干扰箔片（M_ChaffGlint），一看就是干扰烟。服务器生成，复制出生时间，各端按同一个随机种子摆烟片。
// BlocksLine：一条线从烟里穿过（或有一头在烟里）就算看不见——玩家和 AI 的自动索敌（锁不上也就没有瞄准辅助）、AI 的视线、导弹追踪、
// HUD 的敌人标记（界面那边接）都查它。IsInside：这个点在烟里。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechSmoke.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USoundBase;
class USoundAttenuation;

UCLASS()
class AMechSmoke : public AActor
{
	GENERATED_BODY()

public:
	AMechSmoke();

	/** 这条线有没有被烟挡住（一头在烟里也算）。HUD 要不要画某台机体的标记：BlocksLine(World, 镜头位置, 机体位置) */
	static bool BlocksLine(const UWorld* World, const FVector& A, const FVector& B);
	/** 这个点在不在烟里 */
	static bool IsInside(const UWorld* World, const FVector& P);

	/** 现在烟的半径（随时间涨开、散掉） */
	float GetRadius() const;

	static constexpr float Radius = 1000.f;
	static constexpr float Duration = 20.f;
	/** 烟里的敌人每秒受的伤害（服务器判定，算给放烟的人，只伤敌方；11-02） */
	static constexpr float DamagePerSecond = 20.f;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	float ServerNow() const;
	float Age() const;
	/** 按年龄摆烟片和箔片 */
	void Animate();
	UStaticMeshComponent* MakePlane(const FVector& At, float Size, UMaterialInstanceDynamic* MID);

	UPROPERTY(Replicated)
	float SpawnTime = 0.f;
	float DamageAccum = 0.f;

	/** 一片烟 / 一片箔片：出现的时间、起始位置、往外飘的方向、转速、序列帧起点、不透明度、开始变淡的时间 */
	struct FPuff
	{
		FVector Start = FVector::ZeroVector;
		FVector Drift = FVector::ZeroVector;
		float Delay = 0.f;
		float Rot0 = 0.f;
		float Spin = 0.f;
		float Frame0 = 0.f;
		float Opacity = 1.f;
		float FadeAt = 0.f;
		float Rate = 0.f;
	};
	TArray<FPuff> Puffs;
	TArray<FPuff> Glints;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> PuffComps;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMIDs;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> GlintComps;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> GlintMIDs;

	/** 烟片 / 箔片用的平面（引擎自带 100 厘米见方的 Plane，UV 0–1） */
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;

	/** 每片的边长（厘米） */
	TArray<float> PuffSizes;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> PuffMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlintMaterial;

	UPROPERTY()
	TObjectPtr<USoundBase> BurstSound;

	UPROPERTY()
	TObjectPtr<USoundBase> HissSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;
};
