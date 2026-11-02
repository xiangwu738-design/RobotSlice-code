// 11-02 月球基地（L_Moon）真空静音：穹顶外没有空气，听不到远处的声音。
//
// 做法（只在有画面的那端，按本机听者的位置）：听者在大穹顶外时，把已加载的所有衰减设置（USoundAttenuation）改成「9 米内照常、
// 再往外 3 米就没了」，并且越远越闷（低通）——自己的机体（镜头在机体后面 5–6 米）贴身的声音还听得到，像是从机体骨架传过来的；
// 别人打枪、爆炸、推进器在远处一概听不见。回到穹顶里（有空气）恢复原样。2D 声音（界面、通报、音乐）不受影响。
// 改的是共享的资源，离开地图（子系统销毁）时一定还原。
//
// 穹顶：默认大穹顶在原点（tools/moon_layout.py 的 XLG_C：外墙半径 62 米、墙顶 9 米、里面地面 5 米），按半球顶算，顶高 DomeHeight。
// 地图里放带「MoonAir」标签的点可以覆盖（标签 Radius=<厘米> Height=<厘米>，点的位置 = 穹顶中心地面），可以放多个。
// 测试：-MechVacuumLog 每次进出打一行 [vacuum]；-MechVacuumOff 关掉这条规则。

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundAttenuation.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechVacuum.generated.h"

UCLASS()
class UMechVacuum : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMechVacuum, STATGROUP_Tickables); }

	/** 这个位置有空气（在某个穹顶里）；不是月球图 = 永远有 */
	bool HasAir(const FVector& P);
	/** 本机听者现在在真空里（HUD 可以用） */
	bool IsListenerInVacuum() const { return bVacuum; }

	/** 真空里声音照常的半径、再往外多远降到 0（厘米） */
	static constexpr float NearRadius = 900.f;
	static constexpr float NearFalloff = 300.f;
	/** 默认穹顶顶高（离里面地面，厘米） */
	static constexpr float DomeHeight = 3000.f;

private:
	struct FDome
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 6200.f;
		float Height = DomeHeight;
	};
	void ScanDomes();
	void Apply(bool bOn);
	void MuffleNew();

	bool bScanned = false;
	TArray<FDome> Domes;
	bool bVacuum = false;
	float RescanClock = 0.f;
	TMap<TWeakObjectPtr<USoundAttenuation>, FSoundAttenuationSettings> Original;
};
