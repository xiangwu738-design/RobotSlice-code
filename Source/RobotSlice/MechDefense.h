// 保卫战（10-31，设计见 docs/保卫战_设计.md）：波次调度 AMechDefenseDirector + 要守的指挥所 AMechDefenseCore。
// 进图：测试场模式加 ?Defense，比如 "/Game/Maps/L_Defense?Range?Defense"（玩家都是蓝队 0，敌人红队 1；测试场的配装页照样能用）。
// 第一版（这一步只做波次和杂兵）：10 波，每波前准备 20 秒（第一波 12 秒）；杂兵见 MechGrunt.h，精英 = 现有 AI 机甲，
// 第 5、10 波的首领先用「加强的精英机甲」占位（首领战以后单独设计）。指挥所被打爆就输，守满 10 波赢。
// 玩家阵亡 10 秒后在指挥所旁重生，每波结束全员复活、回 40% 装甲。等级 / 词条 / 物资 / 商店、倒地救人、大门和设施是后面的步骤。
// 指挥所是运行时生成的占位方块（不存进地图；用户 10-31：测试图不摆堡垒，巨构防御工事是以后的方向）。
// 子弹 / 炮弹：服务器一份（算命中），各端按组播各自模拟一份画面；红圈预警也走组播。
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechGrunt.h"
#include "MechDefense.generated.h"

class AMechCharacter;
class AMechGrunt;
class APlayerController;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraSystem;
class UStaticMeshComponent;
class USoundBase;

UENUM()
enum class EMechDefensePhase : uint8
{
	Prep,   // 波次之间的准备
	Fight,  // 一波正在打
	Won,
	Lost
};

/** 要守的指挥所（占位：一块混凝土方块 + 天线 + 顶上的信标灯） */
UCLASS()
class AMechDefenseCore : public AActor
{
	GENERATED_BODY()

public:
	AMechDefenseCore();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaTime) override;
	/** 服务器：敌人打它 */
	void ApplyEnemyDamage(float Damage);
	float GetHPFraction() const { return MaxHP > 0.f ? HP / MaxHP : 0.f; }
	bool IsDestroyed() const { return HP <= 0.f; }
	/** 方块的半宽（厘米）：爆炸离它这么近就算打中 */
	float GetRadius() const { return 700.f; }
	/** 最近一次被打的时间（HUD 闪红） */
	float LastHitTime = -100.f;

	UPROPERTY(Replicated)
	float HP = 25000.f;
	UPROPERTY(Replicated)
	float MaxHP = 25000.f;

private:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHit();
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Block;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mast;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Beacon;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BeaconMID;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BeaconGlow;
};

UCLASS()
class AMechDefenseDirector : public AActor
{
	GENERATED_BODY()

public:
	AMechDefenseDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- HUD 读的状态（复制）
	UPROPERTY(Replicated)
	int32 Wave = 0;
	UPROPERTY(Replicated)
	int32 NumWaves = 10;
	UPROPERTY(Replicated)
	EMechDefensePhase Phase = EMechDefensePhase::Prep;
	/** 这一阶段结束的服务器时间（准备倒计时） */
	UPROPERTY(Replicated)
	float PhaseEndTime = 0.f;
	/** 这一波还剩多少敌人（没刷出来的 + 活着的） */
	UPROPERTY(Replicated)
	int32 EnemiesLeft = 0;
	UPROPERTY(Replicated)
	TObjectPtr<AMechDefenseCore> Core;

	// ---- 杂兵用（服务器）
	/** 活着的玩家机体（蓝队） */
	void GetPlayers(TArray<AMechCharacter*>& Out) const;
	/** 打一发子弹 / 炮弹：Radius > 0 是会爆炸的炮弹（Gravity 厘米/秒²）；Style 0 曳光弹、1 炮弹 */
	void FireShot(const FVector& From, const FVector& Velocity, float Gravity, float Damage, float Radius, float CoreDamage, AActor* Source, uint8 Style);
	/** 地上的红圈预警（Duration 秒后到满） */
	void Warn(const FVector& At, float Radius, float Duration);
	/** 范围爆炸：玩家按离体表的距离衰减、指挥所在范围内吃 CoreDamage */
	void Blast(const FVector& At, float Radius, float Damage, float Impact, float CoreDamage, AActor* Source, bool bBig);
	/** 杂兵死了（Killer 为空 = 自爆或测试打死） */
	void OnGruntDied(AMechGrunt* Grunt, AActor* Killer);

	// ---- 肉鸽成长（11-01，MechRun.h）：画面组播放在调度器上：对战里没有调度器，战斗组件不用加 RPC
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRunFx(uint8 Kind, FVector_NetQuantize At, FVector_NetQuantize Vec, float A, float B, float C);
	/** 追踪弹：服务器一份算命中（UMechRunComponent），客户端各画一份 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRunBullet(AMechCharacter* Mech, FVector_NetQuantize From, FVector_NetQuantizeNormal Dir, AActor* Target);
	/** 爆炸画面（不伤人） */
	void RunBlastFX(const FVector& At, float Radius, bool bBig) { MulticastBlastFX(At, Radius, bBig); }

	// ---- 测试（-MechDefenseTest）
	/** 测试（-MechRunTest）：刷一个杂兵（不算在波次里）；bFrozen = 一直瘫痪，不动不打人 */
	AMechGrunt* DebugSpawnGrunt(EMechGrunt Kind, const FVector& At, float HPScale, bool bFrozen);
	/** 测试：马上重生这个玩家（走和阵亡重生同一条路） */
	void DebugRespawn(APlayerController* PC) { RespawnPlayer(PC); }
	/** 测试：一直停在准备阶段，不开下一波 */
	void DebugHoldPrep() { if (Phase == EMechDefensePhase::Prep) { PhaseEndTime = Now() + 100000.f; } }
	/** 把场上的敌人全打死 */
	void DebugKillAll();
	/** 准备阶段直接跳过 */
	void DebugSkipPrep() { if (Phase == EMechDefensePhase::Prep) { PhaseEndTime = Now(); } }
	int32 NumAliveGrunts() const;
	const TArray<TWeakObjectPtr<AMechGrunt>>& GetGrunts() const { return Grunts; }
	/** 这一局每种敌人对玩家 / 指挥所打出的伤害（测试日志） */
	TMap<FString, float> DebugDamage;
	int32 DebugKills = 0;
	int32 DebugKillsByPlayers = 0;

private:
	float Now() const;
	void SetupServer();
	bool bSetup = false;
	void StartWave();
	void EndWave();
	void SpawnTick(float DeltaTime);
	void SpawnGrunt(EMechGrunt Kind, const FVector& At);
	void SpawnElite(const FVector& At, float HPScale);
	void PlayerTick(float DeltaTime);
	void ReviveAll(float HealFraction);
	FVector PickSpawn(bool bAir, float Radius) const;
	void BuildSpawnPoints();
	TArray<FVector> SpawnRing;      // 离指挥所约 170 米一圈能刷地面单位的点（地面高度）
	TArray<FVector> MortarRing;     // 约 220 米一圈
	FVector CoreAt = FVector::ZeroVector;

	struct FWaveDef
	{
		int32 Kamikaze;
		int32 Gunner;
		int32 Tank;
		int32 Mortar;
		int32 Elite;
		float EliteHP;  // 精英血量倍数（首领波的占位）
	};
	// 这一波还没刷出来的
	int32 Queue[4] = {0, 0, 0, 0};
	int32 EliteQueue = 0;
	float EliteHPScale = 1.f;
	float GruntHPScale = 1.f;
	float DronePacketClock = 0.f;
	float TankClock = 0.f;
	float WaveTime = 0.f;
	TArray<TWeakObjectPtr<AMechGrunt>> Grunts;
	TArray<TWeakObjectPtr<AMechCharacter>> Elites;
	int32 PlayerCount() const;

	// 玩家：阵亡后重生
	struct FRespawn
	{
		TWeakObjectPtr<APlayerController> PC;
		float At = 0.f;
	};
	TArray<FRespawn> Respawns;
	TSet<TWeakObjectPtr<AMechCharacter>> BoundPlayers;
	void RespawnPlayer(APlayerController* PC);

	// ---- 子弹 / 炮弹（服务器算命中；各端各模拟一份画面）
	struct FShot
	{
		FVector Pos;
		FVector Vel;
		float Gravity = 0.f;
		float Damage = 0.f;
		float Radius = 0.f;
		float CoreDamage = 0.f;
		float Life = 4.f;
		uint8 Style = 0;
		bool bLive = false;  // 服务器那份（算伤害）
		TWeakObjectPtr<AActor> Instigator;
	};
	TArray<FShot> Shots;
	void UpdateShots(float DeltaTime);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShot(FVector_NetQuantize From, FVector_NetQuantize Velocity, float Gravity, uint8 Style);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWarn(FVector_NetQuantize At, float Radius, float Duration);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBlastFX(FVector_NetQuantize At, float Radius, bool bBig);

	// ---- 红圈（各端）
	struct FWarnRing
	{
		TObjectPtr<UStaticMeshComponent> Comp;
		TObjectPtr<UMaterialInstanceDynamic> MID;
		float Born = 0.f;
		float Duration = 1.f;
	};
	TArray<FWarnRing> Rings;
	void UpdateRings();

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> TracerISM;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ShellISM;
	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> RingMat;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> HitFX;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> BlastFX;
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DustFX;
	UPROPERTY()
	TObjectPtr<USoundBase> BlastSound;
	UPROPERTY()
	TObjectPtr<USoundBase> ShotSound;
};
