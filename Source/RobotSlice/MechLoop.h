// 一维循环地图（11-02 老旧街道 L_OldStreet）。用户：「地图本身看上去是无限老旧街道相接，但实际上只有一个」，机体和子弹沿街循环。
//
// 地图那边（tools/ue_build_oldstreet.py）：沿 UE X 循环，周期 100 米，接缝 X = ±50 米；全精度的街（带碰撞）摆了 k = -1、0、+1 三份（-150～+150 米），
// 更远是低模和远景。地图里一个带「OldStreetBounds」标签的点存数字：PeriodX=<厘米> SeamX=<厘米>（点的 X 是循环的中心）。没有这个点的图一切照旧。
//
// 做法（「周期世界」：每台机体在 ±周期 处都有一份「镜像」）：
//  1. 机体走出接缝 3 米就瞬移一个周期回来（UMechMovementComponent::OnMovementUpdated，服务器和本机预测同一套，结果一样）。
//     放钩爪 / 紧急脱离这类按世界坐标走的根运动时先不挪，放完再挪（全精度的街两边各多一个周期，够用）。
//     本机镜头：瞬移那一帧关一次镜头延迟、通知渲染这是镜头切换（时域历史不拿 100 米外的画面混）。
//     别人的机体（插值回放）：收到的位置按上一个位置展开成连续的，整段缓冲都过了接缝再一起挪回来（不卡顿）。
//  2. 镜像碰撞：每台机体在 ±周期 处各有一个和本体一样的胶囊（组件属于机体自己，打中它 = 打中这台），
//     所以子弹、刀、爆炸、导弹、机体之间的推挤跨接缝都对，不用每处改射线。
//  3. 镜像画面（AMechLoopGhosts，只在有画面的那端）：每台机体的骨骼网格（跟本体同一套骨骼姿势）和身上的静态网格（武器、喷焰……）
//     在 ±1、±2 个周期处各画一份，离镜头 260 米以外的不画。
//  4. 子弹、导弹、手雷、地雷这类东西本身不复制镜像：飞出接缝半个周期（离中心 100 米，全精度的街还没到头）再挪回来；子弹按离镜头最近的那份画。
//  5. 「谁离我多远、在哪个方向」：锁定、AI、导弹追踪、HUD 锁定框 / 小地图都用 MechLoop::Near（目标离我最近的那一份）。
//
// 测试：-MechLoopLog 每次瞬移打一行 [loop]。-MechLoopNoCut 瞬移时不通知镜头切换（对比画面用）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechLoop.generated.h"

class UPrimitiveComponent;

namespace MechLoop
{
	/** 这张图沿 X 循环（下面的函数在不循环的图上原样返回，可以放心到处调） */
	bool IsActive(const UWorld* World);
	/** 周期、接缝离中心多远（厘米）；不循环 = 0 */
	float Period(const UWorld* World);
	float Seam(const UWorld* World);
	/** Target 离 Ref 最近的那一份（只改 X） */
	FVector Near(const UWorld* World, const FVector& Target, const FVector& Ref);
	/** From → To 的最短位移 */
	FVector Delta(const UWorld* World, const FVector& From, const FVector& To);
	float Dist(const UWorld* World, const FVector& A, const FVector& B);
	float DistSquared(const UWorld* World, const FVector& A, const FVector& B);
	/** X 离中心超过「接缝 + Margin」时要挪多少才回来（整周期的倍数，不用挪 = 0） */
	float WrapShift(const UWorld* World, float X, float Margin);
	/** 本机镜头位置（没有本机玩家 = 原点）：东西按离它最近的那份画 */
	FVector ViewLocation(const UWorld* World);
	/** 测试开关：-MechLoopLog / -MechLoopNoCut */
	bool LogOn();
	bool CutOn();
	/** 子弹、导弹这类东西飞出接缝多远才挪回来（周期的一半：全精度的街两边各多一个周期，挪之前碰撞都在） */
	inline float ProjectileMargin(const UWorld* World) { return Period(World) * 0.5f; }
	/** 机体走出接缝多远才挪（防止在接缝上来回挪） */
	constexpr float MechMargin = 300.f;
	/** 镜像画面画几圈（±1..±GhostRings 个周期），离镜头多远以外不画 */
	constexpr int32 GhostRings = 2;
	constexpr float GhostMaxDist = 26000.f;
}

/** 读地图里的 OldStreetBounds 点、开局放镜像画面 */
UCLASS()
class UMechLoopSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool IsLoop() { Scan(); return bLoop; }
	float GetPeriod() { Scan(); return Period; }
	float GetSeam() { Scan(); return SeamX; }
	float GetCenter() { Scan(); return CenterX; }

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void Scan();
	bool bScanned = false;
	bool bLoop = false;
	float Period = 0.f;
	float SeamX = 0.f;
	float CenterX = 0.f;
};

/** 镜像画面：每台机体的网格在 ±周期 处各画一份（只在有画面的那端，不复制，不碰撞） */
UCLASS(NotPlaceable, Transient)
class AMechLoopGhosts : public AActor
{
	GENERATED_BODY()

public:
	AMechLoopGhosts();
	virtual void Tick(float DeltaTime) override;

private:
	struct FPart
	{
		TWeakObjectPtr<UPrimitiveComponent> Source;
		/** 每个偏移一份（顺序同 Offsets） */
		TArray<UPrimitiveComponent*> Copies;
	};
	struct FMechSet
	{
		TWeakObjectPtr<AActor> Mech;
		TArray<FPart> Parts;
		float RescanClock = 0.f;
	};
	TArray<FMechSet> Sets;
	TArray<float> Offsets;
	float FindClock = 0.f;

	/** 所有镜像组件（防垃圾回收） */
	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> AllCopies;

	void RescanParts(FMechSet& Set);
	UPrimitiveComponent* MakeCopy(UPrimitiveComponent* Source);
	void DestroyPart(FPart& Part);
	static bool IsMirrored(const UPrimitiveComponent* C);
};
