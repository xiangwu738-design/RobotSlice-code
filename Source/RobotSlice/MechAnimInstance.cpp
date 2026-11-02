#include "MechAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "HAL/IConsoleManager.h"
#include "MechCharacter.h"
#include "MechCatalog.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "MechWeapons.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"

UMechAnimInstance::UMechAnimInstance()
{
	RootMotionMode = ERootMotionMode::IgnoreRootMotion;
	// 只在第一次构造（类默认对象）时加载；原生类的实例不会从类默认对象抄属性，所以每个实例都从这份缓存里拿
	static TArray<UAnimSequence*> Loaded = []
	{
		static const TCHAR* Dirs[8] = {TEXT("F"), TEXT("FR"), TEXT("RR"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("LL"), TEXT("FL")};
		TArray<FString> Names;
		Names.SetNum(MechClip::Num);
		Names[MechClip::Idle] = TEXT("M_Neutral_Stand_Idle_Loop");
		for (int32 d = 0; d < 8; ++d)
		{
			Names[MechClip::Walk + d] = FString::Printf(TEXT("M_Neutral_Walk_Loop_%s"), Dirs[d]);
			Names[MechClip::Run + d] = FString::Printf(TEXT("M_Neutral_Run_Loop_%s"), Dirs[d]);
		}
		// 下落循环（10-31 换）：以前是 Game Animation Sample 的 M_Neutral_Jump_Loop_Fall——片段本身就乱（膝盖折过 180 度、小腿穿过大腿、
		// 脚踝转 180–270 度、单帧转 110–178 度），一直靠 LimitLegs 压着。换成 UE4 模板人形的跳跃循环（Fall/ThirdPersonJump_Loop，
		// 左腿收起、右腿垂着，膝 74–115 度、每帧最多转 5 度），首尾接不上的地方不播（见 Update 里的下落片段时间）。
		// -MechFallClipOld = 用回旧片段（对比）；新片段还没导入时也用旧的
		static const bool bOldFallClip = FParse::Param(FCommandLine::Get(), TEXT("MechFallClipOld"));
		Names[MechClip::Fall] = !bOldFallClip && FPackageName::DoesPackageExist(TEXT("/Game/Robot/Anims/Fall/ThirdPersonJump_Loop"))
			? TEXT("Fall/ThirdPersonJump_Loop") : TEXT("M_Neutral_Jump_Loop_Fall");
		Names[MechClip::Crouch] = TEXT("M_Neutral_Crouch_Idle_Loop");
		// 武士刀动作包（UE4 人形重定向）
		static const TCHAR* KDirs[8] = {TEXT("Forward"), TEXT("ForwardRight"), TEXT("Right"), TEXT("BackRight"), TEXT("Back"), TEXT("BackLeft"),
			TEXT("Left"), TEXT("ForwardLeft")};
		static const TCHAR* KStagger[4] = {TEXT("Back"), TEXT("Left"), TEXT("Forward"), TEXT("Right")};  // 受击来自 前 右 后 左 → 往后 / 往左 / 往前 / 往右退
		Names[MechClip::KIdle] = TEXT("Katana/Idle_InPlace");
		for (int32 i = 0; i < 4; ++i)
		{
			Names[MechClip::KLight + i] = FString::Printf(TEXT("Katana/Combo1_Attack%d_InPlace"), i + 1);
			Names[MechClip::KStagger + i] = FString::Printf(TEXT("Katana/Stagger%s_InPlace"), KStagger[i]);
		}
		Names[MechClip::KHeavy] = TEXT("Katana/Attack2_InPlace");
		for (int32 d = 0; d < 8; ++d)
		{
			Names[MechClip::KDodge + d] = FString::Printf(TEXT("Katana/Dodge%s_InPlace"), KDirs[d]);
		}
		Names[MechClip::KDraw] = TEXT("Katana/Unsheathe_InPlace");
		Names[MechClip::KSheathe] = TEXT("Katana/Sheathe_InPlace");
		Names[MechClip::KBlockStart] = TEXT("Katana/BlockStart_InPlace");
		Names[MechClip::KBlockLoop] = TEXT("Katana/BlockLoop_InPlace");
		Names[MechClip::KBlockHit] = TEXT("Katana/BlockHit_InPlace");
		Names[MechClip::KBlockEnd] = TEXT("Katana/BlockEnd_InPlace");
		// 其他刀（顺序同 EMechMeleeWeapon 的 1–6 号；空的用武士刀那组补）：待机、轻击 1–4、重斩、格挡起手 / 持续 / 挡住 / 收手
		static const TCHAR* BladeNames[MechClip::BladeSets][MechClip::BladeStride] = {
			{TEXT("Longsword/Longs_Idle1"), TEXT("Longsword/Longs_Attack_R"), TEXT("Longsword/Longs_Attack_L"), TEXT("Longsword/Longs_Attack_RD"),
				TEXT("Longsword/Longs_Attack_D"), TEXT("Longsword/Longs_Attack_Thrust"), TEXT("Longsword/Longs_BlockStart"), TEXT("Longsword/Longs_BlockLoop"),
				TEXT("Longsword/Longs_BlockImpact"), TEXT("Longsword/Longs_BlockEnd")},
			{TEXT("SwordPro/Sword1h_Idle"), TEXT("SwordPro/Attack_Place_fast_Rdown_1"), TEXT("SwordPro/Attack_Place_fast_Ldown_1"),
				TEXT("SwordPro/Attack_Place_fast_T_1"), TEXT("SwordPro/Attack_Place_med_R_1"), TEXT("SwordPro/Attack_Move_strong_Rdown_1"), nullptr,
				TEXT("SwordPro/Sword1h_Guarding_Idle_1"), TEXT("SwordPro/Sword1h_Parry_Mid"), nullptr},
			{TEXT("GreatSwordIdle/GreatSword_Idle_InPlace"), TEXT("GreatSword/GreatSword_Attack01_InPlace"), TEXT("GreatSword/GreatSword_Attack02_InPlace"),
				TEXT("GreatSword/GreatSword_Attack03_InPlace"), TEXT("GreatSword/GreatSword_Attack04_InPlace"), TEXT("GreatSword/GreatSword_Attack06_InPlace"),
				TEXT("GreatSword/GreatSword_Defense_Start_InPlace"), TEXT("GreatSword/GreatSword_Defense_Loop_InPlace"),
				TEXT("GreatSword/GreatSword_Defense_Hit_InPlace"), TEXT("GreatSword/GreatSword_Defense_End_InPlace")},
			{TEXT("TwinSword/TwinSword_Idle_InPlace"), TEXT("TwinSword/TwinSword_Attack01_InPlace"), TEXT("TwinSword/TwinSword_Attack02_InPlace"),
				TEXT("TwinSword/TwinSword_Attack03_InPlace"), TEXT("TwinSword/TwinSword_Attack07_InPlace"), TEXT("TwinSword/TwinSword_Attack09_InPlace"),
				nullptr, nullptr, nullptr, nullptr},
			{TEXT("Hammer/Idle_InPlace"), TEXT("Hammer/Attack1_InPlace"), TEXT("Hammer/Attack2_InPlace"), TEXT("Hammer/Attack3_InPlace"),
				TEXT("Hammer/HeavyAttack1_InPlace"), TEXT("Hammer/LeapAttack_InPlace"), TEXT("Hammer/BlockStart_InPlace"), TEXT("Hammer/BlockLoop_InPlace"),
				TEXT("Hammer/BlockImpact_InPlace"), TEXT("Hammer/BlockEnd_InPlace")},
			// 匕首（10-30 加右键近身挥刀）：双匕首包四段轻击 + 一个重击；匕首不能格挡，格挡几格留空
			{TEXT("Dagger/TwinDaggers_Idle_InPlace"), TEXT("Dagger/TwinDaggers_Attack1_1_InPlace"), TEXT("Dagger/TwinDaggers_Attack1_2_InPlace"),
				TEXT("Dagger/TwinDaggers_Attack3_InPlace"), TEXT("Dagger/TwinDaggers_Attack14_InPlace"), TEXT("Dagger/TwinDaggers_Attack2_InPlace"),
				nullptr, nullptr, nullptr, nullptr},
		};
		for (int32 b = 0; b < MechClip::BladeSets; ++b)
		{
			for (int32 k = 0; k < MechClip::BladeStride; ++k)
			{
				Names[MechClip::BladeBase + b * MechClip::BladeStride + k] = BladeNames[b][k] ? BladeNames[b][k] : TEXT("");
			}
		}
		Names[MechClip::RifleAim] = TEXT("Rifle/Aim_CC");
		Names[MechClip::RifleAimUp] = TEXT("Rifle/Aim_U");
		Names[MechClip::RifleAimDown] = TEXT("Rifle/Aim_D");
		Names[MechClip::RifleReload] = TEXT("Rifle/Rifle_Reload_2");
		Names[MechClip::DualAim] = TEXT("DualPistol/Dual_Aim_IDLE");
		Names[MechClip::DualReload] = TEXT("DualPistol/Dual_Reload");
		Names[MechClip::Throw] = TEXT("Throw/Pistol_Grenade_Throw_Single_InPlace");
		Names[MechClip::FlyHover] = TEXT("Flight/A_Flight_Idle_A");
		Names[MechClip::FlyFast] = TEXT("Flight/A_Flight_FastMove_A");
		Names[MechClip::FlyLand] = TEXT("Flight/A_SuperheroLanding_A");
		Names[MechClip::MoveStart] = TEXT("StartsStops/SK_stand_to_jog_0_v2");
		Names[MechClip::MoveStop] = TEXT("StartsStops/SK_jog_to_stand_0_v2");
		// 起飞片段：0–0.17 秒收腿（膝弯到约 150 度）、0.17–0.33 秒蹬直，后面慢慢回悬停（用 Blender 读 FBX 每帧膝角看过）
		Names[MechClip::FlyKick] = TEXT("Flight/A_Flight_Hover_Start_A");
		TArray<UAnimSequence*> Out;
		for (const FString& Name : Names)
		{
			if (Name.IsEmpty())
			{
				Out.Add(nullptr);
				continue;
			}
			const FString Path = TEXT("/Game/Robot/Anims/") + Name;
			// 还没导入的组不报错（FObjectFinder 找不到会打错误日志），先查一下资源在不在
			UAnimSequence* Seq = nullptr;
			if (FPackageName::DoesPackageExist(Path))
			{
				ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(*Path);
				Seq = Finder.Object;
			}
			Out.Add(Seq);
		}
		// 其他刀缺的片段用武士刀的同一个位置补上（攻击、格挡都能看，只是不像那把刀）
		static const int32 Katana[MechClip::BladeStride] = {MechClip::KIdle, MechClip::KLight, MechClip::KLight + 1, MechClip::KLight + 2, MechClip::KLight + 3,
			MechClip::KHeavy, MechClip::KBlockStart, MechClip::KBlockLoop, MechClip::KBlockHit, MechClip::KBlockEnd};
		for (int32 b = 0; b < MechClip::BladeSets; ++b)
		{
			for (int32 k = 0; k < MechClip::BladeStride; ++k)
			{
				UAnimSequence*& Slot = Out[MechClip::BladeBase + b * MechClip::BladeStride + k];
				if (!Slot)
				{
					Slot = Out[Katana[k]];
				}
			}
		}
		return Out;
	}();
	Clips.Append(Loaded);  // 类默认对象也持有一份，加载的片段不会被回收
}

namespace
{
	/** 左脚落地的同步标记 */
	bool IsLeftPlant(FName Marker)
	{
		const FString S = Marker.ToString();
		return S.Equals(TEXT("L"), ESearchCase::IgnoreCase) || S.Equals(TEXT("Lfoot"), ESearchCase::IgnoreCase) ||
			S.Equals(TEXT("Foot_L"), ESearchCase::IgnoreCase) || S.Equals(TEXT("LeftFoot"), ESearchCase::IgnoreCase);
	}
}

void FMechAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	ResetLegStats();
	const UMechAnimInstance* Inst = CastChecked<UMechAnimInstance>(InAnimInstance);
	FString Report;
	for (int32 i = 0; i < MechClip::Num; ++i)
	{
		FClip& C = Clips[i];
		C = FClip();
		C.Seq = Inst->Clips.IsValidIndex(i) ? Inst->Clips[i].Get() : nullptr;
		if (!C.Seq)
		{
			continue;
		}
		C.Length = FMath::Max(C.Seq->GetPlayLength(), 0.01f);
		FTransform A, B;
		C.Seq->GetBoneTransform(A, FSkeletonPoseBoneIndex(0), FAnimExtractContext(0.0), false);
		C.Seq->GetBoneTransform(B, FSkeletonPoseBoneIndex(0), FAnimExtractContext((double)C.Length), false);
		C.Speed = FVector::Dist2D(A.GetLocation(), B.GetLocation()) / C.Length;
		for (const FAnimSyncMarker& M : C.Seq->AuthoredSyncMarkers)
		{
			if (IsLeftPlant(M.MarkerName))
			{
				C.Plants.Add(M.Time);
			}
		}
		for (const FAnimSyncMarker& M : C.Seq->AuthoredSyncMarkers)
		{
			if (M.MarkerName == TEXT("Hit") && C.HitAt <= 0.f)
			{
				C.HitAt = M.Time;
			}
		}
		C.Plants.Sort();
		if (C.Plants.IsEmpty())
		{
			C.Plants.Add(0.f);
		}
		C.Cycle = C.Length / C.Plants.Num();
		Report += FString::Printf(TEXT(" %d:%.0f/%.2fs/%d步"), i, C.Speed, C.Length, C.Plants.Num());
	}
	if (Clips[MechClip::Walk].Seq && Clips[MechClip::Walk].Speed > 10.f)
	{
		WalkSpeed = Clips[MechClip::Walk].Speed;
	}
	if (Clips[MechClip::Run].Seq && Clips[MechClip::Run].Speed > WalkSpeed + 50.f)
	{
		RunSpeed = Clips[MechClip::Run].Speed;
	}
	// 10-31 下落循环换成 UE4 跳跃循环：首尾接不上，改成在中间一段平滑来回播（Update 里）
	bFallSwing = Clips[MechClip::Fall].Seq && Clips[MechClip::Fall].Seq->GetName() == TEXT("ThirdPersonJump_Loop");
	static bool bLogged = false;
	if (!bLogged)
	{
		bLogged = true;
		UE_LOG(LogTemp, Display, TEXT("[anim] 移动动作（编号:速度 厘米每秒/时长/左脚落地次数）%s"), *Report);
	}
}

void FMechAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	bValid = false;
	const AMechCharacter* Mech = Cast<AMechCharacter>(InAnimInstance->TryGetPawnOwner());
	const USkeletalMeshComponent* Mesh = InAnimInstance->GetSkelMeshComponent();
	// 10-31 阵营机体：按网格查这台的头 / 踝限位（没有配装的网格——摄影棚、结算展示——也按网格认）
	const UObject* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (Asset != LimitsFor)
	{
		LimitsFor = Asset;
		const MechCatalog::FFrameBody* Body = MechCatalog::FrameBodyForMesh(Asset);
		HeadYawMax = Body ? Body->HeadYaw : 0.f;
		HeadPitchMax = Body ? Body->HeadPitch : 0.f;
		AnkleUpMax = Body ? Body->AnkleUp : 35.f;
	}
	if (!Mech || !Mesh)
	{
		return;
	}
	const float Scale = FMath::Max(Mesh->GetComponentScale().Z, 0.01f);
	// 模型查看模式：机体原地不动，按给定的假速度 / 空中 / 冲刺播动作
	// 别人的机体（10-31 联机抖动）：速度、在不在空中用插值缓冲回放出来的，跟画面上的位置是同一个时间点，而且连续——
	// 以前用复制下来的速度，每来一个包阶跃一次（高延迟抖动时走跑混合、步幅、倾斜跟着一跳一跳），落地 / 起跳也比画面早零点几秒
	bGlideLean = Mech->GetMechMovement()->GliderRules != nullptr && Mech->IsBoosting();
	WorldVelocity = Mech->bAnimOverride ? Mech->AnimOverrideVelocity : Mech->GetMechMovement()->GetVisualVelocity();
	// 按画面上（网格）的朝向换算：别人的机体胶囊朝向随网络更新一跳一跳，网格是平滑的
	LocalVelocity = Mech->GetMechMesh()->VisualActorQuat().UnrotateVector(WorldVelocity) / Scale;
	MeshToWorld = Mesh->GetComponentTransform();
	bAir = Mech->bAnimOverride ? Mech->bAnimOverrideAir : Mech->GetMechMovement()->IsVisualFalling();
	bBoost = Mech->bAnimOverride ? Mech->bAnimOverrideBoost : Mech->IsBoosting();
	// 空中向上喷射（模型查看里按「空中、不冲刺、往上走」算）
	bThrust = Mech->bAnimOverride ? (Mech->bAnimOverrideAir && !Mech->bAnimOverrideBoost && Mech->AnimOverrideVelocity.Z > 0.f)
		: Mech->IsThrustingUp();  // 10-31：以前直接读移动组件，客户端上别人的机体那里永远是假（只有服务器 / 操控者自己算），向上喷射时一直是下落姿势
	Action = FMechActionAnim();
	Mech->GetActionAnim(Action);
	static const bool bAnimLog = FParse::Param(FCommandLine::Get(), TEXT("MechAnimLog"));
	bLogThis = bAnimLog && Mech->IsLocallyControlled() && Mech->IsPlayerControlled();
	bValid = true;
}

void FMechAnimProxy::Update(float Dt)
{
	LegDt = FMath::Max(Dt, 0.f);  // 腿部限速用（暂停时为 0，不限速）
	for (FClip& C : Clips)
	{
		C.Weight = 0.f;
	}
	if (!bValid || Dt <= 0.f)
	{
		Clips[MechClip::Idle].Weight = 1.f;
		return;
	}
	const float V = LocalVelocity.Size2D();

	// 空中、地面冲刺滑行、落地下蹲
	if (bWasAir && !bAir)
	{
		LandW = FMath::Clamp(-LastVZ / 1400.f, 0.25f, 0.85f);
		if (LastVZ < -1900.f && Clips[MechClip::FlyLand].Seq)
		{
			HeroTime = 0.f;  // 从高处重重落下：单膝落地
		}
	}
	bWasAir = bAir;
	if (bAir)
	{
		LastVZ = WorldVelocity.Z;
	}
	// -MechAnimOldAir：空中用改之前的混法（对比截图用）
	static const bool bOldAir = FParse::Param(FCommandLine::Get(), TEXT("MechAnimOldAir"));
	// 离地后快一点换成空中姿势：离地头零点几秒里走跑循环还占着一半权重，腿一边跑一边悬空
	AirW = FMath::FInterpTo(AirW, bAir ? 1.f : 0.f, Dt, bAir ? (bOldAir ? 5.f : 8.f) : 14.f);
	// 空中推进：往前冲时超人式平飞（按朝前的程度），其余（横移、后退、向上喷）悬停
	const float FlatSpeed = LocalVelocity.Size2D();
	const float Ahead = FlatSpeed > 100.f ? FMath::Clamp(LocalVelocity.X / FlatSpeed, 0.f, 1.f) * FMath::Clamp((FlatSpeed - 300.f) / 600.f, 0.f, 1.f) : 0.f;
	const bool bPowered = bAir && (bBoost || bThrust);
	// 空中在减速（松了方向键滑行、反向急停）：沿速度方向的加速度明显为负就不平飞，立起来用悬停姿势刹住（顺着加速度的后仰由下面的倾斜给）。
	// 速度变化按 0.1 秒左右抹平，免得一帧的碰撞 / 截图卡顿误判
	const FVector FlatV(WorldVelocity.X, WorldVelocity.Y, 0.f);
	const FVector FlatAccel = FVector((WorldVelocity - PrevVelocity) / Dt) * FVector(1.f, 1.f, 0.f);
	AirDecel = FMath::FInterpTo(AirDecel, FlatV.Size() > 150.f ? -FVector::DotProduct(FlatAccel, FlatV.GetSafeNormal()) : 0.f, Dt, 10.f);
	const float Brake = bOldAir ? 0.f : FMath::Clamp((AirDecel - 150.f) / 300.f, 0.f, 1.f);
	const float FlyWant = bPowered && bBoost ? Ahead * (1.f - Brake) * (1.f - FMath::Clamp(Action.GunAim, 0.f, 1.f)) : 0.f;
	// 收平飞要比进平飞快：空中松方向键 / 反向急停时机体会原地掉头，平飞（身体躺平）还没收完就跟着转，
	// 看着是一块横着的板在空中打转、腿甩来甩去；急停时先立起来变悬停，再起速才重新躺平
	const float FlyRate = bOldAir || FlyWant > FlyW ? 4.f : 9.f;
	FlyW = FMath::FInterpTo(FlyW, FlyWant, Dt, FlyRate);
	HoverW = FMath::FInterpTo(HoverW, bPowered ? 1.f - FlyWant : 0.f, Dt, bOldAir ? 4.f : 6.f);
	if (HeroTime >= 0.f)
	{
		HeroTime += Dt;
		if (HeroTime >= Clips[MechClip::FlyLand].Length || bAir)
		{
			HeroTime = -1.f;
		}
	}
	GlideW = FMath::FInterpTo(GlideW, bBoost && !bAir ? 1.f : 0.f, Dt, 7.f);
	LandW = FMath::FInterpTo(LandW, 0.f, Dt, 6.f);
	MoveW = FMath::FInterpTo(MoveW, FMath::Clamp(V / (WalkSpeed * 0.5f), 0.f, 1.f), Dt, 10.f);
	Gait = FMath::FInterpTo(Gait, FMath::Clamp((V - WalkSpeed) / (RunSpeed - WalkSpeed), 0.f, 1.f), Dt, 6.f);
	if (V > 10.f)
	{
		// 0 = 前，90 = 右（机体自己的朝向）
		const float Want = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
		Angle += FMath::FindDeltaAngleDegrees(Angle, Want) * FMath::Min(1.f, Dt * 10.f);
	}
	const float A = FMath::Fmod(FMath::Fmod(Angle, 360.f) + 360.f, 360.f) / 45.f;
	const int32 D0 = FMath::FloorToInt(A) % 8;
	const int32 D1 = (D0 + 1) % 8;
	const float Frac = A - FMath::FloorToFloat(A);

	// 权重：地面 =（站立 / 走跑 / 滑行）和蹲按比例混，空中换下落循环
	const float Ground = 1.f - AirW;
	const float CrouchAmt = FMath::Clamp(GlideW * 0.45f + LandW, 0.f, 0.9f);
	const float Stand = Ground * (1.f - CrouchAmt);
	const float Loco = Stand * (1.f - GlideW);
	const float Moving = Loco * MoveW;
	auto Add = [this](int32 i, float W) { Clips[Clips[i].Seq ? i : MechClip::Idle].Weight += W; };
	Add(MechClip::Idle, Loco * (1.f - MoveW) + Stand * GlideW);
	Add(MechClip::Crouch, Ground * CrouchAmt);
	const float Powered = FMath::Min(1.f, FlyW + HoverW);
	Add(MechClip::Fall, AirW * (1.f - Powered));
	if (Clips[MechClip::FlyFast].Seq && Clips[MechClip::FlyHover].Seq)
	{
		Add(MechClip::FlyFast, AirW * FlyW);
		Add(MechClip::FlyHover, AirW * FMath::Max(0.f, Powered - FlyW));
	}
	else
	{
		Add(MechClip::Fall, AirW * Powered);
	}
	Add(MechClip::Walk + D0, Moving * (1.f - Gait) * (1.f - Frac));
	Add(MechClip::Walk + D1, Moving * (1.f - Gait) * Frac);
	Add(MechClip::Run + D0, Moving * Gait * (1.f - Frac));
	Add(MechClip::Run + D1, Moving * Gait * Frac);

	// 步频：混合后的步幅 × 步频 = 实际速度，限制在自然步频附近（太慢就有点滑步，太快就有点拖步）
	float Stride = 0.f, StrideW = 0.f;
	for (int32 i = MechClip::Walk; i < MechClip::Fall; ++i)
	{
		if (Clips[i].Seq && Clips[i].Weight > 0.f)
		{
			Stride += Clips[i].Weight * Clips[i].Speed * Clips[i].Cycle;
			StrideW += Clips[i].Weight;
		}
	}
	const float WalkRate = 1.f / FMath::Max(Clips[MechClip::Walk].Cycle, 0.1f);
	const float RunRate = 1.f / FMath::Max(Clips[MechClip::Run].Cycle, 0.1f);
	const float Rate = StrideW > 0.001f && Stride > 1.f ? FMath::Clamp(V / (Stride / StrideW), WalkRate * 0.7f, RunRate * 1.5f) : WalkRate;
	// 空中不迈步：脚不着地时步频照速度算（滑翔 15 米/秒就按跑步最快的步频倒腾），离地、落地那零点几秒里走跑还占着一些权重，
	// 腿就在空中飞快地蹬；按着地的程度放慢，落地时从停住的那一步接着走
	Phase = FMath::Fmod(Phase + Rate * Dt * (bOldAir ? 1.f : 1.f - AirW), 840.f);  // 840 能被 1–8 整除，各片段的步数循环回绕时不跳
	const int32 Step = FMath::FloorToInt(Phase);
	const float In = Phase - Step;
	for (int32 i = MechClip::Walk; i < MechClip::Fall; ++i)
	{
		// 第 k 次左脚落地到下一次之间按相位线性取时间：整段循环都用上，各方向的脚步对齐
		FClip& C = Clips[i];
		const int32 N = C.Plants.Num();
		if (N == 0)
		{
			continue;  // 缺这个片段
		}
		const int32 K = Step % N;
		const float From = C.Plants[K];
		const float To = K + 1 < N ? C.Plants[K + 1] : C.Plants[0] + C.Length;
		C.Time = FMath::Fmod(From + In * (To - From), C.Length);
	}
	auto Loop = [Dt](float& T, const FClip& C) { T = FMath::Fmod(T + Dt, C.Length); return T; };
	Clips[MechClip::Idle].Time = Loop(IdleTime, Clips[MechClip::Idle]);
	if (bFallSwing)
	{
		// UE4 跳跃循环 0.7 秒，最后一帧膝盖一下跳回 25 度（不是无缝循环）：只用 0.05–0.64 秒这段（收起的左膝 106 → 74 → 81 度慢慢摆），
		// 按余弦来回播——两头速度为零，掉头不顿；来回一趟 1.2 秒，平均和原速一样
		constexpr float SwingA = 0.05f;
		constexpr float SwingB = 0.64f;
		FallTime = FMath::Fmod(FallTime + Dt, 2.f * (SwingB - SwingA));
		Clips[MechClip::Fall].Time = SwingA + (SwingB - SwingA) * 0.5f * (1.f - FMath::Cos(PI * FallTime / (SwingB - SwingA)));
	}
	else
	{
		Clips[MechClip::Fall].Time = Loop(FallTime, Clips[MechClip::Fall]);
	}
	Clips[MechClip::Crouch].Time = Loop(CrouchTime, Clips[MechClip::Crouch]);
	if (Clips[MechClip::FlyFast].Seq && Clips[MechClip::FlyHover].Seq)
	{
		Clips[MechClip::FlyFast].Time = Loop(FlyTime, Clips[MechClip::FlyFast]);
		Clips[MechClip::FlyHover].Time = Loop(HoverTime, Clips[MechClip::FlyHover]);
	}
	StanceTime += Dt;  // 持刀 / 端枪待机：按各自片段长度取模（Evaluate 里）

	// 起步：静止一会儿之后往前起跑；急停：跑着突然停下（只在地面、没在出招时，Evaluate 里压在移动层上）
	if (!bAir && V > WalkSpeed * 0.6f && PrevSpeed < 60.f && StillTime > 0.25f && LocalVelocity.X > V * 0.7f && Clips[MechClip::MoveStart].Seq)
	{
		StartTime = 0.f;
		StopTime = -1.f;
	}
	if (!bAir && V < 40.f && PrevSpeed > RunSpeed * 0.6f && Clips[MechClip::MoveStop].Seq)
	{
		StopTime = 0.f;
		StartTime = -1.f;
	}
	StillTime = V < 40.f ? StillTime + Dt : 0.f;
	PrevSpeed = V;
	if (StartTime >= 0.f)
	{
		StartTime += Dt;
		if (StartTime > FMath::Min(Clips[MechClip::MoveStart].Length, 0.6f) || bAir)
		{
			StartTime = -1.f;
		}
	}
	if (StopTime >= 0.f)
	{
		StopTime += Dt;
		if (StopTime > FMath::Min(Clips[MechClip::MoveStop].Length, 0.9f) || bAir || V > WalkSpeed * 0.5f)
		{
			StopTime = -1.f;
		}
	}

	// 倾斜：顺着加速度倾（起步前倾、刹车后仰、快速闪避猛地一歪），冲刺滑行时再往速度方向压低
	FVector Accel = (WorldVelocity - PrevVelocity) / Dt;
	PrevVelocity = WorldVelocity;
	Accel.Z = 0.f;
	FVector Want = Accel.GetClampedToMaxSize(3000.f) / 3000.f * 9.f;
	const FVector Flat(WorldVelocity.X, WorldVelocity.Y, 0.f);
	Want += Flat.GetSafeNormal() * GlideW * 8.f * FMath::Clamp(Flat.Size() / 1500.f, 0.f, 1.f);
	float LeanRate = 5.f;
	// 11-02 石林飞行器：身子顺着航迹压下去（平飞前倾 22°，俯冲最多 80°，爬升时立起来一点）
	if (bGlideLean && bAir && WorldVelocity.SizeSquared() > 250000.f)
	{
		const float SinP = WorldVelocity.Z / WorldVelocity.Size();
		const FVector FlatDir = Flat.SizeSquared() > 2500.f ? Flat.GetSafeNormal() : LeanVec.GetSafeNormal();
		Want = FlatDir * (22.f + (SinP < 0.f ? -SinP * 58.f : -SinP * 14.f));
		LeanRate = 6.f;
	}
	static const bool bMeleeOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld"));
	if (Action.bFullMelee && !bMeleeOld)
	{
		// 出刀（10-30 刀剑手感）：不按加速度倾——突进是先猛加速、快砍到时减速，按加速度倾就是起步往前一扑、
		// 冲到跟前反而往后仰着刹车，刀还没落下身子先缩回去了。改成顺着突进速度前倾（20 米/秒以上约 12 度，压着冲过去），
		// 砍下后速度收掉、身子跟着慢慢立起来；空中三维突进只按水平那部分
		Want = Flat.GetSafeNormal() * 12.f * FMath::Clamp((Flat.Size() - 300.f) / 1700.f, 0.f, 1.f);
		LeanRate = 10.f;
	}
	LeanVec = FMath::VInterpTo(LeanVec, Want, Dt, LeanRate);
	const FVector LeanTotal = LeanVec + Action.RecoilLean;  // 开火后仰不走平滑（要一下顶出去）
	const float Deg = LeanTotal.Size();
	if (Deg > 0.05f)
	{
		const FVector Axis = MeshToWorld.InverseTransformVectorNoScale(FVector::UpVector ^ (LeanTotal / Deg)).GetSafeNormal();
		Lean = FQuat(Axis, FMath::DegreesToRadians(Deg));
	}
	else
	{
		Lean = FQuat::Identity;
	}

	// 出刀：腿的混合状态（10-30 刀剑手感）。突进程度按速度算、命中前只升不降（腿的时间不能倒着走）；
	// 腿交给移动循环只在可取消之后、玩家走起来时（之前不吃移动输入，砍下后的那点惯性滑行腿照片段站稳）
	const bool bNewMelee = Action.FullClip != PrevFullClip || Action.FullTime + 0.001f < PrevFullTime || PrevFullWeight <= 0.f;
	if (Action.bFullMelee)
	{
		if (bNewMelee)
		{
			MeleeLungeW = 0.f;
			MeleeMoveLegs = 0.f;
			// 从待机 / 移动起手（上一帧没在出刀）：起手不裁，从片段第 0 帧播（待机姿势接得上）；接在上一段后面才裁（见 MeleeClipStart）
			bMeleeFromIdle = !(bPrevMelee && PrevFullWeight > 0.05f);
		}
		if (Action.FullTime < Action.FullStrikeAt)
		{
			const float Want3D = FMath::Clamp((WorldVelocity.Size() - 500.f) / 1500.f, 0.f, 1.f);
			MeleeLungeW = FMath::Max(MeleeLungeW, FMath::FInterpTo(MeleeLungeW, Want3D, Dt, 14.f));
		}
		const bool bFree = Action.FullTime >= Action.FullCancel;
		MeleeMoveLegs = FMath::FInterpTo(MeleeMoveLegs, bFree ? FMath::Clamp((WorldVelocity.Size2D() - 150.f) / 350.f, 0.f, 1.f) : 0.f, Dt, 10.f);
	}
	else
	{
		MeleeLungeW = 0.f;
		MeleeMoveLegs = 0.f;
	}
	// 动作层跳变：换了片段、同一片段从头再来、权重一帧变了很多（打断、起手）→ 从上一帧画面上的姿势过渡过去（Evaluate 里做）
	if (!bMeleeOld)
	{
		const bool bFullJump = (Action.FullClip != PrevFullClip && FMath::Max(Action.FullWeight, PrevFullWeight) > 0.05f) ||
			(Action.FullClip >= 0 && Action.FullClip == PrevFullClip && Action.FullTime + 0.001f < PrevFullTime && PrevFullWeight > 0.05f) ||
			FMath::Abs(Action.FullWeight - PrevFullWeight) > 0.3f;
		const bool bUpperJump = (Action.UpperClip != PrevUpperClip && FMath::Max(Action.UpperWeight, PrevUpperWeight) > 0.05f) ||
			(Action.UpperClip >= 0 && Action.UpperClip == PrevUpperClip && !Action.bUpperLoop && Action.UpperTime + 0.001f < PrevUpperTime && PrevUpperWeight > 0.05f) ||
			FMath::Abs(Action.UpperWeight - PrevUpperWeight) > 0.3f;
		if (bFullJump || bUpperJump)
		{
			bBlendStart = true;
			BlendDuration = FMath::Clamp(Action.BlendTime, 0.05f, 0.25f);
			BlendElapsed = Dt;  // 这一帧已经是起点之后一帧
		}
		else
		{
			BlendElapsed += Dt;
		}
	}
	PrevFullClip = Action.FullClip;
	PrevFullTime = Action.FullTime;
	PrevFullWeight = Action.FullWeight;
	bPrevMelee = Action.bFullMelee;
	PrevUpperClip = Action.UpperClip;
	PrevUpperTime = Action.UpperTime;
	PrevUpperWeight = Action.UpperWeight;
	LastFrameDt = FrameDt;
	FrameDt = Dt;

	LogTime += Dt;
	LogClock += Dt;
	if (bLogThis && LogClock >= 0.1f)
	{
		LogClock = 0.f;
		static const TCHAR* Short[] = {TEXT("站"), TEXT("走"), TEXT("跑"), TEXT("落"), TEXT("蹲"), TEXT("平飞"), TEXT("悬停")};
		FString W;
		auto Sum = [this](int32 From, int32 To) { float S = 0.f; for (int32 i = From; i < To; ++i) S += Clips[i].Weight; return S; };
		const float Parts[] = {Clips[MechClip::Idle].Weight, Sum(MechClip::Walk, MechClip::Run), Sum(MechClip::Run, MechClip::Fall), Clips[MechClip::Fall].Weight,
			Clips[MechClip::Crouch].Weight, Clips[MechClip::FlyFast].Weight, Clips[MechClip::FlyHover].Weight};
		for (int32 i = 0; i < UE_ARRAY_COUNT(Parts); ++i)
		{
			if (Parts[i] > 0.02f)
			{
				W += FString::Printf(TEXT(" %s%.2f"), Short[i], Parts[i]);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[animlog] %.2f 空中%d 推%d 喷%d 速度(前%.0f 右%.0f 上%.0f) 空%.2f 平飞%.2f 悬停%.2f 走跑%.2f 方向%.0f 起步%.2f 急停%.2f 单膝%.2f 全身%d×%.2f 蹬墙%.2f 倾%.1f 减速%.0f 下落片段%.2f秒 限幅前膝%.0f/%.0f 髋%.0f/%.0f |%s"),
			LogTime, bAir ? 1 : 0, bBoost ? 1 : 0, bThrust ? 1 : 0, LocalVelocity.X, LocalVelocity.Y, WorldVelocity.Z, AirW, FlyW, HoverW, MoveW, Angle, StartTime,
			StopTime, HeroTime, Action.FullClip, Action.FullWeight, FMath::Min(Action.KickTime, 9.f), LeanVec.Size(), AirDecel,
			Clips[MechClip::Fall].Time, RawKnee[0], RawKnee[1], RawHip[0], RawHip[1], *W);
		// 腿部关节限位：这 0.1 秒里各量限位前→后的极值（度），每帧最大转角（限速前→后）、限速起作用的帧数
		const float* R = LegStatRaw;
		const float* O = LegStatOut;
		UE_LOG(LogTemp, Display, TEXT("[leglimit] %.2f 空%.2f 髋屈 %.0f→%.0f 髋后摆 %.0f→%.0f 外展 %.0f→%.0f 内收 %.0f→%.0f 髋扭 %.0f→%.0f | 膝弯 %.0f→%.0f 反折 %.0f→%.0f 膝侧弯 %.0f→%.0f 膝扭 %.0f→%.0f | 踝上 %.0f→%.0f 踝下 %.0f→%.0f 踝偏 %.0f→%.0f 踝翻 %.0f→%.0f | 每帧最大转角 %.1f→%.1f 限速帧 %d"),
			LogTime, AirW, R[HipFlexMax], O[HipFlexMax], R[HipFlexMin], O[HipFlexMin], R[HipSideMax], O[HipSideMax], R[HipSideMin], O[HipSideMin], R[HipTwist], O[HipTwist],
			R[KneeFlexMax], O[KneeFlexMax], R[KneeFlexMin], O[KneeFlexMin], R[KneeSide], O[KneeSide], R[KneeTwist], O[KneeTwist],
			R[AnkPitchMax], O[AnkPitchMax], R[AnkPitchMin], O[AnkPitchMin], R[AnkSide], O[AnkSide], R[AnkRoll], O[AnkRoll], R[StepMax], O[StepMax], LegCapped);
		ResetLegStats();
		if (HeadYawMax > 0.f)
		{
			UE_LOG(LogTemp, Display, TEXT("[headlimit] %.2f %s 偏航 %.0f→%.0f（限 %.0f） 俯仰 %.0f→%.0f 侧歪 %.0f→%.0f（限 %.0f） 踝上翘上限 %.0f"), LogTime,
				*GetNameSafe(LimitsFor), HeadStatRaw[0], HeadStatOut[0], HeadYawMax, HeadStatRaw[1], HeadStatOut[1], HeadStatRaw[2], HeadStatOut[2], HeadPitchMax,
				AnkleUpMax);
			HeadStatRaw[0] = HeadStatRaw[1] = HeadStatRaw[2] = 0.f;
			HeadStatOut[0] = HeadStatOut[1] = HeadStatOut[2] = 0.f;
		}
	}
}

float FMechAnimProxy::MapTime(int32 Clip, float T, float StrikeAt, float Duration, bool bLoop) const
{
	const FClip& C = Clips[Clip];
	if (bLoop)
	{
		return FMath::Fmod(FMath::Max(T, 0.f), C.Length);
	}
	if (StrikeAt > 0.f && C.HitAt > 0.f && Duration > StrikeAt)
	{
		// 命中前：片段从头播到 Hit 标记；命中后：剩下的部分拉到这一招结束
		if (T < StrikeAt)
		{
			return T / StrikeAt * C.HitAt;
		}
		return FMath::Min(C.HitAt + (T - StrikeAt) / (Duration - StrikeAt) * (C.Length - C.HitAt), C.Length);
	}
	if (Duration > 0.f)
	{
		return FMath::Clamp(T / Duration * C.Length, 0.f, C.Length);
	}
	return FMath::Clamp(T, 0.f, C.Length);
}

// ---------------------------------------------------------------- 出刀片段的分段时间映射（10-31 用户：快刀、双刀、匕首连招段与段之间还有跳变）
// 原因：出招快，片段被压得很短。攻击片段都是「待机 → 起手 → 挥砍 → 收刀 → 回到待机」一整段（双刀 1.7 秒、快刀 2 秒），以前命中前从第 0 帧播到 Hit、
// 命中后把剩下的整段（1.3–1.5 秒）拉到这一招结束：双刀轻击二从出招到可取消只有 0.18 秒，片段要播 0.8 秒（×4.4），收招那一截 ×5–6；
// 连着按时每一段都在可取消时刻被下一段切掉——上一段停在「正往待机收」的半路（收得飞快），下一段从片段第 0 帧（待机姿势）重新起手（也飞快），
// 30 帧下刀尖一帧挪 1–3 米，中间还绕回待机姿势一趟。改成按阶段映射、每段不超过 mech.MeleeClipRate 倍速（默认 1 = 原速）：
//  · 命中前：片段从 Hit − StrikeAt × 倍速 播到 Hit（起手比这长就裁掉开头的「从待机抬手」，从起手半路接上），命中时刻不变；
//  · 命中 → 可取消：从 Hit 往后播 (Cancel − StrikeAt) × 倍速（一小段收刀，刀顺着挥出去的方向收住），连按时下一段从这里接；
//  · 可取消 → 结束：没接下一段才走到这里，往收招播（最多 1.5 倍上限），不必播到片段结尾——这一招末尾的权重淡出会混回持刀待机。
// 出招时刻表（MeleeSpecs）、伤害判定时刻一点没动：-MechMeleeFeelTest 改前改后两次跑，每一帧的出招段号、出招时间、顿帧、离木桩距离完全一样。
// 只给快刀、单手刀用（游戏里实测这两把段内和段间都明显更顺，见 README「连招段间跳变」）；双刀、匕首试过没变好（这两个包的挥砍本身就极快——
// 双刀动捕手腕 35–43 米/秒、匕首 55 米/秒，相邻两段又不是同一套连招，跳变主要来自片段内容，光改时间映射压不下去），其余几把没人说有问题，都照旧
namespace
{
	/** 这个片段是不是快刀 / 单手刀的攻击（MechClip 的刀组：快刀 = 第 0 组、单手刀 = 第 1 组） */
	bool UsePhaseMap(int32 Clip)
	{
		const int32 Set = Clip >= MechClip::BladeBase ? (Clip - MechClip::BladeBase) / MechClip::BladeStride : -1;
		return Set == 0 || Set == 1;
	}
	// 上限 1 = 片段最快按原速播（动捕本身的挥砍就够快了）。离线按片段算过 1 / 1.5 / 2（tools/blade_clip_curves.py 的轨迹 + 同样的映射和过渡）：
	// 七把刀连招手腕每帧位移、二阶差都是 1 最小（双刀 2 倍时衔接处比改前还大：上一段停在刚砍完、下一段从起手半路开始，过渡要跨一大截），游戏里实测见 README
	TAutoConsoleVariable<float> CVarMeleeClipRate(TEXT("mech.MeleeClipRate"), 1.f,
		TEXT("出刀片段每段最快按几倍速播（10-31 连招段间跳变）：命中前超过就裁掉起手开头，命中后只播这么快的一小段收刀。默认 1（原速）"));
}

float FMechAnimProxy::MeleeClipStart(int32 Clip, float StrikeAt) const
{
	const FClip& C = Clips[Clip];
	if (bMeleeFromIdle && Clip == Action.FullClip)
	{
		return 0.f;  // 从待机 / 移动起手：片段第 0 帧就是持刀待机姿势，照旧整段起手（裁了反而一出招先跳一下）
	}
	const float Rate = FMath::Max(CVarMeleeClipRate.GetValueOnAnyThread(), 0.5f);
	return C.HitAt > 0.f && StrikeAt > 0.f ? FMath::Max(0.f, C.HitAt - StrikeAt * Rate) : 0.f;
}

float FMechAnimProxy::MeleeClipTime(int32 Clip, float T, float StrikeAt, float Cancel, float Duration) const
{
	const FClip& C = Clips[Clip];
	const float H = C.HitAt;
	if (StrikeAt <= 0.f || H <= 0.f || Duration <= StrikeAt)
	{
		return MapTime(Clip, T, StrikeAt, Duration, false);
	}
	const float Rate = FMath::Max(CVarMeleeClipRate.GetValueOnAnyThread(), 0.5f);
	if (T < StrikeAt)
	{
		const float Start = MeleeClipStart(Clip, StrikeAt);
		return Start + FMath::Max(T, 0.f) / StrikeAt * (H - Start);
	}
	const float Can = FMath::Clamp(Cancel > 0.f ? Cancel : Duration, StrikeAt + 0.001f, Duration);
	// 命中后：以前的倍速（剩下的整段拉到这一招结束）和上限取小的——慢刀本来不快的话照旧
	const float OldRate = (C.Length - H) / (Duration - StrikeAt);
	const float Follow = FMath::Min(OldRate, Rate);
	if (T < Can)
	{
		return FMath::Min(H + (T - StrikeAt) * Follow, C.Length);
	}
	const float AtCancel = FMath::Min(H + (Can - StrikeAt) * Follow, C.Length);
	const float Recover = FMath::Min((C.Length - AtCancel) / FMath::Max(Duration - Can, 0.001f), Rate * 1.5f);
	return FMath::Min(AtCancel + (T - Can) * Recover, C.Length);
}

void FMechAnimProxy::MeleeLayer(FPoseContext& Output, const TArray<bool, TInlineAllocator<160>>& Upper, FCompactPoseBoneIndex Pelvis, FCompactPoseBoneIndex Spine)
{
	// 出刀的全身混合（10-30 玩家：「刀剑战斗不流畅，可能因为只有上半身有动作」）。以前整段片段按一个时间、一个权重盖在移动层上：
	// 攻击片段是原地版，脚在原地迈步、骨盆前后挪，突进时整台机体按 40–60 米/秒滑过去，腿却在原地倒腾（看着就是上身在砍、
	// 腿在原地跑步滑行）；可取消之后玩家一走，腿还在播原地收招，又是滑着走。现在分开：
	//  · 上身：照旧按片段时间（Hit 标记对齐命中时刻）；
	//  · 骨盆和腿：突进时提前走到「砍下那一刻」的弓步（命中前六成时间里先快后慢走完），压低重心保持这个弓步滑过去，
	//    砍下那一刻和上身汇合，之后照片段收招（踩实、重心回收）；不突进时（连招后几段贴身砍）就是片段本来的迈步、转胯；
	//  · 空中出招：腿换空中姿势（下落 / 悬停），不在半空迈地面步；可取消之后玩家走起来：腿交回移动循环；
	//  · 腿和上身不是同一时刻时，上身按片段里那一刻的朝向在模型空间立住（挥刀方向不被另一时刻的骨盆扭偏）。
	const int32 Clip = Action.FullClip;
	const FClip& C = Clips[Clip];
	const float W = FMath::Min(Action.FullWeight, 1.f);
	if (W <= 0.001f || !C.Seq)
	{
		return;
	}
	const float S = Action.FullStrikeAt;
	const float D = Action.FullDuration;
	const float T = Action.FullTime;
	// 10-31：按阶段映射（起手太长裁掉开头、命中后只播一小段收刀，见 MeleeClipTime）；-MechComboOld = 以前的整段拉伸
	static const bool bComboOld = FParse::Param(FCommandLine::Get(), TEXT("MechComboOld"));
	const bool bPhase = !bComboOld && UsePhaseMap(Clip);
	const float UpperT = bPhase ? MeleeClipTime(Clip, T, S, Action.FullCancel, D) : MapTime(Clip, T, S, D, false);
	float LegT = UpperT;
	if (S > 0.f && T < S && MeleeLungeW > 0.001f)
	{
		const float StrikeT = MapTime(Clip, S, S, D, false);
		const float From = bPhase ? MeleeClipStart(Clip, S) : 0.f;
		const float X = FMath::Clamp(T / (S * 0.6f), 0.f, 1.f);
		const float Warp = FMath::Max(UpperT, From + (StrikeT - From) * (1.f - (1.f - X) * (1.f - X)));
		LegT = FMath::Lerp(UpperT, Warp, FMath::Min(MeleeLungeW, 1.f));
	}
	const float UpperW = W * (1.f - FMath::Clamp(Action.GunAim, 0.f, 1.f));
	const float LowerW = W * (1.f - AirW) * (1.f - MeleeMoveLegs);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Sample = [&](float Time, FCompactPose& Pose) {
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attr;
		Pose.SetBoneContainer(&Bones);
		Curve.InitFrom(Output.Curve);
		FAnimationPoseData Data(Pose, Curve, Attr);
		C.Seq->GetAnimationPose(Data, FAnimExtractContext((double)Time));
	};
	FCompactPose UpPose;
	Sample(UpperT, UpPose);
	const bool bSplit = FMath::Abs(LegT - UpperT) > 0.0005f;
	FCompactPose LegPose;
	if (bSplit)
	{
		Sample(LegT, LegPose);
	}
	const FCompactPose& LowPose = bSplit ? LegPose : UpPose;
	for (const FCompactPoseBoneIndex B : Output.Pose.ForEachBoneIndex())
	{
		if (B.GetInt() == 0)
		{
			continue;
		}
		const bool bUp = Upper[B.GetInt()];
		const float BW = bUp ? UpperW : LowerW;
		if (BW > 0.001f)
		{
			Output.Pose[B].BlendWith(bUp ? UpPose[B] : LowPose[B], BW);
		}
	}
	if (Pelvis.IsValid() && Spine.IsValid() && Pelvis.GetInt() > 0 && UpperW > 0.001f && (bSplit || LowerW < UpperW - 0.01f))
	{
		const FQuat WantCS = UpPose[Pelvis].GetRotation() * UpPose[Spine].GetRotation();
		const FQuat Local = Output.Pose[Pelvis].GetRotation().Inverse() * WantCS;
		Output.Pose[Spine].SetRotation(FQuat::Slerp(Output.Pose[Spine].GetRotation(), Local, FMath::Min(UpperW, 1.f)).GetNormalized());
	}
}

void FMechAnimProxy::TransitionBlend(FCompactPose& Pose)
{
	// 「死混合」（dead blending）：跳变那一刻拍下上一帧画面上的姿势和各骨头的速度，之后这个起点姿势带着速度往前推（速度按
	// 过渡时长的一半为半衰期衰减），再按平滑曲线（3x²−2x³）混到每一帧新算出来的姿势。和两个片段交叉淡化比：不用知道上一个
	// 动作是什么（打断、换段、格挡起收都一样处理），挥到一半的刀会顺着惯性再走一点再转向下一刀，不会定住或者抽回去
	const int32 Num = Pose.GetNumBones();
	if (bBlendStart)
	{
		bBlendStart = false;
		if (LastPose.Num() == Num && LastPose2.Num() == Num && Num > 0)
		{
			BlendFrom = LastPose;
			BlendAngVel.SetNumUninitialized(Num);
			BlendLinVel.SetNumUninitialized(Num);
			const float Inv = 1.f / FMath::Max(LastFrameDt, 1.f / 240.f);
			for (int32 i = 0; i < Num; ++i)
			{
				FQuat Dq = (LastPose[i].GetRotation() * LastPose2[i].GetRotation().Inverse()).GetNormalized();
				if (Dq.W < 0.f)
				{
					Dq = FQuat(-Dq.X, -Dq.Y, -Dq.Z, -Dq.W);  // 取短的那一边
				}
				BlendAngVel[i] = (Dq.ToRotationVector() * Inv).GetClampedToMaxSize(10.f);
				BlendLinVel[i] = ((LastPose[i].GetLocation() - LastPose2[i].GetLocation()) * Inv).GetClampedToMaxSize(250.f);
			}
		}
		else
		{
			BlendElapsed = 10.f;  // 没有上一帧（刚生成、换了细节级别）：不过渡
		}
	}
	if (BlendElapsed < BlendDuration && BlendFrom.Num() == Num)
	{
		const float Lambda = 2.f * UE_LN2 / FMath::Max(BlendDuration, 0.02f);
		const float Tau = (1.f - FMath::Exp(-Lambda * BlendElapsed)) / Lambda;
		const float X = FMath::Clamp(BlendElapsed / BlendDuration, 0.f, 1.f);
		const float Alpha = X * X * (3.f - 2.f * X);
		for (const FCompactPoseBoneIndex B : Pose.ForEachBoneIndex())
		{
			const int32 i = B.GetInt();
			if (i == 0)
			{
				continue;  // 根骨只放整体倾斜，另外算
			}
			const FTransform& From = BlendFrom[i];
			const FQuat SrcRot = (FQuat::MakeFromRotationVector(BlendAngVel[i] * Tau) * From.GetRotation()).GetNormalized();
			const FVector SrcLoc = From.GetLocation() + BlendLinVel[i] * Tau;
			FTransform& Out = Pose[B];
			Out.SetRotation(FQuat::Slerp(SrcRot, Out.GetRotation(), Alpha).GetNormalized());
			Out.SetLocation(FMath::Lerp(SrcLoc, Out.GetLocation(), Alpha));
		}
	}
	// 记下这一帧画面上的姿势（下一次跳变的起点和速度）
	Swap(LastPose, LastPose2);
	LastPose.SetNumUninitialized(Num);
	for (const FCompactPoseBoneIndex B : Pose.ForEachBoneIndex())
	{
		LastPose[B.GetInt()] = Pose[B];
	}
}

bool FMechAnimProxy::Evaluate(FPoseContext& Output)
{
	int32 Active[MechClip::Num];
	int32 N = 0;
	float Total = 0.f;
	for (int32 i = 0; i < MechClip::Num; ++i)
	{
		if (Clips[i].Seq && Clips[i].Weight > 0.001f)
		{
			Active[N++] = i;
			Total += Clips[i].Weight;
		}
	}
	if (N == 0)
	{
		Output.ResetToRefPose();
		return true;
	}
	TArray<FCompactPose, TInlineAllocator<8>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<8>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<8>> Attributes;
	TArray<float, TInlineAllocator<8>> Weights;
	Poses.SetNum(N);
	Curves.SetNum(N);
	Attributes.SetNum(N);
	Weights.SetNum(N);
	for (int32 k = 0; k < N; ++k)
	{
		const FClip& C = Clips[Active[k]];
		Poses[k].SetBoneContainer(&Output.Pose.GetBoneContainer());
		Curves[k].InitFrom(Output.Curve);
		FAnimationPoseData Data(Poses[k], Curves[k], Attributes[k]);
		C.Seq->GetAnimationPose(Data, FAnimExtractContext((double)C.Time));
		Weights[k] = C.Weight / Total;
	}
	FAnimationPoseData OutData(Output);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, OutData);

	// 动作层：上身（spine_01 及以下的骨头）或全身（除根骨）往动作姿势混
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	TArray<bool, TInlineAllocator<160>> Upper;
	Upper.SetNumZeroed(Output.Pose.GetNumBones());
	const int32 SpineMesh = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	const FCompactPoseBoneIndex Spine = SpineMesh != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMesh)) : FCompactPoseBoneIndex(INDEX_NONE);
	const FCompactPoseBoneIndex Pelvis = Spine.IsValid() ? Bones.GetParentBoneIndex(Spine) : FCompactPoseBoneIndex(INDEX_NONE);
	if (Spine.IsValid())
	{
		for (const FCompactPoseBoneIndex B : Output.Pose.ForEachBoneIndex())
		{
			const FCompactPoseBoneIndex P = Bones.GetParentBoneIndex(B);
			Upper[B.GetInt()] = B == Spine || (P.GetInt() >= 0 && Upper[P.GetInt()]);
		}
	}
	// UpperScale：全身层在上身（spine_01 往上）的权重再乘这个（端枪交战时上身让给端枪）；LowerScale：骨盆和腿的权重再乘这个（空中时地面步法让给空中姿势）
	auto Layer = [&](int32 Clip, float Time, float W, bool bUpperOnly, float UpperScale = 1.f, float LowerScale = 1.f) {
		if (W <= 0.001f || Clip < 0 || Clip >= MechClip::Num || !Clips[Clip].Seq)
		{
			return;
		}
		FCompactPose Pose;
		FBlendedCurve Curve;
		UE::Anim::FStackAttributeContainer Attr;
		Pose.SetBoneContainer(&Bones);
		Curve.InitFrom(Output.Curve);
		FAnimationPoseData Data(Pose, Curve, Attr);
		Clips[Clip].Seq->GetAnimationPose(Data, FAnimExtractContext((double)Time));
		W = FMath::Min(W, 1.f);
		const float WUpper = W * FMath::Clamp(UpperScale, 0.f, 1.f);
		const float WLower = W * FMath::Clamp(LowerScale, 0.f, 1.f);
		for (const FCompactPoseBoneIndex B : Output.Pose.ForEachBoneIndex())
		{
			if (B.GetInt() == 0 || (bUpperOnly && !Upper[B.GetInt()]))
			{
				continue;
			}
			const float BW = Upper[B.GetInt()] ? WUpper : WLower;
			if (BW > 0.001f)
			{
				Output.Pose[B].BlendWith(Pose[B], BW);
			}
		}
	};
	const float AimLock = FMath::Clamp(Action.GunAim, 0.f, 1.f);
	// 起步 / 急停 / 单膝落地：压在移动层上（全身，淡入淡出）
	auto Fade = [](float T, float Len, float In, float Out) { return FMath::Clamp(FMath::Min(T / In, (Len - T) / Out), 0.f, 1.f); };
	if (StartTime >= 0.f)
	{
		const float Len = FMath::Min(Clips[MechClip::MoveStart].Length, 0.6f);
		Layer(MechClip::MoveStart, StartTime, 0.65f * Fade(StartTime, Len, 0.08f, 0.25f), false, 1.f - AimLock);
	}
	if (StopTime >= 0.f)
	{
		const float Len = FMath::Min(Clips[MechClip::MoveStop].Length, 0.9f);
		Layer(MechClip::MoveStop, StopTime, 0.8f * Fade(StopTime, Len, 0.06f, 0.3f), false, 1.f - AimLock);
	}
	if (HeroTime >= 0.f)
	{
		Layer(MechClip::FlyLand, HeroTime, Fade(HeroTime, Clips[MechClip::FlyLand].Length, 0.04f, 0.35f) * (1.f - MoveW * 0.7f), false, 1.f - AimLock);
	}
	// 蹬墙跳：起飞片段里「收腿 → 蹬直」那一段（片段 0.07→0.49 秒，按下时腿已经收到一半、0.1 秒收到底、0.25 秒蹬直），
	// 只盖骨盆和腿（上身照常端枪 / 持刀）；之后 0.18 秒淡回下落 / 悬停。整机朝墙外的倾斜由上面按加速度的倾斜给
	constexpr float KickLen = 0.42f;
	if (Action.KickTime >= 0.f && Action.KickTime < KickLen)
	{
		Layer(MechClip::FlyKick, 0.07f + Action.KickTime, Fade(Action.KickTime, KickLen, 0.03f, 0.18f), false, 0.f);
	}
	// 手上的姿势：枪（长枪按俯仰混瞄上 / 瞄下）或这把刀的持刀待机
	if (Action.GunClip >= 0 && Action.GunClip < MechClip::Num && Clips[Action.GunClip].Seq)
	{
		Layer(Action.GunClip, FMath::Fmod(StanceTime, Clips[Action.GunClip].Length), Action.GunWeight, true);
		if (Action.GunClip == MechClip::RifleAim)
		{
			const int32 Tilt = Action.AimPitch >= 0.f ? MechClip::RifleAimUp : MechClip::RifleAimDown;
			Layer(Tilt, 0.f, Action.GunWeight * FMath::Clamp(FMath::Abs(Action.AimPitch) / 70.f, 0.f, 1.f), true);
		}
		// 交战时上身在模型空间立住：spine_01 的局部旋转换成「端枪动作里骨盆 × spine_01」再除掉这一帧的骨盆，
		// 骨盆怎么歪（下落、后退、单膝落地）上身都按端枪动作的朝向端着（剩下的偏差由 UMechMeshComponent 转上身补，最多 70 度）
		const float MeshSpaceW = AimLock * FMath::Min(Action.GunWeight, 1.f);
		if (MeshSpaceW > 0.001f && Pelvis.IsValid() && Pelvis.GetInt() > 0)
		{
			FCompactPose AimPose;
			FBlendedCurve AimCurve;
			UE::Anim::FStackAttributeContainer AimAttr;
			AimPose.SetBoneContainer(&Bones);
			AimCurve.InitFrom(Output.Curve);
			FAnimationPoseData AimData(AimPose, AimCurve, AimAttr);
			Clips[Action.GunClip].Seq->GetAnimationPose(AimData, FAnimExtractContext((double)FMath::Fmod(StanceTime, Clips[Action.GunClip].Length)));
			const FQuat WantCS = AimPose[Pelvis].GetRotation() * AimPose[Spine].GetRotation();
			const FQuat Local = Output.Pose[Pelvis].GetRotation().Inverse() * WantCS;
			Output.Pose[Spine].SetRotation(FQuat::Slerp(Output.Pose[Spine].GetRotation(), Local, MeshSpaceW).GetNormalized());
		}
	}
	if (Action.StanceClip >= 0 && Action.StanceClip < MechClip::Num && Clips[Action.StanceClip].Seq)
	{
		Layer(Action.StanceClip, FMath::Fmod(StanceTime, Clips[Action.StanceClip].Length), Action.StanceWeight, true);
	}
	if (Action.UpperClip >= 0)
	{
		Layer(Action.UpperClip, MapTime(Action.UpperClip, Action.UpperTime, 0.f, Action.UpperDuration, Action.bUpperLoop), Action.UpperWeight, true);
	}
	static const bool bMeleeOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld"));
	if (Action.FullClip >= 0 && Action.bFullMelee && !bMeleeOld && Clips[Action.FullClip].Seq)
	{
		MeleeLayer(Output, Upper, Pelvis, Spine);
	}
	else if (Action.FullClip >= 0)
	{
		// 闪避、失衡的腿是地面上的步子（侧跨、后撤）：空中快速闪避 / 反向急停时照播，腿就在半空里迈步。空中按离地程度让给下落 / 悬停，
		// 上身留一半（闪避的甩身还看得出来）
		static const bool bOldAir = FParse::Param(FCommandLine::Get(), TEXT("MechAnimOldAir"));
		const float AirLegs = Action.bFullGroundLegs && !bOldAir ? AirW : 0.f;
		Layer(Action.FullClip, MapTime(Action.FullClip, Action.FullTime, Action.FullStrikeAt, Action.FullDuration, false), Action.FullWeight, false,
			(1.f - AimLock) * (1.f - 0.5f * AirLegs), 1.f - AirLegs);
	}
	Output.Pose.NormalizeRotations();
	// 动作层之间的过渡（10-30 刀剑手感：待机→出招、连招各段之间、出招→移动 / 待机、格挡起收以前是硬切或 0.05 秒线性淡入，姿势抽一下）
	if (!bMeleeOld)
	{
		TransitionBlend(Output.Pose);
	}
	// 腿部关节限位 + 每帧限速：所有动作层、过渡都混完以后最后一步（过渡会带着速度往前推，可能推出范围）。
	// 减震的两根小骨头在 UMechMeshComponent::FinalizeBoneTransform 里按定稿的腿再对准，顺序上在这之后
	LimitLegs(Output.Pose);
	// 10-31：维克托中 / 重的下沉炮塔头限住转动（不擦颊板 / 战斗室）
	LimitHead(Output.Pose);
	// 原地播放：根骨的位移和转向都去掉，只留整体倾斜（以脚底为轴）
	Output.Pose[FCompactPoseBoneIndex(0)] = FTransform(Lean);
	return true;
}

// ---------------------------------------------------------------- 腿部关节限位（10-30 玩家：「腿部乱动还是没有解决，也许可以加一个关节限位」）
// 动作全是真人动作重定向来的（走跑 / 下落是 Game Animation Sample，刀、闪避、失衡、飞行是商城包），再加上几十层按权重混合、
// 出刀全身混合、死混合过渡：真人能做的姿势（膝盖弯到 170 度、膝侧扭、大腿内收过中线、脚踝翻转），混合中途还会出现两个姿势都没有的
// 中间姿势；机体的腿是硬零件（小腿后鳍、膝后减震、大腿液压杆、胯甲），超出机械关节能转的范围就互相插进去。
// 所以在所有层混完、过渡也算完以后，把每个腿部关节按「机械关节」的范围收一遍：
//   角度都相对绑定姿势（腿伸直、脚尖朝前）在父骨头坐标里分解成 弯曲（主平面里的摆动）/ 侧摆（离开主平面）/ 扭转（绕骨头自身）三个量：
//   髋 = 大腿相对骨盆：前屈 +（往前抬）/ −（往后摆）、外展 +（往外）/ −（往里过中线）、内外旋
//   膝 = 小腿相对大腿：弯曲 +（往后弯，只能单向；− 是反折）、侧弯、扭转——像个铰链，侧弯 / 扭转只留几度
//   踝 = 脚相对小腿：俯仰 +（脚尖上翘）/ −（下压）、偏摆 +（脚尖往外）、侧翻（绕脚尖方向）
// 每项给下限、上限，和两头的软区宽度：进入软区以后按指数渐近压向极限（斜率连续，不会在某个角度一下卡住、来回抖）。
// 空中再把膝盖、抬腿上限收紧（原来的空中收腿限幅：下落循环膝盖弯到 170 度、提膝到胸口）。
// 最后每根腿骨每帧最多转多少度：混合权重突变、换片段那一帧腿能一下转几十度，超过就按比例追（正常跑步转得比这慢，不受影响）。
namespace MechLegLimit
{
	struct FRange
	{
		float Lo, Hi, SoftLo, SoftHi;
	};
	// 限位表（度）。地面（走跑、出刀、闪避、失衡、落地、单膝落地）都按这组；空中只把膝弯曲、髋前屈的上限换成下面的 Air*
	const FRange HipFlex{-40.f, 115.f, 12.f, 20.f};    // 往后摆 40、往前抬 115（略高于水平，出刀弓步 / 蹲都在里面）
	const FRange HipSide{-20.f, 45.f, 10.f, 12.f};     // 往里过中线 20（防两腿互穿）、往外 45
	const FRange HipTwist{-30.f, 30.f, 12.f, 12.f};    // 大腿内外旋
	const FRange KneeFlex{-2.f, 135.f, 2.f, 25.f};     // 只能往后弯，最多 135（软区从 110 起）；反折最多 2 度
	const FRange KneeSide{-5.f, 5.f, 3.f, 3.f};        // 铰链：侧弯只留 5 度
	const FRange KneeTwist{-10.f, 10.f, 5.f, 5.f};     // 小腿绕自身扭转 10 度
	const FRange AnkPitch{-50.f, 35.f, 12.f, 12.f};    // 脚尖下压 50、上翘 35
	const FRange AnkSide{-15.f, 20.f, 8.f, 8.f};       // 脚尖往里 15、往外 20
	const FRange AnkRoll{-20.f, 20.f, 8.f, 8.f};       // 脚掌侧翻
	// 空中（按离地程度 AirW 插值）：膝盖最多 110（软区从 85 起）、大腿前屈最多 95（软区从 75 起，大约水平）
	constexpr float AirKneeHi = 110.f;
	constexpr float AirHipHi = 95.f;
	// 每根腿骨每秒最多转多少度（髋 / 膝 / 踝）
	constexpr float Speed[3] = {900.f, 1100.f, 1100.f};

	float SoftRange(float A, const FRange& R, float Hi)
	{
		const float SHi = FMath::Clamp(R.SoftHi, 0.01f, (Hi - R.Lo) * 0.5f);
		const float SLo = FMath::Clamp(R.SoftLo, 0.01f, (Hi - R.Lo) * 0.5f);
		if (A > Hi - SHi)
		{
			return Hi - SHi + SHi * (1.f - FMath::Exp(-(A - (Hi - SHi)) / SHi));
		}
		if (A < R.Lo + SLo)
		{
			return R.Lo + SLo - SLo * (1.f - FMath::Exp(-((R.Lo + SLo) - A) / SLo));
		}
		return A;
	}
}

void FMechAnimProxy::ResetLegStats()
{
	for (int32 i = 0; i < NumLegStats; ++i)
	{
		// 取最小的几项（后摆、内收、反折、脚尖下压）从 +999 开始，其余取最大的从 −999 开始
		const bool bMin = i == HipFlexMin || i == HipSideMin || i == KneeFlexMin || i == AnkPitchMin;
		LegStatRaw[i] = LegStatOut[i] = bMin ? 999.f : -999.f;
	}
	LegCapped = 0;
}

void FMechAnimProxy::LimitLegs(FCompactPose& Pose)
{
	using namespace MechLegLimit;
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("MechNoLegLimit"));
	static const bool bOldFall = FParse::Param(FCommandLine::Get(), TEXT("MechAnimOldFall"));
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	auto Find = [&Bones](const TCHAR* Name) {
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return Mesh != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh)) : FCompactPoseBoneIndex(INDEX_NONE);
	};
	// 绑定姿势下这根骨头在网格空间的变换（自己的局部变换一路乘到根骨）
	auto RefCS = [&Bones](FCompactPoseBoneIndex B) {
		FTransform T = FTransform::Identity;
		while (B.IsValid())
		{
			T = T * Bones.GetRefPoseTransform(B);
			B = Bones.GetParentBoneIndex(B);
		}
		return T;
	};
	// 一个关节：骨头、绑定姿势的局部旋转、在父骨头坐标里的主方向 N（髋 / 膝 = 指向下一节，踝 = 脚尖方向）、弯曲方向、侧摆方向
	struct FJoint
	{
		FCompactPoseBoneIndex Bone = FCompactPoseBoneIndex(INDEX_NONE);
		FQuat RefRot = FQuat::Identity;
		FVector N, Bend, Side;
	};
	auto Decompose = [](const FJoint& J, const FQuat& Rot, float Out[3]) {
		const FQuat Rel = (Rot * J.RefRot.Inverse()).GetNormalized();  // 父坐标里：绑定姿势 → 现在
		FQuat Swing, Twist;
		Rel.ToSwingTwist(J.N, Swing, Twist);
		if (Twist.W < 0.f)
		{
			Twist = FQuat(-Twist.X, -Twist.Y, -Twist.Z, -Twist.W);
		}
		const FVector D = Swing.RotateVector(J.N);
		Out[0] = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(D | J.Bend, D | J.N)));
		Out[1] = static_cast<float>(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(D | J.Side, -1.0, 1.0))));
		Out[2] = static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan2(FVector(Twist.X, Twist.Y, Twist.Z) | J.N, (double)Twist.W)));
	};
	auto Compose = [](const FJoint& J, const float A[3]) {
		const float F = FMath::DegreesToRadians(A[0]);
		const float S = FMath::DegreesToRadians(A[1]);
		const FVector D = (FMath::Cos(S) * (FMath::Cos(F) * J.N + FMath::Sin(F) * J.Bend) + FMath::Sin(S) * J.Side).GetSafeNormal();
		const FQuat Rel = FQuat::FindBetweenNormals(J.N, D) * FQuat(J.N, FMath::DegreesToRadians(A[2]));
		return (Rel * J.RefRot).GetNormalized();
	};
	auto Max = [](float& S, float V) { S = FMath::Max(S, V); };
	auto Min = [](float& S, float V) { S = FMath::Min(S, V); };
	auto Stat = [&](float* S, int32 k, const float A[3]) {
		if (k == 0)
		{
			Max(S[HipFlexMax], A[0]); Min(S[HipFlexMin], A[0]); Max(S[HipSideMax], A[1]); Min(S[HipSideMin], A[1]); Max(S[HipTwist], FMath::Abs(A[2]));
		}
		else if (k == 1)
		{
			Max(S[KneeFlexMax], A[0]); Min(S[KneeFlexMin], A[0]); Max(S[KneeSide], FMath::Abs(A[1])); Max(S[KneeTwist], FMath::Abs(A[2]));
		}
		else
		{
			Max(S[AnkPitchMax], A[0]); Min(S[AnkPitchMin], A[0]); Max(S[AnkSide], FMath::Abs(A[1])); Max(S[AnkRoll], FMath::Abs(A[2]));
		}
	};
	// 带命名空间写全：统计用的枚举 ELegStat 里也有 HipTwist、KneeSide 这些名字，类里的名字优先
	namespace LN = MechLegLimit;
	const FRange* Lim[3][3] = {{&LN::HipFlex, &LN::HipSide, &LN::HipTwist}, {&LN::KneeFlex, &LN::KneeSide, &LN::KneeTwist}, {&LN::AnkPitch, &LN::AnkSide, &LN::AnkRoll}};
	const float AirT = bOldFall ? 0.f : FMath::Clamp(AirW, 0.f, 1.f);
	// 脚尖上翘上限按机体（10-31：相位中 / 重的靴口前沿在脚尖上翘时碰脚背，收到 15 度；其余 35）
	const float FlexHi[3] = {FMath::Lerp(HipFlex.Hi, AirHipHi, AirT), FMath::Lerp(KneeFlex.Hi, AirKneeHi, AirT), FMath::Min(AnkPitch.Hi, AnkleUpMax)};
	// 限速：上一帧有输出、这一帧不是暂停、两次之间没隔太久（画面外不刷新骨骼、刚换细节级别时直接跳到新姿势，不慢慢追）
	const double Now = FPlatformTime::Seconds();
	const bool bSpeed = !bOff && bPrevLeg && LegDt > 0.f && Now - PrevLegClock < 0.25;
	PrevLegClock = Now;
	static const TCHAR* Names[2][4] = {{TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l")}, {TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")}};
	bool bAll = true;
	for (int32 s = 0; s < 2; ++s)
	{
		FCompactPoseBoneIndex B[4];
		bool bOk = true;
		for (int32 k = 0; k < 4; ++k)
		{
			B[k] = Find(Names[s][k]);
			bOk &= B[k].IsValid();
		}
		if (!bOk)
		{
			bAll = false;
			continue;
		}
		// 关节坐标：按绑定姿势在网格空间定方向（前 +Y、上 +Z、左 +X），换到父骨头坐标再正交化
		const FVector Outward(s == 0 ? 1.f : -1.f, 0.f, 0.f);
		const FVector BendCS[3] = {FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f), FVector(0.f, 0.f, 1.f)};  // 髋往前、膝往后、踝脚尖往上
		FTransform CS[4];
		for (int32 k = 0; k < 4; ++k)
		{
			CS[k] = RefCS(B[k]);
		}
		for (int32 k = 0; k < 3; ++k)
		{
			FJoint J;
			J.Bone = B[k];
			J.RefRot = Bones.GetRefPoseTransform(B[k]).GetRotation();
			const FTransform ParentCS = RefCS(Bones.GetParentBoneIndex(B[k]));
			J.N = ParentCS.InverseTransformVectorNoScale((CS[k + 1].GetLocation() - CS[k].GetLocation()).GetSafeNormal()).GetSafeNormal();
			const FVector Bend = ParentCS.InverseTransformVectorNoScale(BendCS[k]);
			J.Bend = (Bend - (Bend | J.N) * J.N).GetSafeNormal();
			const FVector Side = ParentCS.InverseTransformVectorNoScale(Outward);
			J.Side = (Side - (Side | J.N) * J.N - (Side | J.Bend) * J.Bend).GetSafeNormal();

			const FQuat RawRot = Pose[J.Bone].GetRotation();
			float A[3];
			Decompose(J, RawRot, A);
			// 弯曲、扭转角在 ±180 度处会折回去：下落循环里膝盖能折过 180 度（小腿穿过大腿转到前面），直接读成 −150 度就成了「反折」，
			// 限位会把它掰直、一帧转 200 多度。按和上一帧最近的那一圈展开；没有上一帧（或展开得离谱）时按这一项限位范围的中点为中心取一圈
			for (int32 a = 0; a < 3; a += 2)
			{
				const float Center = (Lim[k][a]->Lo + Lim[k][a]->Hi) * 0.5f;
				float V = bPrevLeg ? PrevRawAng[s][k][a / 2] + FMath::FindDeltaAngleDegrees(PrevRawAng[s][k][a / 2], A[a]) : A[a];
				if (!bPrevLeg || FMath::Abs(V - Center) > 270.f)
				{
					V = Center + FMath::FindDeltaAngleDegrees(Center, A[a]);
				}
				A[a] = V;
				PrevRawAng[s][k][a / 2] = V;
			}
			Stat(LegStatRaw, k, A);
			if (k == 0)
			{
				RawHip[s] = A[0];
			}
			else if (k == 1)
			{
				RawKnee[s] = A[0];
			}
			FQuat NewRot = RawRot;
			if (!bOff)
			{
				float L[3];
				for (int32 a = 0; a < 3; ++a)
				{
					L[a] = SoftRange(A[a], *Lim[k][a], a == 0 ? FlexHi[k] : Lim[k][a]->Hi);
				}
				if (FMath::Abs(L[0] - A[0]) + FMath::Abs(L[1] - A[1]) + FMath::Abs(L[2] - A[2]) > 0.01f)
				{
					NewRot = Compose(J, L);
				}
			}
			// 每帧限速（和上一帧画面上的这根骨头比）
			if (bPrevLeg)
			{
				const float Step = static_cast<float>(FMath::RadiansToDegrees(PrevLeg[s][k].AngularDistance(NewRot)));
				Max(LegStatRaw[StepMax], Step);
				const float Cap = Speed[k] * LegDt;
				if (bSpeed && Step > Cap && Step > 0.01f)
				{
					NewRot = FQuat::Slerp(PrevLeg[s][k], NewRot, Cap / Step).GetNormalized();
					++LegCapped;
				}
				Max(LegStatOut[StepMax], static_cast<float>(FMath::RadiansToDegrees(PrevLeg[s][k].AngularDistance(NewRot))));
			}
			float O[3];
			Decompose(J, NewRot, O);
			Stat(LegStatOut, k, O);
			Pose[J.Bone].SetRotation(NewRot);
			PrevLeg[s][k] = NewRot;
			// 过渡（TransitionBlend）记下的「上一帧画面姿势」也换成限位后的，下一次过渡从画面上真实的样子出发
			if (LastPose.IsValidIndex(J.Bone.GetInt()) && LastPose.Num() == Pose.GetNumBones())
			{
				LastPose[J.Bone.GetInt()].SetRotation(NewRot);
			}
		}
	}
	bPrevLeg = bAll;
}

// ---------------------------------------------------------------- 头部转动限位（10-31 阵营机体：维克托重工中型「铁壁」、重型「突击炮」）
// 铁壁的下沉炮塔头被两块颊板夹着、突击炮的头埋在箱形战斗室中间，颊板 / 战斗室都挂在胸（spine_05）上：动作里一转头、歪头就擦
// （导出时的动作抽帧检查：铁壁 92%、突击炮 79% 的帧）。做法照 LimitLegs：头相对胸的总转动（neck_01 → neck_02 → head 三节合起来）
// 相对绑定姿势拆成 偏航（绕胸的上方向）/ 俯仰（往前低头为正）/ 侧歪 三个量，各自软限位（进软区后指数渐近，斜率连续、不卡不抖）：
// 偏航 ±HeadYaw（软区 8 度）、俯仰和侧歪 ±HeadPitch（软区 5 度）；收回的那部分转动加在 neck_01 上（三节各自的弯法留着，头的朝向回到范围里）。
// 上身被程序整体转（端枪对准、失衡后仰）时胸和头一起转，不影响这里。只对网格表里给了限位的机体生效
void FMechAnimProxy::LimitHead(FCompactPose& Pose)
{
	static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("MechNoHeadLimit"));
	if (HeadYawMax <= 0.f || HeadPitchMax <= 0.f)
	{
		return;
	}
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	auto Find = [&Bones](const TCHAR* Name) {
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return Mesh != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh)) : FCompactPoseBoneIndex(INDEX_NONE);
	};
	const FCompactPoseBoneIndex Chest = Find(TEXT("spine_05"));
	const FCompactPoseBoneIndex Head = Find(TEXT("head"));
	if (!Chest.IsValid() || !Head.IsValid())
	{
		return;
	}
	// 头往上一直到胸下面那一节（neck_01）：[head, neck_02, neck_01]
	TArray<FCompactPoseBoneIndex, TInlineAllocator<4>> Chain;
	for (FCompactPoseBoneIndex B = Head; B.IsValid() && B != Chest; B = Bones.GetParentBoneIndex(B))
	{
		Chain.Add(B);
		if (Chain.Num() > 4)
		{
			return;  // 不是胸 → 脖子 → 头的骨架
		}
	}
	if (Chain.Num() == 0 || Bones.GetParentBoneIndex(Chain.Last()) != Chest)
	{
		return;
	}
	// 头在胸坐标里的朝向（现在 / 绑定姿势）：从 neck_01 往下各节局部旋转连乘
	FQuat Now = FQuat::Identity;
	FQuat Ref = FQuat::Identity;
	for (int32 i = Chain.Num() - 1; i >= 0; --i)
	{
		Now = Now * Pose[Chain[i]].GetRotation();
		Ref = Ref * Bones.GetRefPoseTransform(Chain[i]).GetRotation();
	}
	// 胸坐标里的上 / 前 / 左（按绑定姿势的网格空间：上 +Z、前 +Y、左 +X）
	FTransform ChestCS = FTransform::Identity;
	for (FCompactPoseBoneIndex B = Chest; B.IsValid(); B = Bones.GetParentBoneIndex(B))
	{
		ChestCS = ChestCS * Bones.GetRefPoseTransform(B);
	}
	const FVector U = ChestCS.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	FVector F = ChestCS.InverseTransformVectorNoScale(FVector(0.f, 1.f, 0.f));
	F = (F - (F | U) * U).GetSafeNormal();
	FVector S = (U ^ F).GetSafeNormal();
	if ((S | ChestCS.InverseTransformVectorNoScale(FVector(1.f, 0.f, 0.f))) < 0.f)
	{
		S = -S;
	}
	const FQuat D = (Now * Ref.Inverse()).GetNormalized();  // 胸坐标里：绑定姿势 → 现在
	FQuat Swing, Twist;
	D.ToSwingTwist(U, Swing, Twist);
	if (Twist.W < 0.f)
	{
		Twist = FQuat(-Twist.X, -Twist.Y, -Twist.Z, -Twist.W);
	}
	const FVector Up2 = Swing.RotateVector(U);
	float A[3];
	A[0] = static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan2(FVector(Twist.X, Twist.Y, Twist.Z) | U, (double)Twist.W)));
	A[1] = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Up2 | F, Up2 | U)));
	A[2] = static_cast<float>(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up2 | S, -1.0, 1.0))));
	using MechLegLimit::FRange;
	const FRange Yaw{-HeadYawMax, HeadYawMax, 8.f, 8.f};
	const FRange Tilt{-HeadPitchMax, HeadPitchMax, 5.f, 5.f};
	float L[3] = {MechLegLimit::SoftRange(A[0], Yaw, Yaw.Hi), MechLegLimit::SoftRange(A[1], Tilt, Tilt.Hi), MechLegLimit::SoftRange(A[2], Tilt, Tilt.Hi)};
	if (bOff)
	{
		L[0] = A[0], L[1] = A[1], L[2] = A[2];
	}
	for (int32 k = 0; k < 3; ++k)
	{
		HeadStatRaw[k] = FMath::Max(HeadStatRaw[k], FMath::Abs(A[k]));
		HeadStatOut[k] = FMath::Max(HeadStatOut[k], FMath::Abs(L[k]));
	}
	if (FMath::Abs(L[0] - A[0]) + FMath::Abs(L[1] - A[1]) + FMath::Abs(L[2] - A[2]) < 0.01f)
	{
		return;
	}
	const float P = FMath::DegreesToRadians(L[1]);
	const float R = FMath::DegreesToRadians(L[2]);
	const FVector Up3 = (FMath::Cos(R) * (FMath::Cos(P) * U + FMath::Sin(P) * F) + FMath::Sin(R) * S).GetSafeNormal();
	const FQuat DLim = (FQuat::FindBetweenNormals(U, Up3) * FQuat(U, FMath::DegreesToRadians(L[0]))).GetNormalized();
	const FQuat Fix = (DLim * D.Inverse()).GetNormalized();  // 胸坐标里还要再转多少
	const FCompactPoseBoneIndex Top = Chain.Last();
	const FQuat NewTop = (Fix * Pose[Top].GetRotation()).GetNormalized();
	Pose[Top].SetRotation(NewTop);
	if (LastPose.IsValidIndex(Top.GetInt()) && LastPose.Num() == Pose.GetNumBones())
	{
		LastPose[Top.GetInt()].SetRotation(NewTop);
	}
}
