#include "MechGrenade.h"
#include "MechLoop.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechScore.h"
#include "MechShoulderComponent.h"
#include "MechSmoke.h"
#include "MechLoadout.h"
#include "MechWeapons.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}
}

AMechGrenade::AMechGrenade()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bAffectDistanceFieldLighting = false;
	Mesh->bAffectDynamicIndirectLighting = false;
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Mesh);
	Glow->SetCastShadows(false);
	Glow->SetIntensity(0.f);
	Glow->SetAttenuationRadius(400.f);
	ShellMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_Shell"));
	CanMesh = Load<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_Grenade"));
	ShellBlastFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_05/Particles/Ex_03_Explosion"));
	// EMP 炸开：范围贴花（M_EmpField，屏幕空间画在地面上的亮环 + 网格 + 很淡的场）+ 地面细电弧（tools/ue_make_emp_fx.py 的材质），
	// 不用特效包的雷电爆炸（带大团黑烟会盖住机体）。以前的蓝白球壳 M_EmpShell 不再用（资源留着）
	SphereMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
	CylinderMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	EmpShellMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_EmpField"));
	EmpArcMaterial = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_EmpArc"));
	SparkFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	WhistleSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_whistle"));
	ShellBoomSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_artillery_boom"));
	EmpBoomSound = Load<USoundBase>(TEXT("/Game/Audio/Shoulder/S_emp_burst"));
	Attenuation = Load<USoundAttenuation>(TEXT("/Game/Audio/SA_Combat"));
}

void AMechGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMechGrenade, Kind, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechGrenade, LaunchPos, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechGrenade, LaunchVel, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechGrenade, Gravity, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechGrenade, LaunchTime, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechGrenade, Team, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMechGrenade, Shooter, COND_InitialOnly);
}

void AMechGrenade::Init(AMechCharacter* InShooter, EMechGrenadeKind InKind, const FVector& Origin, const FVector& Velocity, float InGravity)
{
	Shooter = InShooter;
	Team = InShooter ? InShooter->GetTeam() : 0;
	Kind = InKind;
	LaunchPos = Origin;
	LaunchVel = Velocity;
	Gravity = InGravity;
	LaunchTime = ServerNow();
}

float AMechGrenade::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

FVector AMechGrenade::PosAt(float T) const
{
	return LaunchPos + LaunchVel * T - FVector(0.f, 0.f, 0.5f * Gravity * T * T);
}

FVector AMechGrenade::VelAt(float T) const
{
	return LaunchVel - FVector(0.f, 0.f, Gravity * T);
}

void AMechGrenade::GetEffectSystems(TArray<UNiagaraSystem*>& Out) const
{
	Out.AddUnique(ShellBlastFX);
	Out.AddUnique(SparkFX);
}

void AMechGrenade::BeginPlay()
{
	Super::BeginPlay();
	const bool bShell = Kind == EMechGrenadeKind::Shell;
	Mesh->SetStaticMesh(bShell ? ShellMesh : CanMesh);
	const FLinearColor TeamColor = AMechCharacter::TeamGlowColor(Team);
	const int32 Slot = Mesh->GetMaterialIndex(TEXT("MI_Robot_Glow"));
	if (Slot != INDEX_NONE && (GlowMID = Mesh->CreateAndSetMaterialInstanceDynamic(Slot)) != nullptr)
	{
		// 炮弹尾部白热（远处看得到弹道），烟雾弹 / EMP 弹头队伍色
		GlowMID->SetVectorParameterValue(TEXT("Emissive"), bShell ? FLinearColor(1.f, 0.7f, 0.4f) * 6.f : Kind == EMechGrenadeKind::Emp ? TeamColor * 3.f : TeamColor);
	}
	Glow->SetLightColor(bShell ? FLinearColor(1.f, 0.65f, 0.35f) : TeamColor);
	Glow->SetIntensity(bShell ? 6000.f : 1500.f);
	if (bShell && WhistleSound)
	{
		UGameplayStatics::SpawnSoundAttached(WhistleSound, Mesh, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, true, 0.8f, 1.f, 0.f,
			Attenuation);
	}
	PrevT = FMath::Max(0.f, ServerNow() - LaunchTime);
	// 画面上从本机看到的无人机发射口出（判定弹道的起点是无人机理想位置；联机时弹在客户端出现得晚，已经飞出去一截），0.2 秒内并回判定弹道
	VisStart = GetWorld()->GetTimeSeconds();
	if (PrevT < 0.5f)
	{
		const EMechShoulder From = bShell ? EMechShoulder::Artillery : Kind == EMechGrenadeKind::Smoke ? EMechShoulder::Smoke : EMechShoulder::Emp;
		VisOffset = UMechShoulderComponent::LaunchVisualOffset(Shooter, PosAt(PrevT), VelAt(PrevT), UMechShoulderComponent::MuzzleLocalFor(From));
	}
}

// ---------------------------------------------------------------- 每帧：沿弹道飞，服务器判碰撞
void AMechGrenade::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDone)
	{
		if (BurstStart >= 0.f)
		{
			TickEmpBurst();
		}
		return;
	}
	const float T = FMath::Max(0.f, ServerNow() - LaunchTime);
	const FVector Pos = PosAt(T);
	const FVector Vel = VelAt(T);
	const bool bShell = Kind == EMechGrenadeKind::Shell;
	// 炮弹头朝前飞；弹罐边飞边翻滚
	const FQuat Face = FRotationMatrix::MakeFromX(Vel.GetSafeNormal()).ToQuat();
	// 画面位置 = 判定弹道 + 刚出膛时的偏移（只影响画面：碰撞都按 PosAt 算）
	const FVector Shown = VisOffset.IsZero() ? Pos : Pos + VisOffset * UMechShoulderComponent::LaunchVisualFade(GetWorld()->GetTimeSeconds() - VisStart);
	SetActorLocationAndRotation(Shown, bShell ? Face : Face * FQuat(FVector::RightVector, T * 9.f));
	if (!HasAuthority())
	{
		return;
	}
	const FVector Prev = PosAt(PrevT);
	PrevT = T;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechGrenade), false, this);
	if (Shooter)
	{
		Params.AddIgnoredActor(Shooter);
	}
	// 敌方机体（炮弹贴着胶囊才算撞上，弹罐宽松一点）；开着护盾、从正面来的撞在盾上
	const float Slack = bShell ? 30.f : 45.f;
	AMechCharacter* Best = nullptr;
	float BestAlong = 2.f;
	FVector BestAt = Pos;
	bool bBestShield = false;
	const float SegLen = FMath::Max(1.f, FVector::Dist(Prev, Pos));
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M == Shooter || M->GetTeam() == Team || M->GetHealth()->IsDead())
		{
			continue;
		}
		const UCapsuleComponent* Cap = M->GetCapsuleComponent();
		const float Half = Cap->GetScaledCapsuleHalfHeight() * 0.7f;
		const FVector C = MechLoop::Near(GetWorld(), M->GetActorLocation(), Pos);  // 老街循环：按离弹道最近的那份
		FVector OnPath, OnBody;
		if (const UMechShoulderComponent* S = M->GetShoulder(); S && S->IsShieldUp())
		{
			const FVector Chest = C + FVector(0.f, 0.f, 40.f);
			const FVector Near = FMath::ClosestPointOnSegment(Chest, Prev, Pos);
			if (FVector::Dist(Near, Chest) < UMechShoulderComponent::ShieldRadius && S->BlocksFrom(Near))
			{
				const float Along = FVector::Dist(Prev, Near) / SegLen;
				if (Along < BestAlong)
				{
					Best = M;
					BestAlong = Along;
					BestAt = Near;
					bBestShield = true;
				}
				continue;
			}
		}
		FMath::SegmentDistToSegmentSafe(Prev, Pos, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half), OnPath, OnBody);
		if (FVector::Dist(OnPath, OnBody) < Cap->GetScaledCapsuleRadius() + Slack)
		{
			const float Along = FVector::Dist(Prev, OnPath) / SegLen;
			if (Along < BestAlong)
			{
				Best = M;
				BestAlong = Along;
				BestAt = OnPath;
				bBestShield = false;
			}
		}
	}
	FHitResult Hit;
	const bool bWall = GetWorld()->LineTraceSingleByObjectType(Hit, Prev, Pos, FCollisionObjectQueryParams(ECC_WorldStatic), Params);
	if (Best && (!bWall || BestAlong <= Hit.Time + 0.02f))
	{
		Detonate(BestAt, -Vel.GetSafeNormal(), bBestShield ? nullptr : Best);
		return;
	}
	if (bWall)
	{
		Detonate(Hit.ImpactPoint, Hit.ImpactNormal, nullptr);
		return;
	}
	if (T > (bShell ? 2.5f : 4.f))
	{
		Detonate(Pos, FVector::UpVector, nullptr);
	}
}

// ---------------------------------------------------------------- 服务器：炸开
void AMechGrenade::Detonate(const FVector& At, const FVector& Normal, AMechCharacter* Direct)
{
	bDone = true;
	Mesh->SetVisibility(false);
	Glow->SetIntensity(0.f);
	SetLifeSpan(Kind == EMechGrenadeKind::Emp ? 1.8f : 0.5f);  // EMP 的范围环要停约 1 秒、1.15 秒淡完（客户端收到炸开的消息还要晚一点）
	MulticastDetonate(At, Normal);
	auto ForEachEnemy = [&](float Radius, TFunctionRef<void(AMechCharacter*, float)> Fn) {
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			AMechCharacter* M = *It;
			if (M->GetTeam() == Team || M->GetHealth()->IsDead())
			{
				continue;
			}
			// 到机体胶囊中线的距离
			const float Half = M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const FVector C = MechLoop::Near(GetWorld(), M->GetActorLocation(), At);  // 老街循环
			const float D = FMath::Max(0.f, FVector::Dist(At, FMath::ClosestPointOnSegment(At, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half)))
				- M->GetCapsuleComponent()->GetScaledCapsuleRadius());
			if (M == Direct || D < Radius)
			{
				Fn(M, M == Direct ? 0.f : D / Radius);
			}
		}
	};
	switch (Kind)
	{
	case EMechGrenadeKind::Shell:
		ForEachEnemy(ShellRadius, [&](AMechCharacter* M, float Frac) {
			const float Fall = FMath::Lerp(1.f, 0.4f, Frac);
			const float Dealt = M->ReceiveHit(ShellDamage * Fall, ShellImpact * Fall, Shooter, At, EMechHitKind::Missile);
			if (Shooter && Dealt > 0.f)
			{
				Shooter->GetCombat()->ConfirmHit(M, M == Direct, EMechHitKind::Missile, Dealt, At);
			}
		});
		break;
	case EMechGrenadeKind::Smoke:
	{
		const FTransform Where(FRotator::ZeroRotator, At + Normal * 40.f);
		if (AMechSmoke* Smoke = GetWorld()->SpawnActorDeferred<AMechSmoke>(AMechSmoke::StaticClass(), Where, Shooter, Shooter,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Smoke->FinishSpawning(Where);
		}
		break;
	}
	case EMechGrenadeKind::Emp:
		ForEachEnemy(EmpRadius, [&](AMechCharacter* M, float Frac) {
			UMechShoulderComponent* S = M->GetShoulder();
			if (S && S->BlocksFrom(At))
			{
				S->OnShieldBlocked(At);  // 护盾挡住了
				return;
			}
			const float Dealt = M->ReceiveHit(EmpDamage, EmpImpact, Shooter, At, EMechHitKind::Missile);
			if (S)
			{
				S->ApplyEmp(EmpDuration + (Shooter && Shooter->IsOwnMaker(MechLoadout::Shoulder) ? MechWeapons::OwnEmpExtra : 0.f));
			}
			if (Shooter)
			{
				Shooter->GetCombat()->ConfirmHit(M, M == Direct, EMechHitKind::Missile, Dealt, At);
				MechScore::NoteControl(Shooter, M);  // 11-01 积分制：EMP 命中一台 = 一次控制命中（加分、算助攻）
			}
		});
		break;
	}
	UE_LOG(LogTemp, Display, TEXT("[shoulder] %s 炸开（%s 发射）%s"), Kind == EMechGrenadeKind::Shell ? TEXT("炮弹") : Kind == EMechGrenadeKind::Smoke ? TEXT("烟雾弹") : TEXT("EMP"),
		Shooter ? *Shooter->DisplayName : TEXT("?"), Direct ? *FString::Printf(TEXT("直接打中 %s"), *Direct->DisplayName) : TEXT(""));
}

// ---------------------------------------------------------------- EMP 炸开的画面
void AMechGrenade::StartEmpBurst(const FVector& At, const FVector& Normal)
{
	if (BurstStart >= 0.f || !SphereMesh)
	{
		return;
	}
	BurstStart = GetWorld()->GetTimeSeconds();
	BurstAt = At;
	BurstNormal = Normal.IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	// 范围贴花：一个比 EMP 半径大一点的球（引擎球半径 50 厘米），材质只画「背面」那层、不做深度测试，
	// 按场景深度还原出这个像素背后的表面，在离炸点 7 米以内的表面上画环、网格（屏幕空间贴花，见 tools/shaders/emp_field.hlsl）
	if (EmpShellMaterial)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, TEXT("EmpField"));
		C->SetStaticMesh(SphereMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->bAffectDistanceFieldLighting = false;
		C->bAffectDynamicIndirectLighting = false;
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		C->SetWorldLocation(At);
		C->SetWorldScale3D(FVector(EmpRadius * 1.1f / 50.f));
		C->SetBoundsScale(1.2f);
		ShellMID = UMaterialInstanceDynamic::Create(EmpShellMaterial, this);
		C->SetMaterial(0, ShellMID);
		ShellMID->SetVectorParameterValue(TEXT("Center"), FLinearColor(At.X, At.Y, At.Z, 0.f));
		ShellMID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.32f, 0.62f, 1.f, 1.f));
		EmpShell = C;
	}
	EmpBolts = NewObject<UInstancedStaticMeshComponent>(this, TEXT("EmpBolts"));
	EmpBolts->SetStaticMesh(CylinderMesh);
	if (EmpArcMaterial)
	{
		// 电弧比材质默认（8）暗很多（强度在 TickEmpBurst 里按阶段设：电光球 1.3、残余 0.9，镜头在范围里再乘 0.25）
		BoltMID = UMaterialInstanceDynamic::Create(EmpArcMaterial, this);
		BoltMID->SetScalarParameterValue(TEXT("Strength"), 1.5f);
		EmpBolts->SetMaterial(0, BoltMID);
	}
	EmpBolts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EmpBolts->SetCastShadow(false);
	EmpBolts->bAffectDistanceFieldLighting = false;
	EmpBolts->bAffectDynamicIndirectLighting = false;
	EmpBolts->SetUsingAbsoluteLocation(true);
	EmpBolts->SetUsingAbsoluteRotation(true);
	EmpBolts->SetUsingAbsoluteScale(true);
	EmpBolts->SetupAttachment(RootComponent);
	EmpBolts->RegisterComponent();
	EmpBolts->SetWorldTransform(FTransform::Identity);
	// 闪光灯：炸点上方一点，蓝色，弱、快灭（夜里照亮一圈地面，白天基本看不出；10-30 从 40000 降到 5000）
	Glow->SetUsingAbsoluteLocation(true);
	Glow->SetWorldLocation(At + BurstNormal * 80.f);
	Glow->SetLightColor(FLinearColor(0.45f, 0.7f, 1.f));
	Glow->SetAttenuationRadius(EmpRadius * 1.5f);
	TickEmpBurst();
}

void AMechGrenade::TickEmpBurst()
{
	// 10-30 第三版（用户试玩：「地面环太规整、特效一般」→「不规则的闪电球炸开」）：
	//  0–0.3 秒：炸点一团不规则、翻滚的电光球（球面上的折线电弧 + 从中心往外的放射电弧，每 0.04 秒整团重新随机），膨胀到作用半径；
	//  0.28–0.6 秒：炸散——一道道电弧从球面甩出去、末端打在范围里的地面上，打中的地方再迸出几道小电弧；
	//  0.5–1.25 秒：残余细电弧在范围里的地面上随机跳几下，越来越稀。
	//  地上只留很淡的一层不规则范围提示（M_EmpField，边缘按角度抖动，不是规整的圆环和网格）。
	//  亮度：电弧材质按曝光倒数归一（白天夜里屏幕上一样亮），强度压低，镜头在范围里再压一截，不会满屏白
	const float T = GetWorld()->GetTimeSeconds() - BurstStart;
	constexpr float Length = 1.25f;
	constexpr float GrowTime = 0.3f;
	if (T > Length)
	{
		// 放完：都藏起来（Actor 过一会儿由服务器删）
		for (UPrimitiveComponent* C : {static_cast<UPrimitiveComponent*>(EmpShell.Get()), static_cast<UPrimitiveComponent*>(EmpBolts.Get())})
		{
			if (C && C->IsVisible())
			{
				C->SetVisibility(false);
			}
		}
		Glow->SetIntensity(0.f);
		BurstStart = -100.f;
		return;
	}
	// 电光球半径：0.3 秒先快后慢地涨到 EMP 半径（三次缓出）
	const float Grow = FMath::Clamp(T / GrowTime, 0.f, 1.f);
	const float R = EmpRadius * (1.f - FMath::Pow(1.f - Grow, 3.f));
	FVector Cam = BurstAt + FVector(0.f, 0.f, 1.e5f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
	{
		Cam = PC->PlayerCameraManager->GetCameraLocation();
	}
	const float CamDist = FVector::Dist(Cam, BurstAt);
	// 镜头在范围里：电弧和地上的提示都再压一截（满屏都是电弧时叠起来会发白）
	const float Inside = 1.f - FMath::SmoothStep(EmpRadius * 0.85f, EmpRadius * 1.15f, CamDist);
	if (ShellMID)
	{
		ShellMID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(R, EmpRadius, T, Inside));
		ShellMID->SetScalarParameterValue(TEXT("Strength"), 1.f);
	}
	// 闪光灯：电光球期间跟着电弧闪（弱），炸散后很快灭
	Glow->SetIntensity(T < 0.42f ? 4000.f * FMath::FRandRange(0.55f, 1.f) : 4000.f * FMath::Exp(-(T - 0.42f) * 9.f));
	if (!EmpBolts)
	{
		return;
	}
	BoltTimer -= GetWorld()->GetDeltaSeconds();
	if (BoltTimer > 0.f && T > 0.f)
	{
		return;  // 折线隔几帧才整团重甩一次（每帧都变看着像噪点）
	}
	BoltTimer = T < 0.6f ? 0.04f : 0.07f;
	BoltSeed = FMath::Rand();
	FRandomStream Rand(BoltSeed);
	TArray<FTransform> Xf;
	// 粗细：近处 1.6 厘米，远了至少一个像素左右
	const float Radius = FMath::Max(1.6f, CamDist * 0.0009f);
	// 球心离炸点往法线方向抬一点（贴地炸开时露出来的多一些），整团绕一根固定的随机轴翻滚
	const FVector Center = BurstAt + BurstNormal * FMath::Min(R * 0.3f, 150.f);
	const FVector TumbleAxis = FRandomStream(static_cast<int32>(BurstAt.X * 7.f + BurstAt.Y * 13.f + BurstAt.Z)).GetUnitVector();
	const FQuat Tumble(TumbleAxis, T * 7.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechEmpArc), false, this);
	auto Ground = [&](const FVector& P, FVector& Out, FVector& Normal) {
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 260.f), P - FVector(0.f, 0.f, 500.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			Out = Hit.ImpactPoint + Hit.ImpactNormal * 3.f;
			Normal = Hit.ImpactNormal;
			return true;
		}
		return false;
	};
	// 1) 电光球：球面上的折线电弧（一段段沿球面走、每个折点半径随机进出）+ 从中心往外的放射电弧；炸散时变稀
	if (T < 0.42f)
	{
		const float Thin = T < GrowTime ? 1.f : 1.f - (T - GrowTime) / 0.12f;
		const float Rb = FMath::Max(R, 50.f) * (T < GrowTime ? 1.f : 1.f + (T - GrowTime) * 1.5f);
		const int32 Surface = FMath::Max(2, FMath::RoundToInt(11.f * Thin));
		const int32 Radial = FMath::Max(1, FMath::RoundToInt(5.f * Thin));
		for (int32 i = 0; i < Surface; ++i)
		{
			const FVector U = Rand.GetUnitVector();
			FVector Side = FVector::CrossProduct(U, Rand.GetUnitVector()).GetSafeNormal();
			if (Side.IsNearlyZero())
			{
				Side = U.GetSafeNormal().ToOrientationQuat().GetRightVector();
			}
			const float Span = FMath::DegreesToRadians(Rand.FRandRange(40.f, 110.f));
			FVector Prev = Center + Tumble.RotateVector(U * Rb * Rand.FRandRange(0.85f, 1.08f));
			for (int32 k = 1; k <= 5; ++k)
			{
				const FVector Dir = FQuat(Side, Span * k / 5.f).RotateVector(U);
				const FVector P = Center + Tumble.RotateVector(Dir * Rb * Rand.FRandRange(0.82f, 1.1f));
				UMechShoulderComponent::AppendArc(Xf, Prev, P, Radius, 0.28f, Rand, 2);
				Prev = P;
			}
		}
		for (int32 i = 0; i < Radial; ++i)
		{
			const FVector D = Rand.GetUnitVector();
			UMechShoulderComponent::AppendArc(Xf, Center + D * 15.f, Center + Tumble.RotateVector(D * Rb * Rand.FRandRange(0.55f, 1.f)), Radius * 1.15f, 0.2f, Rand, 6);
		}
	}
	// 2) 炸散：电弧从球面甩出去打在范围里的地面上，打中处迸几道小电弧
	if (T > 0.28f && T < 0.6f)
	{
		const int32 Count = FMath::Max(2, FMath::RoundToInt(9.f * (1.f - FMath::Square((T - 0.28f) / 0.32f))));
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = Rand.FRandRange(0.f, 2.f * PI);
			const FVector Flat(FMath::Cos(A), FMath::Sin(A), 0.f);
			FVector Hit, N;
			if (!Ground(BurstAt + Flat * EmpRadius * Rand.FRandRange(0.45f, 0.98f), Hit, N) || FVector::Dist2D(Hit, BurstAt) > EmpRadius)
			{
				continue;
			}
			const FVector From = Center + (Flat * 0.8f + FVector(0.f, 0.f, Rand.FRandRange(-0.2f, 0.5f))).GetSafeNormal() * FMath::Max(R, 50.f) * 0.6f;
			UMechShoulderComponent::AppendArc(Xf, From, Hit, Radius, 0.16f, Rand, 9);
			for (int32 k = 0; k < 3; ++k)
			{
				FVector Out = Rand.GetUnitVector();
				if ((Out | N) < 0.f)
				{
					Out -= N * 2.f * (Out | N);
				}
				UMechShoulderComponent::AppendArc(Xf, Hit, Hit + (Out * 0.6f + N * 0.4f).GetSafeNormal() * Rand.FRandRange(20.f, 45.f), Radius * 0.8f, 0.3f, Rand, 3);
			}
		}
	}
	// 3) 残余：范围里的地面上随机跳的细电弧，越来越稀，有时一批里一道都没有（闪几下就没了）
	if (T > 0.5f && T < 1.2f)
	{
		const float Left = 1.f - (T - 0.5f) / 0.7f;
		const int32 Count = Rand.FRand() < 0.35f + 0.5f * Left ? 1 + Rand.RandRange(0, FMath::RoundToInt(2.f * Left)) : 0;
		for (int32 i = 0; i < Count; ++i)
		{
			const float A = Rand.FRandRange(0.f, 2.f * PI);
			const float D = EmpRadius * FMath::Sqrt(Rand.FRandRange(0.05f, 0.95f));
			FVector P0, P1, N;
			if (!Ground(BurstAt + FVector(FMath::Cos(A) * D, FMath::Sin(A) * D, 0.f), P0, N))
			{
				continue;
			}
			const float B = A + Rand.FRandRange(-1.6f, 1.6f);
			const float L = Rand.FRandRange(50.f, 150.f);
			if (!Ground(P0 + FVector(FMath::Cos(B) * L, FMath::Sin(B) * L, 0.f), P1, N) || FVector::Dist2D(P1, BurstAt) > EmpRadius)
			{
				continue;
			}
			UMechShoulderComponent::AppendArc(Xf, P0, P1, Radius * 0.8f, 0.22f, Rand, 6);
		}
	}
	if (BoltMID)
	{
		// 电光球那段亮一点、残余暗一点；镜头在范围里再压
		BoltMID->SetScalarParameterValue(TEXT("Strength"), (T < 0.45f ? 1.3f : 0.9f) * FMath::Lerp(1.f, 0.25f, Inside));
	}
	// 贴着镜头的电弧段：离镜头 5 米内不画、5–12 米内按距离变细（按线段上离镜头最近的点算，一段电弧一米多长，只看中点会漏）。
	// 镜头在范围里时电弧会从镜头边上甩过，一根 1.6 厘米粗的圆柱贴在眼前就是半个屏幕，叠上泛光整屏发白（10-30 第三版前两轮截图「镜头在范围里」就是这样）
	for (int32 i = Xf.Num() - 1; i >= 0; --i)
	{
		const FVector Axis = Xf[i].GetRotation().GetAxisZ() * (Xf[i].GetScale3D().Z * 50.f);  // 引擎圆柱高 100、原点在中间、沿 Z
		const float D = FMath::PointDistToSegment(Cam, Xf[i].GetLocation() - Axis, Xf[i].GetLocation() + Axis);
		if (D < 500.f)
		{
			Xf.RemoveAtSwap(i);
		}
		else if (D < 1200.f)
		{
			const float K = (D - 500.f) / 700.f;
			FVector Sc = Xf[i].GetScale3D();
			Xf[i].SetScale3D(FVector(Sc.X * K, Sc.Y * K, Sc.Z));
		}
	}
	EmpBolts->ClearInstances();
	if (Xf.Num())
	{
		EmpBolts->AddInstances(Xf, false, true);
	}
}

void AMechGrenade::MulticastDetonate_Implementation(FVector_NetQuantize At, FVector_NetQuantizeNormal Normal)
{
	bDone = true;
	Mesh->SetVisibility(false);
	Glow->SetIntensity(0.f);
	switch (Kind)
	{
	case EMechGrenadeKind::Shell:
		if (ShellBlastFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ShellBlastFX, At, FRotator::ZeroRotator, FVector(1.1f), true, true, ENCPoolMethod::AutoRelease);
		}
		UGameplayStatics::PlaySoundAtLocation(this, ShellBoomSound, At, FRotator::ZeroRotator, 1.f, 1.f, 0.f, Attenuation);
		break;
	case EMechGrenadeKind::Emp:
		StartEmpBurst(At, Normal);
		UGameplayStatics::PlaySoundAtLocation(this, EmpBoomSound, At, FRotator::ZeroRotator, 1.f, 1.f, 0.f, Attenuation);
		break;
	default:
		if (SparkFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SparkFX, At, Normal.Rotation(), FVector(0.6f), true, true, ENCPoolMethod::AutoRelease);
		}
		break;
	}
	// 本机玩家离得近：镜头冲一下
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (AMechCharacter* Me = Cast<AMechCharacter>(PC->GetPawn()))
		{
			const float D = FVector::Dist(Me->GetActorLocation(), At);
			const float Range = Kind == EMechGrenadeKind::Shell ? 3000.f : 1500.f;
			if (Kind != EMechGrenadeKind::Smoke && D < Range)
			{
				Me->CameraKick((Me->GetActorLocation() - FVector(At)).GetSafeNormal(), (1.f - D / Range) * (Kind == EMechGrenadeKind::Shell ? 0.7f : 0.35f));
			}
		}
	}
}
