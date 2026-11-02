// 机体的镜头臂（11-02）：和引擎的弹簧臂完全一样，只多一个接口——老街循环（MechLoop.h）里机体瞬移一个周期时，
// 把镜头延迟记着的「上一帧位置」也挪同样的量，镜头照常带着延迟跟随，不会一下子贴到机体上（以前瞬移那帧关掉延迟，镜头会「顿」一下）。
// AMechCharacter 构造时用 SetDefaultSubobjectClass 把模板角色的 CameraBoom 换成它。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "MechSpringArm.generated.h"

UCLASS()
class UMechSpringArm : public USpringArmComponent
{
	GENERATED_BODY()

public:
	/** 机体整体挪了 Offset（世界坐标）：镜头延迟的记录一起挪 */
	void ShiftLag(const FVector& Offset)
	{
		PreviousDesiredLoc += Offset;
		PreviousArmOrigin += Offset;
	}
};
