// -MechMeleeFeelTest（10-30 刀剑手感）：七把刀各从 13 米外、侧着身子（机体朝向偏离木桩 70 度）出轻击——远距离突进 + 转身，
// 接满四连，再出一记重斩（投掷匕首用右键挥刀，没有重斩）。镜头固定在突进路线的侧面，逐帧量：
//  · 每帧骨骼角度跳变：躯干、四肢 25 根主要骨头在组件空间里这一帧转了多少度（取最大）；
//  · 骨骼角速度突变：同一根骨头这一帧和上一帧的转动量（旋转向量）之差，取最大——挥刀本身转得快但连续，这一项小；
//    姿势跳变（两段之间硬切、从静止姿势一下淡入）时前后两帧一个几乎不动、一个转一大截，这一项就很大；
//  · 机体朝向的角速度（度/秒）、实际速度（按两帧位置差，厘米/秒）。
// 每帧一行写到 Saved/MeleeFeel/<新|旧>.csv，日志 [meleefeel] 每把刀一行汇总。加 -MechMeleeFeelVideo 逐帧截图到
// Saved/Screenshots/MeleeFeel/<new|old>/（配 -benchmark -fps=30，ffmpeg 合成视频）；-MechMeleeOld 用改之前的做法（对比）；
// -MechMeleeFeelBlade=N 只测第 N 把（0 双手刀 … 6 匕首，10-31 起可以写几把 1,4,6）。
// 10-31 连招段间跳变：CSV 多记刀尖 / 手腕每帧位移和二阶差（tools/melee_jump_stats.py 统计）；-MechMeleeFeelTag=名字 换文件名；
// -MechMeleeFeelClose 镜头跟着机体看上身；-MechMeleeFeelFrames 一把刀按九种框架各打一遍、记肩甲插进头多深（肩甲擦头，默认关，见 MechShoulderClear.cpp）。
// 用法：UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechMeleeFeelTest -MechMeleeFeelVideo -benchmark -fps=30
//       -RenderOffscreen -ForceRes -ResX=1600 -ResY=900 -windowed -MechHour=13 -MechWeather=Clear [-MechMeleeOld]

#include "MechCombatComponent.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechEffectComponent.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechWeapons.h"

namespace MechMeleeFeelState  // 不用匿名命名空间：合并编译时会和别的文件里同名的变量撞
{
	float Clock = 0.f;
	int32 Seg = -1;
	bool bQuit = false;
	FVector Home = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	TWeakObjectPtr<AMechCharacter> DummyRef;
	TWeakObjectPtr<ACameraActor> Cam;
	float LastPress = -100.f;
	bool bHeavyDone = false;
	int32 Frame = 0;
	// 上一帧的量
	bool bHavePrev = false;
	TArray<FQuat> PrevRot;
	TArray<FVector> PrevOmega;
	FVector PrevLoc = FVector::ZeroVector;
	float PrevYaw = 0.f;
	TArray<FString> Rows;
	// 10-31 段间跳变：右刀尖、左刀尖（双刀；别的刀没有 = 左手）、右手腕、左手腕在网格空间（去掉机体平移和朝向，按世界尺寸厘米）的位置，
	// 上一帧 / 上上帧（每帧位移、位移的二阶差分 = 「这一帧和上一帧的位移差多少」，姿势一跳二阶差分就大）
	constexpr int32 NumPts = 4;
	FVector PrevPt[NumPts];
	FVector PrevPt2[NumPts];
	int32 PtFrames = 0;
	// 每把刀的汇总（只算出招期间）
	struct FSum
	{
		float MaxJump = 0.f;
		float MaxJerk = 0.f;
		FString JerkBone;
		float JerkAt = 0.f;
		float MaxYawRate = 0.f;
		float MaxSpeed = 0.f;
		float MaxAccel = 0.f;
		TArray<float> Jerks;
	};
	FSum Sum;
	float PrevSpeed = 0.f;
	const TCHAR* Bones[] = {TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_03"), TEXT("spine_05"), TEXT("neck_01"), TEXT("head"),
		TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r")};
	constexpr float SegLen = 6.6f;
	constexpr float AttackAt = 1.6f;
}

void MechCombatTest::MeleeFeelTick(AMechCharacter* Me, float DeltaTime)
{
	using namespace MechMeleeFeelState;
	if (bQuit)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	UWorld* World = Me->GetWorld();
	const float Dt = World->GetDeltaSeconds();  // 不受顿帧（这台机体的时间膨胀）影响的帧时间
	Clock += Dt;
	if (!PC || Clock < 3.f)
	{
		return;  // 等木桩生成、开局稳定
	}
	static const bool bOld = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld"));
	static const bool bVideo = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFeelVideo"));
	// 默认七把刀都测；-MechMeleeFeelBlade=N 只测第 N 把
	static const TArray<int32> List = [] {
		// 可以写几把：-MechMeleeFeelBlade=1,4,6
		FString Str;
		TArray<int32> Out;
		if (FParse::Value(FCommandLine::Get(), TEXT("MechMeleeFeelBlade="), Str, false))
		{
			TArray<FString> Parts;
			Str.ParseIntoArray(Parts, TEXT(","));
			for (const FString& P : Parts)
			{
				if (P.IsNumeric())
				{
					Out.Add(FCString::Atoi(*P));
				}
			}
		}
		return Out.Num() ? Out : TArray<int32>{0, 1, 2, 3, 4, 5, 6};
	}();
	// -MechMeleeFeelFrames（10-31 肩甲擦头）：一把刀（-MechMeleeFeelBlade 的第一把，不给就是大剑）按九种框架各打一遍，镜头在左前方看上身；
	// CSV 多记每帧左 / 右肩插进头多深（推之前 / 之后）、甩肘 / 歪头角度；只在推之前有插进去的帧截图（两次跑帧号对得上）
	static const bool bFramesMode = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFeelFrames"));
	static const int32 FrameBlade = FString(FCommandLine::Get()).Contains(TEXT("MechMeleeFeelBlade=")) ? List[0] : 3;
	const int32 NumSegs = bFramesMode ? MechNumFrames : List.Num();
	auto BladeOf = [&](int32 Seg_) { return bFramesMode ? FrameBlade : List.IsValidIndex(Seg_) ? List[Seg_] : 0; };
	// -MechMeleeFeelTag=名字：CSV 和逐帧截图换个名字存（改前 / 改后对比时不互相覆盖），不给就按做法叫「新 / 旧」
	static const FString TagStr = [] {
		FString S;
		return FParse::Value(FCommandLine::Get(), TEXT("MechMeleeFeelTag="), S) && !S.IsEmpty() ? S
			: FString(FParse::Param(FCommandLine::Get(), TEXT("MechMeleeOld")) ? TEXT("旧") : TEXT("新"));
	}();
	const TCHAR* Tag = *TagStr;
	if (!DummyRef.IsValid())
	{
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (It->IsTrainingDummy() && (!DummyRef.IsValid() || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(DummyRef->GetActorLocation(), Me->GetActorLocation())))
			{
				DummyRef = *It;
			}
		}
		if (!DummyRef.IsValid())
		{
			return;
		}
		Home = DummyRef->GetActorLocation();
		Dir = DummyRef->GetActorForwardVector().GetSafeNormal2D();
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Cam = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
		Cam->GetCameraComponent()->SetFieldOfView(68.f);
		Cam->GetCameraComponent()->bConstrainAspectRatio = false;
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		Rows.Add(TEXT("tag,blade,t,frame,move,step,melee_t,driving,speed_h,speed_3d,accel,yaw,yaw_rate,max_bone_deg,jump_bone,max_jerk_deg,jerk_bone,dist,")
			TEXT("tipr_d,tipr_dd,tipl_d,tipl_dd,handr_d,handr_dd,handl_d,handl_dd,tipr_x,tipr_y,tipr_z,mech_frame,shl_d0,shl_d1,shr_d0,shr_d1,shl_swing,shl_clav,shl_tilt,shr_swing,shr_clav,shr_tilt"));
		UE_LOG(LogTemp, Display, TEXT("[meleefeel] 开始（%s做法）"), Tag);
	}
	AMechCharacter* Dummy = DummyRef.Get();
	UMechCombatComponent* Mine = Me->GetCombat();
	const int32 S = FMath::FloorToInt((Clock - 3.f) / SegLen);
	const float T = Clock - 3.f - S * SegLen;
	const int32 BladeIdx = BladeOf(S);
	auto Finish = [&]() {
		const FString Dir0 = FPaths::ProjectSavedDir() / TEXT("MeleeFeel");
		IFileManager::Get().MakeDirectory(*Dir0, true);
		FFileHelper::SaveStringArrayToFile(Rows, *(Dir0 / FString::Printf(TEXT("%s.csv"), Tag)), FFileHelper::EEncodingOptions::ForceUTF8);
		UE_LOG(LogTemp, Display, TEXT("[meleefeel] 全部测完，CSV：%s"), *(Dir0 / FString::Printf(TEXT("%s.csv"), Tag)));
		bQuit = true;
		PC->ConsoleCommand(TEXT("quit"));
	};
	auto Summary = [&](int32 Blade) {
		Sum.Jerks.Sort();
		const float P95 = Sum.Jerks.Num() ? Sum.Jerks[FMath::Min(Sum.Jerks.Num() - 1, FMath::FloorToInt(Sum.Jerks.Num() * 0.95f))] : 0.f;
		UE_LOG(LogTemp, Display, TEXT("[meleefeel] %s %s：每帧骨骼最大转角 %.1f°，骨骼角速度突变 最大 %.1f°（%s，第 %.2f 秒）/ 95%% %.1f°，转向角速度最大 %.0f°/秒，最快 %.0f 厘米/秒，最大加速度 %.0f 厘米/秒²"),
			Tag, MechWeapons::Blade(static_cast<EMechMeleeWeapon>(Blade)).Name, Sum.MaxJump, Sum.MaxJerk, *Sum.JerkBone, Sum.JerkAt, P95, Sum.MaxYawRate, Sum.MaxSpeed, Sum.MaxAccel);
		Sum = FSum();
	};
	if (S >= NumSegs)
	{
		if (Seg >= 0)
		{
			Summary(BladeOf(Seg));
		}
		Finish();
		return;
	}
	const EMechMeleeWeapon BladeType = static_cast<EMechMeleeWeapon>(BladeIdx);
	const bool bDagger = MechWeapons::Blade(BladeType).Knives > 0;
	// 九种框架模式：站在 32 米外（突进够不着，原地出招），镜头在左前方不被木桩挡
	const FVector Start = Home + Dir * (bFramesMode ? 3200.f : 1300.f);
	const float FaceDummy = (-Dir).Rotation().Yaw;
	if (S != Seg)
	{
		if (Seg >= 0)
		{
			Summary(BladeOf(Seg));
		}
		Seg = S;
		FMechLoadout L = Me->GetLoadout();
		L.Shooter = EMechShooter::Rifle;
		L.Melee = BladeType;
		if (bFramesMode)
		{
			L.Frame = static_cast<EMechFrame>(S);
		}
		Me->SetLoadout(L, true);
		Me->GetEffects()->ClearEffects();
		Me->SetActorLocation(Start, false, nullptr, ETeleportType::TeleportPhysics);
		Me->GetCharacterMovement()->StopMovementImmediately();
		Mine->SetForcedTarget(Dummy);
		LastPress = -100.f;
		bHeavyDone = false;
		bHavePrev = false;
		PtFrames = 0;
		// 镜头：突进路线中点的侧面，看全 13 米
		const FVector Mid = (Home + Start) * 0.5f;
		const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
		const FVector CamAt = Mid + Side * 1450.f + FVector(0.f, 0.f, 120.f);
		Cam->SetActorLocationAndRotation(CamAt, (Mid + FVector(0.f, 0.f, 60.f) - CamAt).Rotation());
		PC->SetViewTarget(Cam.Get());
		UE_LOG(LogTemp, Display, TEXT("[meleefeel] %02d %s：13 米外、朝向偏 70 度出轻击"), S, MechWeapons::Blade(BladeType).Name);
	}
	if (PC->GetViewTarget() != Cam.Get())
	{
		PC->SetViewTarget(Cam.Get());
	}
	// -MechMeleeFeelClose（10-31 段间跳变）：镜头跟着机体、在它右前方 7 米看上身（看清连招各段之间刀和手臂怎么接）
	static const bool bClose = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFeelClose"));
	if (bClose)
	{
		const FVector Side = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
		const FVector Focus = Me->GetActorLocation() + FVector(0.f, 0.f, 90.f);
		const FVector CamAt = Focus + Side * 680.f + Dir * 60.f + FVector(0.f, 0.f, 50.f);  // 机体朝 -Dir 冲向木桩：正侧面稍偏后，木桩在画面一边不挡
		Cam->SetActorLocationAndRotation(CamAt, (Focus - CamAt).Rotation());
	}
	if (bFramesMode)
	{
		// 九种框架：镜头在机体左前方、胸口高度看上身（肩甲和头）
		const FVector Fwd = Me->GetActorForwardVector().GetSafeNormal2D();
		const FVector LeftV = FVector::CrossProduct(Fwd, FVector::UpVector).GetSafeNormal();
		const FVector Focus = Me->GetMesh()->GetBoneLocation(TEXT("neck_01"));  // 对着脖子：头和两边肩甲都在画面中间
		const FVector CamAt = Focus + Fwd * 250.f + LeftV * 150.f + FVector(0.f, 0.f, 70.f);
		Cam->SetActorLocationAndRotation(CamAt, (Focus - CamAt).Rotation());
	}
	// 拿刀；出招前保持侧着身子（朝向偏离木桩 70 度），玩家看着木桩（镜头方向 = 瞄准方向）
	if (T < 1.2f && Mine->GetStance() != EMechStance::Blade && Mine->GetSwitchProgress() < 0.f && !Mine->IsMeleeActive())
	{
		Mine->SwitchStance();
	}
	if (T < AttackAt)
	{
		Me->SetActorLocation(FVector(Start.X, Start.Y, Me->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, FaceDummy + 70.f, 0.f));
	}
	PC->SetControlRotation((Dummy->GetActorLocation() + FVector(0.f, 0.f, 60.f) - (Me->GetActorLocation() + FVector(0.f, 0.f, 120.f))).Rotation());
	auto Light = [&]() {
		if (bDagger)
		{
			Mine->RequestDaggerSlash();
		}
		else
		{
			Mine->RequestLight();
		}
		LastPress = T;
	};
	if (T >= AttackAt && LastPress < 0.f)
	{
		Light();  // 第一下：远距离突进
	}
	else if (T > AttackAt && T < 4.6f && Mine->IsMeleeActive() && Mine->GetComboStep() >= 1 && Mine->GetComboStep() < 4 && T - LastPress > 0.1f)
	{
		Light();  // 连按：排到可取消时刻接下一段
	}
	if (!bDagger && !bHeavyDone && T >= 4.9f && !Mine->IsMeleeActive())
	{
		Mine->RequestHeavy();
		bHeavyDone = true;
	}

	// ---- 逐帧量
	const bool bRec = T >= AttackAt - 0.2f && T < SegLen - 0.1f;
	const USkeletalMeshComponent* Mesh = Me->GetMesh();
	const TArray<FTransform>& CS = Mesh->GetComponentSpaceTransforms();
	const int32 NB = UE_ARRAY_COUNT(Bones);
	TArray<FQuat> Rot;
	Rot.SetNum(NB);
	for (int32 i = 0; i < NB; ++i)
	{
		const int32 Idx = Mesh->GetBoneIndex(FName(Bones[i]));
		Rot[i] = CS.IsValidIndex(Idx) ? CS[Idx].GetRotation() : FQuat::Identity;
	}
	const FVector Loc = Me->GetActorLocation();
	const float Yaw = Me->GetActorRotation().Yaw;
	// 刀尖 / 手腕（网格空间、世界尺寸厘米）：刀尖 = 刀模型包围盒最远端（刀身沿模型 +X）
	FVector Pts[NumPts];
	{
		UMechMeshComponent* MM = Me->GetMechMesh();
		const FTransform MeshT = Mesh->GetComponentTransform();
		auto TipOf = [&](int32 Prop, FName HandBone) {
			const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(MM->Props[Prop].Get());
			if (SM && SM->IsVisible() && SM->GetStaticMesh())
			{
				const FBox Box = SM->GetStaticMesh()->GetBoundingBox();
				return MeshT.InverseTransformPositionNoScale(MM->GetPropPoint(Prop, FVector(Box.Max.X, Box.GetCenter().Y, Box.GetCenter().Z)));
			}
			return MeshT.InverseTransformPositionNoScale(Mesh->GetBoneLocation(HandBone));
		};
		Pts[0] = TipOf(0, TEXT("hand_r"));
		Pts[1] = TipOf(4, TEXT("hand_l"));
		Pts[2] = MeshT.InverseTransformPositionNoScale(Mesh->GetBoneLocation(TEXT("hand_r")));
		Pts[3] = MeshT.InverseTransformPositionNoScale(Mesh->GetBoneLocation(TEXT("hand_l")));
	}
	float PtD[NumPts] = {}, PtDD[NumPts] = {};
	for (int32 k = 0; k < NumPts; ++k)
	{
		PtD[k] = PtFrames >= 1 ? FVector::Dist(Pts[k], PrevPt[k]) : 0.f;
		PtDD[k] = PtFrames >= 2 ? (Pts[k] - 2.f * PrevPt[k] + PrevPt2[k]).Size() : 0.f;
		PrevPt2[k] = PrevPt[k];
		PrevPt[k] = Pts[k];
	}
	++PtFrames;
	if (bHavePrev && Dt > 0.f)
	{
		TArray<FVector> Omega;
		Omega.SetNum(NB);
		float MaxJump = 0.f, MaxJerk = 0.f;
		int32 JumpBone = 0, JerkBone = 0;
		for (int32 i = 0; i < NB; ++i)
		{
			FQuat D = (Rot[i] * PrevRot[i].Inverse()).GetNormalized();
			if (D.W < 0.f)
			{
				D = FQuat(-D.X, -D.Y, -D.Z, -D.W);
			}
			Omega[i] = FVector(D.ToRotationVector()) * (180.f / PI);  // 这一帧转了多少（度，旋转向量）
			const float Jump = Omega[i].Size();
			if (Jump > MaxJump)
			{
				MaxJump = Jump;
				JumpBone = i;
			}
			if (PrevOmega.Num() == NB)
			{
				const float Jerk = (Omega[i] - PrevOmega[i]).Size();
				if (Jerk > MaxJerk)
				{
					MaxJerk = Jerk;
					JerkBone = i;
				}
			}
		}
		const FVector V = (Loc - PrevLoc) / Dt;
		const float SpeedH = V.Size2D();
		const float Accel = (V.Size() - PrevSpeed) / Dt;
		const float YawRate = FMath::FindDeltaAngleDegrees(PrevYaw, Yaw) / Dt;
		PrevSpeed = V.Size();
		if (bRec)
		{
			Rows.Add(FString::Printf(TEXT("%s,%d,%.3f,%d,%d,%d,%.3f,%d,%.0f,%.0f,%.0f,%.1f,%.0f,%.2f,%s,%.2f,%s,%.0f"), Tag, BladeIdx, T, Frame,
				static_cast<int32>(Mine->GetMeleeMove()), Mine->GetComboStep(), Mine->GetMeleeTime(), Mine->IsMeleeDriving() ? 1 : 0, SpeedH, V.Size(), Accel, Yaw, YawRate,
				MaxJump, Bones[JumpBone], MaxJerk, Bones[JerkBone], FVector::Dist(Loc, Dummy->GetActorLocation())) +
				FString::Printf(TEXT(",%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f"), PtD[0], PtDD[0], PtD[1], PtDD[1], PtD[2], PtDD[2], PtD[3], PtDD[3],
					Pts[0].X, Pts[0].Y, Pts[0].Z) +
				FString::Printf(TEXT(",%d,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f"), static_cast<int32>(Me->GetLoadout().Frame),
					Me->GetMechMesh()->DebugShoulderDepth[0].X, Me->GetMechMesh()->DebugShoulderDepth[0].Y, Me->GetMechMesh()->DebugShoulderDepth[1].X,
					Me->GetMechMesh()->DebugShoulderDepth[1].Y, Me->GetMechMesh()->DebugShoulderFix[0].X, Me->GetMechMesh()->DebugShoulderFix[0].Y,
					Me->GetMechMesh()->DebugShoulderFix[0].Z, Me->GetMechMesh()->DebugShoulderFix[1].X, Me->GetMechMesh()->DebugShoulderFix[1].Y,
					Me->GetMechMesh()->DebugShoulderFix[1].Z));
			Sum.MaxJump = FMath::Max(Sum.MaxJump, MaxJump);
			if (MaxJerk > Sum.MaxJerk)
			{
				Sum.MaxJerk = MaxJerk;
				Sum.JerkBone = Bones[JerkBone];
				Sum.JerkAt = T;
			}
			Sum.Jerks.Add(MaxJerk);
			Sum.MaxYawRate = FMath::Max(Sum.MaxYawRate, FMath::Abs(YawRate));
			Sum.MaxSpeed = FMath::Max(Sum.MaxSpeed, V.Size());
			Sum.MaxAccel = FMath::Max(Sum.MaxAccel, FMath::Abs(Accel));
		}
		PrevOmega = MoveTemp(Omega);
	}
	PrevRot = MoveTemp(Rot);
	PrevLoc = Loc;
	PrevYaw = Yaw;
	bHavePrev = true;
	// 九种框架模式只拍「推之前肩插进头」的帧（改前改后两次跑同样的帧）
	const bool bShoulderShot = !bFramesMode || Me->GetMechMesh()->DebugShoulderDepth[0].X > 0.f || Me->GetMechMesh()->DebugShoulderDepth[1].X > 0.f;
	if (bRec && bVideo && bShoulderShot)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/MeleeFeel") / (TagStr == TEXT("旧") ? FString(TEXT("old")) : TagStr == TEXT("新") ? FString(TEXT("new")) : TagStr) /
			FString::Printf(TEXT("%05d.png"), Frame), false, false);
	}
	if (bRec)
	{
		++Frame;
	}
}
