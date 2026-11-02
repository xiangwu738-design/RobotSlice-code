// 按地图生效的规则（11-02，石柱山水）。用户：「我打算为关卡设计特殊的机制，例如飞行能力得到提升，这就是一张偏空战的地图。
// 一旦被击落就掉到水里，掉进水就会损毁。」「荒野与废墟的水也会损毁。」
//
// 用户后来定的（地图也叫石林）：
//   「石林的机制我想就是能量回复速率提升，你要维持你一直在空中，不要落下去。」→ 推进耗能不减，回能提升，推进时回能也不停。
//   「空战得有这个适配，目前仅仅加在石林。」→ 俯冲飞行：空中按住 Shift 沿视线飞，低头越冲越快，抬头把速度换成高度和滑翔。
//   「石林的推进器选配为特殊款式且固定，这种飞行器确保每个人通过比较简易的操作一直在天上飞。」→ 不让选推进器，配装界面锁定显示专用的「石林飞行器」。
//
// 两件事，都只看地图，其他地图完全不受影响：
//  1. 飞行（FFlight，数值都在 MechMapRules.cpp 开头那张表）：AMechCharacter::ApplyLoadout 把规则交给移动组件（UMechMovementComponent::GlideStep）。
//     按地图名算，服务器和客户端算出来一样，移动预测不会被拉回。
//  2. 落水损毁（UMechWater）：地图里放一个带「WaterKill」标签的 actor，它的 Z 就是水面高度。标签（都可选）：
//       KillDepth=<厘米>  脚踝往下多深才算落水（默认 60）；L_Nature 岸边有浅水，这个值给大一些
//       HalfX=<厘米> HalfY=<厘米>  只在 actor 周围这个矩形里算（不写 = 整张图）；可以放多个 actor
//     判定在 AMechGameMode::WaterTick（服务器）。团队淘汰算阵亡（不送回出生点）；死斗算阵亡后照常重生；
//     击毁算给助攻窗口（MechScore::AssistWindow）内最后打中它的敌人，没有就算场地（信息区 Field 图标）。
//
// AI（MechAIWater.cpp）用这里的地面格子（BuildGrid）判断脚下、前方能不能站，找最近的落脚点。
// HUD（UMechHudAlerts::DrawWaterWarn）贴近水面时提示，客户端也能用（不依赖格子）。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechMapRules.generated.h"

class AMechCharacter;

namespace MechMapRules
{
	/** 苏联严寒：枪械积热和机体回能同时减慢。 */
	float HeatScale(const UWorld* World);
	float EnergyRegenScale(const UWorld* World);

	/** 这张图的飞行规则（默认 = 和别的图一样）。石林飞行器的手感全在这里，数值在 MechMapRules.cpp 开头 */
	struct FFlight
	{
		/** AI 按空战打（飞起来打）；false = 地面上打、躲着水走 */
		bool bAirMap = false;
		/** 推进器固定：不让选，配装界面这一格只显示专用的这一件（MechLoadout 按 IsBoosterLocked 查）；true 时下面的飞行手感生效 */
		bool bFixedBooster = false;
		/** 专用推进器的名字和一句话说明（配装界面显示） */
		const TCHAR* BoosterName = TEXT("");
		const TCHAR* BoosterDesc = TEXT("");
		/** 喷焰颜色 */
		FLinearColor FlameColor = FLinearColor::White;
		/** 空中回能倍数（乘在 AirRegenScale 上），推进时回能也不停 */
		float AirRegen = 1.f;
		// ---- 飞行（空中按住 Shift）：沿视线飞，速度随爬升 / 俯冲变化（单位 厘米、度、秒）
		/** 平飞速度（再乘框架的速度倍数） */
		float Cruise = 2000.f;
		/** 俯冲极限 / 爬升最低速度 */
		float MaxSpeed = 6500.f;
		float MinSpeed = 900.f;
		/** 低于平飞速度时发动机把速度拉回来（厘米/秒²，差多少比例给多少） */
		float EngineAccel = 3000.f;
		/** 沿航迹的重力：俯冲加速、爬升减速（厘米/秒²） */
		float PathGravity = 1300.f;
		/** 高于平飞速度时每秒损失（超出部分 × 这个） */
		float Drag = 0.28f;
		/** 转向速度（度/秒）：平飞时 / 俯冲极速时 */
		float TurnDeg = 150.f;
		float TurnDegFast = 70.f;
		/** 机头最多抬多高 / 压多低（度） */
		float MaxClimbDeg = 55.f;
		float MaxDiveDeg = 85.f;
		/** 飞行中右键瞄准 / 格挡：平飞速度乘这个（飞行不断） */
		float AimSpeed = 0.7f;
		/** 飞行中按住喷射（空格）：机头再抬这么多度，另外耗 ThrustDrain */
		float SpaceClimbDeg = 25.f;
		/** 飞行耗能（BoostDrain 的倍数）：平飞 1；每抬 90° 再加这么多；俯冲最低到 DiveDrain */
		float ClimbDrain = 1.5f;
		float DiveDrain = 0.3f;
	};

	/** 这张图的飞行规则（没有特殊规则的图返回全 1 的那份） */
	const FFlight& FlightFor(const UWorld* World);
	/** 现在这张图（或房间里选的那张图）推进器固定：配装界面只显示专用的那一件。玩家存的推进器不会被改，换回别的图还是原来那件 */
	bool IsBoosterLocked();
	/** 专用推进器的规则（没锁时返回默认的那份） */
	const FFlight& LockedFlight();
	/** 每帧更新上面那个状态（AMechPlayerController::PlayerTick、AMechGameMode::StartPlay） */
	void UpdateBoosterLock(const UWorld* World);
}

/** 一片会损毁机体的水 */
struct FMechWaterZone
{
	float Z = 0.f;
	float KillDepth = 60.f;
	/** false = 整张图 */
	bool bBounded = false;
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D Half = FVector2D::ZeroVector;
};

UCLASS()
class UMechWater : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 这张图有没有会损毁的水 */
	bool HasWater();
	/** P 这个水平位置上有水：返回那片水（没有 = nullptr） */
	const FMechWaterZone* ZoneAt(const FVector& P);
	/** 这台机体落水了（脚底低于水面 KillDepth） */
	bool IsDrowned(const AMechCharacter* Mech);
	/** 脚下是水（Feet 往下到水面之间没有地面，且 Feet 在水面上方不太远的范围内才查）；OutClear = 离水面多高（厘米）。客户端也能用（走射线） */
	bool OverWater(const FVector& Feet, const AActor* Ignore, float& OutClear);

	// ---------------------------------------------------------------- AI 用的地面格子（服务器；AMechGameMode::ComputeArena 建）
	void BuildGrid(const FVector& Center, const FVector2D& Extent);
	bool IsGridReady() const { return Cells.Num() > 0; }
	/** 最高的地面（没有地面返回 -1e9）；格子外也算没有 */
	float GroundTop(const FVector& P) const;
	/** 这里踩下去不会下水（格子外算不安全） */
	bool IsSafeStep(const FVector& P) const;
	/** 从 P 到 P + Dir * Len 一路都能站（Dir 水平单位向量，分 Steps 段查） */
	bool IsSafeLine(const FVector& P, const FVector& Dir, float Len, int32 Steps = 3) const;
	/** 离 P 最近的能落脚的点（比 P 高不超过 MaxRise），返回脚底位置；Radius 内没有 = false */
	bool FindLanding(const FVector& P, float MaxRise, float Radius, FVector& Out) const;
	float CellSize() const { return Cell; }

protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

private:
	void ScanZones();
	bool bScanned = false;
	TArray<FMechWaterZone> Zones;

	struct FCell
	{
		/** 最高的地面（用来判断要不要飞高、能不能落）、最低的那个采样点（用来判断会不会踩水） */
		float Top = -1.e9f;
		float Low = -1.e9f;
		uint8 bSafe : 1;
		uint8 bLand : 1;
		FCell() : bSafe(0), bLand(0) {}
	};
	TArray<FCell> Cells;
	FVector2D GridMin = FVector2D::ZeroVector;
	int32 NX = 0;
	int32 NY = 0;
	float Cell = 800.f;
	const FCell* CellAt(const FVector& P) const;
};
