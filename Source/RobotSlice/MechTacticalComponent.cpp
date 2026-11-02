#include "MechTacticalComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechEffectComponent.h"
#include "MechHealthComponent.h"
#include "MechMine.h"
#include "MechMovementComponent.h"
#include "MechShoulderComponent.h"
#include "MechLoadout.h"
#include "MechWeapons.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

// 名字带前缀的命名空间（不用匿名命名空间）：将来合并编译（unity）时和别的文件里同名的 Load / 常量不打架
namespace MechTacticalLocal
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	/** 根运动的名字：紧急脱离、弹射升空各一个（两端的根运动按名字、优先级、时长、终点对上） */
	const FName BurstName(TEXT("MechTacBurst"));
	const FName LaunchName(TEXT("MechTacLaunch"));
	/** 被钩爪拉的根运动名字（UMechShoulderComponent 里的 DragName，那边是文件内常量，这里照抄字面量；改名要两边一起改） */
	const FName GrappleDragName(TEXT("MechGrappleDrag"));
	/** 比钩爪（500）高：同时在的时候脱离 / 弹射说了算 */
	constexpr uint16 DashPriority = 600;
	/** 震荡波的颜色：橙白（EMP 是蓝，地雷是暖橙） */
	const FLinearColor ShockColor(1.f, 0.62f, 0.3f);
}

// ---------------------------------------------------------------- 地上的范围圈
UInstancedStaticMeshComponent* MechRangeRing::Create(AActor* Owner, FName Name, UStaticMesh* Cylinder, UMaterialInterface* Material, UMaterialInstanceDynamic*& OutMID)
{
	OutMID = nullptr;
	if (!Owner || !Cylinder || !Owner->GetRootComponent())
	{
		return nullptr;
	}
	// 和充能步枪的激光一样：一串拉长的引擎圆柱（ISM，绝对坐标，每次更新整串重摆）
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
	C->SetStaticMesh(Cylinder);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->bAffectDistanceFieldLighting = false;
	C->bAffectDynamicIndirectLighting = false;
	C->SetUsingAbsoluteLocation(true);
	C->SetUsingAbsoluteRotation(true);
	C->SetUsingAbsoluteScale(true);
	C->SetupAttachment(Owner->GetRootComponent());
	// 上一帧位置自己给（= 这一帧，见 Update）：每次整串重建，不给的话动态模糊 / TSR 拿到的上一帧变换对不上，会拖出一道道虚影（曳光 10-30 踩过）
	C->SetHasPerInstancePrevTransforms(true);
	C->RegisterComponent();
	C->SetWorldTransform(FTransform::Identity);
	C->SetVisibility(false);
	if (Material)
	{
		OutMID = UMaterialInstanceDynamic::Create(Material, Owner);
		C->SetMaterial(0, OutMID);
	}
	return C;
}

void MechRangeRing::Update(UInstancedStaticMeshComponent* Comp, UMaterialInstanceDynamic* MID, const FVector& Center, float Radius, float Intensity, const FLinearColor& Color, bool bDashed)
{
	UWorld* World = Comp ? Comp->GetWorld() : nullptr;
	if (!World || Radius < 1.f)
	{
		Hide(Comp);
		return;
	}
	// 粗细：近处 3 厘米，远了按离镜头的距离加粗（远处也有一两个像素宽，和充能步枪的激光一个思路）
	float Thick = 3.f;
	if (const APlayerController* PC = World->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Thick = FMath::Clamp(static_cast<float>(FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Center)) * 0.0012f, 3.f, 30.f);
	}
	// 圈上的点：每个点从上方 2.5 米往下找地面（只看地形），贴在地面上方 5 厘米；找不到（悬空、平台边外）就按 Center 的高度
	const int32 N = FMath::Clamp(FMath::RoundToInt(Radius / 40.f), 16, 72);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechTacticalRing), false, Comp->GetOwner());
	TArray<FVector> Pts;
	Pts.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const float A = 2.f * PI * i / N;
		FVector P = Center + FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0.f);
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 250.f), P - FVector(0.f, 0.f, 400.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			P = Hit.ImpactPoint + FVector(0.f, 0.f, 5.f);
		}
		Pts.Add(P);
	}
	TArray<FTransform> Xf;
	Xf.Reserve(N);
	const float R = Thick / 50.f;  // 引擎圆柱：半径 50、高 100、原点在中心、沿 Z
	for (int32 i = 0; i < N; ++i)
	{
		if (bDashed && (i % 2) == 1)
		{
			continue;  // 虚线：隔一段空一段
		}
		const FVector A = Pts[i];
		const FVector B = Pts[(i + 1) % N];
		const FVector Seg = B - A;
		const float L = Seg.Size();
		if (L < 0.1f)
		{
			continue;
		}
		Xf.Add(FTransform(FRotationMatrix::MakeFromZ(Seg / L).ToQuat(), (A + B) * 0.5f, FVector(R, R, L / 100.f)));
	}
	Comp->ClearInstances();
	if (Xf.Num())
	{
		Comp->AddInstances(Xf, false, true);
		Comp->BatchUpdateInstancesTransforms(0, Xf, Xf, true, true, true);
	}
	if (MID)
	{
		// 光束材质的参数是 Color / Intensity；两套名字都设（和战斗组件 EnsureBeamMeshes 一样）
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("Emissive"), Color);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		MID->SetScalarParameterValue(TEXT("EmissiveStrength"), Intensity * 3.f);
	}
	if (!Comp->IsVisible())
	{
		Comp->SetVisibility(true);
	}
}

void MechRangeRing::Hide(UInstancedStaticMeshComponent* Comp)
{
	if (Comp && Comp->IsVisible())
	{
		Comp->ClearInstances();
		Comp->SetVisibility(false);
	}
}

// ---------------------------------------------------------------- 组件
UMechTacticalComponent::UMechTacticalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	// 全部是工程里已有的素材（占位，要换的见设计文档）
	CylinderMesh = MechTacticalLocal::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	RingMaterial = MechTacticalLocal::Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_MechBeam"));
	DustFX = MechTacticalLocal::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01"));
	GroundBlastFX = MechTacticalLocal::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_01/Particles/Ex_02_Earth_Explosion/Exp_01"));
	BoostSound = MechTacticalLocal::Load<USoundBase>(TEXT("/Game/Audio/Combat/S_quickboost"));
	PopSound = MechTacticalLocal::Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_launcher_pop"));
	ChargeSound = MechTacticalLocal::Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_ready"));
	BlastSound = MechTacticalLocal::Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_boom"));
	Attenuation = MechTacticalLocal::Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
}

void UMechTacticalComponent::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInstanceDynamic* MID = nullptr;
	Ring = MechRangeRing::Create(GetOwner(), TEXT("TacticalRing"), CylinderMesh, RingMaterial, MID);
	RingMID = MID;
}

void UMechTacticalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMechTacticalComponent, Cooldown, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UMechTacticalComponent, CooldownLength, COND_OwnerOnly);
}

void UMechTacticalComponent::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	Out.AddUnique(DustFX);
	Out.AddUnique(GroundBlastFX);
	if (const AMechMine* Mine = GetDefault<AMechMine>())
	{
		Mine->GetEffectSystems(Out);
	}
}

// ---------------------------------------------------------------- 小工具
AMechCharacter* UMechTacticalComponent::GetMech() const
{
	return Cast<AMechCharacter>(GetOwner());
}

float UMechTacticalComponent::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

float UMechTacticalComponent::BurstDist() const
{
	const AMechCharacter* Mech = GetMech();
	return BurstDistance * (Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnBurstDistance : 1.f);
}

float UMechTacticalComponent::LaunchRise() const
{
	const AMechCharacter* Mech = GetMech();
	return LaunchHeight * (Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnLaunchHeight : 1.f);
}

float UMechTacticalComponent::ShockReach() const
{
	const AMechCharacter* Mech = GetMech();
	return ShockRadius * (Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnShockRadius : 1.f);
}

int32 UMechTacticalComponent::MineLimit() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech && Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnMaxMines : MaxMines;
}

float UMechTacticalComponent::ScaledCooldown(float Base) const
{
	const AMechCharacter* Mech = GetMech();
	return Base * (Mech ? FMath::Min(1.f, Mech->EffectScale(EMechStat::MissileCooldown)) : 1.f);
}

EMechShoulder UMechTacticalComponent::GetType() const
{
	const AMechCharacter* Mech = GetMech();
	return Mech ? Mech->GetLoadout().Shoulder : EMechShoulder::Missiles;
}

float UMechTacticalComponent::BaseCooldown(EMechShoulder Type)
{
	switch (Type)
	{
	case EMechShoulder::Launch:
		return LaunchCooldown;
	case EMechShoulder::Shockwave:
		return ShockCooldown;
	case EMechShoulder::Mine:
		return MineCooldown;
	default:
		return BurstCooldown;
	}
}

float UMechTacticalComponent::GetReady() const
{
	return Cooldown <= 0.f ? 1.f : FMath::Clamp(1.f - Cooldown / FMath::Max(CooldownLength, 0.01f), 0.f, 1.f);
}

bool UMechTacticalComponent::CanUse() const
{
	const AMechCharacter* Mech = GetMech();
	if (!Mech || !Mech->CanAct() || Mech->GetHealth()->IsStaggered() || Mech->IsNetted())
	{
		return false;
	}
	if (IsMobility(GetType()))
	{
		// 推进器失灵（EMP）时脱离 / 弹射都靠推进器，用不了；带着核心只能走地面（和钩爪、快速闪避一样）。这两样都是对手能拿来反制的
		return !Mech->IsEmped() && !Mech->IsCarryingCore();
	}
	return true;
}

float UMechTacticalComponent::GetCharge() const
{
	const float T = GetWorld()->GetTimeSeconds() - ChargeStart;
	return T >= 0.f && T < ShockCharge ? T / ShockCharge : -1.f;
}

void UMechTacticalComponent::PlayAt(USoundBase* Sound, const FVector& At, float Volume, float Pitch) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, At, FRotator::ZeroRotator, Volume, Pitch * (1.f + FMath::FRandRange(-0.04f, 0.04f)), 0.f, Attenuation);
	}
}

void UMechTacticalComponent::OnLoadoutChanged()
{
	// 换装（测试场原地换、每回合新机体）：冷却清零，正在蓄的震荡波作废。各端都清（客户端预测的冷却也清掉，服务器要是真在冷却会再复制过来）
	Cooldown = 0.f;
	CooldownLength = 1.f;
	ShockAt = -1.f;
	ChargeStart = -100.f;
	ThrustFxUntil = -1.f;
}

// ---------------------------------------------------------------- 按下
void UMechTacticalComponent::Pressed(const FVector& DirOverride, const FVector& PointOverride)
{
	AMechCharacter* Mech = GetMech();
	if (!Mech || Cooldown > 0.f || !CanUse())
	{
		return;
	}
	const EMechShoulder Type = GetType();
	if (!MechIsTacticalShoulder(Type))
	{
		return;
	}
	const bool bAuth = GetOwner()->HasAuthority();
	switch (Type)
	{
	case EMechShoulder::Burst:
	case EMechShoulder::Launch:
	{
		FVector Target, Dir;
		uint16 Ms = 0;
		if (!PlanDash(Type, DirOverride, Target, Ms, Dir))
		{
			return;  // 贴着墙往墙里冲 / 头顶贴着高架：这一下不算，不进冷却
		}
		// 本地马上开始（操控的那一端预测；服务器 / AI 就是真的）。紧急脱离会把能量清零：先记下原来的，服务器不认时还回去
		++DashStamp;
		PreDashEnergy = Mech->GetMechMovement()->Energy;
		bPreDashOverheated = Mech->GetMechMovement()->bOverheated;
		ApplyDash(Type, Target, Ms);
		PlayUseFX(Type, Dir);
		if (bAuth)
		{
			Cooldown = CooldownLength = ScaledCooldown(BaseCooldown(Type));
			++DebugDashes;
			BroadcastUse(Type, Dir);
			UE_LOG(LogTemp, Display, TEXT("[tactical] %s %s → %s，%d 毫秒"), *Mech->DisplayName, Type == EMechShoulder::Burst ? TEXT("紧急脱离") : TEXT("弹射升空"),
				*Target.ToCompactString(), Ms);
		}
		else
		{
			Cooldown = CooldownLength = BaseCooldown(Type);
			ServerDash(static_cast<uint8>(Type), Target, Ms, DashStamp, Dir);
		}
		break;
	}
	case EMechShoulder::Shockwave:
		if (ShockAt >= 0.f || GetCharge() >= 0.f)
		{
			return;
		}
		PlayUseFX(Type, Mech->GetActorForwardVector().GetSafeNormal2D());
		Mech->GetCombat()->NoteEngaged();
		if (bAuth)
		{
			StartShockAuthority();
		}
		else
		{
			Cooldown = CooldownLength = BaseCooldown(Type);
			ServerShock();
		}
		break;
	case EMechShoulder::Mine:
	{
		FVector Origin, Vel;
		PlanMineThrow(PointOverride, Origin, Vel);
		PlayUseFX(Type, Vel.GetSafeNormal2D());
		Mech->GetCombat()->NoteEngaged();
		if (bAuth)
		{
			ThrowMineAuthority(Origin, Vel);
		}
		else
		{
			Cooldown = CooldownLength = BaseCooldown(Type);
			ServerThrowMine(Origin, Vel);
		}
		break;
	}
	default:
		break;
	}
}

// ---------------------------------------------------------------- 移动型：算路线
bool UMechTacticalComponent::PlanDash(EMechShoulder Type, const FVector& DirOverride, FVector& OutTarget, uint16& OutMs, FVector& OutDir) const
{
	const AMechCharacter* Mech = GetMech();
	const UMechMovementComponent* Move = Mech ? Mech->GetMechMovement() : nullptr;
	const UCapsuleComponent* Cap = Mech ? Mech->GetCapsuleComponent() : nullptr;
	if (!Move || !Cap)
	{
		return false;
	}
	const FVector Start = Mech->GetActorLocation();
	const float Radius = Cap->GetScaledCapsuleRadius();
	const float Half = Cap->GetScaledCapsuleHalfHeight();
	const float StepUp = Move->MaxStepHeight;

	// 方向：AI 指定的 > 这一帧（或上一帧）的移动输入 > 没按方向：紧急脱离往后（面朝敌人往后撤是最常见的脱身方式），弹射升空就是正上方
	FVector Dir = DirOverride.GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		FVector In = Mech->GetPendingMovementInputVector();
		if (In.IsNearlyZero())
		{
			In = Mech->GetLastMovementInputVector();
		}
		Dir = In.GetSafeNormal2D();
	}
	if (Dir.IsNearlyZero() && Type == EMechShoulder::Burst)
	{
		Dir = -FRotator(0.f, Mech->GetControlRotation().Yaw, 0.f).Vector();
	}
	OutDir = Dir;

	// 路线只看地形（别的机体不算挡路，真撞上了让移动组件自己挤开）
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechTacticalDash), false, Mech);
	FCollisionResponseParams Response;
	Move->InitCollisionParams(Params, Response);
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	const ECollisionChannel Channel = Cap->GetCollisionObjectType();
	// 扫一个胶囊：Lift 是把胶囊底部抬高多少（跨过台阶、地面起伏），半径收一点；碰到东西返回 true，OutEnd = 停下的位置（往回让 10 厘米）
	auto Sweep = [&](const FVector& A, const FVector& B, float Lift, FVector& OutEnd, FHitResult& OutHit) -> bool {
		const float S = Lift * 0.5f;
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius * 0.9f, FMath::Max(Radius, Half - S));
		const FVector Up(0.f, 0.f, S);
		if (GetWorld()->SweepSingleByChannel(OutHit, A + Up, B + Up, FQuat::Identity, Channel, Shape, Params, Response))
		{
			OutEnd = OutHit.bStartPenetrating ? A : OutHit.Location - Up - (B - A).GetSafeNormal() * 10.f;
			return true;
		}
		OutEnd = B;
		return false;
	};

	// 脚下多深都找不到地面 = 冲出了地图边（10-31 合并后的自动对战里，AI 在街道图边上往外脱离，冲完直接掉出地图）。
	// 真的悬崖 / 屋顶边底下总有地面（自然地形高差上百米，所以往下找 300 米）；找不到就停在出界之前
	auto OverVoid = [&](const FVector& P) {
		FHitResult Below;
		return !GetWorld()->LineTraceSingleByChannel(Below, P, P - FVector(0.f, 0.f, 30000.f), Channel, Params, Response);
	};

	FVector End = Start;
	if (Type == EMechShoulder::Burst)
	{
		if (Move->IsFalling())
		{
			// 空中：保持高度直线冲出去（撞墙停在墙前）；沿途分五个点看脚下，出了地图边就停在前一个点
			FHitResult Hit;
			Sweep(Start, Start + Dir * BurstDist(), 0.f, End, Hit);
			const FVector Swept = End;
			for (int32 k = 1; k <= 5; ++k)
			{
				if (OverVoid(FMath::Lerp(Start, Swept, k / 5.f)))
				{
					End = FMath::Lerp(Start, Swept, (k - 1) / 5.f);
					break;
				}
			}
		}
		else
		{
			// 地面：分五段贴着地面走——每段抬着胶囊底部扫过去（跨过台阶），撞上能走的坡就抬高一截再扫（上坡），撞上墙就停；
			// 段尾往下找地面（下坡、下台阶跟着走；找不到 = 前面是悬崖 / 平台边，按当前高度平着冲出去，之后照常下落）
			FVector P = Start;
			const float Seg = BurstDist() / 5.f;
			for (int32 i = 0; i < 5; ++i)
			{
				FVector Next;
				FHitResult Hit;
				if (Sweep(P, P + Dir * Seg, StepUp, Next, Hit))
				{
					FVector Raised;
					FHitResult Hit2;
					const FVector Up(0.f, 0.f, StepUp * 2.f);
					if (Hit.ImpactNormal.Z > 0.6f && !Sweep(P + Up, P + Up + Dir * Seg, StepUp, Raised, Hit2))
					{
						Next = Raised;
					}
					else
					{
						P = Next;
						break;
					}
				}
				FHitResult Floor;
				if (GetWorld()->LineTraceSingleByChannel(Floor, Next, Next - FVector(0.f, 0.f, Half + StepUp * 3.f), Channel, Params, Response) && Floor.ImpactNormal.Z > 0.6f)
				{
					Next.Z = Floor.ImpactPoint.Z + Half + 2.f;
				}
				else if (OverVoid(Next))
				{
					break;  // 这一段冲出了地图边：停在这一段之前（P 不动）
				}
				P = Next;
			}
			End = P;
		}
	}
	else
	{
		// 弹射升空：往上（带一点按键方向的水平位移），顶到天花板 / 高架就停在下面
		FHitResult Hit;
		Sweep(Start, Start + FVector(0.f, 0.f, LaunchRise()) + Dir * LaunchDrift, 0.f, End, Hit);
	}

	// 终点取整到厘米、时长取整到毫秒：发给服务器的就是这两个数（网络上传的位置本来就按厘米量化），两端算出来一模一样，根运动才对得上
	End = FVector(FMath::RoundToDouble(End.X), FMath::RoundToDouble(End.Y), FMath::RoundToDouble(End.Z));
	const float Dist = FVector::Dist(Start, End);
	const float Gain = Type == EMechShoulder::Burst ? FVector::Dist2D(Start, End) : End.Z - Start.Z;
	if (Gain < DashMinDistance)
	{
		return false;
	}
	if (Type == EMechShoulder::Burst)
	{
		OutMs = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(Dist / BurstSpeed * 1000.f), 120, 800));
	}
	else
	{
		// 顶到东西提前停：时长按实际走的比例缩短（速度不变）
		const float Full = FVector(Dir * LaunchDrift + FVector(0.f, 0.f, LaunchRise())).Size();
		OutMs = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(LaunchDuration * 1000.f * Dist / FMath::Max(Full, 1.f)), 150, 800));
	}
	OutTarget = End;
	return true;
}

void UMechTacticalComponent::ApplyDash(EMechShoulder Type, const FVector& Target, uint16 Ms)
{
	AMechCharacter* Mech = GetMech();
	UMechMovementComponent* Move = Mech ? Mech->GetMechMovement() : nullptr;
	if (!Move)
	{
		return;
	}
	// 自己的钩爪正把自己往哪拉：松开（和快速闪避一样）；正被别人的钩爪拉过去：挣脱——两端都撤掉拉拽的根运动
	if (UMechShoulderComponent* Sh = Mech->GetShoulder())
	{
		Sh->OnQuickBoostCancel();
	}
	Move->RemoveRootMotionSource(MechTacticalLocal::GrappleDragName);
	Move->RemoveRootMotionSource(MechTacticalLocal::BurstName);
	Move->RemoveRootMotionSource(MechTacticalLocal::LaunchName);
	TSharedPtr<FRootMotionSource_MoveToForce> RM = MakeShared<FRootMotionSource_MoveToForce>();
	RM->InstanceName = Type == EMechShoulder::Launch ? MechTacticalLocal::LaunchName : MechTacticalLocal::BurstName;
	RM->AccumulateMode = ERootMotionAccumulateMode::Override;
	RM->Priority = MechTacticalLocal::DashPriority;
	RM->StartLocation = Mech->GetActorLocation();
	RM->TargetLocation = Target;
	RM->Duration = Ms / 1000.f;
	// 不限速：两端起点差一点也会在终点对齐；路线事先按地形扫过，不会一路顶着墙越推越快
	RM->bRestrictSpeedToExpected = false;
	// 到了以后留一点速度（冲出去有惯性；弹射到顶还往上飘一点，接着能滑翔），不会突然停死
	RM->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	RM->FinishVelocityParams.ClampVelocity = Type == EMechShoulder::Launch ? LaunchFinishSpeed : BurstFinishSpeed;
	Move->ApplyRootMotionSource(RM);
	if (Type == EMechShoulder::Burst)
	{
		// 10-31 用户定：紧急脱离用完能量清零（免得脱离完马上接着飞）。用之前不看能量、过热也能用，这一点不变。
		// 两端（操控的那一端预测、服务器认了以后）都在这里先清一次；冲刺这一整段由移动组件按根运动 MechTacBurst 每帧钉在 0、过热
		// （UMechMovementComponent::UpdateCharacterStateBeforeMovement 最后），冲完从 0 开始回，要回到 35 才能再推进。服务器不认时
		// 客户端的能量由 AMechCharacter::Tick 的能量同步拉回服务器的值
		Move->Energy = 0.f;
		Move->bOverheated = true;
	}
}

void UMechTacticalComponent::ServerDash_Implementation(uint8 Type, FVector_NetQuantize Target, uint16 Ms, uint8 Stamp, FVector_NetQuantizeNormal Dir)
{
	AMechCharacter* Mech = GetMech();
	const EMechShoulder T = static_cast<EMechShoulder>(Type);
	// 核一遍：配装对、冷却好了（留一点余量）、能用、终点离服务器上的位置不离谱、时长合理；不认就让客户端撤掉预测（位置由移动纠正拉回）
	const float Reach = T == EMechShoulder::Launch ? LaunchRise() + LaunchDrift : BurstDist();
	const bool bOk = Mech && IsMobility(T) && T == GetType() && CooldownReadyOnServer() && CanUse() &&
		FVector::Dist(Mech->GetActorLocation(), FVector(Target)) < Reach + 400.f && Ms >= 100 && Ms <= 1000;
	if (!bOk)
	{
		ClientCancelDash(Stamp);
		UE_LOG(LogTemp, Display, TEXT("[tactical] %s 的%s服务器不认（冷却 %.2f）"), Mech ? *Mech->DisplayName : TEXT("?"),
			T == EMechShoulder::Launch ? TEXT("弹射升空") : TEXT("紧急脱离"), Cooldown);
		return;
	}
	ApplyDash(T, Target, Ms);
	Cooldown = CooldownLength = ScaledCooldown(BaseCooldown(T));
	++DebugDashes;
	BroadcastUse(T, Dir);
	UE_LOG(LogTemp, Display, TEXT("[tactical] %s %s → %s，%d 毫秒（客户端发来）"), *Mech->DisplayName, T == EMechShoulder::Burst ? TEXT("紧急脱离") : TEXT("弹射升空"),
		*FVector(Target).ToCompactString(), Ms);
}

void UMechTacticalComponent::ClientCancelDash_Implementation(uint8 Stamp)
{
	if (Stamp != DashStamp)
	{
		return;
	}
	if (AMechCharacter* Mech = GetMech())
	{
		UMechMovementComponent* Move = Mech->GetMechMovement();
		const bool bWasBurst = Move->GetRootMotionSource(MechTacticalLocal::BurstName).IsValid();
		Move->RemoveRootMotionSource(MechTacticalLocal::BurstName);
		Move->RemoveRootMotionSource(MechTacticalLocal::LaunchName);
		if (bWasBurst)
		{
			// 预测时清掉的能量还回去（服务器那边没清；之后差得多的话 AMechCharacter::Tick 的能量同步会再对齐）
			Move->Energy = FMath::Max(Move->Energy, PreDashEnergy);
			Move->bOverheated = bPreDashOverheated;
		}
	}
	ThrustFxUntil = -1.f;
	Cooldown = 0.f;  // 服务器要是真在冷却，会再复制过来
}

// ---------------------------------------------------------------- 震荡波
void UMechTacticalComponent::ServerShock_Implementation()
{
	StartShockAuthority();
}

void UMechTacticalComponent::StartShockAuthority()
{
	AMechCharacter* Mech = GetMech();
	if (!Mech || !CooldownReadyOnServer() || !CanUse() || ShockAt >= 0.f || GetType() != EMechShoulder::Shockwave)
	{
		return;
	}
	ShockAt = GetWorld()->GetTimeSeconds() + ShockCharge;
	Cooldown = CooldownLength = ScaledCooldown(ShockCooldown);
	Mech->GetCombat()->NoteEngaged();
	BroadcastUse(EMechShoulder::Shockwave, Mech->GetActorForwardVector().GetSafeNormal2D());
	UE_LOG(LogTemp, Display, TEXT("[tactical] %s 震荡波蓄力"), *Mech->DisplayName);
}

void UMechTacticalComponent::ShockBlast()
{
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	const FVector C = Mech->GetActorLocation();
	const FVector Feet = C - FVector(0.f, 0.f, Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	MulticastShockBlast(Feet);
	int32 Hits = 0;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M == Mech || M->GetTeam() == Mech->GetTeam() || M->GetHealth()->IsDead())
		{
			continue;
		}
		// 到对方胶囊中线的距离再减半径（球形范围：头顶高架上的够不着）
		const UCapsuleComponent* Cap = M->GetCapsuleComponent();
		const float MHalf = Cap->GetScaledCapsuleHalfHeight();
		const FVector MC = M->GetActorLocation();
		const float D = FMath::Max(0.f, FVector::Dist(C, FMath::ClosestPointOnSegment(C, MC - FVector(0.f, 0.f, MHalf), MC + FVector(0.f, 0.f, MHalf))) - Cap->GetScaledCapsuleRadius());
		if (D >= ShockReach())
		{
			continue;
		}
		// 隔着墙不算
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechShockwave), false, Mech);
		Params.AddIgnoredActor(M);
		FHitResult Wall;
		if (GetWorld()->LineTraceSingleByObjectType(Wall, C, MC, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			continue;
		}
		// 中心满额，边上六成。受击点放在对方靠近自己的那一侧：机体受击时按「受击点 → 自己」的反方向推开（AMechCharacter::OnHealthHit 的击退），
		// 这样就是从震荡波中心往外推；冲击大的还会离地。格挡 / 霸体照常减（ReceiveHit 里），算是反制
		const float Fall = FMath::Lerp(1.f, 0.6f, D / ShockReach());
		FVector Away = (MC - C).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = M->GetActorForwardVector().GetSafeNormal2D();
		}
		const FVector At = MC - Away * (Cap->GetScaledCapsuleRadius() + 10.f);
		UMechCombatComponent* VC = M->GetCombat();
		const bool bWasSwinging = VC && VC->IsMeleeActive();
		const float Dealt = M->ReceiveHit(ShockDamage * Fall, ShockImpact * Fall, Mech, At, EMechHitKind::Missile, ShockStagger);
		// 打断正在出的刀（大剑霸体重斩除外，InterruptMelee 里自己判断）
		if (bWasSwinging && VC->IsMeleeActive())
		{
			VC->InterruptMelee();
		}
		if (Dealt > 0.f)
		{
			Mech->GetCombat()->ConfirmHit(M, false, EMechHitKind::Missile, Dealt, At);
		}
		++Hits;
	}
	// 范围里敌人的地雷：一起扫掉（地雷的反制之一）
	for (TActorIterator<AMechMine> It(GetWorld()); It; ++It)
	{
		if (It->IsActive() && It->GetTeam() != Mech->GetTeam() && FVector::Dist(It->GetActorLocation(), C) < ShockReach() + 150.f)
		{
			It->Break(true);
		}
	}
	DebugShockHits += Hits;
	UE_LOG(LogTemp, Display, TEXT("[tactical] %s 震荡波炸开，震到 %d 台"), *Mech->DisplayName, Hits);
}

void UMechTacticalComponent::MulticastShockBlast_Implementation(FVector_NetQuantize At)
{
	PlayShockBlast(At);
}

void UMechTacticalComponent::PlayShockBlast(const FVector& At)
{
	AMechCharacter* Mech = GetMech();
	BlastStart = GetWorld()->GetTimeSeconds();
	BlastAt = At;
	ChargeStart = -100.f;
	if (GroundBlastFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, GroundBlastFX, At, FRotator::ZeroRotator, FVector(1.1f), true, true, ENCPoolMethod::AutoRelease);
	}
	PlayAt(BlastSound, At, 1.f, 1.25f);
	if (Mech)
	{
		Mech->FlashBody(MechTacticalLocal::ShockColor, 1.2f);
	}
	// 本机玩家：自己放的镜头往上顿一下；附近的被冲一下（越近越重）
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AMechCharacter* Me = Cast<AMechCharacter>(PC->GetPawn()))
		{
			const float Range = ShockReach() * 1.8f;
			const float D = FVector::Dist(Me->GetActorLocation(), At);
			if (Me == Mech)
			{
				Me->CameraKick(FVector::UpVector, 0.3f);
			}
			else if (D < Range)
			{
				Me->CameraKick((Me->GetActorLocation() - At).GetSafeNormal(), (1.f - D / Range) * 0.6f);
			}
		}
	}
}

// ---------------------------------------------------------------- 地雷
void UMechTacticalComponent::PlanMineThrow(const FVector& PointOverride, FVector& OutOrigin, FVector& OutVelocity) const
{
	const AMechCharacter* Mech = GetMech();
	const float Half = Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Fwd = FRotator(0.f, Mech->GetControlRotation().Yaw, 0.f).Vector();
	FVector Point = PointOverride;
	if (Point.IsZero())
	{
		Point = Mech->IsPlayerControlled() ? Mech->GetCombat()->GetFreeAimPoint() : Mech->GetActorLocation() + Fwd * 800.f - FVector(0.f, 0.f, Half);
	}
	// 11-01 并入肩部后从左肩旁的无人机抛出（以前战术装备不占肩部，从机体前上方、手的位置大概在那）；落点和飞行时间的算法没变
	OutOrigin = Mech->GetShoulder()->LaunchPointToward(Point, UMechShoulderComponent::MuzzleLocalFor(EMechShoulder::Mine));
	// 水平最远 MineRange（沿视线截短，瞄向远处地面时落点还在那条视线上）；瞄的点比发射口高很多（瞄天、瞄高处）就按脚下的高度扔，
	// 落地时服务器再找真正的地面
	FVector Flat(Point.X - OutOrigin.X, Point.Y - OutOrigin.Y, 0.f);
	float DZ = Point.Z - OutOrigin.Z;
	const float Len = Flat.Size();
	if (Len > MineRange)
	{
		const float Scale = MineRange / Len;
		Flat *= Scale;
		DZ *= Scale;
	}
	if (DZ > 200.f)
	{
		DZ = Mech->GetActorLocation().Z - Half - OutOrigin.Z;
	}
	if (Flat.Size() < 100.f)
	{
		Flat = Fwd * 300.f;  // 直直往脚下瞄：丢在身前 3 米
	}
	const FVector Target = OutOrigin + Flat + FVector(0.f, 0.f, DZ);
	// 飞行时间按水平距离：近处 0.35 秒、15 米约 0.85 秒（太慢对面早跑了，太快看不清扔到哪）
	const float T = FMath::Clamp(Flat.Size() / 1800.f, 0.35f, 0.9f);
	OutVelocity = (Target - OutOrigin) / T + FVector(0.f, 0.f, 0.5f * AMechMine::Gravity * T);
}

void UMechTacticalComponent::ServerThrowMine_Implementation(FVector_NetQuantize Origin, FVector_NetQuantize10 Velocity)
{
	ThrowMineAuthority(Origin, Velocity);
}

void UMechTacticalComponent::ThrowMineAuthority(const FVector& Origin, const FVector& Velocity)
{
	AMechCharacter* Mech = GetMech();
	if (!Mech || !CooldownReadyOnServer() || !CanUse() || GetType() != EMechShoulder::Mine)
	{
		return;
	}
	// 起点和服务器上的机体差太多就按服务器的（只防明显不对）；初速度限个上限
	FVector O = Origin;
	if (FVector::Dist(O, Mech->GetActorLocation()) > 500.f)
	{
		O = Mech->GetActorLocation() + FVector(0.f, 0.f, 60.f);
	}
	const FVector V = Velocity.GetClampedToMaxSize(4000.f);
	const FTransform Where(V.Rotation(), O);
	if (AMechMine* Mine = GetWorld()->SpawnActorDeferred<AMechMine>(AMechMine::StaticClass(), Where, Mech, Mech, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
	{
		Mine->Init(Mech, O, V);
		Mine->FinishSpawning(Where);
		Mines.Add(Mine);
		++DebugMines;
	}
	// 同时最多 MineLimit() 颗：多出来的把最早那颗碎掉（冒火花，不炸）
	Mines.RemoveAll([](const TWeakObjectPtr<AMechMine>& M) { return !M.IsValid() || !M->IsActive(); });
	while (Mines.Num() > MineLimit())
	{
		if (AMechMine* Old = Mines[0].Get())
		{
			Old->Break(true);
		}
		Mines.RemoveAt(0);
	}
	Cooldown = CooldownLength = ScaledCooldown(MineCooldown) * (Mech->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnMineCooldown : 1.f);
	Mech->GetCombat()->NoteEngaged();
	BroadcastUse(EMechShoulder::Mine, V.GetSafeNormal2D());
	UE_LOG(LogTemp, Display, TEXT("[tactical] %s 扔出地雷（场上 %d 颗）"), *Mech->DisplayName, Mines.Num());
}

// ---------------------------------------------------------------- 画面和声音
void UMechTacticalComponent::BroadcastUse(EMechShoulder Type, const FVector& Dir)
{
	const AMechCharacter* Mech = GetMech();
	if (Mech && !Mech->IsLocallyControlled())
	{
		PlayUseFX(Type, Dir);  // 房主看客户端的机体
	}
	MulticastUseFX(static_cast<uint8>(Type), Dir);
}

void UMechTacticalComponent::MulticastUseFX_Implementation(uint8 Type, FVector_NetQuantizeNormal Dir)
{
	const AMechCharacter* Mech = GetMech();
	// 服务器已经播过（BroadcastUse）；操控这台的那一端按下时已经播过
	if (!Mech || GetOwner()->HasAuthority() || Mech->IsLocallyControlled())
	{
		return;
	}
	PlayUseFX(static_cast<EMechShoulder>(Type), Dir);
}

void UMechTacticalComponent::PlayUseFX(EMechShoulder Type, const FVector& Dir)
{
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	LastUseTime = Now;
	// 11-01 并入肩部后这四件也是左肩旁的无人机：用的时候往后一顿、接着转向准星（和发射肩部武器同一个反馈，不新做资源）。
	// 移动型不按冲出去的方向转：往后脱离时机身会一下子掉头，看着像抽搐
	if (UMechShoulderComponent* Sh = Mech->GetShoulder())
	{
		Sh->OnDroneFired(IsMobility(Type) ? FVector::ZeroVector : Dir);
	}
	const FVector At = Mech->GetActorLocation();
	const FVector Feet = At - FVector(0.f, 0.f, Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const bool bGround = !Mech->GetMechMovement()->IsFalling();
	switch (Type)
	{
	case EMechShoulder::Burst:
		// 推进器整段猛喷（喷焰、推进灯）、播一次闪避动作（朝冲出去的方向），比快速闪避低沉的推进声；地面上脚下扬一团尘
		ThrustFxUntil = Now + 0.4f;
		Mech->PlayThrusterBurst(1.f, Dir);
		PlayAt(BoostSound, At, 1.f, 0.72f);
		if (bGround && DustFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DustFX, Feet, (-Dir).Rotation(), FVector(1.3f), true, true, ENCPoolMethod::AutoRelease);
		}
		break;
	case EMechShoulder::Launch:
		// 脚下炸起一圈土（弹射的反冲），推进器整段猛喷，低沉的推进声 + 一声闷响
		ThrustFxUntil = Now + LaunchDuration + 0.1f;
		Mech->PlayThrusterBurst(1.f);
		PlayAt(BoostSound, At, 1.f, 0.55f);
		PlayAt(PopSound, Feet, 0.9f, 0.6f);
		if (GroundBlastFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, GroundBlastFX, Feet, FRotator::ZeroRotator, FVector(0.55f), true, true, ENCPoolMethod::AutoRelease);
		}
		break;
	case EMechShoulder::Shockwave:
		// 蓄力：机体发橙光、地上亮出范围圈（TickComponent 里每帧摆），上膛似的一声
		ChargeStart = Now;
		PlayAt(ChargeSound, At, 1.f, 0.7f);
		break;
	case EMechShoulder::Mine:
		PlayAt(PopSound, At + FVector(0.f, 0.f, 60.f), 0.7f, 1.35f);
		break;
	default:
		break;
	}
	// 自己的镜头：视野像快速闪避那样冲一下（移动组件的 QuickBoostFlash 只在本地 / 服务器的移动里衰减，所以只在本地设）
	if (IsMobility(Type) && Mech->IsLocallyControlled())
	{
		Mech->GetMechMovement()->QuickBoostFlash = 1.f;
	}
}

// ---------------------------------------------------------------- 每帧
void UMechTacticalComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AMechCharacter* Mech = GetMech();
	if (!Mech)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	// 冷却：服务器上是真值；客户端在复制来的值之间本地也往下走，HUD 读数连续（和肩部武器一样）
	Cooldown = FMath::Max(0.f, Cooldown - DeltaTime);
	const bool bReady = Cooldown <= 0.f;
	if (bReady && !bWasReady)
	{
		ReadySince = Now;
	}
	bWasReady = bReady;

	if (GetOwner()->HasAuthority())
	{
		// 震荡波蓄满就炸；蓄力中被打进失衡 / 被网住 / 被击毁就断掉（冷却照样走——打断蓄力本身就是反制）
		if (ShockAt >= 0.f && Now >= ShockAt)
		{
			ShockAt = -1.f;
			if (CanUse() && GetType() == EMechShoulder::Shockwave)
			{
				ShockBlast();
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[tactical] %s 的震荡波蓄力被打断"), *Mech->DisplayName);
			}
		}
		Mines.RemoveAll([](const TWeakObjectPtr<AMechMine>& M) { return !M.IsValid() || !M->IsActive(); });
	}

	// 冲刺 / 弹射整段推进器都开着（喷焰、推进灯；各端自己播）
	if (Now < ThrustFxUntil && !Mech->GetHealth()->IsDead())
	{
		Mech->PlayThrusterBurst(0.9f);
	}

	// 震荡波：蓄力时机体发橙光、脚下亮出 9 米的虚线范围圈（跟着机体走，一闪一闪）——给对手看的提示；
	// 炸开后一道实线圈从脚下 0.3 秒扩到 9 米（先快后慢），再 0.3 秒淡掉
	if (Ring)
	{
		const float Charge = GetCharge();
		const float SinceBlast = Now - BlastStart;
		if (Charge >= 0.f && !Mech->GetHealth()->IsDead())
		{
			const float Pulse = 0.5f + 0.5f * FMath::Sin(Charge * PI * 4.f);
			const FVector Feet = Mech->GetActorLocation() - FVector(0.f, 0.f, Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			MechRangeRing::Update(Ring, RingMID, Feet, ShockReach(), 1.2f + 1.3f * Pulse, MechTacticalLocal::ShockColor, true);
			Mech->FlashBody(MechTacticalLocal::ShockColor, 0.4f + 0.8f * Charge);
		}
		else if (SinceBlast >= 0.f && SinceBlast < 0.6f)
		{
			const float Fade = 1.f - FMath::Clamp((SinceBlast - 0.3f) / 0.3f, 0.f, 1.f);
			MechRangeRing::Update(Ring, RingMID, BlastAt, ShockReach() * (1.f - FMath::Exp(-SinceBlast * 12.f)), 3.f * Fade, MechTacticalLocal::ShockColor, false);
		}
		else
		{
			MechRangeRing::Hide(Ring);
		}
	}
}

// ---------------------------------------------------------------- AI
void UMechTacticalComponent::AIThink(AMechCharacter* Target, bool bSeeTarget, float DamageWindow, float DeltaSeconds)
{
	AMechCharacter* Mech = GetMech();
	AIClock -= DeltaSeconds;
	if (!Mech || !MechIsTacticalShoulder(GetType()) || AIClock > 0.f || Cooldown > 0.f || ShockAt >= 0.f || !CanUse())
	{
		return;
	}
	AIClock = FMath::FRandRange(0.2f, 0.35f);  // 隔 0.2–0.35 秒想一次（反应有快有慢，不是每帧掷骰子）
	const FVector Me = Mech->GetActorLocation();
	const float Half = Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// 看一圈：30 米内几台敌人、最近多远、震荡波圈里几台、有没有人正冲刀过来（12 米内在出招）、敌人平均在哪
	int32 Near = 0;
	int32 Close = 0;
	float Closest = 1.e9f;
	bool bLunge = false;
	FVector Sum = FVector::ZeroVector;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		const AMechCharacter* E = *It;
		if (E == Mech || E->GetTeam() == Mech->GetTeam() || E->GetHealth()->IsDead())
		{
			continue;
		}
		const float D = FVector::Dist(E->GetActorLocation(), Me);
		if (D > 3000.f)
		{
			continue;
		}
		++Near;
		Sum += E->GetActorLocation();
		Closest = FMath::Min(Closest, D);
		if (D < ShockReach() * 0.85f)
		{
			++Close;
		}
		if (D < 1200.f && E->GetCombat() && E->GetCombat()->IsMeleeActive())
		{
			bLunge = true;
		}
	}
	const FVector Away = Near > 0 ? (Me - Sum / Near).GetSafeNormal2D() : -Mech->GetActorForwardVector().GetSafeNormal2D();
	const float APFrac = Mech->GetHealth()->GetAPFraction();
	switch (GetType())
	{
	case EMechShoulder::Burst:
		// 被围（两台以上在 30 米内）还在挨打、装甲见底有人贴着、有人冲刀过来：朝远离敌人的方向脱离（带一点随机，不总是正后方）
		if ((Near >= 2 && DamageWindow > 250.f) || (APFrac < 0.35f && Closest < 1500.f) || (bLunge && FMath::FRand() < 0.5f))
		{
			Pressed((Away + FMath::VRand().GetSafeNormal2D() * 0.35f).GetSafeNormal2D());
		}
		break;
	case EMechShoulder::Launch:
		// 有人冲刀过来 / 被围着打：往上弹（带一点远离敌人的方向）
		if ((bLunge && Closest < 1000.f && FMath::FRand() < 0.6f) || (Near >= 2 && DamageWindow > 300.f))
		{
			Pressed(Away);
		}
		break;
	case EMechShoulder::Shockwave:
		// 贴身：两台以上在圈里，或者一台在圈里且正冲刀过来（不冲刀时也有三成会用）
		if (Close >= 2 || (Close >= 1 && (bLunge || FMath::FRand() < 0.3f)))
		{
			Pressed();
		}
		break;
	case EMechShoulder::Mine:
		if (Target && bSeeTarget)
		{
			const float D = FVector::Dist2D(Target->GetActorLocation(), Me);
			const float TargetHalf = Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (D > 800.f && D < MineRange && FMath::FRand() < 0.5f)
			{
				// 中距离：扔到他脚下（带一点提前量，飞行约 0.6 秒）
				Pressed(FVector::ZeroVector, Target->GetActorLocation() + Target->GetVelocity() * 0.6f - FVector(0.f, 0.f, TargetHalf));
			}
			else if (DamageWindow > 300.f && D < 2500.f)
			{
				// 挨打往后撤：丢在自己身后脚下，拦追上来的
				Pressed(FVector::ZeroVector, Me + Away * 200.f - FVector(0.f, 0.f, Half));
			}
		}
		break;
	default:
		break;
	}
}
