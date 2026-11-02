// 高处图（10-31，城区街口 / 相位电子园区这种带 MechArena 场地标记的新图）：给 AI 认路和抢高点用。
// 开局把场地按 3 米一格从上往下打射线，记下每一格所有能站的面（地面、桥面、高架、屋顶……一格可以有好几层，机体胶囊放得下才算），
// 再算相邻格之间能怎么过去：走（高差 60 厘米内）、跳（矮护栏 / 1.4 米内的台阶）、往下跳（从边上走下去）、喷射上去（贴着墙往上喷）。
// 从出生点灌一遍「去得了也回得来」的格子（楼里面、封死的顶、坑里都不算），高出街面 5 米以上、连成片的面就是「高处」。
// AI（AMechAIController）拿它：看不见敌人时按格子找路绕过去（每台 AI 的路线偏好不一样，不会全挤在一条路上）、
// 交战时挑一个看得见目标的高处喷射上去打一会儿，待够了 / 看不见了 / 对方钻到脚下就从边上走下来。
// 没有 MechArena 标记的图（街道、自然、小巷、测试场、夺芯山谷）不建，AI 照旧（MechAIController 里是原来那套）。
// AI 只在服务器上跑，所以只在服务器建；分帧建（每帧几毫秒，开局倒计时里就建完），建好之前 AI 照旧。

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechHighGround.generated.h"

UCLASS()
class UMechHighGround : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** 边之间怎么过去 */
	enum EKind : uint8 { Walk, Hop, Drop, Climb };

	/** 这台机体能怎么走：地面型推进器不能喷射上升，只能走和跳 */
	struct FAbility
	{
		/** 喷射最多能升多高（厘米）；0 = 不能喷射 */
		float MaxClimb = 0.f;
	};

	/** AI 每帧调：第一次调时找场地标记、开始分帧建；返回 true = 建好了能用（没标记的图永远 false） */
	bool Update();
	bool IsReady() const { return Phase == EPhase::Ready; }
	/** 这张图不建（没有 MechArena 标记） */
	bool IsDisabled() const { return Phase == EPhase::Disabled; }

	/** 街面高度（世界 Z，厘米）：有效格子里最多的那个高度 */
	float FloorZ() const { return Floor; }
	/** 高出街面多少算高处 */
	static constexpr float HighMin = 500.f;
	/** 最高的高处比街面高多少（厘米） */
	float MaxHigh() const { return MaxHighZ - Floor; }

	/** 脚下的节点：所在格子里，脚下 MaxBelow 以内、脚上 MaxAbove 以内最近的一层；没有 = INDEX_NONE */
	int32 FindNode(const FVector& Feet, float MaxBelow = 350.f, float MaxAbove = 120.f) const;
	/** P 附近（水平 Radius 内）最近的有效节点；bFloorOnly = 只要街面那层 */
	int32 NearestNode(const FVector& P, float Radius = 900.f, bool bFloorOnly = false) const;
	FVector NodeFeet(int32 N) const { return Nodes[N].Feet; }
	float NodeHeight(int32 N) const { return Nodes[N].Feet.Z - Floor; }
	/** 高处片区编号（-1 = 不是高处或片太小） */
	int32 NodeZone(int32 N) const { return Nodes[N].Zone; }
	bool IsValidNode(int32 N) const { return Nodes.IsValidIndex(N) && Nodes[N].bValid; }

	/** 能站的高处节点（片区够大的），抢高点时从里面抽 */
	const TArray<int32>& PerchNodes() const { return Perches; }
	int32 NumZones() const { return ZoneSize.Num(); }
	int32 ZoneNodeCount(int32 Zone) const { return ZoneSize.IsValidIndex(Zone) ? ZoneSize[Zone] : 0; }

	/** 从 N 往 Dir 方向走一格还在不在同一片高处（站在高处横移前先看一眼，别走下去） */
	bool StepStaysInZone(int32 N, const FVector& Dir) const;

	/** A*：Seed ≠ 0 时每台 AI 的格子代价带自己的平滑噪声（路线偏好），Bad 是这台走不通过的边（起点 << 32 | 终点）。
	 *  OutPts = 一路上的节点脚下位置（不含起点），OutKinds = 走到这个点用的方式（EKind） */
	bool FindPath(int32 From, int32 To, const FAbility& Ability, float Seed, const TSet<uint64>* Bad, TArray<FVector>& OutPts, TArray<uint8>& OutKinds,
		TArray<int32>* OutNodes = nullptr, float* OutCost = nullptr) const;

	/** 两个节点之间这条边的方式（没有这条边 = 255） */
	uint8 EdgeKind(int32 From, int32 To) const;

	/** 打一行统计（节点、高处、片区、各种边数、建图耗时） */
	FString Describe() const;

private:
	enum class EPhase : uint8 { NotStarted, Columns, Edges, Flood, Ready, Disabled };
	EPhase Phase = EPhase::NotStarted;

	struct FNode
	{
		FVector Feet;
		int32 Col = 0;
		int32 FirstEdge = 0;
		int16 Zone = -1;
		uint8 NumEdges = 0;
		bool bValid = false;
	};
	struct FEdge
	{
		int32 To = 0;
		float Cost = 0.f;
		float Rise = 0.f;
		uint8 Kind = Walk;
	};

	void Begin();
	void BuildColumn(int32 C);
	void BuildEdges(int32 N);
	void Finish();
	bool LineClear(const FVector& A, const FVector& B) const;
	int32 ColOf(const FVector& P) const;
	FVector ColCenter(int32 C) const;

	TArray<FNode> Nodes;
	TArray<FEdge> Edges;
	/** 每格的节点：ColFirst[c] 起 ColNum[c] 个，按高度从高到低 */
	TArray<int32> ColFirst;
	TArray<uint8> ColNum;
	TArray<int32> ZoneSize;
	TArray<int32> Perches;

	FVector Center = FVector::ZeroVector;
	FVector2D Half = FVector2D::ZeroVector;
	FVector Origin = FVector::ZeroVector;
	int32 NX = 0;
	int32 NY = 0;
	float Cell = 300.f;
	float TopZ = 0.f;
	float BottomZ = 0.f;
	float Floor = 0.f;
	float MaxHighZ = 0.f;
	int32 Cursor = 0;
	uint64 LastFrame = 0;
	double BuildSeconds = 0.0;
	int32 BuildFrames = 0;
	int32 EdgeCount[4] = {0, 0, 0, 0};
	TArray<FVector> Seeds;
};
