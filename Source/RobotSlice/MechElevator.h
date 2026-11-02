// 电梯（11-01 用户：「这个平台有点小，而且有点低。做一个电梯上下」）：一块平台按服务器时间在底和顶之间往返——
// 底下停 StopTime 秒 → 先慢后快再慢地升 MoveTime 秒 → 顶上停 StopTime 秒 → 降回去。位置只由服务器时间算，
// 联机各端看到的一样，不用同步位置；机体站在上面由角色移动的「站在会动的东西上」带着走。
// 地图脚本里摆（tools/ue_build_dreamfield.py）：Actor 的位置 = 电梯在底部时平台顶面的中心，Travel = 升多高（厘米），Size = 平台长宽厚（厘米）。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechElevator.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class AMechElevator : public AActor
{
	GENERATED_BODY()

public:
	AMechElevator();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 升多高（厘米） */
	UPROPERTY(EditAnywhere, Category = "Elevator")
	float Travel = 2400.f;
	/** 平台长、宽、厚（厘米） */
	UPROPERTY(EditAnywhere, Category = "Elevator")
	FVector Size = FVector(1200.f, 1200.f, 60.f);
	/** 单程用时、两头各停多久（秒） */
	UPROPERTY(EditAnywhere, Category = "Elevator")
	float MoveTime = 6.f;
	UPROPERTY(EditAnywhere, Category = "Elevator")
	float StopTime = 4.f;
	/** 相位（秒）：几部电梯错开运行 */
	UPROPERTY(EditAnywhere, Category = "Elevator")
	float Phase = 0.f;
	UPROPERTY(EditAnywhere, Category = "Elevator")
	TObjectPtr<UMaterialInterface> PlatformMaterial;

	/** 现在升到了几成（0 底、1 顶；HUD / 声音以后用） */
	float GetLift01() const { return Lift01; }

private:
	float LiftAt(float Time) const;
	void ApplyLift(float L);
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Platform;
	float Lift01 = 0.f;
};
