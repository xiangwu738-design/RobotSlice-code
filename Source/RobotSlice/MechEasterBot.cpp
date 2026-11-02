// 彩蛋的无人机 / 机械狗（11-02，说明见 MechEasterBot.h、MechEaster.h）

#include "MechEasterBot.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MechCharacter.h"
#include "MechHealthComponent.h"
#include "MechLoop.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

namespace MechEasterBotLocal
{
	// ---- 无人机
	constexpr float DroneSpeed = 2200.f;
	constexpr float DroneRange = 30000.f;      // 300 米内的机体都算目标
	constexpr float DroneFireRange = 12000.f;  // 120 米内开火
	constexpr float DroneClearance = 700.f;    // 离地至少 7 米
	constexpr float RotorSpin = 2400.f;        // 旋翼转速（度/秒）
	// ---- 狗
	constexpr float DogCenter = 40.f;          // 碰撞球中心离脚底多高（11-02 狗缩到真 Spot 大小：60→40）
	constexpr float DogWalk = 260.f;
	constexpr float DogRun = 750.f;
	constexpr float ClimbTime = 1.3f;
	constexpr float SlideTime = 1.2f;

	// 11-02 涂装改成三种颜色完全随机（ApplyLivery），原来的品牌色点缀和 12 组配色删了

	// 狗的骨头（建模那边的约定）
	const FName BoneBody(TEXT("body"));
	const FName BoneHead(TEXT("head"));
	const FName BoneTail(TEXT("tail_antenna"));
	const FName LegUpper[4] = {FName(TEXT("leg_fl_upper")), FName(TEXT("leg_fr_upper")), FName(TEXT("leg_bl_upper")), FName(TEXT("leg_br_upper"))};
	const FName LegLower[4] = {FName(TEXT("leg_fl_lower")), FName(TEXT("leg_fr_lower")), FName(TEXT("leg_bl_lower")), FName(TEXT("leg_br_lower"))};
	/** 对角两条腿同步（左前 + 右后、右前 + 左后） */
	const float LegPhase[4] = {0.f, PI, PI, 0.f};

	FQuat AxisRot(const FVector& Axis, float Deg)
	{
		return FQuat(Axis, FMath::DegreesToRadians(Deg));
	}
}

using namespace MechEasterBotLocal;

AMechEasterBot::AMechEasterBot()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(false);
	bAlwaysRelevant = true;  // 一群最多几十个，远处的也要同步（狗群一秒跑几十米）
	SetNetUpdateFrequency(15.f);

	// 根：打得中的球（照保卫战杂兵：WorldDynamic、只查询；子弹 / 刀 / 导弹爆炸按标签找它）
	HitBox = CreateDefaultSubobject<USphereComponent>(TEXT("HitBox"));
	RootComponent = HitBox;
	HitBox->InitSphereRadius(110.f);
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCollisionObjectType(ECC_WorldDynamic);
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetGenerateOverlapEvents(false);
	Tags.Add(TEXT("MechShootable"));
	Tags.Add(TEXT("MechGrunt"));

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(HitBox);
}

void AMechEasterBot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechEasterBot, Kind);
	DOREPLIFETIME(AMechEasterBot, BotRole);
	DOREPLIFETIME(AMechEasterBot, Weapon);
	DOREPLIFETIME(AMechEasterBot, Seed);
	DOREPLIFETIME(AMechEasterBot, RepPos);
	DOREPLIFETIME(AMechEasterBot, RepVel);
	DOREPLIFETIME(AMechEasterBot, RepYaw);
	DOREPLIFETIME(AMechEasterBot, RepAnim);
	DOREPLIFETIME(AMechEasterBot, HP);
	DOREPLIFETIME(AMechEasterBot, MaxHP);
	DOREPLIFETIME(AMechEasterBot, bDead);
	DOREPLIFETIME(AMechEasterBot, RunStart);
	DOREPLIFETIME(AMechEasterBot, RunOrigin);
	DOREPLIFETIME(AMechEasterBot, RunDir);
	DOREPLIFETIME(AMechEasterBot, RunWeave);
}

void AMechEasterBot::InitBot(EMechEasterKind InKind, EMechEasterRole InRole, EMechEasterWeapon InWeapon, int32 InSeed, AMechEasterDirector* InDirector)
{
	Kind = InKind;
	BotRole = InRole;
	Weapon = InWeapon;
	Seed = InSeed;
	Director = InDirector;
	MaxHP = HP = Kind == EMechEasterKind::Drone ? MechEaster::DroneHP : MechEaster::DogHP;
	OrbitAngle = FMath::FRandRange(0.f, 2.f * PI);
	OrbitRadius = FMath::FRandRange(2500.f, 4500.f);
	OrbitHeight = FMath::FRandRange(1200.f, 2600.f);
	FireClock = FMath::FRandRange(1.5f, 4.f);
	DogClock = FMath::FRandRange(0.5f, 2.f);
}

void AMechEasterBot::InitStampede(float StartTime, const FVector& Origin, const FVector& Dir, float WeavePhase)
{
	RunStart = StartTime;
	RunOrigin = Origin;
	RunDir = Dir.GetSafeNormal2D();
	RunWeave = WeavePhase;
}

void AMechEasterBot::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		RepPos = GetActorLocation();
		RepYaw = GetActorRotation().Yaw;
		Ground = GetActorLocation().Z - DogCenter;
	}
	BuildVisuals();
	SetupMirrors();
}

void AMechEasterBot::OnRep_Kind()
{
	if (HasActorBegunPlay())
	{
		BuildVisuals();
	}
}

// ---------------------------------------------------------------- 画面
void AMechEasterBot::BuildVisuals()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	if (Kind == EMechEasterKind::Drone)
	{
		HitBox->SetSphereRadius(120.f);
		BuildDrone();
	}
	else
	{
		HitBox->SetSphereRadius(60.f);  // 11-02 狗缩小（身长 1.5→1.0 米）：85→60
		Visual->SetRelativeLocation(FVector(0.f, 0.f, -DogCenter));  // 狗的原点在脚底
		BuildDog();
	}
}

void AMechEasterBot::BuildDrone()
{
	UStaticMesh* BodyMesh = MechEaster::Load<UStaticMesh>(MechEaster::DroneBodyPath());
	if (!BodyMesh)
	{
		BuildPlaceholder();
		return;
	}
	Body = NewObject<UStaticMeshComponent>(this, TEXT("DroneBody"));
	Body->SetStaticMesh(BodyMesh);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetupAttachment(Visual);
	Body->RegisterComponent();
	ApplyLivery(Body);
	// 旋翼：挂在机身的四个插槽上（没有插槽就按四个角放），每帧绕局部 Z 转
	UStaticMesh* RotorMesh = MechEaster::Load<UStaticMesh>(MechEaster::DroneRotorPath());
	if (!RotorMesh)
	{
		UE_LOG(LogTemp, Display, TEXT("[easter] 缺旋翼模型 %s，无人机不画旋翼"), MechEaster::DroneRotorPath());
		return;
	}
	const FName Sockets[4] = {FName(TEXT("Rotor_FL")), FName(TEXT("Rotor_FR")), FName(TEXT("Rotor_BL")), FName(TEXT("Rotor_BR"))};
	const FVector Corners[4] = {FVector(55.f, -55.f, 15.f), FVector(55.f, 55.f, 15.f), FVector(-55.f, -55.f, 15.f), FVector(-55.f, 55.f, 15.f)};
	for (int32 k = 0; k < 4; ++k)
	{
		UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(this);
		R->SetStaticMesh(RotorMesh);
		R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		R->SetCastShadow(false);
		if (Body->DoesSocketExist(Sockets[k]))
		{
			R->SetupAttachment(Body, Sockets[k]);
		}
		else
		{
			R->SetupAttachment(Body);
			R->SetRelativeLocation(Corners[k]);
		}
		R->RegisterComponent();
		ApplyLivery(R);
		Rotors.Add(R);
	}
}

void AMechEasterBot::BuildDog()
{
	USkeletalMesh* Mesh = MechEaster::Load<USkeletalMesh>(MechEaster::DogMeshPath());
	if (!Mesh)
	{
		BuildPlaceholder();
		return;
	}
	AnimIdle = MechEaster::Load<UAnimSequence>(TEXT("/Game/Easter/Dog/A_PDog_Idle"));
	AnimWalk = MechEaster::Load<UAnimSequence>(TEXT("/Game/Easter/Dog/A_PDog_Walk"));
	AnimRun = MechEaster::Load<UAnimSequence>(TEXT("/Game/Easter/Dog/A_PDog_Run"));
	AnimSit = MechEaster::Load<UAnimSequence>(TEXT("/Game/Easter/Dog/A_PDog_Sit"));
	if (AnimIdle && AnimWalk && AnimRun)
	{
		// 有动作片段：单片段模式按状态换着播
		DogAnimMesh = NewObject<USkeletalMeshComponent>(this, TEXT("DogMesh"));
		DogAnimMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DogAnimMesh->SetupAttachment(Visual);
		DogAnimMesh->RegisterComponent();
		DogAnimMesh->SetSkeletalMeshAsset(Mesh);
		DogAnimMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		ApplyLivery(DogAnimMesh);
		PlayDogAnim(0, 0.f);
		return;
	}
	// 没有动作片段：可摆姿势的网格，代码按骨头名摆步态（UpdateDogPose）
	DogPoseMesh = NewObject<UPoseableMeshComponent>(this, TEXT("DogPose"));
	DogPoseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DogPoseMesh->SetupAttachment(Visual);
	DogPoseMesh->RegisterComponent();
	DogPoseMesh->SetSkinnedAssetAndUpdate(Mesh, true);
	ApplyLivery(DogPoseMesh);
	TArray<FName> Bones = {BoneBody, BoneHead, BoneTail};
	for (int32 k = 0; k < 4; ++k)
	{
		Bones.Add(LegUpper[k]);
		Bones.Add(LegLower[k]);
	}
	TArray<FString> Missing;
	for (const FName& B : Bones)
	{
		if (DogPoseMesh->GetBoneIndex(B) != INDEX_NONE)
		{
			RefRot.Add(B, DogPoseMesh->GetBoneTransformByName(B, EBoneSpaces::ComponentSpace).GetRotation());
		}
		else
		{
			Missing.Add(B.ToString());
		}
	}
	static bool bLogged = false;
	if (!bLogged)
	{
		bLogged = true;
		UE_LOG(LogTemp, Display, TEXT("[easter] 机械狗没有动作片段（A_PDog_Idle/Walk/Run），用代码摆步态%s"),
			Missing.Num() ? *FString::Printf(TEXT("；缺骨头 %s"), *FString::Join(Missing, TEXT(" "))) : TEXT(""));
	}
}

void AMechEasterBot::BuildPlaceholder()
{
	// -MechEasterPlaceholder：模型没到时只为测逻辑，用引擎自带的方块 / 圆柱（不进正式版）
	UStaticMesh* Cube = MechEaster::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube"));
	UStaticMesh* Cylinder = MechEaster::Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	UMaterialInterface* Paint = MechEaster::Load<UMaterialInterface>(TEXT("/Game/Robot/Materials/MI_Robot_PaintDark"));
	auto Part = [this, Paint](UStaticMesh* M, const FVector& At, const FVector& Scale) {
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Visual);
		C->SetRelativeLocation(At);
		C->SetRelativeScale3D(Scale);
		if (Paint)
		{
			C->SetMaterial(0, Paint);
		}
		C->RegisterComponent();
		ApplyLivery(C);
		Placeholder.Add(C);
		return C;
	};
	if (Kind == EMechEasterKind::Drone)
	{
		Part(Cube, FVector::ZeroVector, FVector(1.2f, 1.2f, 0.3f));
		for (const FVector& At : {FVector(60.f, -60.f, 20.f), FVector(60.f, 60.f, 20.f), FVector(-60.f, -60.f, 20.f), FVector(-60.f, 60.f, 20.f)})
		{
			Rotors.Add(Part(Cylinder, At, FVector(0.6f, 0.08f, 0.02f)));
		}
	}
	else
	{
		Part(Cube, FVector(0.f, 0.f, 70.f), FVector(1.f, 0.45f, 0.3f));
		Part(Cube, FVector(60.f, 0.f, 95.f), FVector(0.35f, 0.3f, 0.28f));
		for (const FVector& At : {FVector(38.f, -18.f, 30.f), FVector(38.f, 18.f, 30.f), FVector(-38.f, -18.f, 30.f), FVector(-38.f, 18.f, 30.f)})
		{
			Part(Cylinder, At, FVector(0.1f, 0.1f, 0.6f));
		}
	}
}

void AMechEasterBot::ApplyLivery(UMeshComponent* C)
{
	if (!C)
	{
		return;
	}
	// 种子各端一样 → 涂装各端一样。11-02 用户：每只三种颜色（主色 / 副色 / 点缀和灯），三种都完全随机（不再从配好的几组里挑、点缀也不再固定品牌色）。
	// 色相、饱和度、亮度都随机；点缀兼当灯，饱和度和亮度偏高一点，免得灯是灰的
	FRandomStream R(Seed);
	const FLinearColor A = FLinearColor::MakeFromHSV8(static_cast<uint8>(R.RandRange(0, 255)), static_cast<uint8>(R.RandRange(40, 255)), static_cast<uint8>(R.RandRange(40, 240)));
	const FLinearColor B = FLinearColor::MakeFromHSV8(static_cast<uint8>(R.RandRange(0, 255)), static_cast<uint8>(R.RandRange(40, 255)), static_cast<uint8>(R.RandRange(40, 240)));
	const FLinearColor Accent = FLinearColor::MakeFromHSV8(static_cast<uint8>(R.RandRange(0, 255)), static_cast<uint8>(R.RandRange(170, 255)), static_cast<uint8>(R.RandRange(190, 255)));
	const float Glow = R.FRandRange(6.f, 14.f);
	UMaterialInterface* Parent = MechEaster::Load<UMaterialInterface>(MechEaster::LiveryPath());
	for (int32 i = 0; i < C->GetNumMaterials(); ++i)
	{
		UMaterialInterface* Src = C->GetMaterial(i);
		if (!Src)
		{
			Src = Parent;
		}
		if (!Src)
		{
			continue;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Src, this);
		MID->SetVectorParameterValue(TEXT("PaintA"), A);
		MID->SetVectorParameterValue(TEXT("PaintB"), B);
		MID->SetVectorParameterValue(TEXT("Accent"), Accent);
		MID->SetScalarParameterValue(TEXT("AccentGlow"), Glow);
		C->SetMaterial(i, MID);
		LiveryMIDs.Add(MID);
	}
}

void AMechEasterBot::SetupMirrors()
{
	// 老街循环：±周期 处各放一个一样的球（同 MechLoop.h 第 2 条），跨接缝打得中
	if (!MechLoop::IsActive(GetWorld()))
	{
		return;
	}
	for (int32 k = 0; k < 2; ++k)
	{
		USphereComponent* S = NewObject<USphereComponent>(this);
		S->InitSphereRadius(HitBox->GetUnscaledSphereRadius());
		S->SetUsingAbsoluteLocation(true);
		S->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		S->SetCollisionObjectType(ECC_WorldDynamic);
		S->SetCollisionResponseToAllChannels(ECR_Ignore);
		S->SetGenerateOverlapEvents(false);
		S->SetupAttachment(HitBox);
		S->RegisterComponent();
		MirrorBoxes.Add(S);
	}
}

FVector AMechEasterBot::GetVisualCenter() const
{
	if (!Visual)
	{
		return GetActorLocation();
	}
	return Visual->GetComponentLocation() + FVector(0.f, 0.f, Kind == EMechEasterKind::Dog ? DogCenter : 0.f);
}

void AMechEasterBot::PlayDogAnim(uint8 Anim, float Speed)
{
	if (!DogAnimMesh)
	{
		return;
	}
	UAnimSequence* Seq = AnimIdle;
	float Rate = 1.f;
	switch (Anim)
	{
	case 1:
	case 5:
		Seq = AnimWalk;
		Rate = FMath::Clamp(Speed / DogWalk, 0.5f, 2.f);
		break;
	case 2:
		Seq = AnimRun;
		Rate = FMath::Clamp(Speed / DogRun, 0.6f, 4.f);
		break;
	case 3:
	case 4:
		Seq = AnimSit ? AnimSit.Get() : AnimIdle.Get();
		break;
	default:
		break;
	}
	if (Anim != PlayingAnim && Seq)
	{
		PlayingAnim = Anim;
		DogAnimMesh->PlayAnimation(Seq, true);
	}
	DogAnimMesh->SetPlayRate(Rate);
}

void AMechEasterBot::UpdateDogPose(float DeltaTime, float Speed)
{
	if (!DogPoseMesh || RefRot.IsEmpty())
	{
		return;
	}
	const uint8 Anim = RepAnim;
	const bool bSit = Anim == 3;
	const bool bSlide = Anim == 4;
	// 步频随速度（走路约 2 步/秒，狂奔封顶 9）；幅度随速度变大
	const float Freq = FMath::Clamp(Speed / 120.f, 0.f, 9.f);
	GaitPhase = FMath::Fmod(GaitPhase + DeltaTime * Freq * 2.f * PI, 2.f * PI);
	const float Amp = Speed < 10.f || bSit || bSlide ? 0.f : FMath::Clamp(Speed / DogRun * 28.f, 10.f, 38.f);
	const float T = VisClock + static_cast<float>(Seed % 97);
	const FVector Side = FVector::RightVector;  // 网格 +X 朝前：腿绕组件 Y 摆
	// 身体：蹲着时抬头（后半身压低），滑滑梯时后仰一点
	const float BodyPitch = bSit ? -18.f : bSlide ? -8.f : 0.f;
	const FQuat BodyQ = AxisRot(Side, BodyPitch);
	auto Set = [this](const FName& Bone, const FQuat& Q) {
		if (const FQuat* Ref = RefRot.Find(Bone))
		{
			DogPoseMesh->SetBoneRotationByName(Bone, (Q * *Ref).Rotator(), EBoneSpaces::ComponentSpace);
		}
	};
	Set(BoneBody, BodyQ);
	// 头：走路时歪一歪，站着时左右看；尾巴天线：跑起来摇得快
	const float HeadTilt = (Speed > 10.f ? 10.f : 6.f) * FMath::Sin(T * 1.3f);
	const float HeadLook = Speed > 10.f ? 0.f : 22.f * FMath::Sin(T * 0.45f);
	Set(BoneHead, BodyQ * AxisRot(FVector::UpVector, HeadLook) * AxisRot(FVector::ForwardVector, HeadTilt));
	const float Wag = 25.f * FMath::Sin(T * (Speed > 10.f ? 11.f : 4.f));
	Set(BoneTail, BodyQ * AxisRot(FVector::UpVector, Wag));
	for (int32 k = 0; k < 4; ++k)
	{
		const bool bRear = k >= 2;
		float Upper = Amp * FMath::Sin(GaitPhase + LegPhase[k]);
		float Knee = Amp * 1.1f * FMath::Max(0.f, FMath::Cos(GaitPhase + LegPhase[k]));  // 往前迈的那半步抬脚
		if (bSit && bRear)
		{
			Upper = -50.f;
			Knee = 85.f;
		}
		else if (bSlide)
		{
			Upper = bRear ? 30.f : -30.f;  // 前腿往前伸、后腿往后蹬
			Knee = 0.f;
		}
		Set(LegUpper[k], BodyQ * AxisRot(Side, Upper));
		Set(LegLower[k], BodyQ * AxisRot(Side, Upper + Knee));
	}
	DogPoseMesh->RefreshBoneTransforms();
}

void AMechEasterBot::UpdateVisuals(float DeltaTime)
{
	VisClock += DeltaTime;
	UWorld* World = GetWorld();
	FVector Offset = FVector::ZeroVector;
	FRotator Tilt = FRotator::ZeroRotator;
	FVector Scale(1.f);
	const FVector LocalVel = GetActorQuat().UnrotateVector(VisVel);
	float Speed = VisVel.Size2D();
	if (Kind == EMechEasterKind::Drone)
	{
		// 旋翼：相邻的反着转
		for (int32 k = 0; k < Rotors.Num(); ++k)
		{
			if (Rotors[k])
			{
				const float Angle = FMath::Fmod(VisClock * RotorSpin * ((k == 0 || k == 3) ? 1.f : -1.f) + k * 37.f, 360.f);
				Rotors[k]->SetRelativeRotation(FRotator(0.f, Angle, 0.f));
			}
		}
		// 机身：往前飞就低头、往侧面飞就侧倾，悬停时轻轻上下浮
		Tilt = FRotator(-FMath::Clamp(LocalVel.X / DroneSpeed * 18.f, -18.f, 18.f), 0.f, FMath::Clamp(LocalVel.Y / DroneSpeed * 18.f, -18.f, 18.f));
		Offset = FVector(0.f, 0.f, 15.f * FMath::Sin(VisClock * 2.1f + Seed % 17));
		const float Pulse = 0.75f + 0.25f * FMath::Sin(VisClock * 3.f + Seed % 13);
		for (UMaterialInstanceDynamic* MID : LiveryMIDs)
		{
			if (MID)
			{
				MID->SetScalarParameterValue(TEXT("AccentGlow"), 10.f * Pulse);
			}
		}
	}
	else
	{
		Offset = FVector(0.f, 0.f, -DogCenter);
		if (BotRole == EMechEasterRole::Stampede)
		{
			Speed = StampedeSpeed(MechEaster::ServerNow(World));
			// 越跑越快：拉长（快到光速时像一道线）
			Scale.X = 1.f + Speed / 6000.f;
		}
		// 爬梯子 / 滑滑梯：身体顺着坡
		if (RepAnim == 4 || RepAnim == 5)
		{
			Tilt.Pitch = FMath::RadiansToDegrees(FMath::Atan2(VisVel.Z, FMath::Max(VisVel.Size2D(), 1.f)));
		}
		else if (Speed > 10.f)
		{
			Offset.Z += 4.f * FMath::Abs(FMath::Sin(GaitPhase));  // 小跑时一颠一颠
		}
		const uint8 Anim = BotRole == EMechEasterRole::Stampede ? (Speed > 10.f ? 2 : 0) : RepAnim;
		if (DogAnimMesh)
		{
			PlayDogAnim(Anim, Speed);
		}
		else if (DogPoseMesh)
		{
			RepAnim = Anim;  // 本地画面用（服务器上也是这个值）
			UpdateDogPose(DeltaTime, Speed);
		}
	}
	Visual->SetRelativeRotation(Tilt);
	Visual->SetRelativeScale3D(Scale);
	if (MechLoop::IsActive(World))
	{
		// 画在离镜头最近的那一份；镜像的打击球跟着本体挪
		const FVector Draw = MechLoop::Near(World, GetActorLocation(), MechLoop::ViewLocation(World));
		Visual->SetWorldLocation(Draw + GetActorQuat().RotateVector(Offset));
		const float P = MechLoop::Period(World);
		for (int32 k = 0; k < MirrorBoxes.Num(); ++k)
		{
			if (MirrorBoxes[k])
			{
				MirrorBoxes[k]->SetWorldLocation(GetActorLocation() + FVector(k == 0 ? P : -P, 0.f, 0.f));
			}
		}
	}
	else
	{
		Visual->SetRelativeLocation(Offset);
	}
}

// ---------------------------------------------------------------- 每帧
void AMechEasterBot::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDead || bGone)
	{
		return;
	}
	if (BotRole == EMechEasterRole::Stampede)
	{
		TickStampede(DeltaTime);  // 各端按服务器时间自己算
	}
	else if (HasAuthority())
	{
		ServerTick(DeltaTime);
	}
	else
	{
		ClientSmooth(DeltaTime);
	}
	if (!bDead && !bGone)
	{
		UpdateVisuals(DeltaTime);
	}
}

void AMechEasterBot::ClientSmooth(float DeltaTime)
{
	// 位置按 15 次/秒到，中间按速度往前推一点再追过去（同保卫战杂兵）；循环街上按最近的那一份追，追完再挪回接缝里
	UWorld* World = GetWorld();
	const FVector Cur = GetActorLocation();
	FVector Want = FVector(RepPos) + FVector(RepVel) * 0.05f;
	Want = MechLoop::Near(World, Want, Cur);
	FVector P = FVector::DistSquared(Cur, Want) > FMath::Square(3000.f) ? Want : FMath::Lerp(Cur, Want, 1.f - FMath::Exp(-12.f * DeltaTime));
	if (MechLoop::IsActive(World))
	{
		P.X += MechLoop::WrapShift(World, P.X, MechLoop::MechMargin);
	}
	SetActorLocation(P);
	FRotator R = GetActorRotation();
	R.Yaw = FMath::RInterpTo(FRotator(0.f, R.Yaw, 0.f), FRotator(0.f, RepYaw, 0.f), DeltaTime, 8.f).Yaw;
	SetActorRotation(FRotator(0.f, R.Yaw, 0.f));
	VisVel = FMath::VInterpTo(VisVel, FVector(RepVel), DeltaTime, 6.f);
}

void AMechEasterBot::ServerTick(float DeltaTime)
{
	if (Kind == EMechEasterKind::Drone)
	{
		TickDrone(DeltaTime);
	}
	else
	{
		TickFriendlyDog(DeltaTime);
	}
	if (bDead || IsActorBeingDestroyed())
	{
		return;
	}
	RepPos = GetActorLocation();
	RepVel = Vel;
	RepYaw = GetActorRotation().Yaw;
	VisVel = Vel;
}

void AMechEasterBot::TickDrone(float DeltaTime)
{
	AMechEasterDirector* D = Director.Get();
	UWorld* World = GetWorld();
	FVector P = GetActorLocation();
	LifeLeft -= DeltaTime;
	if (LifeLeft <= 0.f)
	{
		// 到时间了：往上飞走，飞远了就拿掉
		Vel = FMath::VInterpTo(Vel, FVector(0.f, 0.f, 2500.f), DeltaTime, 1.5f);
		SetActorLocation(P + Vel * DeltaTime);
		if (LifeLeft < -8.f)
		{
			Remove();
		}
		return;
	}
	RetargetClock -= DeltaTime;
	if (RetargetClock <= 0.f)
	{
		RetargetClock = 1.5f;
		Target = D ? D->NearestTarget(P, DroneRange) : nullptr;
	}
	AActor* T = Target.Get();
	if (T && !MechEaster::IsTargetMech(Cast<AMechCharacter>(T)))
	{
		Target = nullptr;
		T = nullptr;
	}
	// 绕着目标在各自的半径和高度上盘旋（不分队伍：最近的机体就是目标）
	FVector Want = P;
	if (T)
	{
		const FVector TP = MechLoop::Near(World, T->GetActorLocation(), P);
		OrbitAngle += DeltaTime * 0.35f * ((Seed & 1) ? 1.f : -1.f);
		Want = TP + FVector(FMath::Cos(OrbitAngle), FMath::Sin(OrbitAngle), 0.f) * OrbitRadius + FVector(0.f, 0.f, OrbitHeight);
	}
	GroundClock -= DeltaTime;
	if (GroundClock <= 0.f)
	{
		GroundClock = 0.3f;
		Ground = D ? D->GroundZ(P + FVector(0.f, 0.f, 500.f), this) : P.Z - 2000.f;
	}
	Want.Z = FMath::Max(Want.Z, Ground + DroneClearance);
	const FVector To = Want - P;
	FVector WantVel = To.GetSafeNormal() * FMath::Min(DroneSpeed, To.Size() * 1.2f);
	// 前方 9 米有东西：往上抬一会儿
	AvoidClock -= DeltaTime;
	if (AvoidClock <= 0.f && !Vel.IsNearlyZero())
	{
		AvoidClock = 0.3f;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterDroneAvoid), false, this);
		if (World->LineTraceSingleByObjectType(Hit, P, P + Vel.GetSafeNormal() * 900.f, FCollisionObjectQueryParams(ECC_WorldStatic), Q))
		{
			Lift = 1.2f;
		}
	}
	if (Lift > 0.f)
	{
		Lift -= DeltaTime;
		WantVel.Z = FMath::Max(WantVel.Z, 1600.f);
	}
	Vel = FMath::VInterpTo(Vel, WantVel, DeltaTime, 2.f);
	P += Vel * DeltaTime;
	if (MechLoop::IsActive(World))
	{
		P.X += MechLoop::WrapShift(World, P.X, MechLoop::MechMargin);
	}
	SetActorLocation(P);
	// 机头对着目标（没有目标就顺着飞的方向）
	const FVector Face = T ? MechLoop::Near(World, T->GetActorLocation(), P) - P : Vel;
	if (!Face.IsNearlyZero())
	{
		const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, Face.Rotation().Yaw, 220.f * DeltaTime);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}
	// 开火：只在对战进行中；机枪一串 5 发，其他一次一发
	if (!T || !D || !MechEaster::FightOn(World))
	{
		return;
	}
	FVector Muzzle = P + GetActorForwardVector() * 80.f - FVector(0.f, 0.f, 30.f);
	if (Body && Body->DoesSocketExist(TEXT("Muzzle")))
	{
		// 插槽在画面上的机身上（循环街上画面可能在另一份），按相对画面的偏移换回本体
		Muzzle = P + (Body->GetSocketLocation(TEXT("Muzzle")) - Visual->GetComponentLocation());
	}
	if (BurstLeft > 0)
	{
		BurstClock -= DeltaTime;
		if (BurstClock <= 0.f)
		{
			BurstClock = 0.12f;
			--BurstLeft;
			D->FireWeapon(this, Weapon, Muzzle, T);
		}
		return;
	}
	FireClock -= DeltaTime;
	if (FireClock > 0.f)
	{
		return;
	}
	const FVector TP = MechLoop::Near(World, T->GetActorLocation(), P);
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterDroneLOS), false, this);
	Q.AddIgnoredActor(T);
	if (FVector::Dist(TP, P) > DroneFireRange || World->LineTraceSingleByObjectType(Hit, Muzzle, TP, FCollisionObjectQueryParams(ECC_WorldStatic), Q))
	{
		FireClock = 0.5f;  // 看不见 / 太远：过一会儿再看
		return;
	}
	switch (Weapon)
	{
	case EMechEasterWeapon::Gun:
		FireClock = FMath::FRandRange(2.2f, 3.f);
		BurstLeft = 5;
		BurstClock = 0.f;
		break;
	case EMechEasterWeapon::Rocket:
		FireClock = FMath::FRandRange(4.f, 5.f);
		D->FireWeapon(this, Weapon, Muzzle, T);
		break;
	case EMechEasterWeapon::Grenade:
		FireClock = FMath::FRandRange(4.5f, 6.f);
		D->FireWeapon(this, Weapon, Muzzle, T);
		break;
	case EMechEasterWeapon::Cannon:
		FireClock = FMath::FRandRange(3.5f, 4.5f);
		D->FireWeapon(this, Weapon, Muzzle, T);
		break;
	}
}

// ---------------------------------------------------------------- 苏联的狗
bool AMechEasterBot::DogLineClear(const FVector& From, const FVector& To) const
{
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterDogLine), false, this);
	const FVector Up(0.f, 0.f, 40.f - DogCenter);  // 脚底往上 40 厘米
	return !GetWorld()->LineTraceSingleByChannel(Hit, From + Up, To + Up, ECC_Visibility, Q);
}

bool AMechEasterBot::DogWalkTo(const FVector& Goal, float Speed, float DeltaTime)
{
	FVector P = GetActorLocation();
	FVector To = Goal - P;
	To.Z = 0.f;
	const float Dist = To.Size();
	if (Dist < 40.f)
	{
		Vel = FVector::ZeroVector;
		return true;
	}
	const FVector Dir = To / Dist;
	P += Dir * FMath::Min(Dist, Speed * DeltaTime);
	GroundClock -= DeltaTime;
	if (GroundClock <= 0.f)
	{
		GroundClock = 0.2f;
		if (const AMechEasterDirector* D = Director.Get())
		{
			Ground = D->GroundZ(FVector(P.X, P.Y, P.Z + 150.f), this);
		}
	}
	P.Z = FMath::FInterpTo(P.Z, Ground + DogCenter, DeltaTime, 10.f);
	SetActorLocation(P);
	Vel = Dir * Speed;
	const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, Dir.Rotation().Yaw, 360.f * DeltaTime);
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
	return false;
}

void AMechEasterBot::ReleaseSpot()
{
	AMechEasterDirector* D = Director.Get();
	if (D && D->GetSpots().IsValidIndex(DogSpot))
	{
		FMechEasterSpot& S = D->GetSpots()[DogSpot];
		for (TWeakObjectPtr<AMechEasterBot>& U : S.Users)
		{
			if (U.Get() == this)
			{
				U = nullptr;
			}
		}
	}
	DogSpot = -1;
}

void AMechEasterBot::DogPickNext()
{
	AMechEasterDirector* D = Director.Get();
	if (!D)
	{
		return;
	}
	ReleaseSpot();
	bThenSit = false;
	TArray<FMechEasterSpot>& Spots = D->GetSpots();
	const float Roll = FMath::FRand();
	// 滑梯（一次一只）、跷跷板（两头各一只）、蹲着、在院子里溜达
	TArray<int32> Free;
	const uint8 WantKind = static_cast<uint8>(Roll < 0.3f ? 0 : Roll < 0.55f ? 1 : Roll < 0.75f ? 2 : 255);
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		const FMechEasterSpot& S = Spots[i];
		if (S.Kind == WantKind && (!S.Users[0].IsValid() || (S.Kind == 1 && !S.Users[1].IsValid())))
		{
			Free.Add(i);
		}
	}
	if (Free.Num())
	{
		DogSpot = Free[FMath::RandHelper(Free.Num())];
		FMechEasterSpot& S = Spots[DogSpot];
		DogSlot = S.Users[0].IsValid() ? 1 : 0;
		S.Users[DogSlot] = this;
		DogClock = 15.f;  // 走过去最多这么久（被挡住就放弃）
		if (S.Kind == 0)
		{
			DogState = EDog::ToSlide;
			DogGoal = S.At - S.Dir * 100.f + FVector(0.f, 0.f, DogCenter);  // 梯子脚下
		}
		else if (S.Kind == 1)
		{
			DogState = EDog::ToSeesaw;
			DogGoal = S.At + S.Dir * (DogSlot ? 1.f : -1.f) * S.Len * 0.42f + FVector(0.f, 0.f, DogCenter);
		}
		else
		{
			DogState = EDog::Walk;
			bThenSit = true;
			DogGoal = S.At + FVector(FMath::FRandRange(-120.f, 120.f), FMath::FRandRange(-120.f, 120.f), DogCenter);
		}
		return;
	}
	if (WantKind == 2)
	{
		DogState = EDog::Sit;
		DogClock = FMath::FRandRange(4.f, 8.f);
		return;
	}
	// 溜达：院子里随便一个走得过去的点
	const FVector P = GetActorLocation();
	const FVector Home = D->GetDogHome();
	for (int32 Try = 0; Try < 6; ++Try)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		const FVector G = Home + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * FMath::FRandRange(0.f, D->GetDogRoam());
		const FVector Goal(G.X, G.Y, P.Z);
		if (DogLineClear(P, Goal))
		{
			DogState = EDog::Walk;
			DogGoal = Goal;
			DogClock = 12.f;
			return;
		}
	}
	DogState = EDog::Idle;
	DogClock = FMath::FRandRange(1.f, 2.f);
}

void AMechEasterBot::TickFriendlyDog(float DeltaTime)
{
	AMechEasterDirector* D = Director.Get();
	if (!D)
	{
		return;
	}
	DogClock -= DeltaTime;
	const FVector Before = GetActorLocation();
	FMechEasterSpot* S = D->GetSpots().IsValidIndex(DogSpot) ? &D->GetSpots()[DogSpot] : nullptr;
	switch (DogState)
	{
	case EDog::Idle:
		RepAnim = 0;
		Vel = FVector::ZeroVector;
		if (DogClock <= 0.f)
		{
			DogPickNext();
		}
		break;
	case EDog::Walk:
		RepAnim = 1;
		if (DogWalkTo(DogGoal, DogWalk, DeltaTime) || DogClock <= 0.f)
		{
			DogState = bThenSit ? EDog::Sit : EDog::Idle;
			DogClock = bThenSit ? FMath::FRandRange(5.f, 10.f) : FMath::FRandRange(1.f, 3.f);
		}
		break;
	case EDog::Sit:
		RepAnim = 3;
		Vel = FVector::ZeroVector;
		if (DogClock <= 0.f)
		{
			ReleaseSpot();
			DogState = EDog::Idle;
			DogClock = FMath::FRandRange(0.5f, 1.5f);
		}
		break;
	case EDog::ToSlide:
		RepAnim = 1;
		if (!S || DogClock <= 0.f)
		{
			ReleaseSpot();
			DogState = EDog::Idle;
		}
		else if (DogWalkTo(DogGoal, DogWalk, DeltaTime))
		{
			DogState = EDog::Climb;
			DogClock = ClimbTime;
		}
		break;
	case EDog::Climb:
	case EDog::Slide:
	{
		if (!S)
		{
			DogState = EDog::Idle;
			break;
		}
		// 爬梯子：从梯子脚下升到顶；滑：从顶顺着滑道滑到底（越滑越快）
		const FVector Base = S->At - S->Dir * 100.f;
		const FVector Top = S->At + FVector(0.f, 0.f, S->Height);
		const FVector Bottom = S->At + S->Dir * S->Len;
		const bool bClimb = DogState == EDog::Climb;
		const float K = FMath::Clamp(1.f - DogClock / (bClimb ? ClimbTime : SlideTime), 0.f, 1.f);
		const FVector Feet = bClimb ? FMath::Lerp(Base, Top, K) : FMath::Lerp(Top, Bottom, FMath::Pow(K, 1.6f));
		SetActorLocation(Feet + FVector(0.f, 0.f, DogCenter));
		const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, S->Dir.Rotation().Yaw, 360.f * DeltaTime);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		RepAnim = bClimb ? 5 : 4;
		if (DogClock <= 0.f)
		{
			if (bClimb)
			{
				DogState = EDog::Slide;
				DogClock = SlideTime;
			}
			else
			{
				// 滑完：一半的时候再玩一次
				Ground = Bottom.Z;
				if (FMath::FRand() < 0.5f)
				{
					DogState = EDog::ToSlide;
					DogGoal = Base + FVector(0.f, 0.f, DogCenter);
					DogClock = 12.f;
				}
				else
				{
					ReleaseSpot();
					DogState = EDog::Idle;
					DogClock = FMath::FRandRange(1.f, 2.5f);
				}
			}
		}
		break;
	}
	case EDog::ToSeesaw:
		RepAnim = 1;
		if (!S || DogClock <= 0.f)
		{
			ReleaseSpot();
			DogState = EDog::Idle;
		}
		else if (DogWalkTo(DogGoal, DogWalk, DeltaTime))
		{
			DogState = EDog::Seesaw;
			DogClock = FMath::FRandRange(8.f, 14.f);
		}
		break;
	case EDog::Seesaw:
	{
		if (!S)
		{
			DogState = EDog::Idle;
			break;
		}
		// 两头一上一下（板子本身是静止的模型，狗在上面一起一落）
		const FVector End = S->At + S->Dir * (DogSlot ? 1.f : -1.f) * S->Len * 0.42f;
		const float Ph = MechEaster::ServerNow(GetWorld()) * 2.2f + (DogSlot ? PI : 0.f);
		const float Z = S->At.Z + S->Height * (0.5f + 0.45f * FMath::Sin(Ph));
		SetActorLocation(FVector(End.X, End.Y, Z + DogCenter));
		const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, (S->At - End).Rotation().Yaw, 360.f * DeltaTime);
		SetActorRotation(FRotator(0.f, Yaw, 0.f));
		RepAnim = 3;
		if (DogClock <= 0.f)
		{
			Ground = S->At.Z;
			ReleaseSpot();
			DogState = EDog::Idle;
			DogClock = FMath::FRandRange(1.f, 2.f);
		}
		break;
	}
	case EDog::Flee:
		RepAnim = 2;
		if (DogWalkTo(DogGoal, DogRun, DeltaTime) || DogClock <= 0.f)
		{
			DogState = EDog::Idle;
			DogClock = FMath::FRandRange(1.5f, 3.f);
		}
		break;
	}
	// 爬 / 滑 / 跷跷板是直接摆位置的：速度按位移算（画面的倾斜、动作快慢用）
	if (DogState == EDog::Climb || DogState == EDog::Slide || DogState == EDog::Seesaw)
	{
		Vel = DeltaTime > 0.f ? (GetActorLocation() - Before) / DeltaTime : FVector::ZeroVector;
	}
}

// ---------------------------------------------------------------- 老街的狗群
float AMechEasterBot::StampedeSpeed(float Now) const
{
	const float T = Now - RunStart;
	return T <= 0.f ? 0.f : MechEaster::StampedeV0 * FMath::Exp(T / MechEaster::StampedeTau);
}

FVector AMechEasterBot::StampedePos(float Now) const
{
	// 速度 v = V0·e^(t/τ)，跑过的路 = V0·τ·(e^(t/τ) − 1)；左右轻轻摆
	const float T = FMath::Max(0.f, Now - RunStart);
	const float Dist = MechEaster::StampedeV0 * MechEaster::StampedeTau * (FMath::Exp(T / MechEaster::StampedeTau) - 1.f);
	const FVector Dir = FVector(RunDir);
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir);
	return FVector(RunOrigin) + Dir * Dist + Side * (60.f * FMath::Sin(T * 3.f + RunWeave));
}

void AMechEasterBot::TickStampede(float DeltaTime)
{
	UWorld* World = GetWorld();
	const float Now = MechEaster::ServerNow(World);
	const float Speed = StampedeSpeed(Now);
	const FVector Feet = StampedePos(Now);
	if (Speed >= MechEaster::StampedeLight)
	{
		// 到「光速」：一闪就没了（各端自己算，同一时刻）
		bGone = true;
		if (UNiagaraSystem* FX = MechEaster::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01")))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, GetVisualCenter(), FVector(RunDir).Rotation(), FVector(0.8f), true, true, ENCPoolMethod::AutoRelease);
		}
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		if (HasAuthority())
		{
			SetLifeSpan(1.5f);
		}
		return;
	}
	FVector Canon = Feet;
	if (MechLoop::IsActive(World))
	{
		Canon.X += MechLoop::WrapShift(World, Canon.X, 0.f);
	}
	SetActorLocationAndRotation(Canon + FVector(0.f, 0.f, DogCenter), FRotator(0.f, FVector(RunDir).Rotation().Yaw, 0.f));
	VisVel = FVector(RunDir) * Speed;
	Vel = VisVel;
	if (!HasAuthority())
	{
		return;
	}
	// 服务器：这一帧跑过的那一段扫过的机体都算被撞（快的时候一帧跑好几米，按线段算不会漏）
	const float T = Now - RunStart;
	if (T <= 0.f)
	{
		LastStampedeT = -1.f;
		return;
	}
	const FVector Prev = (LastStampedeT >= 0.f ? StampedePos(RunStart + LastStampedeT) : Feet) + FVector(0.f, 0.f, DogCenter);
	const FVector Cur = Feet + FVector(0.f, 0.f, DogCenter);
	LastStampedeT = T;
	const FVector Mid = (Prev + Cur) * 0.5f;
	const float Damage = MechEaster::StampedeBaseDamage * Speed / MechEaster::StampedeRefSpeed;
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		AMechCharacter* M = *It;
		if (!MechEaster::IsTargetMech(M))
		{
			continue;
		}
		const UCapsuleComponent* Cap = M->GetCapsuleComponent();
		const FVector C = MechLoop::Near(World, M->GetActorLocation(), Mid);
		const FVector Hit = FMath::ClosestPointOnSegment(C, Prev, Cur);
		if (FVector::Dist2D(Hit, C) > Cap->GetScaledCapsuleRadius() + 70.f || FMath::Abs(Hit.Z - C.Z) > Cap->GetScaledCapsuleHalfHeight() + 40.f)
		{
			continue;
		}
		float& Last = LastBump.FindOrAdd(TWeakObjectPtr<AActor>(M));
		if (Now - Last < 1.f)
		{
			continue;
		}
		Last = Now;
		const float Dealt = M->ReceiveHit(Damage, FMath::Min(Damage * 0.25f, 600.f), this, MechLoop::Near(World, Hit, M->GetActorLocation()), EMechHitKind::Missile);
		if (MechEaster::LogOn())
		{
			UE_LOG(LogTemp, Display, TEXT("[easter] 狗撞到 %s：速度 %.0f 米/秒，伤害 %.0f（实际 %.0f）"), *M->DisplayName, Speed / 100.f, Damage, Dealt);
		}
	}
}

// ---------------------------------------------------------------- 被打 / 死
float AMechEasterBot::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bDead || bGone || DamageAmount <= 0.f)
	{
		return 0.f;
	}
	AActor* From = DamageCauser;
	if (!From && EventInstigator)
	{
		From = EventInstigator->GetPawn();
	}
	if (BotRole == EMechEasterRole::Friendly)
	{
		// 苏联的狗打不坏：被打了就往反方向跑开
		const FVector P = GetActorLocation();
		FVector Away = From ? (P - From->GetActorLocation()).GetSafeNormal2D() : FMath::VRand().GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector::ForwardVector;
		}
		ReleaseSpot();
		DogState = EDog::Flee;
		DogClock = 3.f;
		DogGoal = P + Away * 1500.f;
		for (const float Turn : {0.f, 45.f, -45.f, 90.f, -90.f})
		{
			const FVector Goal = P + FRotator(0.f, Turn, 0.f).RotateVector(Away) * 1500.f;
			if (DogLineClear(P, Goal))
			{
				DogGoal = Goal;
				break;
			}
		}
		return 0.f;
	}
	// 只有机体打得坏（打掉不算击杀、不算积分）
	AMechCharacter* By = Cast<AMechCharacter>(From);
	if (!By)
	{
		return 0.f;
	}
	HP -= DamageAmount;
	if (HP <= 0.f)
	{
		HP = 0.f;
		Die(By);
	}
	return DamageAmount;
}

void AMechEasterBot::Die(AActor* Killer)
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	const AMechCharacter* M = Cast<AMechCharacter>(Killer);
	UE_LOG(LogTemp, Display, TEXT("[easter] %s 被 %s 打掉"), Kind == EMechEasterKind::Drone ? TEXT("无人机") : TEXT("狗"), M ? *M->DisplayName : TEXT("?"));
	MulticastDeathFX(GetActorLocation());
	OnRep_Dead();
	SetLifeSpan(0.6f);
}

void AMechEasterBot::Remove()
{
	if (HasAuthority() && !IsActorBeingDestroyed())
	{
		Destroy();
	}
}

void AMechEasterBot::OnRep_Dead()
{
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void AMechEasterBot::MulticastDeathFX_Implementation(FVector_NetQuantize At)
{
	UWorld* World = GetWorld();
	const FVector P = MechLoop::IsActive(World) ? MechLoop::Near(World, At, MechLoop::ViewLocation(World)) : FVector(At);
	if (UNiagaraSystem* FX = MechEaster::Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01")))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, FX, P, FRotator::ZeroRotator, FVector(1.3f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (USoundBase* Snd = MechEaster::Load<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small")))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Snd, P, FRotator::ZeroRotator, 0.45f, 1.3f);
	}
}

FString AMechEasterBot::DebugInfo() const
{
	const TCHAR* RoleName = BotRole == EMechEasterRole::Hostile ? TEXT("无人机") : BotRole == EMechEasterRole::Friendly ? TEXT("友好的狗") : TEXT("狗群");
	return FString::Printf(TEXT("%s %s 在 %s，血 %.0f/%.0f，状态 %d，目标 %s%s"), RoleName, *GetName(), *GetActorLocation().ToCompactString(), HP, MaxHP, RepAnim,
		*GetNameSafe(Target.Get()), bDead ? TEXT("（已打掉）") : bGone ? TEXT("（已消失）") : TEXT(""));
}
