// 肩部武器（导弹以外的）：火炮、护盾、钩爪、捕获网、烟雾弹、EMP。E 按下 Pressed()、松开 Released()（玩家和 AI 同一个接口）；导弹仍在 UMechCombatComponent。
//  火炮：按住 E 架炮（0.45 秒），架好后开镜、准星自动拉向锁定的敌人（带提前量），松开开炮；没架好就松开 = 收炮不开。冷却 10 秒。
//  护盾：身前展开一面六边形能量盾 3 秒，挡住正面 75° 内的所有远程攻击（子弹、飞刀、导弹、炮弹、EMP、网、钩爪；刀挡不住）。冷却 12 秒。
//  烟雾弹 / EMP：抛物线抛向准星（最远 40 米），弹在 AMechGrenade。冷却各 14 秒。
//  钩爪：爪子沿准星射出（准星贴近锁定的敌人就射向他），最远 45 米。
//        抓到地形 → 沿直线把自己拉过去（根运动：自己这一端先算好目标，服务器用同一个目标，联机不会被拉回）；途中快速闪避就松开。
//        抓到敌人 → 一点伤害，把对方拉到自己身前（对方那一端也按同一个目标拉）。没抓到就收回，冷却短一些。
//        按住右键（射击姿态瞄准、近战姿态格挡都算）时改为「救队友」：准星附近（小锥角、看得见、射程内）的队友被锁定，
//        爪子飞过去把他拉到自己身前（不伤害，被网住的顺便挣脱）；附近没有队友就照常发射。不按右键永远不会勾到队友（免得乱拉）。
//  捕获网：射出一张网（AMechNet），飞出去张开到直径约 4.5 米；罩住敌人 → 对方 2 秒动不了、不能开火 / 出刀 / 喷射，网上通电；
//        被网住的一方连按快速闪避挣扎，每下少 0.3 秒。
// 被网住的状态也记在这里（每台机体都有这个组件）。模型：发射器就是肩部挂件（机体按配装换模型），爪子是机体上单独一个网格，
// 缆绳是一串拉长的圆柱（每帧重摆）。
// 浮空无人机（10-30 玩家：「肩部武器没有绑定」——挂件悬在头边、和机体之间没有连接 → 六样都改成浮空无人机）：
//  挂件网格（AMechCharacter::PodMesh）不再挂在机体上，而是在机体左肩外上方的「理想点」附近飞：弹簧阻尼追着理想点（带速度前馈，
//  匀速跟着走不落后，起步 / 急停 / 转身时才拖一点）、轻微上下浮动、按落后的方向倾斜；底下两个小推进器（冷色光点）。
//  放左肩：第三人称镜头在右肩后上方，准星那条视线正好穿过右肩外上方一米左右的地方，放右边会挡准星。
//  开火 / 瞄准（火炮架炮、钩爪、刚发射、导弹齐射）时转向目标，平时朝机体前方。
//  发射起点分两种：判定用 LaunchPoint（理想位置，按胶囊和瞄准方向算，不带弹簧和浮动，服务器和操控那一端一样）；
//  画面用 VisualPoint（本机看到的无人机），发射特效、缆绳从这里出，弹（导弹 / 炮弹 / 网 / 抛射弹）刚出现时也从这里出、0.2 秒内并到判定弹道上
//  （和曳光「从本机看到的枪口出发」一个思路）。贴墙时理想点往机体这边收（从头部往理想点扫一个球）。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MechTypes.h"
#include "MechShoulderComponent.generated.h"

class AMechCharacter;
class AMechNet;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

/** 钩爪状态 */
UENUM()
enum class EMechGrapple : uint8
{
	Idle,
	/** 爪子在飞（抓地形 / 抓人 / 没抓到，飞到头再说） */
	Flying,
	/** 抓住地形，把自己拉过去 */
	Latched,
	/** 抓住敌人，把对方拉过来 */
	Hooked,
	/** 收回 */
	Retract
};

/** 钩爪这一发（服务器设，复制给所有人画爪子和缆绳） */
USTRUCT()
struct FMechGrappleRep
{
	GENERATED_BODY()

	UPROPERTY()
	EMechGrapple State = EMechGrapple::Idle;

	/** 第几发（发射那一端编的号，操控的客户端认得出是不是自己预测的那一发） */
	UPROPERTY()
	uint8 Stamp = 0;

	/** 这一发是去救队友的（不伤害、不冒火花） */
	UPROPERTY()
	bool bAlly = false;

	UPROPERTY()
	FVector_NetQuantize Origin = FVector::ZeroVector;

	/** 飞向 / 抓住的点（抓人时是开始飞的那一刻对方的位置，之后跟着对方） */
	UPROPERTY()
	FVector_NetQuantize Tip = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<AMechCharacter> Victim;

	/** 这个状态开始的服务器时间、飞行要多久 */
	UPROPERTY()
	float ServerTime = 0.f;

	UPROPERTY()
	float FlightTime = 0.f;
};

UCLASS(ClassGroup=(Mech))
class UMechShoulderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMechShoulderComponent();

	// ---------------------------------------------------------------- 操作
	/** E 按下 / 松开（火炮按住架炮、松开开炮；护盾按下开盾；其余按下就发射） */
	void Pressed();
	void Released();
	/** 按配装发射（钩爪 / 捕获网 / 烟雾弹 / EMP；导弹不走这里） */
	void Fire();
	/** 机体换配装后调：肩部换了就把钩爪收好、冷却清零 */
	void OnLoadoutChanged();

	// ---------------------------------------------------------------- 状态（HUD、AI、机体用）
	/** 就绪程度 0–1、还要冷却几秒 */
	float GetReady() const;
	float GetCooldown() const { return Cooldown; }
	/** 服务器：缩短冷却（保卫战 维克托 补给 2） */
	void RunReduceCooldown(float Seconds) { Cooldown = FMath::Max(0.f, Cooldown - Seconds); }
	/** 钩爪：准星现在指的地方抓不抓得到（0 抓不到、1 地形、2 敌人、3 队友（按住右键）；本机玩家每帧算） */
	int32 GetAimHint() const { return AimHint; }
	/** 按住右键、钩爪就绪时锁定的队友（本机玩家每帧算，HUD 在他身上画锁定标记）；没有就是空 */
	AMechCharacter* GetAllyAim() const { return AllyAim.Get(); }
	/** 「救队友」模式：本机玩家按住右键（射击姿态瞄准 / 近战姿态格挡都算） */
	bool IsAllyMode() const;
	EMechGrapple GetGrappleState() const { return Vis.State; }
	/** 爪子收着时该在的位置（发射器模型上的爪座；调试统计「爪子脱离发射器」用） */
	FVector GetClawRestWorld() const;
	/** 最近一次发射的时间（本地；发射器后坐动作用） */
	float GetLastFireTime() const { return LastFireTime; }
	/** 被网住了：动不了、不能攻击 */
	bool IsNetted() const { return bNetted; }
	// ---- 火炮
	/** 正按住 E 架炮（本地） */
	bool IsArtilleryHeld() const { return bArtyHeld; }
	/** 架炮进度 0–1（1 = 架好，开镜、自动瞄准） */
	float GetArtilleryDeploy() const;
	/** 炮架着（复制，别人画炮管抬起） */
	bool IsArtilleryDeployed() const { return bArtyDeployed || bArtyHeld; }
	// ---- 护盾
	bool IsShieldUp() const;
	/** 护盾还剩多少（0–1） */
	float GetShieldLeft() const;
	/** 护盾开着、From 这个点在正面 75° 内：挡得住 */
	bool BlocksFrom(const FVector& From) const;
	/** 服务器：护盾挡下一次攻击（闪一下、响一声） */
	void OnShieldBlocked(const FVector& At);
	/** 模型查看拍照用：不管冷却直接开盾 3 秒；Gain 是这次的亮度倍数 */
	void DebugShield(float Gain = 1.f) { ShieldEnd = ServerNow() + ShieldTime(); ShieldGain = Gain; }
	float ShieldGain = 1.f;
	// ---- EMP
	/** 服务器：被 EMP 打中（能量清零、推进器失灵、锁定失效 Duration 秒） */
	void ApplyEmp(float Duration);
	bool IsEmped() const;
	/** 网还剩多少（0–1，HUD 用） */
	float GetNetLeft() const;
	/** 最近一次挣扎的时间（本地，HUD 网格抖一下） */
	float GetLastStruggleTime() const { return LastStruggleTime; }
	/** 测试用（服务器）：钩住敌人、抓住地形把自己拉过去、网罩住敌人各几次 */
	int32 DebugHooks = 0;
	int32 DebugLatches = 0;
	int32 DebugNets = 0;
	/** 测试用（服务器）：护盾挡下了几次、钩爪救了几次队友 */
	int32 DebugBlocks = 0;
	int32 DebugRescues = 0;

	// ---------------------------------------------------------------- 别的组件调（服务器）
	/** 被网罩住（AMechNet 调） */
	void ApplyNet(AMechCharacter* By, AMechNet* Net);
	/** 被网住时按了快速闪避（移动组件里调，服务器上） */
	void Struggle();
	/** 被钩爪钩住：拉到 Target（对方的钩爪调；被拉的是玩家时他那一端也按同样的拉）。bRescue：队友来救——被网住的先挣脱 */
	void ReceiveDrag(const FVector& Target, uint16 Ms, bool bRescue = false);
	/** 自己正被拉 / 正在拉自己：快速闪避时松开（移动组件里调，服务器和本地都调） */
	void OnQuickBoostCancel();

	/** 用到的特效（开局预热用） */
	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;

	// ---------------------------------------------------------------- 浮空无人机
	/** 判定用的发射起点（各端一致）：无人机理想位置（胶囊 + 瞄准方向，不带弹簧、浮动）朝 Dir 时，机身模型坐标 Local 那一点的世界位置；贴墙时收在墙前 */
	FVector LaunchPoint(const FVector& Dir, const FVector& Local) const;
	/** 同上，朝 AimPoint 那个点 */
	FVector LaunchPointToward(const FVector& AimPoint, const FVector& Local) const;
	/** 画面上的发射口：本机看到的无人机（带弹簧、浮动）朝 Dir 时 Local 那一点（发射特效、弹刚出现的位置） */
	FVector VisualPoint(const FVector& Dir, const FVector& Local) const;
	/** 无人机机身中心（画面上的）和这一帧的理想点（按网格算、不带弹簧；-MechJitterLog「无人机离理想点」用） */
	FVector GetDroneLocation() const { return DronePos; }
	FVector GetDroneIdealVisual() const { return DroneIdealVis; }
	/** 刚发射（各端播发射特效时调）：无人机马上转向 Dir、往后上方一顿 */
	void OnDroneFired(const FVector& Dir);
	/** 各肩部武器发射口在机身模型里的位置（导弹巢的发射管在战斗组件里） */
	static FVector MuzzleLocalFor(EMechShoulder Type);
	/** 弹刚出现时画面上的偏移：「本机看到的无人机发射口 − 弹现在的判定位置」，弹在 0.2 秒内收掉；射手不在 / 偏太远（不是刚发射的）返回 0 */
	static FVector LaunchVisualOffset(const AActor* Shooter, const FVector& LogicPos, const FVector& Dir, const FVector& Local);
	/** 偏移随时间收掉的比例（0.2 秒从 1 到 0，先快后慢） */
	static float LaunchVisualFade(float Age) { return Age >= 0.2f ? 0.f : FMath::Square(1.f - Age / 0.2f); }

	/** 电弧（EMP 用，炸开的一瞬间和中招机体身上）：从 A 到 B 一道折线，每段一根细圆柱，追加到 Out（世界变换，给绝对坐标的 ISM 用）。
	 *  Jag 折线横向抖动占长度的比例，Radius 粗细（厘米） */
	static void AppendArc(TArray<FTransform>& Out, const FVector& A, const FVector& B, float Radius, float Jag, FRandomStream& Rand, int32 Segments = 7);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---------------------------------------------------------------- 数值
	/** 钩爪：射程、爪子飞多快、自己被拉过去多快、把敌人拉过来多快、拉到身前多远 */
	static constexpr float GrappleRange = 4500.f;
	static constexpr float ClawSpeed = 9000.f;
	static constexpr float PullSpeed = 3200.f;
	static constexpr float DragSpeed = 2800.f;
	static constexpr float DragStop = 600.f;
	static constexpr float GrappleCooldown = 7.f;
	static constexpr float GrappleMissCooldown = 3.f;
	static constexpr float GrappleDamage = 300.f;
	static constexpr float GrappleImpact = 140.f;
	/** 救队友：准星离队友多近算锁定（按距离换算成角度：远处 3 度、近处最多 9 度）、把队友拉到身前多远 */
	static constexpr float AllyAimRadius = 260.f;
	static constexpr float AllyDragStop = 450.f;
	/** 捕获网：飞多快、往下掉多快（厘米/秒²）、网住多久、挣扎一下少多久、最少网住多久、伤害 */
	static constexpr float NetSpeed = 4600.f;
	static constexpr float NetGravity = 320.f;
	static constexpr float NetDuration = 2.f;
	static constexpr float NetStruggle = 0.3f;
	static constexpr float NetMinHold = 0.8f;
	static constexpr float NetCooldown = 12.f;
	/** 框架制造商特色（10-31，AMechCharacter::ApplyLoadout 按 MechCatalog::FrameSpec 设）：被网住、中 EMP 的时间倍数（维克托重工 0.65，相位电子中 EMP 1.5） */
	float NetTimeScale = 1.f;
	float EmpTimeScale = 1.f;
	static constexpr float NetDamage = 200.f;
	/** 火炮：架炮时间、炮弹速度、冷却 */
	static constexpr float ArtilleryDeployTime = 0.45f;
	static constexpr float ShellSpeed = 16000.f;
	static constexpr float ArtilleryCooldown = 10.f;
	/** 护盾：半径（盾面离胸口）、持续、冷却、挡住的正面角度（度） */
	static constexpr float ShieldRadius = 210.f;
	static constexpr float ShieldDuration = 3.f;
	static constexpr float ShieldCooldown = 12.f;
	static constexpr float ShieldAngle = 75.f;
	/** 烟雾弹 / EMP：最远抛多远、抛出速度（决定飞行时间）、重力、冷却 */
	static constexpr float GrenadeRange = 4000.f;
	static constexpr float GrenadeSpeed = 2600.f;
	static constexpr float GrenadeGravity = 980.f;
	static constexpr float GrenadeCooldown = 14.f;

protected:
	virtual void BeginPlay() override;

private:
	AMechCharacter* GetMech() const;
	bool CanFire() const;
	float ServerNow() const;
	/** 发射器（浮空无人机的机身，就是机体的肩部挂件网格）和爪子 */
	UStaticMeshComponent* Launcher() const;
	UStaticMeshComponent* Claw() const;
	/** 判定用的发射口（无人机理想位置朝当前瞄准点） */
	FVector Muzzle() const;

	// ---- 浮空无人机
	void UpdateDrone(float DeltaTime);
	/** 理想点（机身包围盒中心）：bVisual 按画面上的网格（别人的机体在本机是网格平滑追着胶囊），否则按胶囊；Yaw 是摆位用的水平朝向（瞄准方向），
	 *  已经做过贴墙收缩 */
	FVector DroneIdeal(bool bVisual, float Yaw) const;
	/** 判定用的摆位朝向：瞄准方向的水平朝向（没有就用机体朝向） */
	float DroneAimYaw() const;
	/** 从 From 往 To 扫一个半径 Radius 的球，碰到地形就停在碰到的地方（往 From 这边收） */
	FVector ClampToWorld(const FVector& From, const FVector& To, float Radius) const;
	/** 机身网格的世界变换：包围盒中心放在 Center、朝向 Rot */
	FTransform DroneFrame(const FVector& Center, const FQuat& Rot) const;
	/** 机身网格（按配装换）的包围盒中心（模型坐标）和缩放（各模型统一到差不多大） */
	void CacheDroneMesh();
	FVector DroneMeshCenter = FVector::ZeroVector;
	FVector DroneMeshExtent = FVector(20.f);
	float DroneScale = 1.f;
	const UStaticMesh* DroneMeshFor = nullptr;
	bool bDroneInit = false;
	bool bDroneDown = false;
	FVector DronePos = FVector::ZeroVector;
	FVector DroneVel = FVector::ZeroVector;
	FVector DronePrevIdeal = FVector::ZeroVector;
	FVector DroneIdealVis = FVector::ZeroVector;
	FQuat DroneRot = FQuat::Identity;
	FVector DroneSpin = FVector::ZeroVector;
	float DroneYaw = 0.f;
	float DronePhase = 0.f;
	float DroneFiredAt = -100.f;
	FVector DroneFiredDir = FVector::ForwardVector;
	/** 左肩外上方的偏移（机体坐标：前 X、右 Y、上 Z，厘米，相对胶囊中心）；-MechDroneOffset=X,Y,Z 可改（比构图用）。
	 *  10-30 第二轮（用户试玩：「离机体稍远」）从 (-25, -90, 110) 收近。镜头在右肩后方，同样的距离在画面上看着比实际远（越靠近镜头越往左偏） */
	FVector DroneOffset = FVector(-20.f, -66.f, 98.f);
	/** 交战时（锁定了敌人 / 2 秒内开过火）理想点往背离敌人的方向挪：挪多远（厘米）、现在挪了几成（平滑）、往哪挪（世界水平方向，平滑）。
	 *  只往后、往左外挪（按瞄准朝向拆开），不会往机体或准星那边挪 */
	static constexpr float DroneEngagePush = 35.f;
	float DroneEngage = 0.f;
	FVector DroneAway = FVector::ZeroVector;
	/** 钩爪自拉时左手抓住无人机、被它拉过去（0–1，平滑进出） */
	float DroneGrab = 0.f;

	/** 机身底下的两个小推进器：深色短圆柱 + 冷色光点（挂在机身网格上，跟着它走） */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DronePads[2];

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> DroneGlows[2];

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DroneGlowMID;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> EmpArcMID;
	/** 发射方向：玩家对准星（准星贴近锁定的敌人就对他，网还带提前量），AI 对锁定的目标；OutPoint 是瞄的那个点 */
	void AimShot(float Speed, FVector& OutOrigin, FVector& OutDir, FVector* OutPoint = nullptr) const;
	/** 冷却按插件效果缩短（「导弹冷却」那项对所有肩部武器都算） */
	float ScaledCooldown(float Base) const;
	/** 护盾持续多久（新纪框架配新纪护盾更久，11-01 同厂加成） */
	float ShieldTime() const;
	/** 钩爪、捕获网打中后的冷却（11-01 同厂加成） */
	float GrappleCd() const;
	float NetCd() const;

	// ---- 火炮
	void FireArtillery();
	void FireShellAuthority(const FVector& Origin, const FVector& Dir);
	/** 架着炮时把玩家的准星往锁定目标（提前量点）拉 */
	void ArtilleryAutoAim(float DeltaTime);

	UFUNCTION(Server, Reliable)
	void ServerSetArtillery(bool bOn);

	UFUNCTION(Server, Reliable)
	void ServerFireShell(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir);

	UPROPERTY(Replicated)
	bool bArtyDeployed = false;
	bool bArtyHeld = false;
	float ArtyHoldStart = 0.f;

	// ---- 烟雾弹 / EMP
	/** 从 Origin 抛到 Target 的初速度（飞行时间按距离定，0.35–1.5 秒） */
	static FVector LobVelocity(const FVector& Origin, const FVector& Target);
	/** Kind 是 EMechGrenadeKind */
	void FireGrenadeAuthority(uint8 Kind, const FVector& Origin, const FVector& Velocity);

	UFUNCTION(Server, Reliable)
	void ServerFireGrenade(uint8 Kind, FVector_NetQuantize Origin, FVector_NetQuantize10 Velocity);

	// ---- 护盾
	void ActivateShieldAuthority();
	void UpdateShield(float DeltaTime);

	UFUNCTION(Server, Reliable)
	void ServerShield();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShieldHit(FVector_NetQuantize At);

	/** 护盾到期的服务器时间 */
	UPROPERTY(Replicated)
	float ShieldEnd = -100.f;
	/** 画面：盾在显示（含收起的那 0.3 秒）、展开 / 收起开始的时间、最近四次受击（盾面 UV 和时间，轮流写） */
	bool bShieldShown = false;
	bool bShieldClosing = false;
	float ShieldShownAt = 0.f;
	float ShieldClosedAt = 0.f;
	FLinearColor ShieldHits[4] = {FLinearColor(0.f, 0.f, -100.f, 0.f), FLinearColor(0.f, 0.f, -100.f, 0.f), FLinearColor(0.f, 0.f, -100.f, 0.f),
		FLinearColor(0.f, 0.f, -100.f, 0.f)};
	int32 ShieldHitNext = 0;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	/** 盾面材质 M_ShieldFX（蜂窝、展开、受击波纹都在材质里） */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShieldMID;

	UPROPERTY()
	TObjectPtr<UAudioComponent> ShieldHum;

	// ---- EMP（被打中的一方）
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastEmp(float Duration);
	float EmpUntil = -100.f;
	float EmpStart = -100.f;
	/** 中招机体身上爬动的细电弧：每道连着两根骨头附近的点（相对骨头的偏移，跟着动作走），活 0.08–0.22 秒，
	 *  折线每 0.05 秒重新抖一次；画成一串细圆柱（ISM，绝对坐标，骨骼定稿后每帧重摆） */
	struct FEmpArc
	{
		int32 BoneA = INDEX_NONE;
		int32 BoneB = INDEX_NONE;
		FVector OffA = FVector::ZeroVector;
		FVector OffB = FVector::ZeroVector;
		float Die = 0.f;
		int32 Seed = 0;
	};
	TArray<FEmpArc> EmpArcList;
	TArray<int32> EmpBones;
	float EmpJagTimer = 0.f;
	int32 EmpJagSeed = 0;
	void UpdateEmpArcs(float DeltaTime);

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> EmpArcs;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> EmpArcMaterial;

	UPROPERTY()
	TObjectPtr<class UPointLightComponent> EmpLight;

	// ---- 钩爪
	struct FTraceResult
	{
		bool bWorld = false;
		FVector Point = FVector::ZeroVector;
		FVector Normal = FVector::UpVector;
		AMechCharacter* Mech = nullptr;
		float Dist = 0.f;
	};
	FTraceResult TraceGrapple(const FVector& Origin, const FVector& Dir) const;
	/** 救队友：准星附近能勾到的队友（小锥角、射程内、中间没有墙），没有返回空 */
	AMechCharacter* FindAllyTarget(const FVector& Origin) const;
	bool AllyReachable(const AMechCharacter* Ally, const FVector& Origin, float Range) const;
	/** 机体画面上的胸口（按网格算，不按胶囊：别人的机体在本机是网格平滑追着胶囊） */
	static FVector BodyPoint(const AMechCharacter* M);
	/** 抓住地形后自己被拉到哪（取整到厘米，客户端和服务器算出来一模一样）、拉多久（毫秒） */
	void PullTarget(const FTraceResult& Hit, const FVector& Origin, FVector& OutTarget, uint16& OutMs) const;
	void FireGrappleAuthority(const FVector& Origin, const FVector& Dir, uint8 Stamp, bool bClientWorld, const FVector& ClientTarget, uint16 ClientMs,
		AMechCharacter* Ally = nullptr);
	void TickGrappleAuthority();
	void TickGrapplePredicted();
	/** 根运动：把自己 / 被拉的人拉到 Target，Ms 毫秒 */
	static void ApplyPull(AMechCharacter* Who, FName Name, const FVector& Target, uint16 Ms, float FinishSpeed);
	static void RemovePull(AMechCharacter* Who, FName Name);
	void SetRep(EMechGrapple State);
	/** 本地画面状态切换（爪子、缆绳、声音） */
	void EnterVis(EMechGrapple State, const FVector& Tip, AMechCharacter* Victim, float FlightTime);
	void UpdateVisuals(float DeltaTime);
	void UpdateAimHint();

	UFUNCTION(Server, Reliable)
	void ServerFireGrapple(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, uint8 Stamp, bool bWorld, FVector_NetQuantize Target, uint16 Ms,
		AMechCharacter* Ally);

	/** 服务器不认这一发（冷却没好之类）/ 中途被打断：操控的客户端撤掉自己预测的拉动 */
	UFUNCTION(Client, Reliable)
	void ClientCancelGrapple(uint8 Stamp);

	/** 被钩爪拉：对方那一端按同一个目标、同样时长拉（bRescue：队友救出来的，本地先解开网的移动限制，不等复制） */
	UFUNCTION(Client, Reliable)
	void ClientApplyDrag(FVector_NetQuantize Target, uint16 Ms, bool bRescue);

	UFUNCTION()
	void OnRep_Grapple();

	UPROPERTY(ReplicatedUsing=OnRep_Grapple)
	FMechGrappleRep Grapple;

	// ---- 捕获网
	void FireNetAuthority(const FVector& Origin, const FVector& Dir);

	UFUNCTION(Server, Reliable)
	void ServerFireNet(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir);

	/** 发射的枪口焰和声音（Type = EMechShoulder） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastLaunchFX(FVector_NetQuantize At, FVector_NetQuantizeNormal Dir, uint8 Type);

	UFUNCTION()
	void OnRep_Netted();
	void ApplyNetLock();
	void ReleaseNet();

	UPROPERTY(ReplicatedUsing=OnRep_Netted)
	bool bNetted = false;

	/** 网住的开始 / 结束服务器时间 */
	UPROPERTY(Replicated)
	float NetStart = 0.f;

	UPROPERTY(Replicated)
	float NetEnd = 0.f;

	UPROPERTY()
	TObjectPtr<AMechNet> HeldNet;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastStruggle();

	// ---- 共用状态
	UPROPERTY(Replicated)
	float Cooldown = 0.f;
	/** 冷却总长（就绪程度用） */
	UPROPERTY(Replicated)
	float CooldownLength = 1.f;

	/** 本地画面用的钩爪状态（服务器 / 操控那一端自己推进，别人按复制来的） */
	struct FVis
	{
		EMechGrapple State = EMechGrapple::Idle;
		FVector Tip = FVector::ZeroVector;
		TWeakObjectPtr<AMechCharacter> Victim;
		float Start = 0.f;
		float FlightTime = 0.f;
		FVector From = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
	};
	FVis Vis;

	// 服务器（和预测的客户端）推进钩爪用
	uint8 LocalStamp = 0;
	/** 抓住地形后拉自己的目标和时长；预测：拉之前飞行的剩余时间 */
	bool bPendingPull = false;
	FVector PullTo = FVector::ZeroVector;
	uint16 PullMs = 0;
	float PhaseEnd = 0.f;
	bool bPredicting = false;

	int32 AimHint = 0;
	TWeakObjectPtr<AMechCharacter> AllyAim;
	float LastFireTime = -100.f;
	float LastStruggleTime = -100.f;

	// ---------------------------------------------------------------- 表现
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Cable;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CableMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CableMaterial;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> LaunchFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkFX;

	UPROPERTY()
	TObjectPtr<USoundBase> GrappleFireSound;

	UPROPERTY()
	TObjectPtr<USoundBase> GrappleCableSound;

	UPROPERTY()
	TObjectPtr<USoundBase> GrappleLatchSound;

	UPROPERTY()
	TObjectPtr<USoundBase> GrappleReelSound;

	UPROPERTY()
	TObjectPtr<USoundBase> GrappleReleaseSound;

	UPROPERTY()
	TObjectPtr<USoundBase> NetFireSound;

	UPROPERTY()
	TObjectPtr<USoundBase> NetStruggleSound;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ShieldDomeMesh;

	/** 盾面材质（代码里直接挂，不依赖网格资源里存的材质槽） */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShieldMaterial;

	UPROPERTY()
	TObjectPtr<USoundBase> ArtilleryFireSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ArtilleryReadySound;

	UPROPERTY()
	TObjectPtr<USoundBase> LauncherPopSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ShieldUpSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ShieldHitSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ShieldDownSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ShieldHumSound;

	UPROPERTY()
	TObjectPtr<USoundBase> EmpZapSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	void PlayAt(USoundBase* Sound, const FVector& At, float Volume = 1.f, float Pitch = 1.f) const;
};
