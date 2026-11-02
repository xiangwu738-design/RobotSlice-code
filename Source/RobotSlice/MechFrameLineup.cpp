#include "MechFrameLineup.h"

#include "Animation/Skeleton.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "MechCatalog.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechLoadout.h"
#include "MechSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"

namespace MechLineupPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的东西撞
{
	/** 一张照片：什么时候摆好镜头、镜头位置 / 看向哪、视野、只显示哪一台（-1 全显示）、文件名 */
	struct FShot
	{
		FVector Eye;
		FVector Look;
		float Fov;
		int32 Only;
		FString Name;
		// 10-31 手部特写（-MechLineupHands=标签）：拍的时候才按第 HandOf 台右手骨头的位置摆镜头（展示姿势要几帧才摆好），
		// 镜头从 HandDir 方向、离手 HandDist 厘米看过去
		int32 HandOf = -1;
		FVector HandDir = FVector::ZeroVector;
		float HandDist = 0.f;
	};

	/** 10-31 手指脱节（「手部重复」）：每节手指骨头离「父骨头 × 绑定姿势里的局部平移」多远，世界厘米（组件空间 × 组件缩放）。
	 *  手指零件整块挂在各自的骨头上，正常是 0；以前 UMechMeshComponent::ApplyFingerPose 拿已经转过的父骨头算局部变换，
	 *  第二、三节留在动作里的原处，这个数就是它们浮出去的距离。Worst 写最远的那节骨头名 */
	float FingerGap(const USkeletalMeshComponent* SK, const TCHAR* Side, FString& Worst)
	{
		const USkinnedAsset* Asset = SK ? SK->GetSkinnedAsset() : nullptr;
		if (!Asset)
		{
			return -1.f;
		}
		const FReferenceSkeleton& R = Asset->GetRefSkeleton();
		const TArray<FTransform>& Ref = R.GetRefBonePose();
		const TArray<FTransform>& CS = SK->GetComponentSpaceTransforms();
		float Max = 0.f;
		static const TCHAR* Fingers[] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
		for (const TCHAR* F : Fingers)
		{
			for (int32 j = 1; j <= 3; ++j)
			{
				const FName Bone(*FString::Printf(TEXT("%s_%02d_%s"), F, j, Side));
				const int32 i = R.FindBoneIndex(Bone);
				const int32 p = i != INDEX_NONE ? R.GetParentIndex(i) : INDEX_NONE;
				if (p == INDEX_NONE || !CS.IsValidIndex(i) || !CS.IsValidIndex(p))
				{
					continue;
				}
				const float Gap = FVector::Dist(CS[i].GetLocation(), (Ref[i] * CS[p]).GetLocation()) * SK->GetComponentScale().X;
				if (Gap > Max)
				{
					Max = Gap;
					Worst = Bone.ToString();
				}
			}
		}
		return Max;
	}

	struct FState
	{
		bool bSetup = false;
		bool bDone = false;
		float Clock = 0.f;
		TArray<TWeakObjectPtr<AMechCharacter>> Mechs;
		TArray<int32> Frames;  // 每台是哪种框架（显示顺序）
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<FShot> Shots;
		int32 Next = 0;
		float ShotStart = 0.f;
		bool bPending = false;
	};
	FState State;

	/** 开头核对每台网格：骨头和骨架 / v3 是否一致、材质槽、面数、减震骨头有没有蒙皮 */
	void CheckMeshes()
	{
		const USkeletalMesh* V3 = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Robot/Meshes/SKM_Robot_v3"));
		for (int32 F = 0; F < MechNumFrames; ++F)
		{
			const MechCatalog::FFrameBody& Body = MechCatalog::FrameBody(F);
			const USkeletalMesh* SM = LoadObject<USkeletalMesh>(nullptr, Body.Mesh);
			if (!SM)
			{
				UE_LOG(LogTemp, Warning, TEXT("[lineup] %s 网格 %s 没有（没导入）"), Body.Key, Body.Mesh);
				continue;
			}
			const FReferenceSkeleton& R = SM->GetRefSkeleton();
			// 和骨架（SK_Mannequin）比名字和父子关系，和 v3（新纪轻型）比绑定姿势
			bool bSkel = SM->GetSkeleton() != nullptr;
			if (bSkel)
			{
				const FReferenceSkeleton& RS = SM->GetSkeleton()->GetReferenceSkeleton();
				for (int32 i = 0; i < R.GetNum(); ++i)
				{
					const int32 j = RS.FindBoneIndex(R.GetBoneName(i));
					const int32 P = R.GetParentIndex(i);
					bSkel &= j != INDEX_NONE && (P == INDEX_NONE ? RS.GetParentIndex(j) == INDEX_NONE : RS.GetParentIndex(j) == RS.FindBoneIndex(R.GetBoneName(P)));
				}
			}
			bool bSameOrder = V3 && V3->GetRefSkeleton().GetNum() == R.GetNum();
			double MaxPos = 0.0, MaxAng = 0.0;
			if (V3)
			{
				const FReferenceSkeleton& R0 = V3->GetRefSkeleton();
				for (int32 i = 0; i < R.GetNum(); ++i)
				{
					const int32 j = R0.FindBoneIndex(R.GetBoneName(i));
					bSameOrder &= j == i;
					if (j != INDEX_NONE)
					{
						MaxPos = FMath::Max(MaxPos, FVector::Dist(R.GetRefBonePose()[i].GetLocation(), R0.GetRefBonePose()[j].GetLocation()));
						MaxAng = FMath::Max(MaxAng, FMath::RadiansToDegrees(R.GetRefBonePose()[i].GetRotation().AngularDistance(R0.GetRefBonePose()[j].GetRotation())));
					}
				}
			}
			FString Slots;
			for (const FSkeletalMaterial& M : SM->GetMaterials())
			{
				Slots += FString::Printf(TEXT("%s=%s "), *M.MaterialSlotName.ToString().Replace(TEXT("MI_Robot_"), TEXT("")).Replace(TEXT("MI_Kit_"), TEXT("Kit_")),
					M.MaterialInterface ? *M.MaterialInterface->GetName() : TEXT("空"));
			}
			int32 Tris = 0, Verts = 0, Dampers = 0;
			if (const FSkeletalMeshRenderData* RD = SM->GetResourceForRendering())
			{
				if (RD->LODRenderData.Num() > 0)
				{
					TSet<int32> Skinned;
					for (const FSkelMeshRenderSection& Sec : RD->LODRenderData[0].RenderSections)
					{
						Tris += Sec.NumTriangles;
						Verts += Sec.NumVertices;
						for (const FBoneIndexType B : Sec.BoneMap)
						{
							Skinned.Add(B);
						}
					}
					for (int32 i = 0; i < R.GetNum(); ++i)
					{
						Dampers += R.GetBoneName(i).ToString().StartsWith(TEXT("damper_")) && Skinned.Contains(i) ? 1 : 0;
					}
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[lineup] %s %s：骨头 %d 根，和骨架 %s 名字 / 父子%s，和 v3 顺序%s、绑定姿势最大差 %.3f 厘米 / %.3f 度；三角 %d 顶点 %d；有蒙皮的减震骨头 %d 根；材质槽 %d：%s"),
				Body.Key, *SM->GetName(), R.GetNum(), SM->GetSkeleton() ? *SM->GetSkeleton()->GetName() : TEXT("无"), bSkel ? TEXT("一致") : TEXT("不一致"),
				bSameOrder ? TEXT("一致") : TEXT("不一致"), MaxPos, MaxAng, Tris, Verts, Dampers, SM->GetMaterials().Num(), *Slots);
		}
	}
}

void FMechFrameLineup::Tick(AMechCharacter* Me, float DeltaSeconds)
{
	using namespace MechLineupPriv;
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechFrameLineup"));
	if (!bOn || !Me || !Me->IsLocallyControlled() || !Me->IsPlayerControlled() || State.bDone)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	UWorld* World = Me->GetWorld();
	if (!PC || !World)
	{
		return;
	}
	State.Clock += World->GetDeltaSeconds();
	if (!State.bSetup)
	{
		if (State.Clock < 1.5f)
		{
			return;
		}
		State.bSetup = true;
		CheckMeshes();
		// 场地：测试场出生点后面 22 米的空地（出生点在原点朝 +X）。机体朝向太阳那边再偏 25 度（顺光、有一点侧光出体积；第一版朝 +X 是逆光，
		// 全黑一片），-MechLineupFace= 直接给朝向；镜头从机体前方看回来
		float FaceYaw = 0.f;
		{
			const UDirectionalLightComponent* Sun = nullptr;
			for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && It->IsVisible() && It->Intensity > 0.f && (!Sun || It->Intensity > Sun->Intensity))
				{
					Sun = *It;
				}
			}
			if (Sun)
			{
				const FVector To = -Sun->GetForwardVector();
				FaceYaw = FMath::RoundToFloat(FVector(To.X, To.Y, 0.f).Rotation().Yaw + 25.f);
				UE_LOG(LogTemp, Display, TEXT("[lineup] 太阳 %s 强度 %.1f 光照方向 %s → 机体朝 %.0f 度"), *Sun->GetName(), Sun->Intensity, *Sun->GetForwardVector().ToString(), FaceYaw);
			}
		}
		FParse::Value(FCommandLine::Get(), TEXT("MechLineupFace="), FaceYaw);
		const FVector Fwd = FRotator(0.f, FaceYaw, 0.f).Vector();
		const FVector Right = FRotator(0.f, FaceYaw + 90.f, 0.f).Vector();
		FVector Base(-2200.f, 0.f, 0.f);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Base + FVector(0.f, 0.f, 3000.f), Base - FVector(0.f, 0.f, 3000.f), ECC_WorldStatic))
		{
			Base = Hit.ImpactPoint;
		}
		const float Half = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		constexpr float Spacing = 260.f;
		// 自己那台、测试场木桩藏起来，HUD 关掉
		Me->SetActorHiddenInGame(true);
		Me->SetActorEnableCollision(false);
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (It->IsTrainingDummy())
			{
				It->SetActorHiddenInGame(true);
			}
		}
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = false;
		}
		MechLoadout::DisplayOrder(MechLoadout::Frame, State.Frames);
		const int32 N = State.Frames.Num();
		for (int32 k = 0; k < N; ++k)
		{
			// 从镜头看过去第一台（新纪轻型）在最左边：镜头朝 -Fwd 看，画面左边 = 机体的右边
			const FVector At = Base + Right * ((N - 1) * 0.5f - k) * Spacing + FVector(0.f, 0.f, Half + 2.f);
			const FTransform T(FRotator(0.f, FaceYaw, 0.f), At);
			AMechCharacter* M = World->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), T, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!M)
			{
				continue;
			}
			M->SetReplicates(false);
			M->AutoPossessAI = EAutoPossessAI::Disabled;
			M->SetTeam(0);
			M->DisplayName = MechCatalog::FrameBody(State.Frames[k]).Key;
			M->FinishSpawning(T);
			M->SetActorEnableCollision(false);
			if (UCharacterMovementComponent* Move = M->GetCharacterMovement())
			{
				Move->DisableMovement();
				Move->SetComponentTickEnabled(false);
			}
			FMechLoadout L = FMechSettings::Get().Loadout;
			L.Frame = static_cast<EMechFrame>(State.Frames[k]);
			L.Shooter = EMechShooter::Rifle;
			M->SetLoadout(L, true);
			M->SetPaint(FMechSettings::Get().Paint);
			if (UMechCombatComponent* Combat = M->GetCombat())
			{
				Combat->AimOverride = Fwd;
			}
			// 展示姿势：竖大拇指、枪收到背上（同配装照片 / 结算展示）。端枪待机时上身是侧着的（没交战时枪只对准六成），正面看不到胸口；
			// 端枪 / 挥刀 / 飞行的样子另见 -MechViewerShots=frames。
			// 10-31 结算场景：-MechLineupPose=gun（结算里胜方低姿端长枪，枪口压 32 度）/ blade（拿刀站）看另外两种展示姿势，默认 thumbs
			static FString Pose = [] { FString P = TEXT("thumbs"); FParse::Value(FCommandLine::Get(), TEXT("MechLineupPose="), P); return P; }();
			if (Pose == TEXT("blade") && M->GetCombat())
			{
				M->GetCombat()->SetStanceNow(EMechStance::Blade);
			}
			M->SetResultsPose(Pose == TEXT("thumbs"), Pose == TEXT("thumbs") ? 0.f : -3.f, 32.f);
			M->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			State.Mechs.Add(M);
		}
		State.Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(Base + Fwd * 1000.f + FVector(0.f, 0.f, 200.f)));
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
			Cam->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
			Cam->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
		}
		// 镜头表：整排正面 / 斜侧，然后每台单独正面 / 斜侧（斜侧 = 从机体右前方 40 度看，同模型查看的「右前」）
		const float Mid = 125.f;  // 看机体腰上一点（地面以上，世界尺寸）
		auto Ground = [&](const FVector& P) { return FVector(P.X, P.Y, Base.Z); };
		auto Around = [&](float Deg) { return FRotator(0.f, FaceYaw + Deg, 0.f).Vector(); };
		const float RowDist = 3000.f;
		State.Shots.Add({Base + Fwd * RowDist + FVector(0.f, 0.f, Mid + 60.f), Base + FVector(0.f, 0.f, Mid), 42.f, -1, TEXT("00_整排_正面")});
		State.Shots.Add({Base + Around(-30.f) * 2200.f + Right * 600.f + FVector(0.f, 0.f, Mid + 160.f), Base + Right * 300.f + FVector(0.f, 0.f, Mid), 50.f, -1,
			TEXT("00_整排_斜侧")});
		for (int32 k = 0; k < State.Mechs.Num(); ++k)
		{
			const AMechCharacter* M = State.Mechs[k].Get();
			if (!M)
			{
				continue;
			}
			const FVector G = Ground(M->GetActorLocation());
			const FString Key = MechCatalog::FrameBody(State.Frames[k]).Key;
			State.Shots.Add({G + Fwd * 900.f + FVector(0.f, 0.f, Mid + 15.f), G + FVector(0.f, 0.f, Mid), 30.f, k, FString::Printf(TEXT("%02d_%s_正面"), k + 1, *Key)});
			State.Shots.Add({G + Around(-40.f) * 900.f + FVector(0.f, 0.f, Mid + 60.f), G + FVector(0.f, 0.f, Mid), 30.f, k, FString::Printf(TEXT("%02d_%s_斜侧"), k + 1, *Key)});
		}
		// 10-31 -MechLineupHands=标签：只拍九台右手特写（竖大拇指的拳头；-MechLineupPose=blade 时是握刀的手），正面、右前 50 度各一张，
		// 存 Saved/Screenshots/FrameHands/<标签>/，日志 [lineup] 手指脱节。查「手部重复」用，配 -MechFingerPoseOld 拍改前
		if (FString HandsTag; FParse::Value(FCommandLine::Get(), TEXT("MechLineupHands="), HandsTag))
		{
			State.Shots.Reset();
			for (int32 k = 0; k < State.Mechs.Num(); ++k)
			{
				const FString Key = MechCatalog::FrameBody(State.Frames[k]).Key;
				FShot A{FVector::ZeroVector, FVector::ZeroVector, 30.f, k, FString::Printf(TEXT("%02d_%s_右手正面"), k + 1, *Key)};
				A.HandOf = k;
				A.HandDir = (Fwd + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();
				A.HandDist = 130.f;
				State.Shots.Add(A);
				FShot B{FVector::ZeroVector, FVector::ZeroVector, 30.f, k, FString::Printf(TEXT("%02d_%s_右手右前"), k + 1, *Key)};
				B.HandOf = k;
				B.HandDir = (Around(-50.f) + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();
				B.HandDist = 130.f;
				State.Shots.Add(B);
			}
		}
		State.ShotStart = State.Clock + 4.f;  // 等网格贴图流送、距离场建好
		UE_LOG(LogTemp, Display, TEXT("[lineup] 摆好 %d 台，%d 张照片"), State.Mechs.Num(), State.Shots.Num());
		return;
	}
	ACameraActor* Cam = State.Camera.Get();
	if (Cam && PC->GetViewTarget() != Cam)
	{
		PC->SetViewTarget(Cam);
	}
	// 手部特写：等的这 1.2 秒里镜头一直跟着右手（握刀站时手跟着待机动作在动，只在开头算一次会拍空）
	if (Cam && State.bPending && State.Shots.IsValidIndex(State.Next))
	{
		FShot& HS = State.Shots[State.Next];
		if (const AMechCharacter* HM = State.Mechs.IsValidIndex(HS.HandOf) ? State.Mechs[HS.HandOf].Get() : nullptr)
		{
			const USkeletalMeshComponent* HSK = HM->GetMesh();
			HS.Look = (HSK->GetBoneLocation(TEXT("hand_r")) + HSK->GetBoneLocation(TEXT("middle_01_r"))) * 0.5f;
			HS.Eye = HS.Look + HS.HandDir * HS.HandDist;
			Cam->SetActorLocationAndRotation(HS.Eye, (HS.Look - HS.Eye).Rotation());
		}
	}
	if (State.Clock < State.ShotStart || !Cam)
	{
		return;
	}
	if (State.Next >= State.Shots.Num())
	{
		if (State.Clock > State.ShotStart + 1.f)
		{
			State.bDone = true;
			UE_LOG(LogTemp, Display, TEXT("[lineup] 拍完，退出"));
			PC->ConsoleCommand(TEXT("quit"));
		}
		return;
	}
	const FShot& S = State.Shots[State.Next];
	if (!State.bPending)
	{
		// 摆镜头、只显示要拍的那台；等 1.2 秒（TSR 积累、换角度后的流送）再截
		State.bPending = true;
		if (const AMechCharacter* HM = State.Mechs.IsValidIndex(S.HandOf) ? State.Mechs[S.HandOf].Get() : nullptr)
		{
			// 手部特写：对准右手掌心附近（手骨往中指根挪一半），这一刻才算位置
			const USkeletalMeshComponent* SK = HM->GetMesh();
			State.Shots[State.Next].Look = (SK->GetBoneLocation(TEXT("hand_r")) + SK->GetBoneLocation(TEXT("middle_01_r"))) * 0.5f;
			State.Shots[State.Next].Eye = S.Look + S.HandDir * S.HandDist;
		}
		Cam->SetActorLocationAndRotation(S.Eye, (S.Look - S.Eye).Rotation());
		Cam->GetCameraComponent()->SetFieldOfView(S.Fov);
		for (int32 k = 0; k < State.Mechs.Num(); ++k)
		{
			if (AMechCharacter* M = State.Mechs[k].Get())
			{
				M->SetActorHiddenInGame(S.Only >= 0 && S.Only != k);
			}
		}
		State.ShotStart = State.Clock + 1.2f;
		return;
	}
	State.bPending = false;
	if (S.HandOf >= 0)
	{
		FString HandsTag;
		FParse::Value(FCommandLine::Get(), TEXT("MechLineupHands="), HandsTag);
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/FrameHands") / HandsTag / (S.Name + TEXT(".png")), false, false);
		if (const AMechCharacter* HM = State.Mechs[S.HandOf].Get())
		{
			FString WorstR, WorstL;
			const float GapR = FingerGap(HM->GetMesh(), TEXT("r"), WorstR);
			const float GapL = FingerGap(HM->GetMesh(), TEXT("l"), WorstL);
			UE_LOG(LogTemp, Display, TEXT("[lineup] 手指脱节 %s：右手最大 %.2f 厘米（%s），左手最大 %.2f 厘米（%s）"), *S.Name, GapR, *WorstR, GapL, *WorstL);
		}
		++State.Next;
		State.ShotStart = State.Clock + 0.3f;
		return;
	}
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Frames") / (S.Name + TEXT(".png")), false, false);
	// 朝向核对：机体朝向、脚尖朝向（左脚踝 → 左脚掌）、两手连线（右手 → 左手，长枪大致沿枪管）的水平角度
	FString Facing;
	if (const AMechCharacter* M = S.Only >= 0 && State.Mechs.IsValidIndex(S.Only) ? State.Mechs[S.Only].Get() : nullptr)
	{
		const USkeletalMeshComponent* SK = M->GetMesh();
		const FVector Toe = SK->GetBoneLocation(TEXT("ball_l")) - SK->GetBoneLocation(TEXT("foot_l"));
		const FVector Hands = SK->GetBoneLocation(TEXT("hand_l")) - SK->GetBoneLocation(TEXT("hand_r"));
		// 10-31 胸口朝向：两根锁骨根连线（左 → 右）转 -90 度；和机体朝向差多少 = 上身拧了多少（展示姿势应在 ±3 度内）
		const FVector Clav = SK->GetBoneLocation(TEXT("clavicle_r")) - SK->GetBoneLocation(TEXT("clavicle_l"));
		const float Chest = FRotator::NormalizeAxis(Clav.Rotation().Yaw - 90.f - M->GetActorRotation().Yaw);
		const float Tilt = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Clav.GetSafeNormal().Z, -1.f, 1.f)));
		Facing = FString::Printf(TEXT("（机体朝向 %.0f° 脚尖 %.0f° 右手→左手 %.0f° 镜头朝 %.0f°；胸口相对机体 偏航 %+.1f° 侧歪 %+.1f°）"), M->GetActorRotation().Yaw,
			Toe.Rotation().Yaw, Hands.Rotation().Yaw, (S.Look - S.Eye).Rotation().Yaw, Chest, Tilt);
	}
	UE_LOG(LogTemp, Display, TEXT("[lineup] 截图 %s%s"), *S.Name, *Facing);
	++State.Next;
	State.ShotStart = State.Clock + 0.3f;
}
