#include "MechMovementComponent.h"
#include "MechMapRules.h"
#include "MechLoop.h"
#include "MechSpringArm.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechEffectComponent.h"
#include "MechShoulderComponent.h"

UMechMovementComponent::UMechMovementComponent()
{
	MaxWalkSpeed = WalkSpeed;
	// 别人的机体的画面平滑：机体很快（冲刺 15 米/秒、快速闪避 26 米/秒），引擎默认的「差 2.56 米以上不平滑、3.84 米以上直接瞬移」
	// 在网络有抖动时经常触发，看着就是一跳一跳；房主看客户端的平滑时间默认只有 0.04 秒，包一扎堆到就颤（再长网格会明显落后胶囊，房主打不准）
	NetworkMaxSmoothUpdateDistance = 640.f;
	NetworkNoSmoothUpdateDistance = 1000.f;
	NetworkSimulatedSmoothLocationTime = 0.1f;
	NetworkSimulatedSmoothRotationTime = 0.08f;
	ListenServerNetworkSimulatedSmoothLocationTime = 0.06f;
	ListenServerNetworkSimulatedSmoothRotationTime = 0.05f;
	// 插值缓冲（10-31）要服务器每次都把「这个位置是什么时候的」发下来（引擎默认只在 Linear 平滑模式下发，省 4 字节）
	bNetworkAlwaysReplicateTransformUpdateTimestamp = true;
	LateTick.bCanEverTick = true;
	LateTick.bStartWithTickEnabled = true;
	LateTick.bAllowTickOnDedicatedServer = false;
	LateTick.TickGroup = TG_PostUpdateWork;
}

void UMechMovementComponent::TickCharacterPose(float DeltaTime)
{
	// 房主机器上别人（客户端）的机体：引擎默认每处理一个移动包走一下动画，这里不走，交给网格组件每帧走（AMechCharacter::Tick 里关掉了
	// bOnlyAllowAutonomousTickPose）。机体动作不用根运动；放根运动时照引擎原来的走
	if (CharacterOwner && CharacterOwner->HasAuthority() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy && !CharacterOwner->IsPlayingRootMotion())
	{
		return;
	}
	Super::TickCharacterPose(DeltaTime);
}

bool UMechMovementComponent::CanMoveFreely() const
{
	const AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner);
	return Mech && Mech->CanAct() && !Mech->GetHealth()->IsStaggered() && !bNetLock;
}

bool UMechMovementComponent::Spend(float Amount, bool bKeepRegen)
{
	if (bOverheated || Energy <= 0.f)
	{
		return false;
	}
	// 客户端被纠正后重放旧移动时不再扣能量（能量以服务器为准，另行同步）
	if (!CharacterOwner->bClientUpdating)
	{
		if (const AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner))
		{
			Amount *= Mech->EffectScale(EMechStat::EnergyCost);
		}
		Energy = FMath::Max(0.f, Energy - Amount);
		if (!bKeepRegen || Energy <= 0.f)
		{
			SinceSpend = 0.f;  // 用光了也要等 RegenDelay：石柱山水贪多了就是这一下掉下去的
		}
		if (Energy <= 0.f)
		{
			bOverheated = true;
		}
	}
	return true;
}

void UMechMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	if (!CharacterOwner)
	{
		return;
	}
	const bool bAir = IsFalling();
	const FVector Dir = Acceleration.GetSafeNormal2D();
	const bool bFree = CanMoveFreely();
	QuickBoostTimer = FMath::Max(0.f, QuickBoostTimer - DeltaSeconds);
	QuickBoostFlash = FMath::Max(0.f, QuickBoostFlash - DeltaSeconds * 4.f);
	SinceSpend += DeltaSeconds;
	ThrustHold = bWantsThrust ? ThrustHold + DeltaSeconds : 0.f;

	// 快速闪避：一次性，沿输入方向（AI 可指定方向）瞬间加速；地面上带一点向上，免得卡地面
	if (bWantsQuickBoost)
	{
		bWantsQuickBoost = false;
		// 被网住：快速闪避算挣扎（只在服务器结算一次，重放旧移动不算）；钩爪拉自己途中：闪避就松开
		if (AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner))
		{
			if (bNetLock)
			{
				if (CharacterOwner->HasAuthority() && !CharacterOwner->bClientUpdating && Mech->GetShoulder())
				{
					Mech->GetShoulder()->Struggle();
				}
			}
			else if (Mech->GetShoulder())
			{
				Mech->GetShoulder()->OnQuickBoostCancel();
			}
		}
		if (bFree && !bCarryLock && QuickBoostTimer <= 0.f && !bOverheated && Energy >= QuickBoostCost)
		{
			Spend(QuickBoostCost);
			QuickBoostTimer = QuickBoostCooldown;
			QuickBoostFlash = 1.f;
			FVector QDir = QuickBoostDirOverride.IsNearlyZero() ? Dir : QuickBoostDirOverride.GetSafeNormal2D();
			if (QDir.IsNearlyZero())
			{
				QDir = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
			}
			Launch(QDir * QuickBoostSpeed + FVector(0.f, 0.f, bAir ? Velocity.Z : 80.f));
			if (AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner))
			{
				Mech->OnQuickBoostExecuted(QDir);
			}
		}
		QuickBoostDirOverride = FVector::ZeroVector;
	}

	// 蹬墙跳：落地清零；空中刚按下跳跃（这一包带「按了跳跃」，而且不是这一帧从地面起跳——地面起跳时引擎已经让 bWasJumping 为真）就找墙蹬。
	// 按跳跃本来就是随移动包发的（引擎的 FLAG_JumpPressed，只在按下那一包为真），客户端预测、服务器重算用的是同一个输入
	WallJumpTimer = FMath::Max(0.f, WallJumpTimer - DeltaSeconds);
	if (!bAir)
	{
		WallJumps = 0;
		LastWallNormal = FVector::ZeroVector;
	}
	else if (CharacterOwner->bPressedJump && !CharacterOwner->bWasJumping)
	{
		TryWallJump();
	}

	// 11-02 石林飞行器（MechMapRules.h，别的图 GliderRules 是空的）：空中按住冲刺就沿视线飞，这一帧的重力、刹车、空中输入都不算（GetGravityZ、GetMaxBrakingDeceleration）
	bGliding = false;
	// 右键瞄准、格挡（bWantsSlow）不打断飞行，只是慢一点：不然一开镜就从天上掉下去（GlideStep）
	if (GliderRules && bAir && bWantsBoost && bFree && !bOverheated && Energy > 0.f && !bCarryLock && !CurrentRootMotion.HasOverrideVelocity())
	{
		const AMechCharacter* GlideMech = Cast<AMechCharacter>(CharacterOwner);
		bGliding = !(GlideMech && GlideMech->GetCombat() && GlideMech->GetCombat()->IsMeleeDriving());
	}
	// 冲刺：地面上提高最高速度（见 GetMaxSpeed）；空中沿输入方向推，并减小重力滑翔（见 GetGravityZ）
	bBoostActive = bWantsBoost && !bWantsSlow && bFree && !bOverheated && Energy > 0.f && (bAir ? (bCanAirBoost && !bCarryLock) : !Dir.IsNearlyZero());
	if (bGliding)
	{
		bBoostActive = true;
		GlideStep(DeltaSeconds, Dir);
	}
	else if (bBoostActive)
	{
		Spend(BoostDrain * DeltaSeconds, bAir && bRegenWhileFlying);
		if (bAir && !Dir.IsNearlyZero())
		{
			FVector H(Velocity.X, Velocity.Y, 0.f);
			H = (H + Dir * AirBoostAccel * DeltaSeconds).GetClampedToMaxSize(BoostSpeed);
			Velocity = FVector(H.X, H.Y, Velocity.Z);
		}
	}

	// 空中按住跳跃（按下 0.18 秒之后）向上喷射
	bThrusting = bCanThrust && !bCarryLock && bWantsThrust && bFree && bAir && ThrustHold > 0.18f && !bOverheated && Energy > 0.f;
	if (bThrusting && bGliding)
	{
		Spend(ThrustDrain * DeltaSeconds, bRegenWhileFlying);  // 飞行中按喷射 = 机头再抬一点（GlideStep 里算的），只扣能量
	}
	else if (bThrusting)
	{
		Spend(ThrustDrain * DeltaSeconds, bRegenWhileFlying);
		Velocity.Z = FMath::Min(Velocity.Z + ThrustUpAccel * DeltaSeconds, MaxRiseSpeed);
	}

	// 回能量；耗尽后要回到一定值才解除过热
	if (SinceSpend > RegenDelay && !CharacterOwner->bClientUpdating)
	{
		Energy = FMath::Min(MaxEnergy, Energy + RegenPerSecond * MechMapRules::EnergyRegenScale(GetWorld()) * DeltaSeconds * (bAir ? AirRegenScale : GroundRegenScale) * (bCarryLock ? 0.5f : 1.f)
			* (Cast<AMechCharacter>(CharacterOwner) ? Cast<AMechCharacter>(CharacterOwner)->EffectScale(EMechStat::EnergyRegen) : 1.f));
	}
	if (bOverheated && Energy >= OverheatRecover)
	{
		bOverheated = false;
	}

	// 肩部装备「紧急脱离」（10-31 用户定：冲完能量清零，免得脱离完马上接着飞；用的时候照样不看能量、过热也能用）：
	// 冲刺的根运动（MechTacBurst，UMechTacticalComponent::ApplyDash）在的整段，能量钉在 0、过热、回能重新等 RegenDelay——
	// 冲完和自己把能量用光一样，要回到 OverheatRecover 才能再推进。放在移动模拟的最后：操控的客户端、服务器各按自己那段根运动算，两端一致
	// （客户端在服务器的 0 复制回来之前会被 AMechCharacter::Tick 拉回旧值，这里每帧再钉住，过热也不会被提前解除）。
	// 重放旧移动时不动（和扣能量、回能一样，能量以服务器为准）
	static const FName TacBurstName(TEXT("MechTacBurst"));
	if (!CharacterOwner->bClientUpdating && GetRootMotionSource(TacBurstName).IsValid())
	{
		Energy = 0.f;
		bOverheated = true;
		SinceSpend = 0.f;
	}
}

bool UMechMovementComponent::FindKickWall(FVector& OutNormal, FVector& OutPoint) const
{
	const UCapsuleComponent* Cap = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : nullptr;
	if (!Cap || !UpdatedComponent)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechWallJump), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	Response.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);  // 别的机体不算墙（也别挡住后面的墙）
	// 胶囊缩细一点、截短一截再往外扫：脚下的地面、头顶的天花板碰不到，起点也不会已经陷在贴着的墙里
	const float R = Cap->GetScaledCapsuleRadius();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(R * 0.9f, Cap->GetScaledCapsuleHalfHeight() * 0.6f);
	const FVector Center = UpdatedComponent->GetComponentLocation();
	const float Reach = WallJumpReach + R * 0.1f;
	float Best = BIG_NUMBER;
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector Dir = FRotator(0.f, i * 45.f, 0.f).Vector();
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, Center, Center + Dir * Reach, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(), Shape, Params,
			Response) || Hit.bStartPenetrating)
		{
			continue;
		}
		// 竖直面才能蹬（法线几乎水平）：斜坡、台面边沿、天花板都不算
		const FVector N = Hit.ImpactNormal;
		if (FMath::Abs(N.Z) > 0.35f || Hit.Distance >= Best)
		{
			continue;
		}
		const FVector N2 = N.GetSafeNormal2D();
		if (N2.IsNearlyZero() || IsLastWall(N2, Hit.ImpactPoint))
		{
			continue;  // 刚蹬过的那面墙不算（拐角里还能蹬旁边那面）
		}
		Best = Hit.Distance;
		OutNormal = N2;
		OutPoint = Hit.ImpactPoint;
	}
	return Best < BIG_NUMBER;
}

bool UMechMovementComponent::IsLastWall(const FVector& Normal, const FVector& Point) const
{
	return !LastWallNormal.IsNearlyZero() && FVector::DotProduct(Normal, LastWallNormal) > 0.9f &&
		FMath::Abs(FVector::DotProduct(Point - LastWallPoint, LastWallNormal)) < 150.f;
}

bool UMechMovementComponent::CanWallJump(FVector* OutNormal) const
{
	if (!IsFalling() || WallJumps >= WallJumpMax || WallJumpTimer > 0.f || bCarryLock || bNetLock || !CanMoveFreely())
	{
		return false;
	}
	FVector N, P;
	if (!FindKickWall(N, P))
	{
		return false;
	}
	if (OutNormal)
	{
		*OutNormal = N;
	}
	return true;
}

void UMechMovementComponent::TryWallJump()
{
	// 带芯不能蹬墙（带芯只能走地面路线，蹬墙能一路爬上高处）；被网住、失衡不能动
	if (WallJumps >= WallJumpMax || WallJumpTimer > 0.f || bCarryLock || bNetLock || !CanMoveFreely())
	{
		return;
	}
	FVector N, P;
	if (!FindKickWall(N, P))
	{
		return;
	}
	// 朝墙外 + 向上；按键方向往墙里的那部分去掉（贴墙按着前进也能蹬开），沿墙 / 往外的部分再推一把；原来沿墙走的速度留一半
	const FVector In = Acceleration.GetSafeNormal2D();
	const FVector Steer = In - N * FMath::Min(0.f, FVector::DotProduct(In, N));
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	const FVector Tangent = Flat - N * FVector::DotProduct(Flat, N);
	const FVector H = (N * WallJumpOutSpeed + Tangent * 0.5f + Steer * WallJumpSteerSpeed).GetClampedToMaxSize(BoostSpeed);
	Velocity = FVector(H.X, H.Y, FMath::Max(Velocity.Z, WallJumpUpSpeed));
	Spend(WallJumpCost);  // 能量不够也照样蹬（Spend 过热时不扣）
	QuickBoostFlash = FMath::Max(QuickBoostFlash, 0.5f);  // 推进器跟着喷一下（喷焰、视野）
	++WallJumps;
	WallJumpTimer = WallJumpCooldown;
	LastWallNormal = N;
	LastWallPoint = P;
	if (AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner))
	{
		Mech->OnWallJumpExecuted(N);
	}
}

namespace
{
	bool IsMeleeDriven(const ACharacter* Owner)
	{
		const AMechCharacter* Mech = Cast<AMechCharacter>(Owner);
		return Mech && Mech->GetCombat() && Mech->GetCombat()->IsMeleeDriving();
	}
}

void UMechMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// 出刀（到可取消时刻之前，不吃移动输入）：速度由战斗组件每帧按突进曲线设好（10-30 刀剑手感）。以前这里照常按「没有输入」刹车——
	// 地面摩擦 6 × 2 + 刹车 2600，60 帧时每帧把突进速度吃掉两成（帧率越低吃得越多），设的速度曲线和实际走的不一样，
	// 砍下后剩的那点速度一两帧就被刹死；服务器和客户端帧率不同时走的距离也不同。空中照常有重力（在下落的物理里加，不在这里）
	if (IsMeleeDriven(CharacterOwner))
	{
		// 11-01 拼刀弹开：每个子步按曲线算速度（重放旧移动时不算，见 UMechCombatComponent::MulticastClash）
		const AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner);
		if (Mech->GetCombat()->IsClashKnocked() && !CharacterOwner->bClientUpdating)
		{
			Mech->GetCombat()->ClashKnockSubstep(DeltaTime, Velocity);
		}
		return;
	}
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UMechMovementComponent::PhysicsRotation(float DeltaTime)
{
	// 出刀时朝向由战斗组件转（连续的快速转身），这里不再同时朝瞄准方向转（两边抢着转会一顿一顿）
	if (IsMeleeDriven(CharacterOwner))
	{
		return;
	}
	Super::PhysicsRotation(DeltaTime);
}

float UMechMovementComponent::GetMaxSpeed() const
{
	if (MovementMode == MOVE_Walking || MovementMode == MOVE_NavWalking)
	{
		const AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner);
		const bool bStaggered = Mech && Mech->GetHealth() && Mech->GetHealth()->IsStaggered();
		// 夺芯灰盒：浅河走得慢；传送带顺着走快、逆着走慢（按脚下地面的标签，服务器和本地一致）
		float Ground = 1.f;
		if (const UPrimitiveComponent* Floor = CurrentFloor.HitResult.GetComponent())
		{
			if (Floor->ComponentHasTag(TEXT("Water")))
			{
				Ground = 0.75f;
			}
			else if (FMath::Abs(Velocity.X) > 50.f && (Floor->ComponentHasTag(TEXT("ConveyorPos")) || Floor->ComponentHasTag(TEXT("ConveyorNeg"))))
			{
				Ground = Floor->ComponentHasTag(TEXT("ConveyorPos")) == (Velocity.X > 0.f) ? 1.3f : 0.7f;
			}
		}
		return (bBoostActive ? BoostSpeed * (bCarryLock ? 0.5f : 1.f) : WalkSpeed) * (bStaggered ? StaggerMoveScale : 1.f) * (bWantsSlow ? 0.55f : 1.f) * Ground
			* (Mech ? Mech->EffectScale(EMechStat::MoveSpeed) : 1.f) * (bNetLock ? 0.06f : 1.f);
	}
	return Super::GetMaxSpeed();
}

void UMechMovementComponent::GlideStep(float DeltaSeconds, const FVector& Dir)
{
	const MechMapRules::FFlight& F = *GliderRules;
	const FRotator View = CharacterOwner->GetControlRotation();
	float PitchDeg;
	FVector H;
	if (bGlideAI)
	{
		PitchDeg = GlideAIPitch;
		H = !Dir.IsNearlyZero() ? Dir : (Velocity.SizeSquared2D() > 100.f ? Velocity.GetSafeNormal2D() : UpdatedComponent->GetForwardVector().GetSafeNormal2D());
	}
	else
	{
		const FVector Cam = FRotator(0.f, View.Yaw, 0.f).Vector();
		H = Dir.IsNearlyZero() ? Cam : Dir;
		// 往前飞时机头跟视线；往旁边 / 往后飞时不看俯仰（侧移不该跟着掉高度）
		PitchDeg = FRotator::NormalizeAxis(View.Pitch) * FMath::Clamp(FVector::DotProduct(H, Cam), 0.f, 1.f);
	}
	if (bCanThrust && bWantsThrust && ThrustHold > 0.18f)
	{
		PitchDeg += F.SpaceClimbDeg;
	}
	PitchDeg = FMath::Clamp(PitchDeg, -F.MaxDiveDeg, F.MaxClimbDeg);
	const FVector Want = H * FMath::Cos(FMath::DegreesToRadians(PitchDeg)) + FVector(0.f, 0.f, FMath::Sin(FMath::DegreesToRadians(PitchDeg)));
	float Speed = Velocity.Size();
	const FVector Cur = Speed > 1.f ? Velocity / Speed : Want;
	const float Cruise = F.Cruise * GlideSpeedScale * (bWantsSlow ? F.AimSpeed : 1.f);
	// 转向：越快越慢
	const float Fast = FMath::Clamp((Speed - Cruise) / FMath::Max(F.MaxSpeed - Cruise, 1.f), 0.f, 1.f);
	const float TurnRad = FMath::DegreesToRadians(FMath::Lerp(F.TurnDeg, F.TurnDegFast, Fast)) * DeltaSeconds;
	const float Angle = FMath::Acos(FMath::Clamp(static_cast<float>(FVector::DotProduct(Cur, Want)), -1.f, 1.f));
	FVector NewDir = Want;
	if (Angle > TurnRad && Angle > 1.e-3f)
	{
		NewDir = FMath::VInterpNormalRotationTo(Cur, Want, 1.f, FMath::RadiansToDegrees(TurnRad)).GetSafeNormal();
		if (Angle > PI - 0.05f)
		{
			// 正好掉头：往旁边转，别卡住
			NewDir = (Cur + FVector::CrossProduct(FVector::UpVector, Cur) * TurnRad).GetSafeNormal();
		}
	}
	// 速度：沿航迹的重力（俯冲加速、爬升减速）；低于平飞速度发动机补，高于平飞速度阻力慢慢收（抬头后这段速度就是滑翔）
	GlideSinPitch = NewDir.Z;
	float Accel = -F.PathGravity * GlideSinPitch;
	Accel += Speed < Cruise ? F.EngineAccel * (1.f - Speed / Cruise) : -F.Drag * (Speed - Cruise);
	Speed = FMath::Clamp(Speed + Accel * DeltaSeconds, F.MinSpeed * GlideSpeedScale, F.MaxSpeed);
	Velocity = NewDir * Speed;
	Acceleration = FVector::ZeroVector;  // 这一帧的方向已经用过了，PhysFalling 不再算空中输入
	// 耗能：平飞 BoostDrain，爬升多耗、俯冲少耗；推进时回能不停（bRegenWhileFlying）
	const float DrainScale = FMath::Max(F.DiveDrain, 1.f + F.ClimbDrain * GlideSinPitch);
	Spend(BoostDrain * DrainScale * DeltaSeconds, bRegenWhileFlying);
}

float UMechMovementComponent::GetMaxBrakingDeceleration() const
{
	return bGliding && IsFalling() ? 0.f : Super::GetMaxBrakingDeceleration();
}

float UMechMovementComponent::GetGravityZ() const
{
	if (bGliding && IsFalling())
	{
		return 0.f;
	}
	const float G = Super::GetGravityZ();
	return (bBoostActive && IsFalling()) ? G * AirBoostGravity / FMath::Max(GravityScale, 0.01f) : G;
}

void UMechMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsBoost = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsThrust = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
	bWantsQuickBoost = (Flags & FSavedMove_Character::FLAG_Custom_2) != 0;
	bWantsSlow = (Flags & FSavedMove_Character::FLAG_Custom_3) != 0;
}

FNetworkPredictionData_Client* UMechMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UMechMovementComponent* Self = const_cast<UMechMovementComponent*>(this);
		Self->ClientPredictionData = new FNetworkPredictionData_Client_Mech(*this);
	}
	return ClientPredictionData;
}

void UMechMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation,
	FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition,
	uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	++CorrectionCount;
	// -MechCorrectionLog：每次纠正记一行（查哪种动作容易被拉回）。10-31 加：能量、过热、顿帧（时间膨胀）、失衡、受击后几秒、出刀、根运动
	static const bool bLog = FParse::Param(FCommandLine::Get(), TEXT("MechCorrectionLog"));
	if (bLog && UpdatedComponent)
	{
		const AMechCharacter* LogMech = Cast<AMechCharacter>(CharacterOwner);
		UE_LOG(LogTemp, Display, TEXT("[correction] %.2f 第 %d 次 位置差 %.0f 厘米（%s）速度 本地 %s 服务器 %s 空中 %d 本地蹬墙 %d 喷射 %d 冲刺 %d | 能量 %.0f 过热 %d 顿帧 %.2f 失衡 %d 受击后 %.2f 秒 出刀 %d 根运动 %d"),
			GetWorld()->GetTimeSeconds(), CorrectionCount, FVector::Dist(UpdatedComponent->GetComponentLocation(), NewLocation),
			*(NewLocation - UpdatedComponent->GetComponentLocation()).ToCompactString(), *Velocity.ToCompactString(), *NewVelocity.ToCompactString(),
			IsFalling() ? 1 : 0, WallJumps, bThrusting ? 1 : 0, bBoostActive ? 1 : 0, Energy, bOverheated ? 1 : 0, CharacterOwner->CustomTimeDilation,
			LogMech && LogMech->GetHealth()->IsStaggered() ? 1 : 0, LogMech ? GetWorld()->GetTimeSeconds() - LogMech->GetLastDamagedTime() : -1.f,
			IsMeleeDriven(CharacterOwner) ? 1 : 0, CurrentRootMotion.HasActiveRootMotionSources() ? 1 : 0);
	}
	if (UpdatedComponent)
	{
		Diag.CorrMax = FMath::Max(Diag.CorrMax, float(FVector::Dist(UpdatedComponent->GetComponentLocation(), NewLocation)));
	}
	// 自己的机体被拉回时画面上先不跳（见 ClientUpdatePositionAfterServerUpdate）：记下纠正前画面上的位置
	if (WantsOwnSmooth() && !bOwnCorrectionPending && UpdatedComponent)
	{
		OwnShownBefore = UpdatedComponent->GetComponentLocation() + OwnCorrectionOffset;
		bOwnCorrectionPending = true;
	}
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
}

// ---------------------------------------------------------------- 别人的机体：按时间回放的插值缓冲（10-31，说明见头文件）
namespace
{
	// 回放点至少 / 最多比最新位置晚多少秒（中间按实测的包到达抖动 + 位置间隔自动定）；过了最新位置最多按速度外推多久；画面差值收掉的时间常数
	constexpr double InterpMinDelay = 0.04;
	constexpr double InterpMaxDelay = 0.25;
	constexpr double InterpMaxExtrap = 0.1;
	constexpr double InterpErrTau = 0.1;

	bool NetStatsOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechJitterLog"));
		return bOn;
	}

	/** 真实对局诊断（10-31）：启动参数 -MechNetDiag，默认关 */
	bool NetDiagOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechNetDiag"));
		return bOn;
	}
}

bool UMechMovementComponent::WantsInterp() const
{
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechNetSmoothOld"));
	if (bOld || !CharacterOwner || !UpdatedComponent || !GetWorld())
	{
		return false;
	}
	const ENetMode Mode = GetNetMode();
	if (Mode == NM_Standalone || Mode == NM_DedicatedServer)
	{
		return false;
	}
	// 倒地（布娃娃）以后网格归物理管
	const USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	if (!Mesh || Mesh->IsSimulatingPhysics() || Mesh->GetAttachParent() != UpdatedComponent)
	{
		return false;
	}
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return true;
	}
	// 房主机器上客户端操控的机体（掉线交给 AI 以后就不是了）
	return Mode == NM_ListenServer && CharacterOwner->HasAuthority() && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy && !CharacterOwner->IsLocallyControlled();
}

FVector UMechMovementComponent::GetInterpLatestLocation() const
{
	return InterpSamples.Num() ? InterpSamples.Last().Loc : (UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector);
}

void UMechMovementComponent::ResetInterp()
{
	InterpSamples.Reset();
	bInterpClock = false;
	bInterpShown = false;
	bInterpPaused = false;
	InterpErrLoc = FVector::ZeroVector;
	InterpErrRot = FQuat::Identity;
}

void UMechMovementComponent::NoteArrival(float Error)
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	++NetStats.Samples;
	++Diag.Samples;
	if (NetStats.LastArrivalFrame == GFrameCounter)
	{
		if (++NetStats.InFrame == 2)
		{
			++NetStats.BunchFrames;
		}
	}
	else
	{
		if (NetStats.LastArrival >= 0.0)
		{
			const float GapMs = float((Now - NetStats.LastArrival) * 1000.0);
			if (NetStatsOn())
			{
				NetStats.Gaps.Add(GapMs);
			}
			if (NetDiagOn())
			{
				Diag.Gaps.Add(GapMs);
			}
		}
		NetStats.LastArrival = Now;
		NetStats.LastArrivalFrame = GFrameCounter;
		NetStats.InFrame = 1;
	}
	if (Error >= 0.f && NetStatsOn())
	{
		NetStats.Errors.Add(Error);
	}
}

void UMechMovementComponent::AddInterpSample(double T, const FVector& InLoc, const FQuat& Rot, const FVector& Vel, bool bFalling)
{
	// 老街循环（MechLoop.h）：收到的位置按上一个位置展开成连续的（过接缝瞬移一个周期不算传送，回放照常平滑），整段缓冲都过了接缝再一起挪回来
	const FVector Loc = InterpSamples.Num() > 0 ? MechLoop::Near(GetWorld(), InLoc, InterpSamples.Last().Loc) : InLoc;
	if (InterpSamples.Num() > 0)
	{
		FInterpSample& Last = InterpSamples.Last();
		if (T <= Last.T)
		{
			if (T > Last.T - 1.0)
			{
				// 重复的（服务器这一包没动）或乱序到的旧位置。位置没变、速度变了（撞墙急停：服务器的位置不再变，时间戳也就不再更新），
				// 把最后那个点的速度改掉，不然回放会按撞墙前的速度往墙里外推 0.1 秒再停在那里
				if (T == Last.T && FVector::DistSquared(Loc, Last.Loc) < 1.0)
				{
					Last.Vel = Vel;
					Last.bFalling = bFalling;
				}
				return;
			}
			ResetInterp();  // 时间线重新开始了
		}
		else
		{
			const double Gap = T - Last.T;
			const double Jump = FVector::Dist(Loc, Last.Loc);
			if (Jump > 1500.0 && Jump > 8000.0 * Gap)
			{
				ResetInterp();  // 传送（重生、木桩回原位、回合开始）：直接跳过去，不从原地滑过去
			}
			else if (Gap > 0.25)
			{
				// 停了一阵又动起来：机体不动时服务器不更新时间戳，补一个「刚才还在原地」的点，免得从很久以前那个位置慢慢滑过来
				FInterpSample Hold = Last;
				Hold.T = T - FMath::Min(Gap * 0.5, 1.0 / 30.0);
				Hold.Vel = FVector::ZeroVector;
				InterpSamples.Add(Hold);
			}
		}
	}
	FInterpSample S;
	S.T = T;
	S.Loc = Loc;
	S.Rot = Rot.GetNormalized();
	S.Vel = Vel;
	S.bFalling = bFalling;
	InterpSamples.Add(S);

	// 时钟换算：本机时间 − 时间戳 = 这个包的「路上时间 + 两边时钟差」。最快到的那个包当基准，别的包比它晚到多少就是抖动
	const double Now = GetWorld()->GetRealTimeSeconds();
	const double Offset = Now - T;
	if (!bInterpClock)
	{
		bInterpClock = true;
		InterpBase = Offset;
		InterpWinMin = Offset;
		InterpWinStart = Now;
		InterpLate = 0.0;
		InterpGap = 1.0 / 30.0;
		InterpPlayT = T - InterpMinDelay - InterpGap;
	}
	else
	{
		InterpBase = FMath::Min(InterpBase, Offset);
		InterpWinMin = FMath::Min(InterpWinMin, Offset);
		InterpLate = FMath::Max(InterpLate, Offset - InterpBase);
		const int32 Num = InterpSamples.Num();
		if (Num >= 2)
		{
			InterpGap = FMath::Max(InterpGap, FMath::Min(T - InterpSamples[Num - 2].T, 0.25));
		}
	}
	// 回放点之前太久的位置丢掉（留一个在回放点之前，插值要用）
	int32 Drop = 0;
	while (Drop + 1 < InterpSamples.Num() && InterpSamples[Drop + 1].T < InterpPlayT - 0.25)
	{
		++Drop;
	}
	Drop = FMath::Max(Drop, InterpSamples.Num() - 256);
	if (Drop > 0)
	{
		InterpSamples.RemoveAt(0, Drop, EAllowShrinking::No);
	}
	if (InterpSamples.Num() > 0)
	{
		const float Shift = MechLoop::WrapShift(GetWorld(), InterpSamples[0].Loc.X, MechLoop::Period(GetWorld()) * 0.25f);
		if (Shift != 0.f)
		{
			for (FInterpSample& Sample : InterpSamples)
			{
				Sample.Loc.X += Shift;
			}
			InterpShownLoc.X += Shift;
		}
	}
}

bool UMechMovementComponent::EvalInterp(double T, FVector& OutLoc, FQuat& OutRot, FVector& OutVel, bool& bOutFalling) const
{
	const int32 Num = InterpSamples.Num();
	if (Num == 0)
	{
		OutLoc = UpdatedComponent->GetComponentLocation();
		OutRot = UpdatedComponent->GetComponentQuat();
		OutVel = FVector::ZeroVector;
		bOutFalling = IsFalling();
		return false;
	}
	const FInterpSample& First = InterpSamples[0];
	if (T <= First.T)
	{
		OutLoc = First.Loc;
		OutRot = First.Rot;
		OutVel = FVector::ZeroVector;
		bOutFalling = First.bFalling;
		return true;
	}
	const FInterpSample& Last = InterpSamples.Last();
	if (T >= Last.T)
	{
		// 过了最新的位置（包来晚了）：按最后的速度往前推一小段，再停住
		const double Ahead = T - Last.T;
		OutLoc = Last.Loc + Last.Vel * FMath::Min(Ahead, InterpMaxExtrap);
		OutRot = Last.Rot;
		OutVel = Ahead < InterpMaxExtrap ? Last.Vel : FVector::ZeroVector;
		bOutFalling = Last.bFalling;
		return false;
	}
	int32 i = Num - 2;
	while (i > 0 && InterpSamples[i].T > T)
	{
		--i;
	}
	const FInterpSample& A = InterpSamples[i];
	const FInterpSample& B = InterpSamples[i + 1];
	const double H = B.T - A.T;
	if (H < 1e-4)
	{
		OutLoc = B.Loc;
		OutRot = B.Rot;
		OutVel = B.Vel;
		bOutFalling = B.bFalling;
		return true;
	}
	// 三次 Hermite：两端的切线用服务器给的速度（位置、速度在两段之间都连续）；速度和两点连线对不上（撞墙、被推开、根运动）就退回连线
	const float S = float((T - A.T) / H);
	const FVector Chord = B.Loc - A.Loc;
	const float Tol = Chord.Size() * 0.5f + 10.f;
	FVector M0 = A.Vel * float(H);
	FVector M1 = B.Vel * float(H);
	if (FVector::Dist(M0, Chord) > Tol)
	{
		M0 = Chord;
	}
	if (FVector::Dist(M1, Chord) > Tol)
	{
		M1 = Chord;
	}
	const float S2 = S * S;
	const float S3 = S2 * S;
	OutLoc = A.Loc * (2.f * S3 - 3.f * S2 + 1.f) + M0 * (S3 - 2.f * S2 + S) + B.Loc * (3.f * S2 - 2.f * S3) + M1 * (S3 - S2);
	OutVel = (A.Loc * (6.f * S2 - 6.f * S) + M0 * (3.f * S2 - 4.f * S + 1.f) + B.Loc * (6.f * S - 6.f * S2) + M1 * (3.f * S2 - 2.f * S)) / float(H);
	OutRot = FQuat::Slerp(A.Rot, B.Rot, S).GetNormalized();
	bOutFalling = A.bFalling;
	return true;
}

void UMechMovementComponent::TickInterp()
{
	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	const double Now = GetWorld()->GetRealTimeSeconds();
	const double Dt = InterpLastReal < 0.0 ? 0.0 : FMath::Clamp(Now - InterpLastReal, 0.0, 0.25);
	InterpLastReal = Now;

	// 时钟基准：每秒看一次这一秒里最快的包，比基准还慢（网络变慢、对面顿帧时时间戳走得慢）就把基准往那边挪一半；
	// 晚到的最大值、位置间隔的最大值慢慢回落（网络变好了，回放就往最新的位置靠）
	if (Now - InterpWinStart >= 1.0)
	{
		if (InterpWinMin < 1e29 && InterpWinMin > InterpBase)
		{
			InterpBase += 0.5 * (InterpWinMin - InterpBase);
		}
		InterpWinMin = 1e30;
		InterpWinStart = Now;
	}
	InterpLate = FMath::Max(0.0, InterpLate - Dt * 0.03);
	InterpGap = FMath::Max(1.0 / 60.0, InterpGap - Dt * 0.02);
	const double Delay = FMath::Clamp(InterpLate + InterpGap + 0.01, InterpMinDelay, InterpMaxDelay);
	const double Target = Now - InterpBase - Delay;

	// 回放时间按本机时间走，离目标远了就最多快 / 慢一成慢慢靠过去（画面上的速度差一成看不出来）；差 0.3 秒以上直接跳
	const double PrevT = InterpPlayT;
	double Ref = PrevT;
	const double Newest = InterpSamples.Last().T;
	if (bInterpPaused)
	{
		// 停着等缓冲重新攒够（见下）：攒够半个回放延迟、或者目标时间追上来就接着放
		if (Newest - InterpPlayT >= 0.5 * Delay || Target >= InterpPlayT)
		{
			bInterpPaused = false;
		}
	}
	else
	{
		InterpPlayT += Dt;
		const double Err = Target - InterpPlayT;
		if (FMath::Abs(Err) > 0.3)
		{
			InterpPlayT = Target;
			Ref = InterpPlayT - Dt;
		}
		else
		{
			InterpPlayT += FMath::Clamp(Err * Dt * 3.0, -0.1 * Dt, 0.1 * Dt);
		}
		// 跑过最新位置 50 毫秒还没有新的：这台的时间线变慢了，或者包断了一阵。实测：房主机器上客户端的机体被顿帧时
		// （CustomTimeDilation 0.03），服务器把那台客户端的每个移动包压成 3.75 毫秒来算，时间戳跟着慢下来，顿一次就比本机时间落后约 0.1 秒；
		// 以前时钟基准只会慢慢往上挪，回放点一直跑在最新的包前面，退化成「来一个包跳一下」（-MechNetDiag 看到 断粮 19–48 帧/秒、跳变 9–11 次/秒）。
		// 现在按最新的包重新对齐基准，回放点停在这里等缓冲攒够（画面停一下，和顿帧本身那一下停顿重合），再接着平滑地放
		if (InterpPlayT > Newest + 0.05)
		{
			InterpPlayT = FMath::Max(PrevT, Newest + 0.05);
			InterpBase = FMath::Max(InterpBase, Now - Newest);
			bInterpPaused = true;
		}
	}

	FVector Loc, Vel;
	FQuat Rot;
	bool bFall = false;
	const bool bHave = EvalInterp(InterpPlayT, Loc, Rot, Vel, bFall);
	// 画面连续：按现在手上的位置重算上一帧那个时间点，和上一帧实际画的差多少（之前在外推、新包到了改写了轨迹；回放时间跳了），差值慢慢收掉
	if (bInterpShown)
	{
		FVector RefLoc, RefVel;
		FQuat RefRot;
		bool bRefFall = false;
		EvalInterp(Ref, RefLoc, RefRot, RefVel, bRefFall);
		const float Keep = float(FMath::Exp(-Dt / InterpErrTau));
		InterpErrLoc = (InterpShownLoc - RefLoc) * Keep;
		InterpErrRot = FQuat::Slerp(FQuat::Identity, (InterpShownRot * RefRot.Inverse()).GetNormalized(), Keep).GetNormalized();
		if (InterpErrLoc.SizeSquared() > FMath::Square(600.f))
		{
			InterpErrLoc = FVector::ZeroVector;
			InterpErrRot = FQuat::Identity;
		}
	}
	InterpShownLoc = Loc + InterpErrLoc;
	InterpShownRot = (InterpErrRot * Rot).GetNormalized();
	bInterpShown = true;
	InterpVel = Vel;
	bInterpFalling = bFall;
	if (NetStatsOn())
	{
		// 「断粮」只算在动的时候（站着不动时服务器不发新位置，回放点本来就会越过最新的那个）
		if (bHave)
		{
			++NetStats.Frames;
			NetStats.DelaySum += InterpSamples.Last().T - InterpPlayT;
		}
		else if (InterpSamples.Last().Vel.SizeSquared() > FMath::Square(50.f))
		{
			++NetStats.StarveFrames;
		}
	}
	if (NetDiagOn())
	{
		if (bHave)
		{
			++Diag.DelayFrames;
			Diag.DelaySum += InterpSamples.Last().T - InterpPlayT;
		}
		else if (InterpSamples.Last().Vel.SizeSquared() > FMath::Square(50.f))
		{
			++Diag.StarveFrames;
		}
	}

	const FQuat MeshRot = InterpShownRot * CharacterOwner->GetBaseRotationOffset();
	const FVector MeshLoc = InterpShownLoc + InterpShownRot.RotateVector(CharacterOwner->GetBaseTranslationOffset());
	Mesh->SetWorldLocationAndRotation(MeshLoc, MeshRot, false, nullptr, ETeleportType::None);
	InterpMeshWorld = FTransform(MeshRot, MeshLoc, Mesh->GetComponentScale());
	InterpMeshFrame = GFrameCounter;
	bInterpActive = true;
	// 一直不算「平滑完了」：引擎才会每帧都调 SmoothClientPosition，而且移动胶囊时不带着网格走
	bNetworkSmoothingComplete = false;
}

void UMechMovementComponent::LateInterpFix()
{
	if (!bInterpActive || !CharacterOwner)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	if (!WantsInterp())
	{
		// 不再回放（倒地、掉线交给 AI）：网格回到胶囊上的默认位置，交还引擎
		bInterpActive = false;
		ResetInterp();
		if (HasPredictionData_Client())
		{
			FNetworkPredictionData_Client_Character* CD = GetPredictionData_Client_Character();
			CD->MeshTranslationOffset = FVector::ZeroVector;
			CD->OriginalMeshTranslationOffset = FVector::ZeroVector;
			CD->MeshRotationOffset = FQuat::Identity;
			CD->MeshRotationTarget = FQuat::Identity;
		}
		if (Mesh && !Mesh->IsSimulatingPhysics() && Mesh->GetAttachParent() == UpdatedComponent)
		{
			Mesh->SetRelativeLocationAndRotation(CharacterOwner->GetBaseTranslationOffset(), CharacterOwner->GetBaseRotationOffset());
		}
		return;
	}
	if (Mesh && InterpMeshFrame == GFrameCounter &&
		(!Mesh->GetComponentLocation().Equals(InterpMeshWorld.GetLocation(), 0.5f) || Mesh->GetComponentQuat().AngularDistance(InterpMeshWorld.GetRotation()) > 0.002f))
	{
		Mesh->SetWorldLocationAndRotation(InterpMeshWorld.GetLocation(), InterpMeshWorld.GetRotation(), false, nullptr, ETeleportType::None);
	}
}

void UMechMovementComponent::FLateTick::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (IsValid(Target))
	{
		Target->LateInterpFix();
		if (NetDiagOn())
		{
			Target->DiagFrame();
		}
	}
}

void UMechMovementComponent::RegisterComponentTickFunctions(bool bRegister)
{
	Super::RegisterComponentTickFunctions(bRegister);
	if (bRegister)
	{
		if (SetupActorComponentTickFunction(&LateTick))
		{
			LateTick.Target = this;
		}
	}
	else if (LateTick.IsTickFunctionRegistered())
	{
		LateTick.UnRegisterTickFunction();
	}
}

void UMechMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{
	// 统计（-MechJitterLog）：收到新位置的间隔、一帧收到几个；客户端上还记胶囊（外推的）离服务器位置多远——旧做法每次被拉回的量
	if (CharacterOwner && (NetStatsOn() || NetDiagOn()))
	{
		NoteArrival(CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy ? float(FVector::Dist(OldLocation, NewLocation)) : -1.f);
	}
	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
	// 房主机器上客户端的机体：每处理完一个移动包记一个位置，时间戳是那台客户端自己的移动时间（不是包到的时间，所以不带网络抖动）
	if (CharacterOwner && CharacterOwner->HasAuthority() && WantsInterp())
	{
		AddInterpSample(GetServerLastTransformUpdateTimeStamp(), NewLocation, NewRotation, Velocity, IsFalling());
	}
}

void UMechMovementComponent::SimulatedTick(float DeltaSeconds)
{
	// 客户端上别人的机体：时间戳变了 = 收到了新位置，记进缓冲。位置取 ReplicatedMovement（放根运动——钩爪、紧急脱离——时引擎不拿它摆胶囊，
	// 但服务器照样每次都填，所以钩爪拉人、被拉的那段也一样按时间回放）
	if (CharacterOwner && WantsInterp())
	{
		const float Stamp = CharacterOwner->GetReplicatedServerLastTransformUpdateTimeStamp();
		const FRepMovement& Rep = CharacterOwner->GetReplicatedMovement();
		if (Stamp > 0.f && Stamp == InterpLastStamp && InterpSamples.Num() > 0 && !Rep.LinearVelocity.Equals(InterpSamples.Last().Vel, 1.f))
		{
			// 时间戳没变但速度变了（撞墙急停）：同一个时间点、同一个位置，AddInterpSample 只改速度
			AddInterpSample(Stamp, InterpSamples.Last().Loc, InterpSamples.Last().Rot, Rep.LinearVelocity, InterpSamples.Last().bFalling);
		}
		if (Stamp > 0.f && Stamp != InterpLastStamp)
		{
			InterpLastStamp = Stamp;
			TEnumAsByte<EMovementMode> Mode = MOVE_None;
			TEnumAsByte<EMovementMode> GroundMode = MOVE_None;
			uint8 CustomMode = 0;
			UnpackNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode(), Mode, CustomMode, GroundMode);
			AddInterpSample(Stamp, FRepMovement::RebaseOntoLocalOrigin(Rep.Location, CharacterOwner), Rep.Rotation.Quaternion(), Rep.LinearVelocity,
				Mode == MOVE_Falling);
		}
	}
	Super::SimulatedTick(DeltaSeconds);
}

void UMechMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	if (bInterpClock && InterpSamples.Num() > 0 && WantsInterp())
	{
		TickInterp();
		return;
	}
	Super::SmoothClientPosition(DeltaSeconds);
}

// ---------------------------------------------------------------- 自己的机体被服务器纠正时画面不跳（10-31 联机抖动）
// 实测（往返 300 毫秒、±40 毫秒、丢 2%）：客户端自己的机体 75 秒被纠正 237 次、很多一次 1–6 米（击退、顿帧、能量在两端对不上，
// 延迟越高差得越多），胶囊瞬移、镜头臂挂在胶囊上跟着跳——整个画面一抖，看着就像别人在抖。纠正本身照旧（位置以服务器为准），
// 只是网格和镜头臂先留在纠正前画面上的位置，差值 0.12 秒收掉。-MechOwnSmoothOld 用旧做法（直接跳）
bool UMechMovementComponent::WantsOwnSmooth() const
{
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechOwnSmoothOld"));
	return !bOld && CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && CharacterOwner->GetMesh() &&
		!CharacterOwner->GetMesh()->IsSimulatingPhysics();
}

bool UMechMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	if (bOwnCorrectionPending && UpdatedComponent)
	{
		bOwnCorrectionPending = false;
		// 重放完旧移动以后的位置和纠正前画面上的位置差多少；差 8 米以上当传送，直接跳过去
		const FVector Off = MechLoop::Delta(GetWorld(), UpdatedComponent->GetComponentLocation(), OwnShownBefore);  // 老街循环：按最近的那份算
		OwnCorrectionOffset = Off.SizeSquared() < FMath::Square(800.f) ? Off : FVector::ZeroVector;
	}
	return bResult;
}

void UMechMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// 老街循环（MechLoop.h）：本机操控的机体这一帧瞬移了一个周期（自己走出接缝、或被服务器纠正过去）→ 镜头延迟关一帧、通知渲染这是镜头切换
	AMechCharacter* LoopMech = Cast<AMechCharacter>(CharacterOwner);
	USpringArmComponent* LoopBoom = LoopMech && LoopMech->IsLocallyControlled() ? LoopMech->GetCameraBoom() : nullptr;
	if (bLoopLagRestore)
	{
		bLoopLagRestore = false;
		if (LoopBoom)
		{
			LoopBoom->bEnableCameraLag = true;
		}
	}
	const bool bLoop = LoopBoom && UpdatedComponent && MechLoop::IsActive(GetWorld());
	const float LoopX0 = bLoop ? float(UpdatedComponent->GetComponentLocation().X) : 0.f;
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bLoop && UpdatedComponent && FMath::Abs(UpdatedComponent->GetComponentLocation().X - LoopX0) > MechLoop::Period(GetWorld()) * 0.5f)
	{
		// 镜头延迟的记录跟着挪同样的量（整周期），镜头照常带着延迟跟随，不会一下子贴到机体上「顿」一下。
		// 镜头臂不是 UMechSpringArm（不该发生）时退回旧做法：这一帧关掉延迟
		const float Moved = float(UpdatedComponent->GetComponentLocation().X) - LoopX0;
		const float P = MechLoop::Period(GetWorld());
		const float Shift = P * FMath::RoundToFloat(Moved / P);
		if (UMechSpringArm* Arm = Cast<UMechSpringArm>(LoopBoom))
		{
			Arm->ShiftLag(FVector(Shift, 0.f, 0.f));
		}
		else if (LoopBoom->bEnableCameraLag)
		{
			LoopBoom->bEnableCameraLag = false;
			bLoopLagRestore = true;
		}
		if (MechLoop::CutOn())
		{
			if (APlayerController* LoopPC = Cast<APlayerController>(LoopMech->GetController()); LoopPC && LoopPC->PlayerCameraManager)
			{
				LoopPC->PlayerCameraManager->SetGameCameraCutThisFrame();
			}
		}
	}
	if (!CharacterOwner || CharacterOwner->GetLocalRole() != ROLE_AutonomousProxy)
	{
		return;
	}
	USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
	AMechCharacter* OwnMech = Cast<AMechCharacter>(CharacterOwner);
	USpringArmComponent* Boom = OwnMech ? OwnMech->GetCameraBoom() : nullptr;
	if (!WantsOwnSmooth())
	{
		// 倒地（布娃娃）：偏差清掉，镜头臂回原位（网格归物理管）
		if (bOwnApplied && Boom && bOwnBoomBase)
		{
			Boom->SetRelativeLocation(OwnBoomBase);
		}
		bOwnApplied = false;
		OwnCorrectionOffset = FVector::ZeroVector;
		return;
	}
	if (OwnCorrectionOffset.IsZero() && !bOwnApplied)
	{
		return;
	}
	// 按世界时间收（顿帧时这台的时间放慢，收得慢会让偏差留太久）
	OwnCorrectionOffset *= float(FMath::Exp(-GetWorld()->GetDeltaSeconds() / 0.12));
	if (OwnCorrectionOffset.SizeSquared() < 0.01f)
	{
		OwnCorrectionOffset = FVector::ZeroVector;
	}
	const FVector Local = UpdatedComponent->GetComponentTransform().InverseTransformVectorNoScale(OwnCorrectionOffset);
	if (Mesh)
	{
		Mesh->SetRelativeLocation(CharacterOwner->GetBaseTranslationOffset() + Local);
	}
	if (Boom)
	{
		if (!bOwnBoomBase)
		{
			OwnBoomBase = Boom->GetRelativeLocation();
			bOwnBoomBase = true;
		}
		Boom->SetRelativeLocation(OwnBoomBase + Local);
	}
	bOwnApplied = !OwnCorrectionOffset.IsZero();
}

void UMechMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	// 老街循环（MechLoop.h）：走出接缝 3 米就挪一个周期回来。放根运动（钩爪拉、紧急脱离、弹射——按世界坐标定终点）时先不挪，放完再挪
	if (!UpdatedComponent || !CharacterOwner || !MechLoop::IsActive(GetWorld()))
	{
		return;
	}
	const FVector Loc = UpdatedComponent->GetComponentLocation();
	const float Shift = CurrentRootMotion.HasActiveRootMotionSources() ? 0.f : MechLoop::WrapShift(GetWorld(), Loc.X, MechLoop::MechMargin);
	if (Shift != 0.f)
	{
		UpdatedComponent->SetWorldLocation(Loc + FVector(Shift, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner))
	{
		Mech->OnLoopWrapped(Shift);  // 镜像胶囊每次移动完都跟上（没挪也调，Shift = 0）
	}
	if (Shift != 0.f && MechLoop::LogOn() && !CharacterOwner->bClientUpdating)
	{
		UE_LOG(LogTemp, Display, TEXT("[loop] %s 过接缝：X %.0f → %.0f 米（%s）"), *CharacterOwner->GetName(), Loc.X / 100.f, (Loc.X + Shift) / 100.f,
			CharacterOwner->HasAuthority() ? TEXT("服务器") : TEXT("本机预测"));
	}
}

// ---------------------------------------------------------------- 真实对局诊断（-MechNetDiag，10-31）
// 本机双开加模拟延迟复现不了德国房主 + 中国客户端的真实网络，所以做成打包版也能开的诊断：每台机体每帧（所有东西摆完以后）记
// 画面上网格的位置和朝向，每秒写一段：自己的往返延迟和丢包、每台别人机体收到新位置的间隔、跳变 / 回跳次数、逐帧加速度的 95% 分位和最大、
// 网格离胶囊多远、回放比最新位置晚多少、各状态（冲刺 / 喷射 / 闪避 / 钩爪…）下各跳了几次；自己的机体再加被纠正次数和镜头加速度。
// 没开参数时这里一行都不跑
namespace
{
	const TCHAR* const DiagModeNames[7] = {TEXT("地面"), TEXT("空中"), TEXT("冲刺"), TEXT("喷射"), TEXT("闪避"), TEXT("钩爪/根运动"), TEXT("出刀")};

	int32 DiagMode(const AMechCharacter* Mech, const UMechMovementComponent* Move)
	{
		if (Move->CurrentRootMotion.HasActiveRootMotionSources() || (Mech->GetShoulder() && Mech->GetShoulder()->GetGrappleState() != EMechGrapple::Idle))
		{
			return 5;
		}
		if (Mech->GetCombat() && Mech->GetCombat()->IsMeleeActive())
		{
			return 6;
		}
		if (Move->GetVisualVelocity().Size2D() > 1900.f)
		{
			return 4;  // 只有快速闪避（26 米/秒）能这么快，冲刺最高 15 米/秒
		}
		if (Mech->IsThrustingUp())
		{
			return 3;
		}
		if (Mech->IsBoosting())
		{
			return 2;
		}
		return Move->IsVisualFalling() ? 1 : 0;
	}

	float DiagPct(TArray<float>& V, float Q)
	{
		if (V.Num() == 0)
		{
			return -1.f;
		}
		V.Sort();
		return V[FMath::Clamp(FMath::FloorToInt(V.Num() * Q), 0, V.Num() - 1)];
	}
}

void UMechMovementComponent::DiagFrame()
{
	AMechCharacter* Mech = Cast<AMechCharacter>(CharacterOwner);
	USkeletalMeshComponent* Mesh = CharacterOwner ? CharacterOwner->GetMesh() : nullptr;
	UWorld* World = GetWorld();
	if (!Mech || !Mesh || !World || !UpdatedComponent || World->GetNetMode() == NM_Standalone)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	if (Mesh->IsSimulatingPhysics() || Mech->GetHealth()->IsDead())
	{
		Diag.N = 0;
		DiagWrite(World, Now);
		return;
	}
	const FVector P = Mesh->GetComponentLocation();
	const float Yaw = Mesh->GetComponentRotation().Yaw;
	const bool bMine = Mech->IsLocallyControlled() && Mech->IsPlayerControlled();
	const APlayerController* PC = bMine ? Cast<APlayerController>(Mech->GetController()) : nullptr;
	const FVector Cam = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
	if (Diag.N >= 2)
	{
		const double D0 = FMath::Max(Diag.T[1] - Diag.T[0], 1e-4);
		const double D1 = FMath::Max(Now - Diag.T[1], 1e-4);
		const double Dm = 0.5 * (D0 + D1);
		const FVector V0 = (Diag.P[1] - Diag.P[0]) / D0;
		const FVector V1 = (P - Diag.P[1]) / D1;
		Diag.Acc.Add(float(((V1 - V0) / Dm).Size() / 100.0));
		const float Ya = Diag.Yaw[0];
		const float Yb = Ya + FMath::FindDeltaAngleDegrees(Ya, Diag.Yaw[1]);
		const float Yc = Yb + FMath::FindDeltaAngleDegrees(Yb, Yaw);
		Diag.YawAcc.Add(float(FMath::Abs((Yc - Yb) / D1 - (Yb - Ya) / D0) / Dm));
		// 跳变：这一帧离「按上一帧的速度走下去」差 8 厘米以上（60 帧时相当于一帧里速度突变 4.8 米/秒）；
		// 正常动作里只有快速闪避起步、撞墙、蹬墙会这样，所以按状态分开数。回跳：水平方向和上一帧反着走（两帧都走了 0.5 厘米以上）
		const bool bJump = FVector::Dist(P, Diag.P[1] + V0 * D1) > 8.0;
		const int32 Mode = DiagMode(Mech, this);
		++Diag.ModeFrames[Mode];
		if (bJump)
		{
			++Diag.Jumps;
			++Diag.ModeJumps[Mode];
		}
		const FVector S0 = (Diag.P[1] - Diag.P[0]) * FVector(1.f, 1.f, 0.f);
		const FVector S1 = (P - Diag.P[1]) * FVector(1.f, 1.f, 0.f);
		if (S0.SizeSquared() > 0.25f && S1.SizeSquared() > 0.25f && (S0 | S1) < 0.f)
		{
			++Diag.Reverse;
		}
		if (bMine && !Diag.Cam[0].IsZero() && !Cam.IsZero())
		{
			const FVector C0 = (Diag.Cam[1] - Diag.Cam[0]) / D0;
			const FVector C1 = (Cam - Diag.Cam[1]) / D1;
			Diag.CamAcc.Add(float(((C1 - C0) / Dm).Size() / 100.0));
		}
		++Diag.Frames;
		Diag.SpeedSum += GetVisualVelocity().Size() / 100.f;
		const FVector Want = UpdatedComponent->GetComponentTransform().TransformPosition(CharacterOwner->GetBaseTranslationOffset());
		const float Off = float(FVector::Dist(P, Want));
		Diag.OffsetSum += Off;
		Diag.OffsetMax = FMath::Max(Diag.OffsetMax, Off);
	}
	Diag.P[0] = Diag.P[1];
	Diag.P[1] = P;
	Diag.Yaw[0] = Diag.Yaw[1];
	Diag.Yaw[1] = Yaw;
	Diag.T[0] = Diag.T[1];
	Diag.T[1] = Now;
	Diag.Cam[0] = Diag.Cam[1];
	Diag.Cam[1] = Cam;
	++Diag.N;
	if (Diag.Corrections0 < 0)
	{
		Diag.Corrections0 = CorrectionCount;
	}
	DiagWrite(World, Now);
}

void UMechMovementComponent::DiagWrite(UWorld* World, double Now)
{
	static TWeakObjectPtr<UWorld> LastWorld;
	static double NextWrite = 0.0;
	static uint64 LastFrame = 0;
	static FString Path;
	if (LastWorld.Get() != World)
	{
		LastWorld = World;
		NextWrite = Now + 1.0;
		LastFrame = GFrameCounter;
		return;
	}
	if (Now < NextWrite)
	{
		return;
	}
	const double Span = Now - (NextWrite - 1.0);
	NextWrite = Now + 1.0;
	const ENetMode Mode = World->GetNetMode();
	FString Out;
	if (Path.IsEmpty())
	{
		Path = FPaths::ProjectLogDir() / FString::Printf(TEXT("联机诊断_%s_%s.txt"), Mode == NM_ListenServer ? TEXT("房主") : TEXT("客户端"),
			*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechNetSmoothOld"));
		static const bool bOwnOld = FParse::Param(FCommandLine::Get(), TEXT("MechOwnSmoothOld"));
		Out += FString::Printf(TEXT("联机诊断（-MechNetDiag）开始 %s，%s。别人的机体：%s；自己被纠正时：%s\n")
			TEXT("每秒一段。跳变 = 这一帧离按上一帧速度走下去差 8 厘米以上；回跳 = 水平方向和上一帧反着走；加速度是画面上网格的逐帧二阶差分；")
			TEXT("网格离胶囊 = 画面上的机体离碰撞胶囊多远（按时间回放时本来就会晚一点）；回放晚 = 画面比收到的最新位置晚多少；断粮 = 在动但新位置没来、只能外推的帧数\n"),
			*FDateTime::Now().ToString(), Mode == NM_ListenServer ? TEXT("房主") : TEXT("客户端"),
			bOld ? TEXT("旧做法（追最新包，-MechNetSmoothOld）") : TEXT("按时间回放"), bOwnOld ? TEXT("直接跳（-MechOwnSmoothOld）") : TEXT("画面平滑收掉"));
	}
	// 自己的连接：客户端看到房主的往返延迟 / 丢包；房主看到每个客户端的
	FString Conn;
	if (const UNetDriver* ND = World->GetNetDriver())
	{
		auto Describe = [](const UNetConnection* C, const FString& Who) {
			return FString::Printf(TEXT(" %s 往返 %.0f 毫秒 丢包 收 %.1f%% 发 %.1f%%"), *Who, C->AvgLag * 1000.f, C->GetInLossPercentage().GetAvgLossPercentage() * 100.f,
				C->GetOutLossPercentage().GetAvgLossPercentage() * 100.f);
		};
		if (ND->ServerConnection)
		{
			Conn += Describe(ND->ServerConnection, TEXT("到房主"));
		}
		for (const UNetConnection* C : ND->ClientConnections)
		{
			const APlayerController* CPC = C ? C->PlayerController : nullptr;
			if (C)
			{
				Conn += Describe(C, CPC && CPC->PlayerState ? CPC->PlayerState->GetPlayerName() : FString(TEXT("客户端")));
			}
		}
	}
	const float Fps = float((GFrameCounter - LastFrame) / FMath::Max(Span, 1e-3));
	LastFrame = GFrameCounter;
	Out += FString::Printf(TEXT("%s %s 帧率 %.0f |%s\n"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")), Mode == NM_ListenServer ? TEXT("房主") : TEXT("客户端"), Fps, *Conn);
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		AMechCharacter* M = *It;
		UMechMovementComponent* MM = M->GetMechMovement();
		if (!MM)
		{
			continue;
		}
		FDiag& D = MM->Diag;
		if (D.Frames > 0)
		{
			const bool bMine = M->IsLocallyControlled() && M->IsPlayerControlled();
			const TCHAR* Who = bMine ? TEXT("自己") : M->GetLocalRole() == ROLE_SimulatedProxy ? TEXT("别人") : M->GetRemoteRole() == ROLE_AutonomousProxy ? TEXT("房主看客户端")
				: TEXT("服务器AI");
			int32 Worst = 0;
			FString Modes;
			for (int32 k = 0; k < 7; ++k)
			{
				if (D.ModeFrames[k] > 0)
				{
					Modes += FString::Printf(TEXT(" %s %d(%d)"), DiagModeNames[k], D.ModeFrames[k], D.ModeJumps[k]);
				}
				if (D.ModeJumps[k] > D.ModeJumps[Worst])
				{
					Worst = k;
				}
			}
			float GapSum = 0.f;
			float GapMax = 0.f;
			for (float G : D.Gaps)
			{
				GapSum += G;
				GapMax = FMath::Max(GapMax, G);
			}
			FString Line = FString::Printf(TEXT("  %s（%s）速度 %.1f 米/秒 | 收到新位置 %d 次 间隔 平均 %.0f 最大 %.0f 毫秒 | 跳变 %d 回跳 %d | 加速度 95%%/最大 位置 %.0f/%.0f 米/秒² 朝向 %.0f/%.0f 度/秒² | 网格离胶囊 平均 %.0f 最大 %.0f 厘米"),
				*M->DisplayName, Who, D.SpeedSum / D.Frames, D.Samples, D.Gaps.Num() ? GapSum / D.Gaps.Num() : -1.f, D.Gaps.Num() ? GapMax : -1.f, D.Jumps, D.Reverse,
				DiagPct(D.Acc, 0.95f), DiagPct(D.Acc, 1.f), DiagPct(D.YawAcc, 0.95f), DiagPct(D.YawAcc, 1.f), D.OffsetSum / D.Frames, D.OffsetMax);
			if (MM->IsInterpActive() || D.DelayFrames > 0)
			{
				Line += FString::Printf(TEXT(" | 回放晚 %.0f 毫秒 断粮 %d 帧"), D.DelayFrames ? float(D.DelaySum / D.DelayFrames * 1000.0) : -1.f, D.StarveFrames);
			}
			Line += FString::Printf(TEXT(" | 各状态帧数(跳变)：%s | 跳得最多：%s"), *Modes, D.ModeJumps[Worst] > 0 ? DiagModeNames[Worst] : TEXT("无"));
			if (bMine && M->GetLocalRole() == ROLE_AutonomousProxy)
			{
				Line += FString::Printf(TEXT(" | 被服务器纠正 %d 次 最大 %.0f 厘米 | 镜头加速度 95%%/最大 %.0f/%.0f 米/秒²"), MM->CorrectionCount - FMath::Max(D.Corrections0, 0),
					D.CorrMax, DiagPct(D.CamAcc, 0.95f), DiagPct(D.CamAcc, 1.f));
			}
			else if (bMine)
			{
				Line += FString::Printf(TEXT(" | 镜头加速度 95%%/最大 %.0f/%.0f 米/秒²"), DiagPct(D.CamAcc, 0.95f), DiagPct(D.CamAcc, 1.f));
			}
			Out += Line + TEXT("\n");
		}
		// 清零这一秒（跨帧的历史位置留着）
		D.Acc.Reset();
		D.YawAcc.Reset();
		D.CamAcc.Reset();
		D.Gaps.Reset();
		D.Frames = D.Jumps = D.Reverse = D.Samples = D.StarveFrames = D.DelayFrames = 0;
		D.OffsetSum = D.OffsetMax = D.SpeedSum = D.CorrMax = 0.f;
		D.DelaySum = 0.0;
		D.Corrections0 = MM->CorrectionCount;
		FMemory::Memzero(D.ModeFrames);
		FMemory::Memzero(D.ModeJumps);
	}
	TArray<FString> Lines;
	Out.ParseIntoArrayLines(Lines);
	for (const FString& L : Lines)
	{
		UE_LOG(LogTemp, Display, TEXT("[netdiag] %s"), *L);
	}
	FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

// ---------------------------------------------------------------- 移动包
void FSavedMove_Mech::Clear()
{
	Super::Clear();
	bSavedBoost = false;
	bSavedThrust = false;
	bSavedQuickBoost = false;
	bSavedSlow = false;
	SavedThrustHold = 0.f;
	SavedQuickBoostTimer = 0.f;
	SavedWallJumpTimer = 0.f;
	SavedWallJumps = 0;
	SavedWallNormal = FVector::ZeroVector;
	SavedWallPoint = FVector::ZeroVector;
}

void FSavedMove_Mech::RestoreState(UMechMovementComponent* Move) const
{
	Move->ThrustHold = SavedThrustHold;
	Move->QuickBoostTimer = SavedQuickBoostTimer;
	Move->WallJumpTimer = SavedWallJumpTimer;
	Move->WallJumps = SavedWallJumps;
	Move->LastWallNormal = SavedWallNormal;
	Move->LastWallPoint = SavedWallPoint;
}

void FSavedMove_Mech::CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation)
{
	// 两包合成一包：引擎把位置退回旧包开头再按合并后的时长重走一遍，计时 / 计数也要退回旧包开头，不然旧包那段被算两次
	Super::CombineWith(OldMove, InCharacter, PC, OldStartLocation);
	const FSavedMove_Mech* Old = static_cast<const FSavedMove_Mech*>(OldMove);
	if (UMechMovementComponent* Move = Cast<UMechMovementComponent>(InCharacter->GetCharacterMovement()))
	{
		Old->RestoreState(Move);
	}
	SavedThrustHold = Old->SavedThrustHold;
	SavedQuickBoostTimer = Old->SavedQuickBoostTimer;
	SavedWallJumpTimer = Old->SavedWallJumpTimer;
	SavedWallJumps = Old->SavedWallJumps;
	SavedWallNormal = Old->SavedWallNormal;
	SavedWallPoint = Old->SavedWallPoint;
}

uint8 FSavedMove_Mech::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();
	if (bSavedBoost)
	{
		Result |= FLAG_Custom_0;
	}
	if (bSavedThrust)
	{
		Result |= FLAG_Custom_1;
	}
	if (bSavedQuickBoost)
	{
		Result |= FLAG_Custom_2;
	}
	if (bSavedSlow)
	{
		Result |= FLAG_Custom_3;
	}
	return Result;
}

bool FSavedMove_Mech::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const
{
	const FSavedMove_Mech* Other = static_cast<const FSavedMove_Mech*>(NewMove.Get());
	if (bSavedBoost != Other->bSavedBoost || bSavedThrust != Other->bSavedThrust || bSavedSlow != Other->bSavedSlow || bSavedQuickBoost ||
		Other->bSavedQuickBoost)
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FSavedMove_Mech::SetMoveFor(ACharacter* C, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(C, InDeltaTime, NewAccel, ClientData);
	if (const UMechMovementComponent* Move = Cast<UMechMovementComponent>(C->GetCharacterMovement()))
	{
		bSavedBoost = Move->bWantsBoost;
		bSavedThrust = Move->bWantsThrust;
		bSavedQuickBoost = Move->bWantsQuickBoost;
		bSavedSlow = Move->bWantsSlow;
		SavedThrustHold = Move->ThrustHold;
		SavedQuickBoostTimer = Move->QuickBoostTimer;
		SavedWallJumpTimer = Move->WallJumpTimer;
		SavedWallJumps = Move->WallJumps;
		SavedWallNormal = Move->LastWallNormal;
		SavedWallPoint = Move->LastWallPoint;
	}
}

void FSavedMove_Mech::PrepMoveFor(ACharacter* C)
{
	Super::PrepMoveFor(C);
	if (UMechMovementComponent* Move = Cast<UMechMovementComponent>(C->GetCharacterMovement()))
	{
		Move->bWantsBoost = bSavedBoost;
		Move->bWantsThrust = bSavedThrust;
		Move->bWantsQuickBoost = bSavedQuickBoost;
		Move->bWantsSlow = bSavedSlow;
		RestoreState(Move);
	}
}

FSavedMovePtr FNetworkPredictionData_Client_Mech::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Mech());
}
