// -MechAITierTest（11-01 人机强度）：四档 AI 分别打同一个移动靶、再互相对打，量化档位差距。做法和用法写在 MechAITierTest.cpp 开头。
// 用世界子系统跑（命令行带 -MechAITierTest 才创建），不用在玩家控制器 / 战斗组件里加钩子。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechAITierTest.generated.h"

UCLASS()
class UMechAITierTest : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
};
