// 夺芯规则（设计文档「夺芯模式与峡谷地图」）：全场同时只有一颗核心，只从中央铸造台出（开打 15 秒后第一颗，交芯后 10 秒出下一颗）。
// 站在核心旁边读条 1.2 秒捡起（带芯：不能飞、不能挥刀、推进减半），带回自家开启的回收站站满 1.5 秒得 1 分。
// 带芯者被击毁核心掉在原地，谁都能捡；掉在地上 15 秒没人捡就回到中央。击毁不掉核心，阵亡 8 秒后重生。
// 每队两个回收站每 90 秒轮换。先到 5 分获胜。
// 规则只在服务器上算，核心、待重生列表、回收站状态复制给各端（HUD 显示、各端画核心和回收站）。AI 的任务也由这里每 0.5 秒派一次。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechCoreDirector.generated.h"

class AMechCharacter;
class AMechCoreArena;
class AMechGameMode;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EMechCoreState : uint8
{
	Ground,
	Carried,
	Thrown
};

USTRUCT()
struct FMechCore
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Id = 0;

	/** 哪队的核心；-1 = 无主（中央产出的） */
	UPROPERTY()
	int32 OwnerTeam = -1;

	UPROPERTY()
	EMechCoreState State = EMechCoreState::Ground;

	UPROPERTY()
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<AMechCharacter> Carrier = nullptr;

	/** 这颗核心是哪台阵亡机体的（无主核心为空） */
	UPROPERTY()
	TObjectPtr<AMechCharacter> Victim = nullptr;

	UPROPERTY()
	FString VictimName;

	/** 落地时刻（服务器时间），自动回收按它算 */
	UPROPERTY()
	float DropTime = 0.f;

	/** 交芯读条 0–1 */
	UPROPERTY()
	float Channel = 0.f;

	/** 对面看到的带芯者位置（每 3 秒更新一次） */
	UPROPERTY()
	FVector_NetQuantize10 Pulse = FVector::ZeroVector;

	UPROPERTY()
	float PulseTime = -100.f;

	/** 地上的敌方 / 无主核心要站在旁边读条才捡得起来：进度 0–1、正在捡的是谁 */
	UPROPERTY()
	float Grab = 0.f;

	UPROPERTY()
	TObjectPtr<AMechCharacter> Grabber = nullptr;

	// 只在服务器上用
	FVector Velocity = FVector::ZeroVector;
	TWeakObjectPtr<AMechCharacter> Thrower;
	float ThrowTime = -100.f;
	float PickTime = 0.f;
};

/** 阵亡、等重生的机体 */
USTRUCT()
struct FMechDowned
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AMechCharacter> Mech = nullptr;

	UPROPERTY()
	float DiedAt = 0.f;

	/** 服务器时间；-1 = 还在等（核心在地上或被带走） */
	UPROPERTY()
	float RespawnAt = -1.f;

	/** 阵亡时操控它的控制器（真人 / AI），重生后交回去；只在服务器上用 */
	TWeakObjectPtr<AController> Controller;
};

UCLASS()
class AMechCoreDirector : public AActor
{
	GENERATED_BODY()

public:
	AMechCoreDirector();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 起步数值（设计文档的数值表）
	static constexpr float NormalRespawn = 8.f;     // 阵亡后多久重生
	static constexpr float RecallTime = 15.f;       // 掉在地上多久没人捡就回到中央
	static constexpr float PickRadius = 380.f;
	static constexpr float ChannelTime = 1.5f;
	static constexpr float PulseEvery = 3.f;
	static constexpr float StationSwapEvery = 90.f;
	static constexpr float NeutralFirst = 15.f;     // 开打后第一颗核心
	static constexpr float NeutralAfterScore = 10.f;  // 交芯后多久出下一颗
	static constexpr float ThrowCooldown = 2.f;
	/** 捡地上的敌方 / 无主核心要站着读条这么久；队友救人读条短一点（空中接住马上拿到）。被打到失衡就断 */
	static constexpr float GrabTime = 1.2f;
	static constexpr float RescueTime = 0.8f;
	/** 地上的核心这么近之内有本队真人玩家时，本队机器人不捡（让给玩家），改去护着他 */
	static constexpr float HumanFirstRadius = 3000.f;

	void Setup(AMechGameMode* InMode, AMechCoreArena* InArena);
	/** 新开一局：清掉所有核心和待重生 */
	void ResetMatch();
	/** 倒计时结束开打：回收站、无主核心的计时从这里算 */
	void BeginFight();
	void StopMatch();
	void OnMechDied(AMechCharacter* Victim, AActor* Killer);
	/** 带芯者把核心扔出去（玩家按 G、AI 传给队友），Aim 是想扔的方向 */
	void ThrowCore(AMechCharacter* Mech, const FVector& Aim);
	/** 机体重生后换了新的（旧的销毁），待重生表里的指针也要换 */
	void LogSummary() const;

	UPROPERTY(Replicated)
	TArray<FMechCore> Cores;

	UPROPERTY(Replicated)
	TArray<FMechDowned> Downed;

	/** 每队当前开启的回收站（0 中层 A、1 底层 B） */
	UPROPERTY(Replicated)
	int32 ActiveStation[2] = {0, 0};

	UPROPERTY(Replicated)
	float NextSwapTime = 0.f;

	UPROPERTY(Replicated)
	float NextNeutralTime = 0.f;

	UPROPERTY(Replicated)
	TObjectPtr<AMechCoreArena> Arena = nullptr;

	const FMechCore* CoreCarriedBy(const AMechCharacter* Mech) const;
	const FMechCore* CoreOfVictim(const AMechCharacter* Victim) const;
	const FMechDowned* DownedOf(const AMechCharacter* Mech) const;
	FVector StationLocation(int32 Team, int32 Index) const;
	FVector ActiveStationLocation(int32 Team) const { return StationLocation(Team, ActiveStation[Team]); }
	float NetTime() const;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateCores(float DeltaSeconds);
	void UpdateTimers();
	void UpdateRespawns();
	void UpdateVisuals(float DeltaSeconds);
	void AssignAI();
	FMechCore& SpawnCore(int32 OwnerTeam, AMechCharacter* Victim, const FVector& At);
	void PickUp(FMechCore& Core, AMechCharacter* By);
	void Drop(FMechCore& Core, const FVector& At);
	/** 救人 / 交芯 / 回收都会把核心拿掉，并安排它主人什么时候重生 */
	void Resolve(int32 Index, float RespawnAt);
	void Score(int32 Index, int32 Team);
	void Feed(const FString& Text, int32 Team) const;
	FMechDowned* FindDowned(const AMechCharacter* Mech);
	/** 本队离这个位置 HumanFirstRadius 内、还活着的真人玩家（没有返回空） */
	AMechCharacter* HumanMateNear(int32 Team, const FVector& At) const;

	TWeakObjectPtr<AMechGameMode> Mode;
	int32 NextId = 1;
	bool bRunning = false;
	float AIClock = 0.f;
	bool bSwapWarned = false;
	bool bNeutralWarned = false;

	// 各端：核心（菱形晶体 + 线框笼子 + 两道光环 + 光柱）、回收站（地上的圈）
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Orbs;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Frames;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FrameMats;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Beams;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> OrbMats;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Discs[4];

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DiscMats[4];

	/** 各端插值后的核心位置（按 Id） */
	TMap<int32, FVector> Smoothed;

	// 统计（自动对战结束时打印）
	int32 StatScore[2] = {0, 0};
	int32 StatRescue = 0;
	int32 StatRecall = 0;
	int32 StatCapture = 0;
	int32 StatNeutral = 0;
	int32 StatThrow = 0;
	int32 StatCatch = 0;
	int32 StatSteal = 0;
	int32 StatDeaths = 0;
	float StatCarrySum = 0.f;
	int32 StatCarryCount = 0;
	float StatDownSum = 0.f;
	int32 StatDownCount = 0;
	float FightStart = 0.f;
	float LastCarrierLog = 0.f;
};
