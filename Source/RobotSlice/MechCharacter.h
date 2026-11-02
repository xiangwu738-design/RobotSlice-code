// HX-07 机体角色：在模板第三人称角色上加高机动——地面冲刺、空中滑翔推进、按住跳跃向上喷射、快速闪避，
// 四者共用一条能量条。外观是绑在模板人形骨架上的 v3 机体，整体放大到约 2.4 米。
// 战斗：耐久（装甲 + 失衡）在 UMechHealthComponent，武器和锁定在 UMechCombatComponent；
// 这里负责输入、受击反应（上身一抖、镜头震动、顿帧）、失衡和击毁的表现、队伍颜色，以及给 AI 用的操作接口。

#pragma once

#include "CoreMinimal.h"
#include "RobotSliceCharacter.h"
#include "MechTypes.h"
#include "MechEffectComponent.h"
#include "MechCharacter.generated.h"

class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
class UStaticMesh;
class UPointLightComponent;
class UAudioComponent;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;
class UMechHealthComponent;
class UMechCombatComponent;
class UMechEffectComponent;
class UMechMeshComponent;
class UMechShoulderComponent;
class UMechTacticalComponent;
class UMechRunComponent;
class USkeletalMesh;
enum class EMechMaker : uint8;  // MechCatalog.h

UCLASS()
class AMechCharacter : public ARobotSliceCharacter
{
	GENERATED_BODY()

public:
	AMechCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 机动（冲刺 / 喷射 / 快速闪避 / 能量）都在移动组件里，联机可预测 */
	class UMechMovementComponent* GetMechMovement() const;

	float GetEnergy() const;
	float GetMaxEnergy() const;
	bool IsBoosting() const;
	bool IsThrustingUp() const;
	bool IsOverheated() const;

	/** 移动组件真正执行了一次快速闪避（声音、喷焰、闪避动作）；Dir 是闪避方向（世界水平） */
	void OnQuickBoostExecuted(const FVector& Dir);
	/** 移动组件真正执行了一次蹬墙跳（蹬腿动作、声音）；Normal 是墙面朝外的方向（世界水平） */
	void OnWallJumpExecuted(const FVector& Normal);
	/** 离上一次蹬墙跳过了多久（秒，动作用） */
	float GetSinceWallKick() const;

	/** 键位（设置里的 MechKeys）写进所有机体共用的按键映射（类默认对象上那一份），已经在用的本机玩家马上生效；
	 *  模板 IMC_Default 里的键盘移动 / 跳跃映射拿掉（改键后旧键不能还管用），手柄映射不变。键位没变时什么都不做 */
	static void ApplyKeyBindings();

	/** 动画每帧来取的动作层（攻击 / 闪避 / 失衡 / 拔刀收刀 / 格挡 / 持刀待机） */
	void GetActionAnim(struct FMechActionAnim& Out) const;
	/** 模型查看用：直接播一次闪避 / 失衡（Dir 是机体坐标里的方向） */
	void DebugDodge(const FVector& LocalDir);
	void DebugStagger(const FVector& LocalFrom);

	UMechHealthComponent* GetHealth() const { return Health; }
	/** 插件效果（各处数值倍数） */
	UMechEffectComponent* GetEffects() const { return Effects; }
	/** 插件效果对某个数值的倍数（没有效果组件时 1） */
	float EffectScale(EMechStat Stat) const;
	UMechCombatComponent* GetCombat() const { return Combat; }
	/** 肩部武器（钩爪、捕获网；导弹在 Combat 里）和被网住的状态 */
	UMechShoulderComponent* GetShoulder() const { return Shoulder; }
	/** 肩部装备里紧急脱离 / 弹射升空 / 震荡波 / 地雷这四件的实现（11-01 并入肩部，配装是别的时候不工作） */
	UMechTacticalComponent* GetTactical() const { return Tactical; }
	/** 保卫战的肉鸽成长（11-01，MechRun.h）：组件在玩家控制器上（重生不丢），绑到当前机体。对战、AI、别人的机体上永远是空，钩子判空后什么都不做 */
	UMechRunComponent* GetRun() const { return Run.Get(); }
	void SetRun(UMechRunComponent* InRun);
	/** 紧急脱离 / 弹射升空用（各端各自调）：推进器猛喷一下（喷焰、推进灯按 Strength 亮起再衰减，和别人机体的快速闪避闪光同一个量）；
	 *  DodgeWorldDir 非零时顺便播一次闪避动作（世界水平方向；不叫 DodgeDir，那是成员名） */
	void PlayThrusterBurst(float Strength, const FVector& DodgeWorldDir = FVector::ZeroVector);
	/** 被捕获网罩住：动不了、不能攻击 */
	bool IsNetted() const;
	/** 被 EMP 打中：推进器失灵、锁定失效 */
	bool IsEmped() const;
	/** 肩部武器就绪程度 0–1、还要冷却几秒（按配装取导弹或别的） */
	float GetShoulderReady() const;
	float GetShoulderTimer() const;
	/** 钩爪的爪子（只有配装是钩爪时显示，肩部组件每帧摆） */
	UStaticMeshComponent* GetClawMesh() const { return ClawMesh; }
	/** 队伍发光色（网、发光件） */
	static FLinearColor TeamGlowColor(int32 InTeam);
	/** 模型查看（-MechViewer）：机体原地不动，鼠标绕着看、滚轮缩放，数字键切动作；动画按下面的假状态播 */
	bool IsViewerMode() const { return bViewer; }
	bool IsViewerHelpShown() const { return bViewerHelp; }
	FString GetViewerStatus() const;
	bool bAnimOverride = false;
	FVector AnimOverrideVelocity = FVector::ZeroVector;  // 世界坐标，厘米/秒
	bool bAnimOverrideAir = false;
	bool bAnimOverrideBoost = false;

	/** 机体和武器用到的特效（开局预热用） */
	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;
	UMechMeshComponent* GetMechMesh() const;

	int32 GetTeam() const { return Team; }
	void SetTeam(int32 InTeam);

	/** 显示名（蓝1、红2 之类，HUD 和通报用） */
	UPROPERTY(Replicated)
	FString DisplayName;

	/** 11-02 这台人机单独的强度（房主在房间里给每台定，0 新兵…3 王牌）；-1 = 按 GameState::AILevel。要在 AI 控制器接管之前设（FinishSpawning 前），AMechAIController::OnPossess 读 */
	int32 AILevelOverride = -1;

	/** 回合进行中、自己没被击毁才能行动 */
	bool CanAct() const;

	/** 第一人称（机体自己的眼睛）：V 切换，默认按设置；只影响本机画面 */
	bool IsFirstPerson() const { return bFirstPerson; }
	/** 启动画面进行到第几秒（出生、重生、切到第一人称时开始；<0 没在启动）和总长 */
	float GetBootTime() const;
	float GetBootLength() const { return BootLength; }

	/** 夺芯：身上带着核心（不能飞、不能快速闪避、推进减半、不能拔刀），服务器设，复制给各端 */
	bool IsCarryingCore() const { return bCarryingCore; }
	void SetCarryingCore(bool bCarrying);

	// ---------------------------------------------------------------- 配装（目录见 MechLoadout；框架、推进器的数值在 ApplyLoadout）
	/** 服务器：换一套配装，数值马上生效（复制给各端）；bRefill 时装甲、能量回满 */
	void SetLoadout(const FMechLoadout& InLoadout, bool bRefill);
	const FMechLoadout& GetLoadout() const { return Loadout; }
	/** 同厂加成（11-01）：这一类配装（MechLoadout::ECategory）和框架是不是同一家造的 */
	bool IsOwnMaker(int32 Category) const;
	/** 投掷匕首近距离击退（11-01）：叠加到这一下的普通击退上，不能再调一次 LaunchCharacter：引擎的 Launch 是覆盖，同一帧后一次会把前一次吃掉 */
	void AddKnifePush(const FVector& Push);
	/** 框架是哪家造的（10-31 阵营机体；以后 HUD 按制造商换主题用这个）、哪一档（轻 / 中 / 重，数值按它） */
	EMechMaker GetFrameMaker() const;
	EMechFrameTier GetFrameTier() const { return MechFrameTier(Loadout.Frame); }
	/** 涂装（服务器设，同步给所有人） */
	void SetPaint(const FMechPaint& InPaint);
	const FMechPaint& GetPaint() const { return Paint; }
	/** 把涂装刷到网格上（主菜单展示机体也用） */
	void ApplyPaint();
	/** 测试：和按 E / 松开 E 走同一条路 */
	void MissilePressedForTest() { MissilePressed(); }
	void MissileReleasedForTest() { MissileReleased(); }
	/** 主菜单展示：收起手上的枪，竖大拇指 */
	void SetShowcasePose(bool bOn);
	/** 结算场景（MechResultsStage）的展示姿势：bThumbs 竖大拇指（枪不藏，收到背上 / 腰两侧，配装照样看得见）；Lean 上身前倾（度，负 = 挺胸）；
	 *  GunDown 手里端着长枪时枪口往下压多少度（低姿持枪，枪托留在肩窝） */
	void SetResultsPose(bool bThumbs, float Lean, float GunDown = 35.f);
	bool bResultsStow = false;
	float ResultsLean = 0.f;
	/** 10-31 展示姿势（结算场景、主菜单；SetResultsPose / SetShowcasePose 打开）：不交战、上身不去对枪管 / 不在模型空间立住、没有后坐 / 中弹 / 失衡，
	 *  上身转正（UMechMeshComponent::ShowSquare）；竖拇指时上身用待机动作（不叠端枪片段），端长枪的按 ShowGunDown 低姿端着 */
	bool bDisplayPose = false;
	float ShowGunDown = 35.f;

	// ---------------------------------------------------------------- 测试场的木桩
	/** 服务器：变成木桩（不动、不还手、打不死，停手 4 秒回满并回到原位） */
	void MakeTrainingDummy();
	bool IsTrainingDummy() const { return bTrainingDummy; }
	/** 木桩读数（各端按收到的命中自己记）：上一击、这一轮总伤害、每秒伤害（停手 3 秒算新一轮） */
	float GetDummyLastHit() const { return DummyLastHit; }
	float GetDummyStreakDamage() const { return DummyStreakDamage; }
	float GetDummyDPS() const;
	float GetDummyLastHitTime() const { return DummyLastHitTime; }

	/** 服务器上结算一次命中，返回实际伤害 */
	/** 子弹从 From 沿 Dir 打进胶囊后，离哪段骨头最近就算打中哪个部位（头 / 躯干 / 四肢） */
	EMechHitPart ClassifyHit(const FVector& From, const FVector& Dir) const;

	/** StaggerScale < 0 按命中来源默认（刀 0.55、其他 1）；各把刀、飞刀自己给 */
	float ReceiveHit(float Damage, float Impact, AActor* Attacker, const FVector& At, EMechHitKind Kind, float StaggerScale = -1.f);

	/** 顿帧：自己这台机体短暂几乎停住 */
	void HitStop(float Seconds);

	/** 镜头震动（只对本地玩家有效） */
	void AddRecoil(float Amount);
	/** 打击感（只对本机玩家的镜头）：镜头顺着 Dir（世界方向）被推一下再弹回，Strength 0~1；Pitch 是镜头上抬的角速度冲量（开枪后坐）；
	 *  Strength 大时视野一缩、画面边缘色散一闪 */
	void CameraKick(const FVector& Dir, float Strength, float Pitch = 0.f);
	/** 整台机体闪一下（受击闪光，盖一层叠加材质；各端各自闪） */
	void FlashBody(const FLinearColor& Color, float Strength);

	/** 最近一次受击的时间（HUD 红边） */
	float GetLastDamagedTime() const { return LastDamagedTime; }
	/** 最近一次受击点相对机体的位置（HUD 画受击方向） */
	FVector GetLastHitFrom() const { return LastHitFrom; }
	/** 最近一次打中我的是谁（受击方向弧每个来源一条） */
	AActor* GetLastHitAttacker() const { return LastHitAttacker.Get(); }
	/** 拼刀被弹开（11-01，各端在组播里调）：放一小段受击后退动作，WorldFrom 是对面那台的方向；比失衡的 1.2 秒短，出刀时立刻让位 */
	void PlayClashRecoil(const FVector& WorldFrom);

	// ---------------------------------------------------------------- 给 AI 的操作接口
	void SetBoostHeld(bool bHeld);
	void QuickBoostToward(const FVector& Dir);
	void SetJumpThrust(bool bHeld);
	/** 鼠标 / 手柄转镜头：乘设置里的灵敏度、反转 Y；右键瞄准时视野收窄，转速跟着降 */
	virtual void DoLook(float Yaw, float Pitch) override;
	/** 挥刀到可取消时刻前不吃移动输入：刀砍出去人就钉在那，不会边砍边飘 */
	virtual void DoMove(float Right, float Forward) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** 游戏里整体放大的倍数（机体按 1.8 米的人形骨架做，乘上它约 2.4 米） */
	UPROPERTY(EditAnywhere, Category="Mech|Scale")
	float RobotScale = 1.35f;

	UPROPERTY(EditAnywhere, Category="Mech|Camera")
	float BaseFOV = 90.f;

	UPROPERTY(EditAnywhere, Category="Mech|Camera")
	float BoostFOV = 104.f;

	/** 画面问题复现测试（-MechFallTest / -MechTracerTest，MechVisualTest.cpp）要按跳跃、闪避、改镜头 */
	friend struct FMechVisualTest;

	/** 11-02 老街循环（MechLoop.h）：移动组件刚把这台挪了一个周期（Shift 厘米，沿 X）；镜像胶囊跟着摆 */
	void OnLoopWrapped(float Shift);

private:
	/** 老街循环：±周期 处和本体一样的胶囊（属于这台机体，子弹 / 刀 / 爆炸 / 别的机体碰到它 = 碰到这台）；不循环的图上没有 */
	UPROPERTY()
	TArray<TObjectPtr<class UCapsuleComponent>> LoopCapsules;
	void UpdateLoopCapsules();

	void BoostStarted();
	void BoostEnded();
	void QuickBoost();
	void JumpPressed();
	void JumpReleased();
	void FirePressed();
	void FireReleased();
	void MissilePressed();
	void MissileReleased();
	void SlashPressed();
	void AltReleased();
	void StancePressed();
	void ReloadPressed();
	/** 记一次闪避 / 失衡的开始和方向（动作用） */
	void StartDodgeAnim(const FVector& WorldDir);
	void StartStaggerAnim(const FVector& WorldFrom);

	void UpdateThrusterFX(float DeltaSeconds);
	void UpdatePose(float DeltaSeconds);
	void UpdateCameraShake(float DeltaSeconds);

	void OnHealthHit(float Damage, float Impact, AActor* Attacker, FVector At, bool bDirect);
	void OnHealthStaggered();
	void OnHealthDied(AActor* Killer);
	void ApplyTeamColor();

	UFUNCTION()
	void OnRep_Team();

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastQuickBoostFX(FVector_NetQuantizeNormal Dir);

	/** 蹬墙跳：别人的机体在本机也播蹬腿、出声（自己操控的那台本地预测时已经播过） */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastWallJumpFX(FVector_NetQuantizeNormal Normal);
	void PlayWallJumpFX(const FVector& Normal);
	float WallKickStart = -100.f;

	/** 按当前键位重建 MechMappingContext（只在类默认对象上调） */
	void BuildKeyMappings();

	/** 空中动作检查（-MechAirTest，测试场 ?Range）：空中滑翔 / 喷射 / 下落时松方向键、反向急停，每段侧面连拍到 Saved/Screenshots/Air，
	 *  日志 [airtest]；-MechAnimLog 另外每 0.1 秒打一行动画各层权重 [animlog]。最后退出 */
	void AirTestTick(float DeltaSeconds);
	bool bAirTest = false;
	/** 蹬墙跳检查（-MechWallTest，测试场 ?Range）：冲向 100 米处的背景墙，贴墙蹬、同一面墙再蹬（应被拒）、落地后再蹬，日志 [walltest]；
	 *  联机时作为客户端跑会记纠正次数 */
	void WallTestTick(float DeltaSeconds);
	bool bWallTest = false;
	float WallTestClock = 0.f;
	int32 WallTestStep = 0;
	int32 WallTestKicks = 0;
	int32 WallTestCorrections0 = 0;

	/** 自动演示（命令行 -MechDemo）：按固定脚本走、冲刺、喷射、闪避，并定时截图，最后退出。用于无人值守测试。 */
	void DemoTick(float DeltaSeconds);

	/** 成图模式（命令行 -MechBeauty）：依次切到几个预设机位，摆好机体，隐藏 HUD 截图，最后退出 */
	void BeautyTick(float DeltaSeconds);

	/** 动作检查（-MechAnimTest）：机体朝向固定、镜头从侧面看，依次站立 / 走 / 跑 / 左右横移 / 后退 / 斜向 / 冲刺滑行 / 跳跃下落 / 落地，
	 *  每段连拍 4 帧到 Saved/Screenshots/Anim，拼成对照表看步态；最后退出 */
	void AnimTestTick(float DeltaSeconds);

	/** 模型查看：按键见 HUD；-MechViewerShots 自动轮换动作、转镜头、每个动作截一张，跑完退出 */
	void ViewerTick(float DeltaSeconds);

	UPROPERTY(VisibleAnywhere, Category="Mech")
	TObjectPtr<UMechHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, Category="Mech")
	TObjectPtr<UMechCombatComponent> Combat;

	UPROPERTY(VisibleAnywhere, Category="Mech")
	TObjectPtr<UMechEffectComponent> Effects;

	UPROPERTY(VisibleAnywhere, Category="Mech")
	TObjectPtr<UMechShoulderComponent> Shoulder;

	UPROPERTY(VisibleAnywhere, Category="Mech")
	TObjectPtr<UMechTacticalComponent> Tactical;

	TWeakObjectPtr<UMechRunComponent> Run;

	UPROPERTY(VisibleAnywhere, Category="Mech|Weapons")
	TObjectPtr<UStaticMeshComponent> RifleMesh;

	UPROPERTY(VisibleAnywhere, Category="Mech|Weapons")
	TObjectPtr<UStaticMeshComponent> BladeMesh;

	/** 背后的肩部武器挂件：导弹巢 / 钩爪发射器 / 捕获网发射器（按配装换模型） */
	UPROPERTY(VisibleAnywhere, Category="Mech|Weapons")
	TObjectPtr<UStaticMeshComponent> PodMesh;

	UPROPERTY(VisibleAnywhere, Category="Mech|Weapons")
	TObjectPtr<UStaticMeshComponent> ClawMesh;

	/** 肩部挂件（浮空无人机机身）的模型（按 EMechShoulder 编号）：导弹巢、火炮、护盾发生器、钩爪发射器、捕获网发射器、烟雾弹发射器、EMP 发射器、
	 *  推进包（紧急脱离）、弹射器（弹射升空）、环形发生器（震荡波）、布雷器（地雷） */
	UPROPERTY()
	TObjectPtr<UStaticMesh> ShoulderMeshes[11];

	/** 副手：双持手枪的左手枪、双刀的左手刀（配装没选时隐藏） */
	UPROPERTY(VisibleAnywhere, Category="Mech|Weapons")
	TObjectPtr<UStaticMeshComponent> OffGunMesh;

	UPROPERTY(VisibleAnywhere, Category="Mech|Weapons")
	TObjectPtr<UStaticMeshComponent> OffBladeMesh;

	UPROPERTY()
	TObjectPtr<UInputAction> BoostAction;

	UPROPERTY()
	TObjectPtr<UInputAction> QuickBoostAction;

	UPROPERTY()
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY()
	TObjectPtr<UInputAction> MissileAction;

	UPROPERTY()
	TObjectPtr<UInputAction> SlashAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ReloadAction;


	/** 空格（跳跃 + 按住喷射）：模板的跳跃动作按下后下一帧就算松开、而且空格被这里的映射优先吃掉，所以跳跃也由它触发 */
	UPROPERTY()
	TObjectPtr<UInputAction> JumpHoldAction;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> MechMappingContext;

	/** Q：射击 / 近战切换 */
	UPROPERTY()
	TObjectPtr<UInputAction> StanceAction;

	// 动作层状态
	float DrawBlend = 0.f;
	float DodgeStart = -100.f;
	int32 DodgeDir = 0;
	float StaggerStart = -100.f;
	/** 这一段受击动作放多久（失衡、弹反 1.2 秒；拼刀短，见 PlayClashRecoil） */
	float StaggerAnimLen = 1.2f;
	int32 StaggerDir = 0;
	FVector LastHitFrom = FVector::ForwardVector;
	TWeakObjectPtr<AActor> LastHitAttacker;

	UPROPERTY(ReplicatedUsing=OnRep_Loadout)
	FMechLoadout Loadout;

	UPROPERTY(ReplicatedUsing=OnRep_Paint)
	FMechPaint Paint;

	UFUNCTION()
	void OnRep_Paint();

	UFUNCTION()
	void OnRep_Loadout();
	void ApplyLoadout();
	/** 按框架换机体网格（MechCatalog::FrameMesh，10-31 阵营机体）：各端按复制下来的配装各自换；换了以后清掉旧网格上的动态材质、
	 *  重新挂喷焰（每台喷口不同）、重新刷涂装 / 队伍色，网格组件的骨头索引和道具挂点在下一次骨骼定稿时重取 */
	void ApplyFrameMesh();
	/** 喷焰和推进器灯挂到 spine_05 上这台机体的主喷口（BeginPlay 和换网格后调） */
	void SetupThrusterNozzles();
	/** 上一次按框架设好的网格（判断要不要重新挂喷焰 / 刷材质；结算展示会先自己设网格再 SetLoadout） */
	const USkeletalMesh* AppliedFrameMesh = nullptr;
	/** 喷焰锥体底面的粗细（世界尺寸缩放，v3 喇叭口半径 4.1 厘米时 0.10，按每台的喇叭口半径换算） */
	float FlameWidth = 0.10f;
	/** 被击退的倍数（框架：轻型 ×1.3、重型 ×0.6） */
	float KnockbackScale = 1.f;

	UPROPERTY(Replicated)
	bool bTrainingDummy = false;

	UPROPERTY(ReplicatedUsing=OnRep_CarryingCore)
	bool bCarryingCore = false;

	// 第一人称
	void UpdateFirstPerson(float DeltaSeconds);
	void SetFirstPerson(bool bOn, bool bReboot);
	bool bFirstPerson = false;
	bool bViewInit = false;
	/** 瞄准粘滞：准星压在敌人身上时转视角变慢（本机玩家每帧算，DoLook 用） */
	float AimFriction = 1.f;
	/** 摆姿势用的瞄准方向（平滑过的） */
	FVector PoseAim = FVector::ForwardVector;
	void UpdateAimFriction();
	/** 第一人称时自己的机体、枪在自己画面里藏着（换回第三人称时恢复一次） */
	bool bHeadHidden = false;
	float BootStart = -100.f;
	float BootLength = 3.f;
	float StepPhase = 0.f;
	FVector EyeLocal = FVector(20.f, 0.f, 95.f);

	UFUNCTION()
	void OnRep_CarryingCore();
	void ApplyCarryRestrictions();

	/** 玩家按 G 把核心扔出去（服务器上交给夺芯规则） */
	UFUNCTION(Server, Reliable)
	void ServerThrowCore(FVector_NetQuantizeNormal Aim);
	FTransform DummyHome;
	float DummyLastHit = 0.f;
	float DummyStreakDamage = 0.f;
	float DummyStreakStart = -100.f;
	float DummyLastHitTime = -100.f;

	UPROPERTY(VisibleAnywhere, Category="Mech|FX")
	TObjectPtr<UStaticMeshComponent> ThrusterLeft;

	UPROPERTY(VisibleAnywhere, Category="Mech|FX")
	TObjectPtr<UStaticMeshComponent> ThrusterRight;

	UPROPERTY(VisibleAnywhere, Category="Mech|FX")
	TObjectPtr<UPointLightComponent> ThrusterLight;

	/** 只照机体自己的补光（光照通道 1），夜里也看得清机体，不影响场景 */
	UPROPERTY(VisibleAnywhere, Category="Mech|FX")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(VisibleAnywhere, Category="Mech|FX")
	TObjectPtr<UAudioComponent> BoostAudio;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DeathFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DeathShockFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> StaggerFX;

	UPROPERTY()
	TObjectPtr<USoundBase> DeathSound;

	UPROPERTY()
	TObjectPtr<USoundBase> StaggerSound;

	UPROPERTY()
	TObjectPtr<USoundBase> QuickBoostSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(ReplicatedUsing=OnRep_Team)
	int32 Team = 0;
	/** 这一帧收到的匕首近距离击退：普通击退和它谁先到都要叠上（联机时两个组播的先后不定） */
	FVector KnifePush = FVector::ZeroVector;
	uint64 KnifePushFrame = 0;

	/** 服务器上的能量，同步给操控这台机体的客户端（偏差大了就以服务器为准） */
	UPROPERTY(Replicated)
	float NetEnergy = 100.f;

	/** 推进状态（第 1 位冲刺、第 2 位喷射），同步给其他客户端用来显示喷焰 */
	UPROPERTY(Replicated)
	uint8 MobilityFX = 0;

	float ThrustFX = 0.f;
	/** 两个喷口（0 左 1 右）在 spine_05 骨头坐标里的位置和喷射方向（BeginPlay 按网格绑定姿势算好；喷焰挂在这根骨头上，
	 *  长度变了以后按它把锥体底面摆回喷口）。bThrusterOnBone = false 时是 -MechThrusterOld 的旧挂法 */
	FVector NozzleOnBone[2] = {FVector::ZeroVector, FVector::ZeroVector};
	FVector NozzleDirOnBone[2] = {FVector(0.f, 0.f, -1.f), FVector(0.f, 0.f, -1.f)};
	bool bThrusterOnBone = false;
	/** 引擎圆锥网格底面在自己坐标里的 Z（尖端朝 +Z） */
	float FlameBaseZ = -50.f;
	/** 模型查看拍推进器（-MechViewerShots=thrust）：不管动作，喷焰一直开着 */
	bool bViewerForceThrust = false;
	/** 别人的机体快速闪避时的喷焰闪光（自己的看移动组件） */
	float RemoteQuickFlash = 0.f;

	// 受击和姿势
	FVector Flinch = FVector::ZeroVector;
	float StaggerBlend = 0.f;
	float Trauma = 0.f;
	float LastDamagedTime = -100.f;
	FVector BaseSocketOffset = FVector::ZeroVector;
	// 镜头冲击：位置和角度各一个阻尼弹簧，视野收缩、色散随时间衰减
	FVector KickOffset = FVector::ZeroVector;
	FVector KickOffsetVel = FVector::ZeroVector;
	FVector KickRot = FVector::ZeroVector;     // 俯仰、偏航、翻滚（度）
	FVector KickRotVel = FVector::ZeroVector;
	float FovPunch = 0.f;
	float AppliedFovPunch = 0.f;
	float Fringe = 0.f;
	// 受击闪光
	UPROPERTY()
	TObjectPtr<UMaterialInterface> HitFlashMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HitFlashMID;
	/** 推进器喷焰用的叠加发光材质（光束材质 M_MechBeam，自带白天曝光补偿），亮度随推力 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlameMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FlameMID;
	float FlashLevel = 0.f;
	void UpdateFlash(float DeltaSeconds);
	FTimerHandle HitStopTimer;
	bool bWrecked = false;

	bool bDemo = false;
	bool bAnimTest = false;
	bool bViewer = false;
	bool bViewerAuto = false;
	bool bViewerHelp = true;
	bool bViewerPaused = false;
	bool bViewerNight = false;
	bool bViewerFill = true;
	bool bViewerWeaponsOnly = false;
	/** 长枪双手端（有步枪包动作）淡入的程度 */
	float TwoHandBlend = 0.f;
	/** 端枪交战（0–1）：给动作层，上身立住端枪、全身动作在上身让开（FMechActionAnim::GunAim） */
	float GunAimLock = 0.f;
	/** 开枪后坐：上身弹簧（世界轴 × 角度，度）和整机后仰（度）；LastRecoilShot 是已经处理过的那一发 */
	FVector Recoil = FVector::ZeroVector;
	FVector RecoilVel = FVector::ZeroVector;
	float BodyRock = 0.f;
	float BodyRockVel = 0.f;
	float LastRecoilShot = -100.f;
	/** 狙击枪开镜：自己的机体不画（挡视线） */
	bool bScopeHidden = false;
	EVisibilityBasedAnimTickOption TickOptionBeforeScope = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	int32 ViewerAction = 0;
	int32 ViewerFrame = 0;
	int32 ViewerSpeedIndex = 3;
	float ViewerArm = 650.f;
	float ViewerClock = 0.f;
	float ViewerYaw = 0.f;
	float DemoTime = 0.f;
	float DemoYaw = 0.f;
	int32 DemoShot = 0;

	bool bBeauty = false;
	bool bBeautyShotTaken = false;
	float BeautyTime = 0.f;
	int32 BeautyIndex = -1;
	/** 没有专门机位表的地图：成图机位都相对开局出生点 */
	FVector BeautySpawn = FVector::ZeroVector;
	float BeautySpawnYaw = 0.f;
};
