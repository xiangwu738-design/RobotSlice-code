// 夺芯灰盒地图：开局在空中用方块现搭（不动现有地图），每台机器各搭一份，机关按服务器时间同步。两种布局：
// 峡谷（默认，用户定的）：260 米长、谷底宽 36 米。谷底中间一条浅河（走得慢），两侧 9 米高的崖壁栈道，北侧崖壁里有岩洞，南侧有吊笼上栈道。
//   带芯回家四条路：谷底直线 → 回收站 A、南侧栈道 → 回收站 B、北侧岩洞（中段有定时落石闸）→ 回收站 A、谷底坐吊笼 → 回收站 B。
// 装配厂（设计文档里的第一版）：三层厂房，中层大厅 / 侧廊 / 坡道下底层过闸门 / 货梯，地址参数 ?Core=Factory。
// 自然地形（地图里的 CoreArena 点带 CoreTerrain 标签，如山谷图 L_Volcano）：不搭灰盒，地形、路线、掩体全用地图自己的；
//   这里只搭铸造台、隐形边界，出生点 / 回收站 / 路点从地图里带标签的点读，小地图按地形高度现采样。
// 西队（0）在 -X 头，东队（1）在 +X 头，左右镜像。AI 走一张手摆的路点图（FindPath）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechCoreArena.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class AMechCoreArena : public AActor
{
	GENERATED_BODY()

public:
	AMechCoreArena();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0 装配厂，1 峡谷，2 自然地形（生成前设好，复制给各端；地图里有 CoreTerrain 点时 BeginPlay 自己切到 2） */
	UPROPERTY(Replicated)
	uint8 Layout = 1;

	/** 测试：两队换边（蓝队在 +X 头） */
	UPROPERTY(Replicated)
	bool bSwapSides = false;

	/** 搭在做好美术的地图上（GameMode 给带「CollisionOnly」标签）：岩体 / 地面 / 浅河这些灰盒外观隐藏只留碰撞，
	 *  机关（升降台、闸门、铸造台、掩体、桥）照常显示；岩洞另补一层没碰撞的岩石内壁。服务器按标签设，复制给客户端 */
	UPROPERTY(Replicated)
	bool bCollisionOnly = false;

	/** 队伍在哪半边（0 西、1 东） */
	int32 HalfOf(int32 Team) const { return (Team == 0) != bSwapSides ? 0 : 1; }
	FVector SpawnLocation(int32 Team) const { return GetActorLocation() + SpawnPt[HalfOf(Team)]; }
	float SpawnYaw(int32 Team) const { return HalfOf(Team) == 0 ? 0.f : 180.f; }
	/** 每队两个回收站：0 = A，1 = B */
	FVector StationLocation(int32 Team, int32 Index) const { return GetActorLocation() + StationPt[HalfOf(Team)][Index == 0 ? 0 : 1]; }
	FString StationName(int32 Index) const { return StationNames[Index == 0 ? 0 : 1]; }
	static constexpr float StationRadius = 500.f;
	/** 中央铸造台（无主核心从这里出） */
	FVector FoundryLocation() const { return GetActorLocation() + FoundryPt; }
	/** 掉到这个高度以下算掉出地图 */
	float KillZ() const { return GetActorLocation().Z + (Layers.Num() ? Layers[0] : 0.f) - 2500.f; }
	/** 场地中心。GameMode 拿它的高度判掉出地图（低于 30 米送回出生点）、AI 判爬太高（高于 12 米往中间走），这两条是按平地灰盒定的；
	 *  自然地形高差上百米，Z 改成最低地面 +15 米，保证掉出地图只在真掉出去时触发（AI 交火时会多往中间靠，跑任务时不受影响） */
	FVector Center() const { return GetActorLocation() + FVector(0.f, 0.f, CenterZOffset); }
	/** AI 活动范围（半长、半宽） */
	FVector2D Extent() const { return HalfExtent; }

	/** 定时闸门（每半边一道）：Lookahead 秒内会关也算关（AI 别往里走） */
	bool IsGateClosed(int32 Half, float Lookahead = 0.f) const;
	/** 升降平台：0 在高处，1 在低处 */
	float LiftDepth(int32 Half) const;
	FVector LiftCenter(int32 Half, bool bLow) const;

	/** 路点图寻路（只走地面：坡道、升降台；闸门关着时绕开）。Out 是要依次走到的点，最后一个是 To 本身；OutKinds 是走到这一点的路段类型 */
	bool FindPath(const FVector& From, const FVector& To, TArray<FVector>& Out, TArray<uint8>* OutKinds = nullptr) const;
	enum EEdge : uint8 { Walk, Lift, Gate };

	/** 小地图用的俯视形状（相对原点，厘米）：0 地面，1 高处（栈道、桥、坡道），2 低处 / 洞（岩洞、底层），3 实心（岩体、墙），4 水 */
	struct FMapShape
	{
		FBox2D Box;
		uint8 Kind;
	};
	const TArray<FMapShape>& GetMapShapes() const { return MapShapes; }
	FVector2D MapMin() const { return MapLo; }
	FVector2D MapMax() const { return MapHi; }
	/** 这一点在第几层（0 最低） */
	int32 LayerOf(const FVector& P) const;

private:
	void BuildFactory();
	void BuildCanyon();
	/** 自然地形：从地图标签点读布局、搭铸造台和边界、采样小地图、建路点图；地图里没有 CoreTerrain 点返回 false */
	bool BuildTerrain();
	void BuildMovers(UMaterialInterface* LiftMat, UMaterialInterface* GateMat, UMaterialInterface* Glow);
	/** bCollisionOnly 时：隐藏岩体 / 地面 / 浅河外观（碰撞保留），掩体换岩石材质 */
	void ApplyCollisionOnly();
	/** 只留碰撞模式下用的岩石材质（地图包里的世界投影岩石，找不到就用灰盒材质） */
	UMaterialInterface* ArtRockMaterial(UMaterialInterface* Fallback) const;
	void GraphFactory();
	void GraphCanyon();
	/** 中心 + 尺寸（厘米，相对原点）的方块 */
	void AddBox(UInstancedStaticMeshComponent* Into, const FVector& Center, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator);
	/** 两个角点围成的方块 */
	void AddSpan(UInstancedStaticMeshComponent* Into, const FVector& A, const FVector& B);
	/** 按 X/Y 区间加一块楼板（上表面 Z），挖掉 Holes 里的洞 */
	void AddSlab(UInstancedStaticMeshComponent* Into, float ZTop, float Thick, const FBox2D& Area, const TArray<FBox2D>& Holes);
	/** 两端点之间的斜坡（上表面过这两点），Width 是横向宽度 */
	void AddRamp(UInstancedStaticMeshComponent* Into, const FVector& High, const FVector& Low, float Width);
	UInstancedStaticMeshComponent* NewISM(const TCHAR* Name, UMaterialInterface* Material, bool bCollide = true);
	void AddLight(const FVector& At, float Intensity, float Radius, const FLinearColor& Color);
	float NetTime() const;

	int32 AddNode(const FVector& P);
	void AddEdge(int32 A, int32 B, uint8 Kind = Walk, int32 Half = -1);
	int32 NearestNode(const FVector& P) const;

	// 布局（相对原点），Build 时填
	FVector SpawnPt[2];
	FVector StationPt[2][2];
	FVector FoundryPt = FVector::ZeroVector;
	FString StationNames[2];
	TArray<float> Layers;
	float CenterZOffset = 0.f;
	FVector2D HalfExtent = FVector2D(10000.f, 3500.f);
	/** 升降平台：中心 XY、边长、高处 / 低处的上表面 Z */
	FVector LiftXY[2];
	float LiftSize = 1000.f;
	float LiftHighZ = 0.f;
	float LiftLowZ = -700.f;
	/** 闸门：中心 XY、尺寸，开着 / 关着时的中心 Z；警示灯位置 */
	FVector GateXY[2];
	FVector GateSize = FVector(100.f, 1200.f, 600.f);
	float GateOpenZ = -1100.f;
	float GateShutZ = -400.f;
	FVector LampAt[2];
	FVector LampSize = FVector(60.f, 1200.f, 25.f);

	struct FNode
	{
		FVector P;
	};
	struct FEdge
	{
		int32 A;
		int32 B;
		uint8 Kind;
		int32 Half;
	};
	TArray<FNode> Nodes;
	TArray<FEdge> Edges;
	TArray<FMapShape> MapShapes;
	FVector2D MapLo = FVector2D(-10000.f, -3500.f);
	FVector2D MapHi = FVector2D(10000.f, 3500.f);
	void AddMapRect(float X0, float Y0, float X1, float Y1, uint8 Kind);

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Lifts[2];

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Gates[2];

	UPROPERTY()
	TObjectPtr<class UMaterialInstanceDynamic> GateLampMat[2];

	bool bBuilt = false;
};
