// 机体移动：在角色移动组件里做冲刺、空中滑翔、按住跳跃喷射、快速闪避、蹬墙跳和能量，
// 这样联机时客户端可以预测、服务器按同样的输入重算，不会被拉回。
// 三个输入（冲刺按住、跳跃按住、快速闪避一次）随每个移动包一起发给服务器（自定义压缩标志位）；
// 蹬墙跳用引擎自带的「这一包按了跳跃」标志（bPressedJump），不占自定义位。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MechMovementComponent.generated.h"

namespace MechMapRules
{
	struct FFlight;
}

UCLASS()
class UMechMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FSavedMove_Mech;

public:
	UMechMovementComponent();

	// ---------------------------------------------------------------- 输入（本地控制的那一端设置；服务器从移动包里还原）
	bool bWantsBoost = false;
	bool bWantsThrust = false;
	bool bWantsQuickBoost = false;
	/** 右键瞄准 / 格挡：慢走、不能冲刺 */
	bool bWantsSlow = false;
	/** AI 用：快速闪避指定方向（只在服务器上用，不进移动包） */
	FVector QuickBoostDirOverride = FVector::ZeroVector;

	// ---------------------------------------------------------------- 状态
	float Energy = 100.f;
	bool bOverheated = false;
	bool bBoostActive = false;
	bool bThrusting = false;
	/** 快速闪避刚触发时为 1，很快衰减（视野、喷焰用） */
	float QuickBoostFlash = 0.f;

	UPROPERTY(EditAnywhere, Category="Mech|Energy")
	float MaxEnergy = 100.f;

	/** 停止消耗后过多久开始回能量（秒） */
	UPROPERTY(EditAnywhere, Category="Mech|Energy")
	float RegenDelay = 0.6f;

	UPROPERTY(EditAnywhere, Category="Mech|Energy")
	float RegenPerSecond = 35.f;

	/** 耗尽后要回到这个值才能再用推进 */
	UPROPERTY(EditAnywhere, Category="Mech|Energy")
	float OverheatRecover = 35.f;

	UPROPERTY(EditAnywhere, Category="Mech|Boost")
	float WalkSpeed = 680.f;

	UPROPERTY(EditAnywhere, Category="Mech|Boost")
	float BoostSpeed = 1500.f;

	/** 地面冲刺每秒耗能 */
	UPROPERTY(EditAnywhere, Category="Mech|Boost")
	float BoostDrain = 14.f;

	/** 空中冲刺 / 喷射时回能不停（按地图的飞行规则，MechMapRules.h；别的图 false，一推进就停回能） */
	bool bRegenWhileFlying = false;

	/** 石林飞行器（MechMapRules::FFlight，别的图 nullptr）：空中按住冲刺 = 沿视线飞（GlideStep） */
	const MechMapRules::FFlight* GliderRules = nullptr;
	float GlideSpeedScale = 1.f;
	/** 这一帧在飞（不受重力、不刹车） */
	bool bGliding = false;
	/** AI 飞行不看视线（视线要瞄人）：俯仰直接给，水平方向跟移动输入 */
	bool bGlideAI = false;
	float GlideAIPitch = 0.f;
	/** 现在沿航迹的爬升角的正弦（飞行时，HUD / AI / 动画可用） */
	float GlideSinPitch = 0.f;

	/** 空中按住冲刺时的水平推力加速度（厘米/秒²） */
	UPROPERTY(EditAnywhere, Category="Mech|Boost")
	float AirBoostAccel = 2400.f;

	/** 空中冲刺时的重力倍数（滑翔） */
	UPROPERTY(EditAnywhere, Category="Mech|Boost")
	float AirBoostGravity = 0.45f;

	/** 空中按住跳跃时向上的推力加速度，要大于重力才能升起来 */
	UPROPERTY(EditAnywhere, Category="Mech|Thrust")
	float ThrustUpAccel = 2300.f;

	UPROPERTY(EditAnywhere, Category="Mech|Thrust")
	float ThrustDrain = 20.f;

	UPROPERTY(EditAnywhere, Category="Mech|Thrust")
	float MaxRiseSpeed = 650.f;

	UPROPERTY(EditAnywhere, Category="Mech|QuickBoost")
	float QuickBoostSpeed = 2600.f;

	UPROPERTY(EditAnywhere, Category="Mech|QuickBoost")
	float QuickBoostCost = 22.f;

	UPROPERTY(EditAnywhere, Category="Mech|QuickBoost")
	float QuickBoostCooldown = 0.35f;

	// 推进器（配装，AMechCharacter::ApplyLoadout 设）：地面型不能向上喷射、不能空中滑翔；回能按着地 / 空中分别乘倍数
	UPROPERTY(EditAnywhere, Category="Mech|Booster")
	bool bCanThrust = true;

	UPROPERTY(EditAnywhere, Category="Mech|Booster")
	bool bCanAirBoost = true;

	/** 夺芯带芯：不能喷射上升、空中冲刺、快速闪避，冲刺速度和回能都减半（机体设，服务器和本地一致） */
	bool bCarryLock = false;

	/** 被捕获网罩住：几乎走不动，不能冲刺、喷射、闪避；按快速闪避算挣扎（肩部组件按复制来的状态设） */
	bool bNetLock = false;

	UPROPERTY(EditAnywhere, Category="Mech|Booster")
	float GroundRegenScale = 1.f;

	UPROPERTY(EditAnywhere, Category="Mech|Booster")
	float AirRegenScale = 0.5f;

	/** 失衡时移动速度倍数 */
	UPROPERTY(EditAnywhere, Category="Mech|Health")
	float StaggerMoveScale = 0.15f;

	// 蹬墙跳：空中贴着竖直的墙（胶囊外 WallJumpReach 内）按跳跃 → 朝墙外 + 向上蹬出去，再带一点按键方向；
	// 落地前最多连蹬 WallJumpMax 次，同一面墙不能连着蹬（必须换一面墙或落地），每次小耗一点能量（不够也能蹬，蹬腿不靠推进器）
	/** 胶囊表面外多远以内的墙能蹬（厘米） */
	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	float WallJumpReach = 100.f;

	/** 沿墙面法线往外的速度 */
	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	float WallJumpOutSpeed = 950.f;

	/** 向上的速度（原来往上走得更快就保留） */
	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	float WallJumpUpSpeed = 980.f;

	/** 按键方向沿墙面的那部分额外给的速度 */
	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	float WallJumpSteerSpeed = 380.f;

	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	int32 WallJumpMax = 3;

	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	float WallJumpCost = 8.f;

	/** 两次蹬墙之间至少隔多久（秒） */
	UPROPERTY(EditAnywhere, Category="Mech|WallJump")
	float WallJumpCooldown = 0.25f;

	/** 这次落地前已经蹬了几次、上一次蹬的那面墙（法线 + 墙上一点，判「同一面墙」）、冷却；都进移动包存档，客户端重放时还原 */
	int32 WallJumps = 0;
	FVector LastWallNormal = FVector::ZeroVector;
	FVector LastWallPoint = FVector::ZeroVector;
	float WallJumpTimer = 0.f;

	/** 现在按跳跃能不能蹬墙（HUD / 测试用；不改状态）。OutNormal 是要蹬的那面墙朝外的方向 */
	bool CanWallJump(FVector* OutNormal = nullptr) const;

	/** 测试（-MechFlightTest）：把回能等待重新算起 */
	float& SinceSpendForTest() { return SinceSpend; }

	/** 收到服务器纠正的次数（联机测试统计用） */
	int32 CorrectionCount = 0;

	// ---------------------------------------------------------------- 别人的机体：按时间回放的插值缓冲（10-31 联机抖动）
	// 用户：「联机时仍旧存在看朋友的模型严重抖动」（房主在德国、其他人在中国，往返 250–350 毫秒、抖动大）。
	// 引擎默认的做法是「追最新的包」：客户端上胶囊收到包就瞬移到服务器位置、两包之间按速度外推，网格按 0.1 秒指数追胶囊；
	// 房主机器上客户端的机体只在移动包到的那一帧走（包扎堆到就一次走几帧，没包就停），网格按 0.06 秒追。包到达的时间一抖，
	// 外推就过头 / 不够，被下一个包拉回，网格跟着一顿一冲（实测见 README「看别人的机体」）。
	// 现在：网格不追胶囊，而是把收到的位置（带服务器 / 发送端的时间戳）存起来，按「比最新的包晚一点」的时间点回放，
	// 在前后两个位置之间按速度做三次插值——包怎么抖都不影响画面，代价是画面上的位置比胶囊晚约 0.05–0.25 秒（按实测抖动自动调）。
	// 胶囊（碰撞、命中判定）照旧按引擎的做法；-MechNetSmoothOld 用改之前的做法（对比测量用）
	/** 画面上（网格）的速度、在不在空中：别人的机体用回放出来的（连续，不随包跳），其余直接用移动组件的。动画按这个混合 */
	FVector GetVisualVelocity() const { return bInterpActive ? InterpVel : Velocity; }
	bool IsVisualFalling() const { return bInterpActive ? bInterpFalling : IsFalling(); }
	/** 这台的网格现在由插值缓冲摆（客户端上别人的机体、房主机器上客户端的机体） */
	bool IsInterpActive() const { return bInterpActive; }
	/** 收到的最新位置（胶囊中心，没有外推）：别人的机体摆瞄准姿势时用它算瞄准方向（胶囊在两包之间外推，按胶囊算方向会一漂一跳） */
	FVector GetInterpLatestLocation() const;

	/** -MechJitterLog 的联机统计（每 5 秒 AMechPlayerController::JitterTick 取走清零） */
	struct FNetStats
	{
		/** 收到的新位置数、一帧里收到两个以上的帧数、在动时回放追上了最新位置只能外推 / 停住的帧数、在两个位置之间正常插值的帧数 */
		int32 Samples = 0;
		int32 BunchFrames = 0;
		int32 StarveFrames = 0;
		int32 Frames = 0;
		/** 回放点比最新位置晚多少（秒）的累加 */
		double DelaySum = 0.0;
		/** 收到新位置的间隔（毫秒，本机时间）；客户端上：收到时胶囊（外推的）离服务器位置多远（厘米，即旧做法每次被拉回多少） */
		TArray<float> Gaps;
		TArray<float> Errors;
		double LastArrival = -1.0;
		uint64 LastArrivalFrame = 0;
		int32 InFrame = 0;
	};
	FNetStats NetStats;

	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 自己的机体（客户端）被服务器纠正时画面上的偏差（世界坐标）：网格和镜头臂先留在纠正前的位置，0.12 秒收掉（-MechOwnSmoothOld 关掉，见 cpp） */
	FVector OwnCorrectionOffset = FVector::ZeroVector;

	/** 真实对局诊断（-MechNetDiag，默认关；打包版也能用）：每台机体每帧记画面上（网格）的位置 / 朝向，每秒汇总一段，
	 *  写到 Saved/Logs/联机诊断_<房主|客户端>_<开始时间>.txt（同时进日志，搜 [netdiag]）。见 cpp 的 DiagWrite */
	struct FDiag
	{
		FVector P[2] = {FVector::ZeroVector, FVector::ZeroVector};
		float Yaw[2] = {0.f, 0.f};
		double T[2] = {0.0, 0.0};
		FVector Cam[2] = {FVector::ZeroVector, FVector::ZeroVector};
		int32 N = 0;
		/** 这一秒：位置加速度（米/秒²）、朝向角加速度（度/秒²）、镜头加速度（米/秒²，只记自己的机体）、收到新位置的间隔（毫秒） */
		TArray<float> Acc, YawAcc, CamAcc, Gaps;
		int32 Frames = 0, Jumps = 0, Reverse = 0, Samples = 0, StarveFrames = 0, DelayFrames = 0, Corrections0 = -1;
		float OffsetSum = 0.f, OffsetMax = 0.f, SpeedSum = 0.f, CorrMax = 0.f;
		double DelaySum = 0.0;
		/** 各状态（0 地面 1 空中 2 冲刺 3 喷射 4 闪避 5 钩爪/根运动 6 出刀）的帧数和跳变次数 */
		int32 ModeFrames[7] = {};
		int32 ModeJumps[7] = {};
	};
	FDiag Diag;

	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;
	virtual float GetMaxBrakingDeceleration() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual void TickCharacterPose(float DeltaTime) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	/** 出刀到可取消时刻之前（UMechCombatComponent::IsMeleeDriving）：速度、朝向由战斗组件按突进曲线定，这里不刹车、不转向 */
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysicsRotation(float DeltaTime) override;

protected:
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void SimulatedTick(float DeltaSeconds) override;
	virtual void SmoothClientPosition(float DeltaSeconds) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	using UCharacterMovementComponent::OnClientCorrectionReceived;
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation,
		FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase,
		bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection) override;

private:
	bool Spend(float Amount, bool bKeepRegen = false);
	/** 石林飞行器一帧：按视线（AI 按指定俯仰）转向、按爬升角改速度，并扣能量 */
	void GlideStep(float DeltaSeconds, const FVector& Dir);
	bool CanMoveFreely() const;
	/** 找能蹬的墙：胶囊四周 8 个方向扫，最近的竖直面（不算别的机体）；OutPoint 是墙上的接触点 */
	bool FindKickWall(FVector& OutNormal, FVector& OutPoint) const;
	/** 这面墙是不是上一次蹬过的那面（朝向差不多、在同一个平面上） */
	bool IsLastWall(const FVector& Normal, const FVector& Point) const;
	/** 空中刚按下跳跃：贴墙就蹬出去 */
	void TryWallJump();

	float SinceSpend = 10.f;
	float QuickBoostTimer = 0.f;
	float ThrustHold = 0.f;

	// ---------------------------------------------------------------- 插值缓冲（见上面 GetVisualVelocity 的说明）
	struct FInterpSample
	{
		/** 时间戳：服务器 / 操控那台客户端的移动时间（同一台机体同一条时间线），不是本机收到的时间 */
		double T = 0.0;
		FVector Loc = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		FVector Vel = FVector::ZeroVector;
		bool bFalling = false;
	};
	/** 这台现在该不该用插值缓冲（别人的机体、没开 -MechNetSmoothOld、没倒地） */
	bool WantsInterp() const;
	void AddInterpSample(double T, const FVector& Loc, const FQuat& Rot, const FVector& Vel, bool bFalling);
	/** 按时间点取回放出来的胶囊位置 / 朝向 / 速度；返回 false = 已经过了最新位置（外推 / 停住） */
	bool EvalInterp(double T, FVector& OutLoc, FQuat& OutRot, FVector& OutVel, bool& bOutFalling) const;
	void ResetInterp();
	/** 每帧推进回放时间、摆网格（客户端：SimulatedTick 末尾；房主：TickComponent 末尾，都经 SmoothClientPosition） */
	void TickInterp();
	/** 帧末再摆一次：网格摆好以后胶囊又被转了（房主机器上客户端出刀突进时战斗组件在物理之后转身），网格挂在胶囊上会被带着绕胶囊甩 */
	void LateInterpFix();
	void NoteArrival(float Error);

	TArray<FInterpSample> InterpSamples;
	/** 本机时间 − 位置时间戳：最快到的那个包（时钟换算的基准）、这一秒里最快的、包比基准晚到多少（最大值，慢慢回落）、位置间隔（最大值，慢慢回落） */
	double InterpBase = 0.0;
	double InterpWinMin = 1e30;
	double InterpWinStart = 0.0;
	double InterpLate = 0.0;
	double InterpGap = 1.0 / 30.0;
	/** 回放时间（位置时间线上）、上一帧的本机时间 */
	double InterpPlayT = 0.0;
	double InterpLastReal = -1.0;
	float InterpLastStamp = -1.f;
	bool bInterpClock = false;
	/** 回放点跑过了最新位置、停着等缓冲攒够（见 TickInterp） */
	bool bInterpPaused = false;
	bool bInterpActive = false;
	bool bInterpFalling = false;
	FVector InterpVel = FVector::ZeroVector;
	/** 画面连续用：上一帧画出来的胶囊位置 / 朝向，和回放出来的差（新包改写了过去的轨迹、回放时间跳了时，差值慢慢收掉，画面不跳） */
	FVector InterpShownLoc = FVector::ZeroVector;
	FQuat InterpShownRot = FQuat::Identity;
	FVector InterpErrLoc = FVector::ZeroVector;
	FQuat InterpErrRot = FQuat::Identity;
	bool bInterpShown = false;
	/** 这一帧网格该在的世界变换和摆的是第几帧（LateInterpFix 只补这一帧摆过的） */
	FTransform InterpMeshWorld;
	uint64 InterpMeshFrame = 0;

	/** 诊断：每帧记一次（帧末），每秒写一段（第一个过整秒的机体替所有机体写） */
	void DiagFrame();
	static void DiagWrite(UWorld* World, double Now);

	/** 自己的机体纠正平滑：开着没有、纠正前画面上的位置、镜头臂原来的相对位置 */
	bool WantsOwnSmooth() const;
	FVector OwnShownBefore = FVector::ZeroVector;
	bool bOwnCorrectionPending = false;
	FVector OwnBoomBase = FVector::ZeroVector;
	bool bOwnBoomBase = false;
	bool bOwnApplied = false;

	struct FLateTick : public FTickFunction
	{
		UMechMovementComponent* Target = nullptr;
		virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
		virtual FString DiagnosticMessage() override { return TEXT("UMechMovementComponent::LateInterpFix"); }
	};
	FLateTick LateTick;
};

/** 带机体输入的移动包 */
class FSavedMove_Mech : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	uint8 bSavedBoost : 1;
	uint8 bSavedThrust : 1;
	uint8 bSavedQuickBoost : 1;
	uint8 bSavedSlow : 1;

	// 这一包开始时的计时 / 计数状态：客户端被纠正后重放旧移动时还原，重放出来的结果才和当初（以及服务器）一致
	// （比如重放到蹬墙那一包时，「同一面墙」「冷却」要按当时的状态判，不能按现在的）
	float SavedThrustHold = 0.f;
	float SavedQuickBoostTimer = 0.f;
	float SavedWallJumpTimer = 0.f;
	int32 SavedWallJumps = 0;
	FVector SavedWallNormal = FVector::ZeroVector;
	FVector SavedWallPoint = FVector::ZeroVector;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* C) override;
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;

private:
	void RestoreState(class UMechMovementComponent* Move) const;
};

class FNetworkPredictionData_Client_Mech : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FNetworkPredictionData_Client_Mech(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}

	virtual FSavedMovePtr AllocateNewMove() override;
};
