// 彩蛋（11-02，不接肉鸽）。用户定的内容见 docs/本轮待办_20261102.md「彩蛋」，说明见 README「彩蛋（11-02）」。
//
// 两种真正的敌人（AMechEasterBot，MechEasterBot.h）：维克托重工的无人机（旋翼代码转）、相位电子的机械狗（四条腿）。
// 在玩家眼里和机器人敌人一样：锁定框、头顶标记、导弹能锁（AMechHUD::DrawEaster，MechHudEaster.cpp；导弹追踪见 SteerMissile）。
// 子弹、刀、导弹爆炸都能打（碰撞球照保卫战杂兵：WorldDynamic + MechShootable / MechGrunt 标签，伤害按 ApplyDamage 交给它）。
// 它们只伤玩家机体，打掉不算击杀、不算积分；被它们打爆算「场地」（没人拿分）。生成时随机彩色涂装，保留品牌色点缀。
//
// 四张图的触发（AMechEasterDirector，服务器算，复制给所有人）：
//   荒野与废墟 L_Nature：塔顶压力板 → 无人机群，带各种肩部武器（机枪 / 火箭 / 榴弹 / 火炮）
//   石林 L_Pillars：山顶压力板 → 无人机群，只用机枪打人
//   苏联 L_Soviet：开局就有几只机械狗在中院玩（滑梯、跷跷板、蹲着），友方标记，打不坏、不能锁定，被打就跑开
//   老街 L_OldStreet：打破某一扇门 → 露出射击按钮 → 打中后一大群狗沿循环街狂奔，越跑越快，到「光速」消失（或被打掉）；
//     被撞受到 200 × 当前速度 ÷ 15 米/秒 的伤害（MechLoop.h 的循环，狗和别的东西一样沿 X 循环）
//   压力板每局（整场）一次，触发后全场提示。房间里「彩蛋」开关（FMechRoomSettings::bEaster，默认开）。
//
// 位置：地图里有标记就按标记（用户摆），没有就按地图数据自己找（见 README）：
//   EasterPlate（压力板中心，可选 Radius=<厘米>）、EasterDoor（要打破的门）、EasterButton（门后的按钮）、
//   EasterDogSpot（苏联的狗玩的地方，Kind=Slide|Seesaw|Sit，Len=<厘米> Height=<厘米>，滑梯 / 跷跷板的 actor 朝向 = 滑下去 / 板子的方向）。
//
// 资源（建模那边出，代码按路径软加载，缺了就不生成并写一行日志，打包不受影响）：
//   /Game/Easter/Drone/SM_VDrone_Body（插槽 Rotor_FL/FR/BL/BR、Muzzle）、/Game/Easter/Drone/SM_VDrone_Rotor（绕局部 Z 转）
//   /Game/Easter/Dog/SK_PDog（骨头 root body head tail_antenna leg_{fl,fr,bl,br}_{upper,lower}），可选 A_PDog_Idle/Walk/Run/Sit
//   /Game/Easter/Materials/M_EasterLivery（PaintA PaintB Accent 颜色、AccentGlow 亮度；槽 Shell / Frame / Accent）
//   模型都按 +X 朝前、+Z 朝上，狗的原点在脚底，无人机的原点在机身中心。
//
// 测试开关：-MechEasterOff 关掉；-MechEasterLog 每 2 秒一行状态；-MechEasterPlaceholder 缺模型时用引擎方块代替（只为测逻辑）；
//   -MechEasterTest=drones|guns|dogs|stampede|plate|door 开打 4 秒后自动触发一次。日志标签 [easter]。
// 控制台（房主 / 单机）：mech.Easter.Drones [台数] [gun|rocket|grenade|cannon|mix]、mech.Easter.Dogs [只数]、mech.Easter.Stampede [只数]、
//   mech.Easter.Plate、mech.Easter.Door、mech.Easter.Clear、mech.Easter.Info。

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MechEaster.generated.h"

class AMechCharacter;
class AMechEasterBot;
class AMechEasterProp;
class UInstancedStaticMeshComponent;
class UNiagaraSystem;
class UProjectileMovementComponent;
class USoundBase;
class UStaticMesh;
class UMaterialInterface;

UENUM()
enum class EMechEasterKind : uint8
{
	Drone,
	Dog
};

UENUM()
enum class EMechEasterRole : uint8
{
	/** 无人机：见人就打，不分队伍 */
	Hostile,
	/** 苏联的狗：打不坏、不能锁定、被打跑开 */
	Friendly,
	/** 老街的狗群：沿街狂奔，撞到谁算谁 */
	Stampede
};

UENUM()
enum class EMechEasterWeapon : uint8
{
	Gun,
	Rocket,
	Grenade,
	Cannon
};

/** 这张图有哪个彩蛋 */
enum class EMechEasterMap : uint8
{
	None,
	Nature,
	Pillars,
	Soviet,
	OldStreet
};

namespace MechEaster
{
	// ---- 资源路径（建模那边的约定，见文件开头）
	inline const TCHAR* DroneBodyPath() { return TEXT("/Game/Easter/Drone/SM_VDrone_Body"); }
	inline const TCHAR* DroneRotorPath() { return TEXT("/Game/Easter/Drone/SM_VDrone_Rotor"); }
	inline const TCHAR* DogMeshPath() { return TEXT("/Game/Easter/Dog/SK_PDog"); }
	inline const TCHAR* LiveryPath() { return TEXT("/Game/Easter/Materials/M_EasterLivery"); }

	/** 按路径安静地加载（没有就返回空，不报错）；路径可以不带「.对象名」 */
	UObject* LoadQuiet(const TCHAR* Path, UClass* Class);
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		return Cast<T>(LoadQuiet(Path, T::StaticClass()));
	}

	/** 模型到了没有（-MechEasterPlaceholder 时缺了也算有，用方块代替）；缺的话第一次问时写一行日志 */
	bool DroneReady();
	bool DogReady();
	bool PlaceholderOn();

	/** 服务器：彩蛋开着（-MechEasterOff 关；从房间开的场看房间开关；其余默认开） */
	bool Enabled(const UWorld* World);
	/** 这张图的彩蛋（按地图名，其他图 None） */
	EMechEasterMap MapOf(const UWorld* World);
	/** 正在打（对战的 Fight 阶段，测试场一直算） */
	bool FightOn(const UWorld* World);
	/** 服务器时间（各端一样，狗群按它算位置） */
	float ServerNow(const UWorld* World);

	/** 测试开关 */
	bool LogOn();
	/** -MechEasterTest= 的值（小写，没有 = 空） */
	const FString& TestWhat();

	/** 能被玩家锁定的彩蛋敌人（活着的无人机 / 狗群的狗；苏联的狗不行） */
	bool IsLockable(const AActor* A);
	/** 机体能不能当它们的目标（活着、不是木桩） */
	bool IsTargetMech(const AMechCharacter* M);

	/** 导弹（AMechMissile::Tick 里一行钩子，服务器）：没锁机体时，刚发射的导弹找玩家准星附近的彩蛋敌人追；
	 *  追上了返回 true（导弹该炸了）。RunTarget 借用导弹上给肉鸽追杂兵的那个指针 */
	bool SteerMissile(AActor* Missile, AMechCharacter* Shooter, UProjectileMovementComponent* Movement, TWeakObjectPtr<AActor>& RunTarget);

	// ---- 数值（想调只改这里）
	constexpr float DroneHP = 520.f;           // 步枪 6 枪
	constexpr float DogHP = 300.f;             // 狗群的狗：步枪 4 枪
	constexpr float DroneLife = 90.f;          // 无人机群在场多久（之后飞走）
	constexpr int32 NatureDrones = 10;
	constexpr int32 PillarsDrones = 7;
	constexpr int32 SovietDogs = 4;
	constexpr int32 StampedeDogs = 36;
	constexpr float StampedeBaseDamage = 200.f;  // 被撞：基础伤害 × 当前速度 ÷ StampedeRefSpeed
	constexpr float StampedeRefSpeed = 1500.f;   // 15 米/秒
	constexpr float StampedeV0 = 900.f;          // 起跑速度（厘米/秒）
	constexpr float StampedeTau = 3.2f;          // 速度每 Tau 秒翻 e 倍
	constexpr float StampedeLight = 30000.f;     // 到这个速度（300 米/秒）就算「光速」消失
	constexpr float PlateRadius = 260.f;         // 压力板：机体脚下离中心这么近、高度差 2 米内算踩上
	constexpr float DoorHP = 700.f;
	constexpr float LockCone = 13.f;             // 和机体锁定一样（UMechCombatComponent::LockCone / LockKeepCone）
	constexpr float LockKeepCone = 24.f;
	constexpr float LockTime = 0.8f;             // 导弹锁定要多久（同 MissileLockTime）
}

/** 一处给苏联的狗玩的地方 */
struct FMechEasterSpot
{
	/** 0 滑梯（At = 梯子顶，Dir = 滑下去的水平方向，Len = 滑道水平长度，Height = 顶的高度）、1 跷跷板（At = 中心，Dir = 板子方向，Len = 板长）、2 蹲着 */
	uint8 Kind = 2;
	FVector At = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	float Len = 400.f;
	float Height = 200.f;
	/** 跷跷板两头各一只、滑梯一次一只 */
	TWeakObjectPtr<AMechEasterBot> Users[2];
};

/** 彩蛋调度：触发、生成、子弹 / 爆炸、全场提示。服务器上按需生成一个，复制给所有人（子弹画面、提示走组播） */
UCLASS(NotPlaceable, Transient)
class AMechEasterDirector : public AActor
{
	GENERATED_BODY()

public:
	AMechEasterDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** 场上的那一个；bCreate = 服务器上没有就生成 */
	static AMechEasterDirector* Get(UWorld* World, bool bCreate);

	// ---- 服务器：生成（返回生成了几个；模型没到返回 0）
	/** Weapon < 0 = 每架随机一种肩部武器 */
	int32 SpawnDrones(const FVector& Around, int32 Count, int32 Weapon, float Life);
	int32 SpawnFriendlyDogs(const FVector& Around, int32 Count);
	/** 狗群：From 是按钮的位置（狗从它上游跑过来） */
	int32 SpawnStampede(const FVector& From, int32 Count);
	/** 这张图的压力板被踩下（By 可以为空） */
	void TriggerPlate(AActor* By);
	/** 老街的门被打破 / 按钮被打中 */
	void OnDoorBroken(AActor* By);
	void OnButtonShot(AActor* By);
	/** 测试：直接打破门、打中按钮 */
	void DebugDoor();
	/** 清掉所有敌对的无人机 / 狗群和子弹（苏联的狗留着） */
	void ClearHostiles();
	void LogInfo() const;

	// ---- 给无人机 / 狗用（服务器）
	void FireWeapon(AMechEasterBot* From, EMechEasterWeapon Weapon, const FVector& Muzzle, AActor* Target);
	/** 范围爆炸：只伤机体，按离体表的距离衰减（同保卫战） */
	void Blast(const FVector& At, float Radius, float Damage, float Impact, AActor* Source);
	/** 全场提示（各端 HUD 正上方显示几秒） */
	void Announce(const FString& Text);
	/** 离 From 最近的目标机体（MaxDist 内，没有 = 空） */
	AMechCharacter* NearestTarget(const FVector& From, float MaxDist) const;
	/** 地面高度（没打到返回 From.Z - 10000） */
	float GroundZ(const FVector& From, const AActor* Ignore) const;
	/** 苏联的狗玩的地方 */
	TArray<FMechEasterSpot>& GetSpots() { return Spots; }
	FVector GetDogHome() const { return DogHome; }
	float GetDogRoam() const { return DogRoam; }

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(const FString& Text);
	/** Style 0 曳光弹、1 火箭、2 榴弹 / 炮弹 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(uint8 Style, FVector_NetQuantize From, FVector_NetQuantize Velocity, float Gravity, AActor* Homing);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBlastFX(FVector_NetQuantize At, float Radius);

private:
	// ---- 开局找位置（服务器，第一次 Tick）
	void Setup();
	bool bSetup = false;
	EMechEasterMap MapKind = EMechEasterMap::None;
	/** 荒野：最高那栋废墟的楼顶；石林：场地里最高的一块平地（找不到返回 false） */
	bool FindNaturePlate(FVector& Out) const;
	bool FindPillarsPlate(FVector& Out) const;
	/** 往下打一条射线找地面 / 楼顶（跳过隐藏的东西：隐形墙、碰撞方块）；OnlyActor 非空时只认它 */
	bool TraceDown(const FVector& XY, float FromZ, FHitResult& Out, const AActor* OnlyActor = nullptr) const;
	/** 在 Center 周围 Half 范围里打 N×N 条射线，找最高的一块平地（四邻高度差 1.5 米内） */
	bool HighestFlat(const FVector& Center, const FVector2D& Half, int32 N, float FromZ, const AActor* OnlyActor, FVector& Out) const;
	void FindSovietSpots();
	void SetupOldStreet();
	/** 带这个标签的 actor（第一个；没有 = 空） */
	AActor* FindTagged(FName Tag) const;
	AMechEasterProp* SpawnProp(uint8 PropKind, const FVector& At, const FRotator& Rot, const FVector& Size, AActor* Linked);

	TWeakObjectPtr<AMechEasterProp> Plate;
	TWeakObjectPtr<AMechEasterProp> Door;
	TWeakObjectPtr<AMechEasterProp> Button;
	bool bPlateUsed = false;
	bool bStampedeUsed = false;
	float PendingStampede = -1.f;
	FVector StampedeFrom = FVector::ZeroVector;
	TArray<FMechEasterSpot> Spots;
	FVector DogHome = FVector::ZeroVector;
	float DogRoam = 1500.f;
	bool bDogsSpawned = false;

	// ---- 阶段：回合结束时清场
	uint8 LastPhase = 255;
	float LogClock = 0.f;
	float TestClock = -1.f;
	bool bTestDone = false;
	void TestTick(float DeltaTime);

	// ---- 子弹 / 火箭 / 炮弹（服务器一份算命中，各端按组播各模拟一份画面）
	struct FShot
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		float Gravity = 0.f;
		float Damage = 0.f;
		float Radius = 0.f;
		float Life = 4.f;
		uint8 Style = 0;
		bool bLive = false;
		TWeakObjectPtr<AActor> Homing;
		TWeakObjectPtr<AActor> Source;
	};
	TArray<FShot> Shots;
	void UpdateShots(float DeltaTime);
	void HitShot(FShot& S, const FVector& At, AActor* HitActor);

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TracerISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ShellISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> RocketISM;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> HitFX;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> BlastFX;
	UPROPERTY()
	TObjectPtr<USoundBase> BlastSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ShotSound;
	UPROPERTY()
	TObjectPtr<USoundBase> RocketSound;
};

/** 每张图一份：服务器开局生成调度；各端存 HUD 用的东西（本机的彩蛋锁定、全场提示） */
UCLASS()
class UMechEasterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	TWeakObjectPtr<AMechEasterDirector> Director;

	// ---- HUD（本机）
	/** 本机玩家锁着的彩蛋敌人（机体锁定优先：锁着机体时这个是空） */
	TWeakObjectPtr<AActor> LocalLock;
	float LocalLockProgress = 0.f;
	bool bLocalLocked = false;
	/** 全场提示 */
	FString AnnounceText;
	float AnnounceTime = -100.f;
	/** 这条提示的提示音响过了（存的是 AnnounceTime） */
	float AnnounceSounded = -100.f;
};
