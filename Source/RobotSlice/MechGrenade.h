// 肩部武器的弹：火炮炮弹（直飞、快、大爆炸）、烟雾弹、EMP 弹（抛物线，落地或碰到机体就炸）。
// 和捕获网一样：服务器生成，复制出生位置、初速度、重力、出生时间，各端按同一条弹道自己算位置；只有服务器判碰撞、结算。
//  炮弹：半径 5 米爆炸，中心伤害最高、边上四成，冲击很大（基本一发失衡）。
//  烟雾弹：落点生成一团烟（AMechSmoke），烟里和隔着烟锁不定、AI 看不见。
//  EMP：半径 7 米内的敌人能量清零、4 秒推进器失灵和锁定失效（效果「Emp」），一点伤害。
//       炸开的样子（10-30 第三版，用户试玩：「地面环太规整、特效一般」）：不规则、翻滚的电光球 0.3 秒膨胀到作用半径 → 炸散，电弧甩出去打在
//       范围里的地面上 → 残余细电弧在地上跳到约 1.2 秒（AMechGrenade::TickEmpBurst，一串拉长圆柱的折线，每次形状随机）；地上只留很淡的不规则提示。
//       以下是第二版的说明（M_EmpField 的画法照旧，内容已经换成很淡的斑驳底色 + 抖动的外缘）：
//       作用范围画在地上——一圈蓝色亮环 0.3 秒从炸点扩到 7 米，
//       外缘停一圈细环、范围内地面亮起淡淡的六边形网格，约 1 秒淡掉；范围内地面上细电弧闪几下；空中一层很淡的场（越厚越亮）。
//       亮环 / 网格是「屏幕空间贴花」：一个覆盖范围的球（材质 M_EmpField，tools/shaders/emp_field.hlsl），按场景深度还原出背后的地面 / 墙 / 机体表面，
//       在表面上画环和网格，所以台阶、坡、墙脚都贴得上。亮度按曝光倒数归一（白天夜里屏幕上一样亮），数值压得低（不刺眼，镜头在范围里也不泛白）；
//       不要中心白闪，闪光灯从 40000 降到 5000。中招机体身上的电弧见 UMechShoulderComponent。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MechGrenade.generated.h"

class AMechCharacter;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class USoundBase;
class USoundAttenuation;

UENUM()
enum class EMechGrenadeKind : uint8
{
	Shell,
	Smoke,
	Emp
};

UCLASS()
class AMechGrenade : public AActor
{
	GENERATED_BODY()

public:
	AMechGrenade();

	/** 服务器上、FinishSpawning 之前 */
	void Init(AMechCharacter* InShooter, EMechGrenadeKind InKind, const FVector& Origin, const FVector& Velocity, float InGravity);

	void GetEffectSystems(TArray<UNiagaraSystem*>& Out) const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 数值
	static constexpr float ShellDamage = 1500.f;
	static constexpr float ShellImpact = 900.f;
	static constexpr float ShellRadius = 500.f;
	static constexpr float EmpRadius = 700.f;
	static constexpr float EmpDamage = 150.f;
	static constexpr float EmpImpact = 200.f;
	static constexpr float EmpDuration = 4.f;

protected:
	virtual void BeginPlay() override;

private:
	float ServerNow() const;
	FVector PosAt(float T) const;
	FVector VelAt(float T) const;
	/** 服务器：在 At 炸开（Direct 是直接撞上的那台） */
	void Detonate(const FVector& At, const FVector& Normal, AMechCharacter* Direct);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastDetonate(FVector_NetQuantize At, FVector_NetQuantizeNormal Normal);

	/** EMP 炸开的画面（各端自己放）：开始、每帧推进 */
	void StartEmpBurst(const FVector& At, const FVector& Normal);
	void TickEmpBurst();
	float BurstStart = -1.f;
	FVector BurstAt = FVector::ZeroVector;
	FVector BurstNormal = FVector::UpVector;
	float BoltTimer = 0.f;
	int32 BoltSeed = 0;

	/** 范围贴花的球（M_EmpField），覆盖整个 EMP 半径 */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> EmpShell;

	/** 地面上爬的细电弧（一串拉长圆柱） */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> EmpBolts;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ShellMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BoltMID;

	/** 弹刚出现时画面上从本机看到的无人机发射口出，0.2 秒内并回判定弹道（偏移、开始的本地时间） */
	FVector VisOffset = FVector::ZeroVector;
	float VisStart = 0.f;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> EmpShellMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> EmpArcMaterial;

	UPROPERTY(Replicated)
	EMechGrenadeKind Kind = EMechGrenadeKind::Shell;

	UPROPERTY(Replicated)
	FVector_NetQuantize LaunchPos = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector_NetQuantize10 LaunchVel = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float Gravity = 0.f;

	UPROPERTY(Replicated)
	float LaunchTime = 0.f;

	UPROPERTY(Replicated)
	int32 Team = 0;

	UPROPERTY(Replicated)
	TObjectPtr<AMechCharacter> Shooter;

	float PrevT = 0.f;
	bool bDone = false;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	UPROPERTY()
	TObjectPtr<UStaticMesh> ShellMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CanMesh;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> ShellBlastFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> EmpBlastFX;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SparkFX;

	UPROPERTY()
	TObjectPtr<USoundBase> WhistleSound;

	UPROPERTY()
	TObjectPtr<USoundBase> ShellBoomSound;

	UPROPERTY()
	TObjectPtr<USoundBase> EmpBoomSound;

	UPROPERTY()
	TObjectPtr<USoundAttenuation> Attenuation;
};
