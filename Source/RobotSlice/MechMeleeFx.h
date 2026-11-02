// 近战特效（10-31 用户：「近战武器的特效能否再丰富一点」）：只管画面，各端按已经同步下来的状态（出招、命中、格挡的组播，
// 连斩 / 割裂层数）自己算，不走网络。由 UMechCombatComponent 在 BeginPlay 里建（专用服务器不建；-MechMeleeFxOld 用以前的粒子刀光）。
//  刀光：每帧记下刀根、刀尖两点连成扫掠面（M_BladeArc），每把刀颜色、宽窄、拖多长不一样，重斩更宽更亮
//  刀刃热光：武器母材质 M_WeaponPBR 的 HeatPrm / HeatColor——出招前段慢慢烧热、砍下时最亮（重斩烧得更明显，也是给对面看的起手提示）
//  刀痕：砍中处在对方装甲上留一道白热 → 橙 → 暗红慢慢冷却的刀口（M_SlashScar，屏幕空间贴花，挂在被砍中的骨头上）
//  冲击环 / 闪光：重击、钝器、破防的冲击环，格挡 / 弹反的金属反光（M_ShockRing）
//  各把刀的机制提示：双手刀弹反后刀刃金光（下一刀加成）、快刀连斩层数越多刀越热越橙、单手刀闪避斩长刀光、
//  大剑霸体重斩全身发橙、双刀割裂的刀口一直烧着（层数 = 刀口数）、钝器砸中冲击环 + 尘土、强力匕首橙色尾迹
// 11-01 刀光第二版（用户：「目前的刀光总体是过于朴素」）：刀光换成 M_BladeTrail——刚扫过那一截白热、主体是刀的颜色、快散时变深，
//  一条条拉丝往刀走的方向流，刀尖外侧一圈软辉光，尾巴碎成块（碎口带亮边）；刀尖沿路洒火花 / 光点（M_BladeSpark），
//  重斩、闪避斩留几个刀身残影（M_BladeGhost）。每把刀的样子在 MechMeleeFx.cpp 的 Arcs[]。-MechTrailOld 用回第一版刀光（对比用）
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechTypes.h"
#include "MechMeleeFx.generated.h"

class AMechCharacter;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class UMechMeleeFx : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechMeleeFx();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- 战斗组件在各端的组播里调
	/** 出招：bDash 单手刀闪避斩，bParryBonus 双手刀弹反后的加成一刀 */
	void OnSwingStart(EMechMelee Move, bool bDash, bool bParryBonus);
	/** 砍中（没被挡住）：对方身上留刀痕；重击、钝器加冲击环；Heavy01 = 这一下冲击的轻重 0–1 */
	void OnBladeHit(AMechCharacter* Victim, const FVector& At, float Heavy01, bool bHeavy);
	/** 刀被挡（在挡的一方调）：Kind 0 挡住、1 弹反、2 破防；Spark 火花的位置 */
	void OnBlocked(const FVector& Spark, int32 Kind, AMechCharacter* By);
	/** 拼刀（11-01，只在一台上调一次）：At 两刀相交处、Axis 两台连线方向：大反光 + 冲击环 + 一大蓬朝两侧崩的火星 */
	void OnClash(const FVector& At, const FVector& Axis);
	/** 刀身这一帧的位置（刀根、刀尖，右手那把；没在记录刀光时返回 false） */
	bool GetBladeNow(FVector& OutBase, FVector& OutTip) const;
	/** 飞刀扎中 Victim */
	void OnKnifeHit(AMechCharacter* Victim, const FVector& At, const FVector& Dir, bool bPower);
	/** 这一刀刀尖正往哪个方向走（命中火花顺着刀走的方向飞）；没在挥刀时返回零向量 */
	FVector GetSwingDir() const { return SwingDir; }

	/** 在自己身上留一道刀痕：Near = 刀身上离身体最近的那一段（两点）、Cut = 刀走的方向、From = 砍的人；Size 刀痕半长（厘米）；
	 *  bSpot = 圆斑（匕首扎中、钝器）；bRend = 双刀割裂（层数还在就一直烧着） */
	void AddScar(const FVector& NearA, const FVector& NearB, const FVector& Cut, const FVector& From, float Size, bool bSpot, bool bRend);

private:
	AMechCharacter* GetMech() const;

	// ---------------------------------------------------------------- 刀光
	struct FTrailSample
	{
		FVector Base;
		FVector Tip;
		float Time = 0.f;
		float Strength = 0.f;
		float Core = 0.f;
		/** 这一刀刀尖走到这儿走了多远（米）：第二版刀光的纹路、碎块贴在这条路上 */
		float Path = 0.f;
		/** 这一刀的随机数（存在点上：新的一刀开始时，上一刀还没散的尾巴纹路不跳） */
		uint8 Seed = 0;
	};
	struct FTrail
	{
		TArray<FTrailSample> Samples;
		TObjectPtr<UProceduralMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		FVector LastTip = FVector::ZeroVector;
		bool bHasLast = false;
		/** 这一刀刀尖走了多远（米） */
		float Path = 0.f;
		/** 上次给刀刃热光设的值（没变就不再设） */
		FVector4f LastHeat = FVector4f(-1.f, 0.f, 0.f, 0.f);
		FLinearColor LastHeatColor = FLinearColor::Transparent;
	};
	FTrail Trails[2];
	/** 这一刀的样子（出招时按刀、招式、标志定好） */
	float History = 0.15f;
	float SwingStrength = 1.f;
	EMechMelee SwingMove = EMechMelee::None;
	bool bSwingDash = false;
	bool bSwingParry = false;
	float SwingClock = -100.f;
	FVector SwingDir = FVector::ZeroVector;
	/** 这一刀的随机数（纹路、碎块每刀不一样） */
	uint8 SwingSeed = 0;
	/** -MechTrailOld：用第一版刀光（10-31：一层光面，没有火花、残影） */
	bool bTrailOld = false;
	/** 第二版刀光这一帧的：刀尖外侧辉光宽（厘米）、火花倍数、火花颜色（0–1）、残影颜色（每帧在 TickComponent 里按刀、招式、连斩、弹反算） */
	float TrailGlow = 15.f;
	float TrailSparkMul = 1.f;
	FLinearColor TrailSparkColor = FLinearColor::White;
	FLinearColor TrailGhostColor = FLinearColor::White;
	void UpdateTrail(int32 Index, USceneComponent* Blade, bool bOn, float DeltaTime);
	void BuildTrail(FTrail& T);
	/** 刀模型局部 X：刀身起点（护手）、刀尖 */
	void BladeRange(const USceneComponent* Blade, float& OutStart, float& OutTip) const;
	void UpdateHeat(FTrail& T, USceneComponent* Blade, float Heat, const FLinearColor& Color);

	// ---------------------------------------------------------------- 火花 / 光点（11-01，刀尖沿路洒的；本机 CPU 算，一个程序网格画）
	struct FSpark
	{
		FVector P = FVector::ZeroVector;
		FVector V = FVector::ZeroVector;
		float Born = 0.f;
		float Life = 0.3f;
		float Size = 1.f;
		/** 按速度拉长多少（秒） */
		float Stretch = 0.02f;
		float Gravity = 0.f;
		float Drag = 0.f;
		/** 闪烁（0 不闪）、相位 */
		float Twinkle = 0.f;
		float Phase = 0.f;
		FColor Color = FColor::White;
	};
	TArray<FSpark> Sparks;
	/** 上一帧没撒完的零头 */
	float SparkDebt = 0.f;
	TObjectPtr<UProceduralMeshComponent> SparkMesh;
	TObjectPtr<UMaterialInstanceDynamic> SparkMID;
	/** 刀尖从 (FromBase, FromTip) 扫到 (ToBase, ToTip)：按走过的路撒火花 */
	void EmitSparks(const FVector& FromBase, const FVector& FromTip, const FVector& ToBase, const FVector& ToTip, const FVector& TipVel, float Count);
	void UpdateSparks(float DeltaTime);

	// ---------------------------------------------------------------- 残影（11-01，重斩、闪避斩：刀模型复制几份留在刚挥过的位置）
	struct FGhost
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float Born = -100.f;
		float Life = 0.12f;
		float Strength = 1.f;
	};
	TArray<FGhost> Ghosts;
	/** 离下一个残影还有多久 */
	float GhostClock = 0.f;
	void AddGhost(USceneComponent* Blade, const FLinearColor& Color, float Strength, float Life);
	void UpdateGhosts();

	// ---------------------------------------------------------------- 刀痕
	struct FScar
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float Born = 0.f;
		float Tau = 0.7f;
		float Life = 2.5f;
		float HalfLen = 30.f;
		float Width = 1.2f;
		float Front = -10.f;
		float Outer = 10.f;
		float Scale = 1.f;
		bool bSpot = false;
		bool bRend = false;
		float Ember = 0.f;
	};
	TArray<FScar> Scars;
	void UpdateScars(float DeltaTime);
	void ClearScars();

	// ---------------------------------------------------------------- 冲击环 / 闪光（世界坐标，不挂在机体上）
	struct FRing
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float Born = 0.f;
		float Life = 0.3f;
		float Radius = 100.f;
		float Width = 0.08f;
		float Strength = 1.f;
		bool bGlint = false;
		FLinearColor Color = FLinearColor::White;
		FVector At = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
	};
	TArray<FRing> Rings;
	/** Normal 零向量 = 正对镜头（闪光） */
	void AddRing(const FVector& At, const FVector& Normal, float Radius, float Width, float Strength, const FLinearColor& Color, float Life, bool bGlint);
	void UpdateRings();

	// ---------------------------------------------------------------- 机制提示
	/** 双手刀弹反成功：下一刀加成还有效的时刻（刀刃金光，各端在组播里记） */
	float ParryReadyUntil = -100.f;
	/** 被弹反：僵直到这个时刻（身上冒电火花） */
	float StunFxUntil = -100.f;
	float StunSparkClock = 0.f;
	/** 割裂层数（上一帧），层数没了刀口才开始冷却 */
	int32 LastRend = 0;

	/** 本组件自己的时钟：跟着机体的顿帧一起停（组件的 DeltaTime 已经乘了机体的 CustomTimeDilation），刀光在砍中那一下定格 */
	float FxClock = 0.f;
	/** -MechMeleeFxLog：每 5 秒记一次本组件每帧平均花多少毫秒（看刀光第二版的开销） */
	double FxTickSeconds = 0.0;
	int32 FxTickFrames = 0;
	float FxLogClock = 0.f;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ArcMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TrailMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SparkMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GhostMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ScarMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMaterial;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkFX;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DustFX;
};
