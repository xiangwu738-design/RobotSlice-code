#include "MechMeshComponent.h"

#include "Engine/SkinnedAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "GameFramework/Character.h"
#include "ReferenceSkeleton.h"
#include "TwoBoneIK.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"

namespace
{
	// ---- 10-31 握持的常数（厘米，人形尺寸 = 组件空间）。和 lookdev/robot_lookdev.py 的「可弯手指的手」（build_grip_hand）一致，改模型要一起改
	constexpr float PalmFront = 1.35f;    // 手心面离手骨线多远（PALM_N[0]）
	constexpr float CavK = -0.8f;         // 手心空腔中心在指根线上的位置（沿 k，四指中间）
	constexpr float CavGap = 0.1f;        // 柄离手心
	constexpr float CavBack = 1.8f;       // 柄的后沿离手腕至少多远（别压进腕关节圆柱）
	constexpr float FingerClear = 1.05f;  // 指根到柄心线（沿 d）至少「半宽 + 这么多」，第一节才包得过去
	// 各节半厚（HAND_T 的一半）、末节长（HAND_TIP）：拇指、食指、中指、无名指、小指
	constexpr float HalfT[5][3] = {{1.15f, 1.f, 0.9f}, {0.95f, 0.88f, 0.8f}, {0.95f, 0.88f, 0.8f}, {0.93f, 0.85f, 0.78f}, {0.85f, 0.8f, 0.73f}};
	constexpr float TipLen[5] = {3.1f, 2.6f, 2.5f, 2.3f, 2.1f};
	// 弯曲角范围（度，相对绑定姿势）：四指三节；拇指二、三节
	constexpr float FingerLimit[3][2] = {{-15.f, 95.f}, {-10.f, 105.f}, {-10.f, 80.f}};
	constexpr float ThumbLimit[3][2] = {{-40.f, 55.f}, {-30.f, 85.f}, {-30.f, 80.f}};
	// 扳机食指（伸直、略弯）
	constexpr float TriggerAngle[3] = {10.f, 15.f, 8.f};
	const TCHAR* const FingerName[5] = {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
	const float ContactT[5] = {0.3f, 0.5f, 0.7f, 0.85f, 1.f};

	/** -MechGripV1：10-31 第一版握持（拇指「弯到碰上就停」、长枪右手肘不转），拍改前改后对比用 */
	bool GripV1()
	{
		static const bool bV1 = FParse::Param(FCommandLine::Get(), TEXT("MechGripV1"));
		return bV1;
	}
	/** 长枪右手：手肘最多绕「肩—腕」连线转多少度来少折手腕（-MechWristSwivel=度 打开试，默认 0 = 不转）。
	 *  10-31 实测 40 度时：霰弹枪手腕多折 50→18 度、冲锋枪 25→15，但手肘抬到和头差不多高（霰弹枪最明显），充能步枪只少 4 度；
	 *  还可能和肩甲擦头的修正（MechShoulderClear）打架，所以默认关着，留给用户定 */
	float MaxWristSwivel()
	{
		static const float Deg = [] {
			float D = 0.f;
			FParse::Value(FCommandLine::Get(), TEXT("MechWristSwivel="), D);
			return GripV1() ? 0.f : D;
		}();
		return Deg;
	}
	/** 点到线段的距离 */
	float PointSegDist(const FVector& Pt, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float T = FMath::Clamp(static_cast<float>(((Pt - A) | AB) / FMath::Max(AB.SizeSquared(), 1e-6)), 0.f, 1.f);
		return static_cast<float>(FVector::Dist(Pt, A + AB * T));
	}

	/** 刚体变换 x → R x + T（绕某点转的组合） */
	struct FAffine
	{
		FQuat R = FQuat::Identity;
		FVector T = FVector::ZeroVector;
		FVector Apply(const FVector& P) const { return R.RotateVector(P) + T; }
	};
	FAffine RotAbout(const FVector& Pivot, const FVector& Axis, float Deg)
	{
		FAffine A;
		A.R = FQuat(Axis, FMath::DegreesToRadians(Deg));
		A.T = Pivot - A.R.RotateVector(Pivot);
		return A;
	}
	/** 先 Inner 再 Outer */
	FAffine Compose(const FAffine& Outer, const FAffine& Inner)
	{
		FAffine A;
		A.R = Outer.R * Inner.R;
		A.T = Outer.R.RotateVector(Inner.T) + Outer.T;
		return A;
	}

	/** 柄（圆角方柱，手坐标）的有向距离：外面正、里面负 */
	struct FHandleSDF
	{
		bool bValid = false;
		FVector C = FVector::ZeroVector, A = FVector::ForwardVector, U = FVector::RightVector, V = FVector::UpVector;
		float HU = 1.f, HV = 1.f, RC = 1.f, A0 = -10.f, A1 = 10.f;
		float Eval(const FVector& P) const
		{
			if (!bValid)
			{
				return 1e6f;
			}
			const FVector Q = P - C;
			const float Al = Q | A;
			const float Dx = FMath::Abs(Q | U) - (HU - RC);
			const float Dy = FMath::Abs(Q | V) - (HV - RC);
			const float Out = FMath::Sqrt(FMath::Square(FMath::Max(Dx, 0.f)) + FMath::Square(FMath::Max(Dy, 0.f))) + FMath::Min(FMath::Max(Dx, Dy), 0.f) - RC;
			return FMath::Max3(Out, A0 - Al, Al - A1);
		}
	};

	/** 从 0 度起找第一次碰上的角：0 度已经碰到就往外张直到不碰；没碰到就往里弯直到碰上（步长 Step，再二分 Bisect 次） */
	template <typename FHit>
	float ContactAngle(FHit&& Hit, float Lo, float Hi, float Step, int32 Bisect)
	{
		float A = 0.f, Prev = 0.f;
		if (Hit(0.f))
		{
			while (A > Lo && Hit(A))
			{
				Prev = A;
				A -= Step;
			}
			return FMath::Max(A, Lo);
		}
		while (A < Hi && !Hit(A))
		{
			Prev = A;
			A += Step;
		}
		A = FMath::Min(A, Hi);
		if (!Hit(A))
		{
			return A;
		}
		float X0 = Prev, X1 = A;
		for (int32 i = 0; i < Bisect; ++i)
		{
			const float M = 0.5f * (X0 + X1);
			if (Hit(M))
			{
				X1 = M;
			}
			else
			{
				X0 = M;
			}
		}
		return X0;
	}
}

void UMechMeshComponent::CacheBones()
{
	const USkinnedAsset* Asset = GetSkinnedAsset();
	if (!Asset || CachedFor == Asset)
	{
		return;
	}
	CachedFor = Asset;
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	Parents.SetNum(Ref.GetNum());
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		Parents[i] = Ref.GetParentIndex(i);
	}
	static const TCHAR* Side[2] = {TEXT("l"), TEXT("r")};
	for (int32 s = 0; s < 2; ++s)
	{
		UpperArm[s] = Ref.FindBoneIndex(*FString::Printf(TEXT("upperarm_%s"), Side[s]));
		LowerArm[s] = Ref.FindBoneIndex(*FString::Printf(TEXT("lowerarm_%s"), Side[s]));
		Hand[s] = Ref.FindBoneIndex(*FString::Printf(TEXT("hand_%s"), Side[s]));
		Clavicle[s] = Ref.FindBoneIndex(*FString::Printf(TEXT("clavicle_%s"), Side[s]));
		ThumbBone[s] = Ref.FindBoneIndex(*FString::Printf(TEXT("thumb_up_%s"), Side[s]));
		Middle1[s] = Ref.FindBoneIndex(*FString::Printf(TEXT("middle_01_%s"), Side[s]));
	}
	Spine1 = Ref.FindBoneIndex(TEXT("spine_01"));
	Spine5 = Ref.FindBoneIndex(TEXT("spine_05"));
	Spine3 = Ref.FindBoneIndex(TEXT("spine_03"));

	// 跨关节减震：绑定姿势里 a 指向 b 的方向，换成各自骨头局部的方向记下来
	Dampers.Reset();
	TArray<FTransform> RefCS;
	RefCS.SetNum(Ref.GetNum());
	for (int32 i = 0; i < Ref.GetNum(); ++i)
	{
		RefCS[i] = Parents[i] == INDEX_NONE ? Ref.GetRefBonePose()[i] : Ref.GetRefBonePose()[i] * RefCS[Parents[i]];
	}
	static const TCHAR* Joint[2] = {TEXT("knee"), TEXT("ankle")};
	// 10-31 阵营机体：九台骨架都带这 8 根减震骨头，但只有新纪轻型（v3）真有减震零件绑在上面，另外八台这几根骨头没有蒙皮权重。
	// 按第 0 级细节各段蒙皮用到的骨头（BoneMap）查：两根都没顶点跟着动的那对不加，每帧就不白转（读不到渲染数据时照旧全加）
	TSet<int32> Skinned;
	bool bKnowSkin = false;
	if (const USkeletalMesh* SM = Cast<USkeletalMesh>(Asset))
	{
		if (const FSkeletalMeshRenderData* RD = SM->GetResourceForRendering())
		{
			if (RD->LODRenderData.Num() > 0)
			{
				bKnowSkin = true;
				for (const FSkelMeshRenderSection& Sec : RD->LODRenderData[0].RenderSections)
				{
					for (const FBoneIndexType B : Sec.BoneMap)
					{
						Skinned.Add(B);
					}
				}
			}
		}
	}
	int32 Unskinned = 0;
	for (int32 s = 0; s < 2; ++s)
	{
		for (int32 j = 0; j < 2; ++j)
		{
			FDamperPair P;
			P.A = Ref.FindBoneIndex(*FString::Printf(TEXT("damper_%s_a_%s"), Joint[j], Side[s]));
			P.B = Ref.FindBoneIndex(*FString::Printf(TEXT("damper_%s_b_%s"), Joint[j], Side[s]));
			if (P.A == INDEX_NONE || P.B == INDEX_NONE)
			{
				continue;
			}
			if (bKnowSkin && !Skinned.Contains(P.A) && !Skinned.Contains(P.B))
			{
				++Unskinned;
				continue;
			}
			const FVector AB = RefCS[P.B].GetLocation() - RefCS[P.A].GetLocation();
			P.DirA = RefCS[P.A].GetRotation().UnrotateVector(AB).GetSafeNormal();
			P.DirB = RefCS[P.B].GetRotation().UnrotateVector(-AB).GetSafeNormal();
			P.RestLen = AB.Size();
			// 导出时骨头从销孔指向对方，减震方向正好落在骨头的某根局部轴上：沿这根轴缩放就是沿减震伸缩
			auto AxisOf = [](const FVector& D)
			{
				const FVector A = D.GetAbs();
				const int32 K = A.X >= A.Y && A.X >= A.Z ? 0 : A.Y >= A.Z ? 1 : 2;
				return A[K] > 0.95f ? K : INDEX_NONE;
			};
			P.AxisA = AxisOf(P.DirA);
			P.AxisB = AxisOf(P.DirB);
			Dampers.Add(P);
			UE_LOG(LogTemp, Display, TEXT("[mech] 减震 %s：伸直销距 %.1f，缩放轴 %d / %d"), *Ref.GetBoneName(P.A).ToString(), P.RestLen, P.AxisA, P.AxisB);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[mech] %s 跨关节减震 %d 对（骨头在但没有零件的 %d 对不对准），骨头 %d 根"), *Asset->GetName(), Dampers.Num(), Unskinned,
		Ref.GetNum());
	CacheHandRig(Ref, RefCS);
}

void UMechMeshComponent::CacheHandRig(const FReferenceSkeleton& Ref, const TArray<FTransform>& RefCS)
{
	static const TCHAR* Side[2] = {TEXT("l"), TEXT("r")};
	for (int32 s = 0; s < 2; ++s)
	{
		FHandRig& H = HandRig[s];
		H = FHandRig();
		Solved[s][0].bValid = Solved[s][1].bValid = false;
		bool bAll = Hand[s] != INDEX_NONE;
		for (int32 f = 0; f < 5; ++f)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				H.Bone[f][j] = Ref.FindBoneIndex(*FString::Printf(TEXT("%s_%02d_%s"), FingerName[f], j + 1, Side[s]));
				bAll &= H.Bone[f][j] != INDEX_NONE;
			}
			if (f > 0)
			{
				H.Meta[f] = Ref.FindBoneIndex(*FString::Printf(TEXT("%s_metacarpal_%s"), FingerName[f], Side[s]));
			}
		}
		if (!bAll)
		{
			continue;
		}
		// 手坐标系：按绑定姿势里的骨头位置算（不管骨头轴向）。D 往中指根、N 手心（拇指尖在这一侧）、K 往拇指那侧
		const FTransform& HandCS = RefCS[Hand[s]];
		auto InHand = [&](int32 B) { return RefCS[B].GetRelativeTransform(HandCS); };
		const FVector D = InHand(H.Bone[2][0]).GetLocation().GetSafeNormal();
		FVector N = FVector::CrossProduct(D, InHand(H.Bone[4][0]).GetLocation() - InHand(H.Bone[1][0]).GetLocation()).GetSafeNormal();
		if ((N | InHand(H.Bone[0][2]).GetLocation()) < 0.f)
		{
			N = -N;
		}
		FVector K = FVector::CrossProduct(D, N).GetSafeNormal();
		if ((K | InHand(H.Bone[0][0]).GetLocation()) < 0.f)
		{
			K = -K;
		}
		H.D = D;
		H.N = N;
		H.K = K;
		for (int32 f = 0; f < 5; ++f)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				const int32 B = H.Bone[f][j];
				H.InHand[f][j] = InHand(B);
				H.RefLocal[f][j] = RefCS[B].GetRelativeTransform(RefCS[Parents[B]]);
			}
			if (H.Meta[f] != INDEX_NONE)
			{
				H.MetaLocal[f] = RefCS[H.Meta[f]].GetRelativeTransform(RefCS[Parents[H.Meta[f]]]);
			}
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector P = H.InHand[i + 1][0].GetLocation();
			H.Knuckle[i] = FVector2D(P | K, P | D);
		}
		H.bValid = true;
		UE_LOG(LogTemp, Display, TEXT("[grip] %s 手：指根 (k,d) 食指 (%.1f,%.1f) 中指 (%.1f,%.1f) 无名指 (%.1f,%.1f) 小指 (%.1f,%.1f)，手心 N (%.2f,%.2f,%.2f)"),
			Side[s], H.Knuckle[0].X, H.Knuckle[0].Y, H.Knuckle[1].X, H.Knuckle[1].Y, H.Knuckle[2].X, H.Knuckle[2].Y, H.Knuckle[3].X, H.Knuckle[3].Y, N.X, N.Y, N.Z);
	}
}

void UMechMeshComponent::CanonicalInHand(int32 Side, const FMechGripShape& Shape, FVector& OutCenter, FVector& OutAxis, FVector& OutWrap) const
{
	const FHandRig& H = HandRig[Side];
	OutAxis = (H.K * Shape.HandDir.X + H.N * Shape.HandDir.Y + H.D * Shape.HandDir.Z).GetSafeNormal();
	OutWrap = (H.D - OutAxis * (H.D | OutAxis)).GetSafeNormal();
	// 柄心线沿 k 每厘米 d 变多少（柄斜着横过手心）：每根手指的指根到柄心线至少「半宽 + FingerClear」，取最紧的那根；柄后沿离手腕至少 CavBack
	const float Tilt = FMath::Abs(Shape.HandDir.X) > 0.3f ? Shape.HandDir.Z / Shape.HandDir.X : 0.f;
	float Dc = 1e6f;
	for (int32 i = 0; i < 4; ++i)
	{
		Dc = FMath::Min(Dc, H.Knuckle[i].Y - Shape.HalfD - FingerClear - (H.Knuckle[i].X - CavK) * Tilt);
	}
	Dc = FMath::Max(Dc, CavBack + Shape.HalfD);
	OutCenter = H.K * CavK + H.N * (PalmFront + Shape.HalfN + CavGap) + H.D * Dc;
}

FTransform UMechMeshComponent::GripPropInHand(int32 Side, const FMechGripShape& Shape)
{
	CacheBones();
	if (!HandRig[Side].bValid)
	{
		return FTransform::Identity;
	}
	FVector Cav, Ax, Wr;
	CanonicalInHand(Side, Shape, Cav, Ax, Wr);
	// 道具的（柄轴, 包手方向）转到手坐标里的（柄轴, 往指尖）
	const FQuat R = FRotationMatrix::MakeFromXZ(Ax, Wr).ToQuat() * FRotationMatrix::MakeFromXZ(Shape.Axis, Shape.Wrap).ToQuat().Inverse();
	return FTransform(R.GetNormalized(), Cav - R.RotateVector(Shape.Center));
}

FVector UMechMeshComponent::GripPropOffset(int32 Side, const FMechGripShape& Shape)
{
	const FTransform P = GripPropInHand(Side, Shape);
	return P.GetRotation().UnrotateVector(P.GetLocation());
}

const UMechMeshComponent::FGripSolve& UMechMeshComponent::SolveGrip(int32 Side, const FMechGripShape* Shape)
{
	FGripSolve& Out = Solved[Side][Shape ? 0 : 1];
	const FHandRig& H = HandRig[Side];
	FVector Cav = FVector::ZeroVector, Ax = H.K, Wr = H.D;
	if (Shape)
	{
		CanonicalInHand(Side, *Shape, Cav, Ax, Wr);
	}
	const FTransform Key(FRotationMatrix::MakeFromXZ(Ax, Wr).ToQuat(), Cav);
	const FVector Params = Shape ? FVector(Shape->HalfD + Shape->AlongMin * 100.f, Shape->HalfN + Shape->AlongMax * 100.f, Shape->Corner) : FVector::ZeroVector;
	const uint8 Flags = Shape ? (Shape->bSupport ? 1 : 0) | (Shape->bTrigger ? 2 : 0) : 4;
	if (Out.bValid && Out.KeyFlags == Flags && Out.Key.Equals(Key, 0.01f) && Out.KeyParams.Equals(Params, 0.01f))
	{
		return Out;
	}
	// 10-31 拇指第二版解一次要几毫秒到十几毫秒（五个角一起搜）。九台机体的骨架、手完全一样，按「手坐标里的柄 + 手」全局共用：
	// 同一把武器、同一只手整局只解一次（换姿态、换武器切回来、别的机体拿同一把都直接用），不然每次切姿态每台机体都卡一下
	struct FSharedGrip
	{
		int32 Side;
		uint8 Flags;
		FVector Sig;
		FTransform Key;
		FVector Params;
		FGripSolve Solve;
	};
	static TArray<FSharedGrip> SharedGrips;
	static FCriticalSection SharedLock;
	const FVector RigSig = H.InHand[0][2].GetLocation() + H.InHand[1][0].GetLocation() * 7.f + H.InHand[4][0].GetLocation() * 13.f;
	if (Shape)
	{
		FScopeLock Lock(&SharedLock);
		for (const FSharedGrip& E : SharedGrips)
		{
			if (E.Side == Side && E.Flags == Flags && E.Sig.Equals(RigSig, 0.01f) && E.Key.Equals(Key, 0.01f) && E.Params.Equals(Params, 0.01f))
			{
				Out = E.Solve;
				Out.SolveMs = 0.f;
				return Out;
			}
		}
	}
	const double SolveStart = FPlatformTime::Seconds();
	auto Share = [&]()
	{
		Out.SolveMs = static_cast<float>((FPlatformTime::Seconds() - SolveStart) * 1000.0);
		FScopeLock Lock(&SharedLock);
		if (SharedGrips.Num() >= 256)
		{
			SharedGrips.Reset();
		}
		SharedGrips.Add({Side, Flags, RigSig, Key, Params, Out});
	};
	Out.bValid = true;
	Out.Key = Key;
	Out.KeyParams = Params;
	Out.KeyFlags = Flags;

	FHandleSDF Hd;
	if (Shape)
	{
		Hd.bValid = true;
		Hd.C = Cav;
		Hd.A = Ax;
		Hd.U = Wr;
		Hd.V = FVector::CrossProduct(Ax, Wr).GetSafeNormal();
		Hd.HU = Shape->HalfD;
		Hd.HV = Shape->HalfN;
		Hd.RC = FMath::Min3(Shape->Corner, Shape->HalfD, Shape->HalfN);
		Hd.A0 = Shape->AlongMin;
		Hd.A1 = Shape->AlongMax;
	}
	// 手掌（第二、三节和拇指别弯进手掌里）：手坐标里的盒子 k -5~3.6、n -1.75~1.35、d 0.7~9.5
	auto Palm = [&H](const FVector& P)
	{
		const float Dx = FMath::Abs((P | H.K) + 0.7f) - 4.3f;
		const float Dy = FMath::Abs((P | H.N) + 0.2f) - 1.55f;
		const float Dz = FMath::Abs((P | H.D) - 5.1f) - 4.4f;
		return FMath::Sqrt(FMath::Square(FMath::Max(Dx, 0.f)) + FMath::Square(FMath::Max(Dy, 0.f)) + FMath::Square(FMath::Max(Dz, 0.f))) +
			FMath::Min(FMath::Max3(Dx, Dy, Dz), 0.f);
	};
	// 线段 A→B 上几个点离柄（和手掌）最近多少，减去半厚：负 = 穿进去了
	auto Clear = [&](const FVector& A, const FVector& B, float Half, bool bPalm)
	{
		float M = 1e6f;
		for (const float T : ContactT)
		{
			const FVector P = FMath::Lerp(A, B, T);
			float V = Hd.Eval(P);
			if (bPalm)
			{
				V = FMath::Min(V, Palm(P));
			}
			M = FMath::Min(M, V - Half);
		}
		return M;
	};
	// 解完以后各节在手坐标里的变换 → 相对父骨头的局部旋转
	auto Finish = [&](int32 f, const FAffine (&Dm)[3])
	{
		FTransform New[3];
		for (int32 j = 0; j < 3; ++j)
		{
			const FTransform& R = H.InHand[f][j];
			New[j] = FTransform(Dm[j].R * R.GetRotation(), Dm[j].Apply(R.GetLocation()));
		}
		const FTransform ParentInHand = f > 0 && H.Meta[f] != INDEX_NONE ? H.MetaLocal[f] : FTransform::Identity;
		Out.Local[f][0] = New[0].GetRelativeTransform(ParentInHand).GetRotation();
		Out.Local[f][1] = New[1].GetRelativeTransform(New[0]).GetRotation();
		Out.Local[f][2] = New[2].GetRelativeTransform(New[1]).GetRotation();
	};

	// 解好的食指、中指、无名指各节（手坐标里的线段 + 半厚）：拇指第二版把它们当障碍（拇指压在四指外面、不穿进去）
	struct FFingerCapsule
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		float Half = 0.f;
	};
	FFingerCapsule FingerCaps[9];

	// ---- 四指：每节绕自己的弯曲轴（指向 × 手心）往手心弯，弯到碰上柄（握拳时碰上手掌）为止
	for (int32 f = 1; f < 5; ++f)
	{
		FVector P[3], Ends[3], Axes[3];
		for (int32 j = 0; j < 3; ++j)
		{
			P[j] = H.InHand[f][j].GetLocation();
		}
		Ends[0] = P[1];
		Ends[1] = P[2];
		Ends[2] = P[2] + (P[2] - P[1]).GetSafeNormal() * TipLen[f];
		for (int32 j = 0; j < 3; ++j)
		{
			Axes[j] = FVector::CrossProduct((Ends[j] - P[j]).GetSafeNormal(), H.N).GetSafeNormal();
		}
		FAffine Dm[3];
		for (int32 j = 0; j < 3; ++j)
		{
			const FVector Pivot = Dm[j].Apply(P[j]);
			const FVector Axis = Dm[j].R.RotateVector(Axes[j]).GetSafeNormal();
			const FVector End = Dm[j].Apply(Ends[j]);
			float Deg;
			if (Shape && Shape->bTrigger && f == 1)
			{
				Deg = TriggerAngle[j];
			}
			else
			{
				auto Hit = [&](float A) { return Clear(Pivot, RotAbout(Pivot, Axis, A).Apply(End), HalfT[f][j], j > 0) < 0.f; };
				Deg = ContactAngle(Hit, FingerLimit[j][0], FingerLimit[j][1], 4.f, 4);
			}
			Out.Angle[f][j] = Deg;
			const FAffine Rm = RotAbout(Pivot, Axis, Deg);
			for (int32 k = j; k < 3; ++k)
			{
				Dm[k] = Compose(Rm, Dm[k]);
			}
		}
		if (f <= 3)
		{
			for (int32 j = 0; j < 3; ++j)
			{
				FingerCaps[(f - 1) * 3 + j] = {Dm[j].Apply(P[j]), Dm[j].Apply(Ends[j]), HalfT[f][j]};
			}
		}
		Finish(f, Dm);
	}

	// ---- 拇指：握拳 / 没有柄时照绑定姿势；有柄时第一节绕柄轴转 α（包过去）、绕 D 转 β（对掌）网格搜，二、三节绕柄轴弯到碰上；
	// 目标是拇指尖落在柄的另一侧（刀 / 握把：压在四指外面；托护木：沿护木靠手腕那侧）
	Out.ThumbMiss = 0.f;
	if (!Shape)
	{
		for (int32 j = 0; j < 3; ++j)
		{
			Out.Local[0][j] = H.RefLocal[0][j].GetRotation();
			Out.Angle[0][j] = 0.f;
		}
		return Out;
	}
	FVector P[3], Ends[3];
	for (int32 j = 0; j < 3; ++j)
	{
		P[j] = H.InHand[0][j].GetLocation();
	}
	Ends[0] = P[1];
	Ends[1] = P[2];
	Ends[2] = P[2] + (P[2] - P[1]).GetSafeNormal() * TipLen[0];
	const FVector Closest = Hd.C + Hd.A * ((P[0] - Hd.C) | Hd.A);
	const FVector At = Hd.A * ((FVector::CrossProduct(Hd.A, Ends[0] - P[0]) | (Closest - P[0])) > 0.f ? 1.f : -1.f);
	const float Tk = FMath::Abs(Hd.A | H.K) > 0.3f ? (1.f - (Hd.C | H.K)) / (Hd.A | H.K) : 0.f;
	const FVector OnAxis = Hd.C + Hd.A * Tk;
	const FVector Target = Shape->bSupport ? OnAxis - (H.D - Hd.A * (H.D | Hd.A)).GetSafeNormal() * (Hd.HU + 1.2f)
		: OnAxis + (H.N - Hd.A * (H.N | Hd.A)).GetSafeNormal() * (Hd.HV + 2.6f);
	auto ThumbMisses = [&](const FAffine (&Dm)[3])
	{
		const FVector Tip = Dm[2].Apply(Ends[2]);
		Out.ThumbTipMiss = static_cast<float>(FVector::Dist(Tip, Target));
		Out.ThumbPadMiss = static_cast<float>(FVector::Dist(FMath::Lerp(Dm[2].Apply(P[2]), Tip, 0.6f), Target));
	};
	Out.ThumbBeta = Out.ThumbTwist = 0.f;

	// ---- 10-31 拇指第二版（用户：粗柄——步枪 9 厘米深的握把、霰弹枪握颈、大剑 r2.6、钝器——拇指够不到另一侧，只能顺着柄贴着；
	// 「真实的手握粗柄时，拇指是压在食指和中指上面绕过来的」）。第一版（下面，托护木和 -MechGripV1 还用）二、三节绕柄轴「弯到碰上就停」：
	// 粗柄上第二节一碰柄就停住，拇指只能顺着柄躺着，拇指尖离目标 6–10 厘米。第二版：
	//   ① 二、三节绕拇指自己的弯曲轴（绑定姿势里三节所在平面的法线，跟着第一节一起转）屈，像真拇指那样往里勾，不再绕柄轴侧着弯；
	//   ② 第一节除了绕柄轴转 α（包过去）、绕 D 转 β（对掌），再加绕自己的扭转（±60 度内），让拇指的弯曲轴能转到和柄轴差不多；
	//   ③ 五个角一起搜，不再「碰上就停」：粗搜（扭 30、α / β 20、二三节 15 度）留最好的三组，再在各自附近按 5 度细搜；
	//   ④ 已经解好的食指、中指、无名指三节也算障碍：拇指可以压在它们外面，不能穿进去；
	//   ⑤ 打分 = 拇指肚（第三节 60% 处）离目标 + 30 × 穿进柄 / 手掌 / 四指的深度 + 0.04 × 反关节的度数 + 0.6 × 第二、三节中点悬空的距离（要贴着柄或四指）。
	// 目标点不变（柄另一侧、压在四指外面）。原型和渲染对比在 tools/grip_lab（grip_core.py 的 solve_thumb，和这里一一对应）
	if (!Shape->bSupport && !GripV1())
	{
		auto Obstacle = [&](const FVector& Pt, bool bPalm)
		{
			float V = Hd.Eval(Pt);
			for (const FFingerCapsule& Cap : FingerCaps)
			{
				V = FMath::Min(V, PointSegDist(Pt, Cap.A, Cap.B) - Cap.Half);
			}
			return bPalm ? FMath::Min(V, Palm(Pt)) : V;
		};
		auto ClearV2 = [&](const FVector& A, const FVector& B, float Half, bool bPalm)
		{
			float M = 1e6f;
			for (const float T : ContactT)
			{
				M = FMath::Min(M, Obstacle(FMath::Lerp(A, B, T), bPalm) - Half);
			}
			return M;
		};
		auto Hang = [&](const FVector& A, const FVector& B, float Half) { return FMath::Max(0.f, Obstacle(FMath::Lerp(A, B, 0.5f), false) - Half); };
		const FVector FlexAxis = FVector::CrossProduct(P[1] - P[0], P[2] - P[1]).GetSafeNormal();
		const FVector MetaAxis = (P[1] - P[0]).GetSafeNormal();
		auto Joint0 = [&](int32 Tw, int32 Al, int32 Be, FAffine& M0)
		{
			M0 = Compose(RotAbout(P[0], At, Al), Compose(RotAbout(P[0], H.D, Be), RotAbout(P[0], MetaAxis, Tw)));
			return FMath::Min(0.f, ClearV2(P[0], M0.Apply(Ends[0]), HalfT[0][0], false));
		};
		auto JointN = [&](int32 j, const FAffine& Prev, int32 Deg, FAffine& Next)
		{
			const FVector Pv = Prev.Apply(P[j]);
			Next = Compose(RotAbout(Pv, Prev.R.RotateVector(FlexAxis), Deg), Prev);
			return FMath::Min(0.f, ClearV2(Pv, Next.Apply(Ends[j]), HalfT[0][j], true));
		};
		auto Hyper = [](int32 Deg) { return 0.04f * FMath::Max(0, -Deg); };
		auto Score = [&](const FAffine& D1, const FAffine& D2, float Pen, int32 A2, int32 A3)
		{
			const FVector Pad = FMath::Lerp(D2.Apply(P[2]), D2.Apply(Ends[2]), 0.6f);
			const float Hanging = Hang(D1.Apply(P[1]), D1.Apply(P[2]), HalfT[0][1]) + Hang(D2.Apply(P[2]), D2.Apply(Ends[2]), HalfT[0][2]);
			return static_cast<float>(FVector::Dist(Pad, Target)) - 30.f * Pen + Hyper(A2) + Hyper(A3) + 0.6f * Hanging;
		};
		struct FThumbCand
		{
			float S = 1e9f;
			int32 Tw = 0, Al = 0, Be = 0, A2 = 0, A3 = 0;
		};
		FThumbCand Top[3];
		auto Keep = [&Top](const FThumbCand& C)
		{
			if (C.S >= Top[2].S)
			{
				return;
			}
			int32 i = 2;
			while (i > 0 && Top[i - 1].S > C.S)
			{
				Top[i] = Top[i - 1];
				--i;
			}
			Top[i] = C;
		};
		const int32 Lo2 = static_cast<int32>(ThumbLimit[1][0]), Hi2 = static_cast<int32>(ThumbLimit[1][1]);
		const int32 Lo3 = static_cast<int32>(ThumbLimit[2][0]), Hi3 = static_cast<int32>(ThumbLimit[2][1]);
		// 粗搜（下界剪枝：打分 ≥ 30 × 穿深 + 反关节，已经不比第三名好的整枝跳过）
		for (int32 Tw = -60; Tw <= 60; Tw += 30)
		{
			for (int32 Al = -90; Al <= 90; Al += 20)
			{
				for (int32 Be = -70; Be <= 70; Be += 20)
				{
					FAffine M0;
					const float Pen0 = Joint0(Tw, Al, Be, M0);
					if (-30.f * Pen0 >= Top[2].S)
					{
						continue;
					}
					for (int32 A2 = Lo2; A2 <= Hi2; A2 += 15)
					{
						FAffine D1;
						const float Pen1 = FMath::Min(Pen0, JointN(1, M0, A2, D1));
						if (-30.f * Pen1 + Hyper(A2) >= Top[2].S)
						{
							continue;
						}
						for (int32 A3 = Lo3; A3 <= Hi3; A3 += 15)
						{
							FAffine D2;
							const float Pen2 = FMath::Min(Pen1, JointN(2, D1, A3, D2));
							Keep({Score(D1, D2, Pen2, A2, A3), Tw, Al, Be, A2, A3});
						}
					}
				}
			}
		}
		// 细搜：前三组附近按 5 度
		float BestS = 1e9f;
		FAffine BestM[3];
		FThumbCand BestC;
		for (const FThumbCand& C : Top)
		{
			if (C.S >= 1e8f)
			{
				continue;
			}
			for (int32 Al = FMath::Max(-90, C.Al - 10); Al <= FMath::Min(90, C.Al + 10); Al += 5)
			{
				for (int32 Be = FMath::Max(-70, C.Be - 10); Be <= FMath::Min(70, C.Be + 10); Be += 5)
				{
					FAffine M0;
					const float Pen0 = Joint0(C.Tw, Al, Be, M0);
					if (-30.f * Pen0 >= BestS)
					{
						continue;
					}
					for (int32 A2 = FMath::Max(Lo2, C.A2 - 10); A2 <= FMath::Min(Hi2, C.A2 + 10); A2 += 5)
					{
						FAffine D1;
						const float Pen1 = FMath::Min(Pen0, JointN(1, M0, A2, D1));
						if (-30.f * Pen1 + Hyper(A2) >= BestS)
						{
							continue;
						}
						for (int32 A3 = FMath::Max(Lo3, C.A3 - 10); A3 <= FMath::Min(Hi3, C.A3 + 10); A3 += 5)
						{
							FAffine D2;
							const float Pen2 = FMath::Min(Pen1, JointN(2, D1, A3, D2));
							const float S = Score(D1, D2, Pen2, A2, A3);
							if (S < BestS)
							{
								BestS = S;
								BestM[0] = M0;
								BestM[1] = D1;
								BestM[2] = D2;
								BestC = {S, C.Tw, Al, Be, A2, A3};
							}
						}
					}
				}
			}
		}
		Out.Angle[0][0] = static_cast<float>(BestC.Al);
		Out.Angle[0][1] = static_cast<float>(BestC.A2);
		Out.Angle[0][2] = static_cast<float>(BestC.A3);
		Out.ThumbBeta = static_cast<float>(BestC.Be);
		Out.ThumbTwist = static_cast<float>(BestC.Tw);
		Out.ThumbMiss = BestS;
		ThumbMisses(BestM);
		Finish(0, BestM);
		Share();
		return Out;
	}
	float Best = 1e9f;
	FAffine BestD[3];
	float BestAng[3] = {0.f, 0.f, 0.f};
	for (int32 Al = -90; Al <= 90; Al += 10)
	{
		for (int32 Be = -70; Be <= 70; Be += 10)
		{
			const FAffine M0 = Compose(RotAbout(P[0], At, Al), RotAbout(P[0], H.D, Be));
			FAffine Dm[3] = {M0, M0, M0};
			float Pen = FMath::Min(0.f, Clear(P[0], M0.Apply(Ends[0]), HalfT[0][0], false));
			float Ang[3] = {float(Al), 0.f, 0.f};
			for (int32 j = 1; j < 3; ++j)
			{
				const FVector Pivot = Dm[j].Apply(P[j]);
				const FVector End = Dm[j].Apply(Ends[j]);
				auto Hit = [&](float A) { return Clear(Pivot, RotAbout(Pivot, At, A).Apply(End), HalfT[0][j], true) < 0.f; };
				Ang[j] = ContactAngle(Hit, ThumbLimit[j][0], ThumbLimit[j][1], 5.f, 0);
				const FAffine Rm = RotAbout(Pivot, At, Ang[j]);
				for (int32 k = j; k < 3; ++k)
				{
					Dm[k] = Compose(Rm, Dm[k]);
				}
				Pen = FMath::Min(Pen, Clear(Pivot, Dm[j].Apply(Ends[j]), HalfT[0][j], false));
			}
			const float Score = FVector::Dist(Dm[2].Apply(Ends[2]), Target) - 30.f * Pen;
			if (Score < Best)
			{
				Best = Score;
				for (int32 j = 0; j < 3; ++j)
				{
					BestD[j] = Dm[j];
					BestAng[j] = Ang[j];
				}
				BestAng[1] = Ang[1];
			}
		}
	}
	for (int32 j = 0; j < 3; ++j)
	{
		Out.Angle[0][j] = BestAng[j];
	}
	Out.ThumbMiss = Best;
	ThumbMisses(BestD);
	Finish(0, BestD);
	Share();
	return Out;
}

void UMechMeshComponent::ApplyFingerPose(TArray<FTransform>& CS, int32 Side, const FGripSolve& S, float Weight, bool bThumb) const
{
	const FHandRig& H = HandRig[Side];
	const float W = FMath::Clamp(Weight, 0.f, 1.f);
	if (!H.bValid || W <= 0.001f || Hand[Side] == INDEX_NONE)
	{
		return;
	}
	// 骨头按父在前：掌骨 → 三节。局部平移照动作（骨架一样，长度不变），局部旋转往解出来的姿势混。
	// 10-31 修「手部重复」（用户：「所有的机器人模型好像都有手部模型重复的问题」）：子骨头的局部变换要按父骨头**改之前**的
	// 组件空间变换算。以前拿这一轮已经转过的父骨头算（CS[B].GetRelativeTransform(CS[P])），算出来的局部平移正好抵消父骨头的转动，
	// 子骨头留在动作里原来的绝对位置、只换了朝向——第一节弯过去了，第二、三节还浮在原处（每节零件只挂一根骨头），
	// 竖大拇指 / 握拳时拳头前面浮着一排指节，握刀时拇指后两节和第一节脱开，看着像手边上又多了一套手指。九台是同一只手，所以全都有。
	// 模型本身没有重复的零件（九台 FBX 和 UE 资源手上的零件逐骨头数过，见 README「握持」）。-MechFingerPoseOld 用回旧算法（拍改前对比用）
	static const bool bOldPose = FParse::Param(FCommandLine::Get(), TEXT("MechFingerPoseOld"));
	auto Set = [&CS](int32 B, int32 P, const FTransform& OldParent, const FQuat& Want, float A)
	{
		const FTransform Old = CS[B];
		const FTransform Cur = Old.GetRelativeTransform(bOldPose ? CS[P] : OldParent);
		CS[B] = FTransform(FQuat::Slerp(Cur.GetRotation(), Want, A).GetNormalized(), Cur.GetLocation(), Cur.GetScale3D()) * CS[P];
		return Old;   // 这根骨头改之前的变换：给下一节当「改之前的父骨头」
	};
	for (int32 f = bThumb ? 0 : 1; f < 5; ++f)
	{
		int32 Parent = Hand[Side];
		FTransform OldParent = CS[Parent];   // 手骨这里不动
		if (f > 0 && H.Meta[f] != INDEX_NONE)
		{
			OldParent = Set(H.Meta[f], Hand[Side], OldParent, H.MetaLocal[f].GetRotation(), W);   // 掌骨回到绑定姿势：机体手掌是一整块
			Parent = H.Meta[f];
		}
		for (int32 j = 0; j < 3; ++j)
		{
			OldParent = Set(H.Bone[f][j], Parent, OldParent, S.Local[f][j], W);
			Parent = H.Bone[f][j];
		}
	}
}

FQuat UMechMeshComponent::ForearmPropRotCS(const TArray<FTransform>& CS, int32 Prop) const
{
	// 和 PlaceProps 里端在前臂上的规则一样，只是在组件空间里算
	const int32 s = PropSide[Prop];
	const FTransform& ToWorld = GetComponentTransform();
	const FVector Up = ToWorld.InverseTransformVectorNoScale(FVector::UpVector);
	const FVector Fwd = ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetForwardVector());
	const FVector Fore = (CS[Hand[s]].GetLocation() - CS[LowerArm[s]].GetLocation()).GetSafeNormal();
	const float ForeUp = Fore | Up;
	const FVector Hint = FMath::Abs(ForeUp) > 0.92f ? Fwd * (ForeUp < 0.f ? 1.f : -1.f) : Up;
	return FRotationMatrix::MakeFromXZ(Fore, Hint).ToQuat() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(PropRoll[Prop])) *
		FQuat(FVector::RightVector, FMath::DegreesToRadians(-PropPitch[Prop]));
}

void UMechMeshComponent::ApplyHandGrips(TArray<FTransform>& CS)
{
	for (int32 s = 0; s < 2; ++s)
	{
		DebugWrist[s] = FVector::ZeroVector;
		if (!HandRig[s].bValid || Hand[s] == INDEX_NONE)
		{
			continue;
		}
		// 左手去抓浮空无人机（钩爪自拉）：半握拳
		if (s == 0 && LeftReachWeight > 0.001f)
		{
			ApplyFingerPose(CS, 0, SolveGrip(0, nullptr), LeftReachWeight * 0.8f, false);
			continue;
		}
		const FMechHandGrip& G = Grip[s];
		float W = FMath::Clamp(G.Weight, 0.f, 1.f);
		if (s == 1)
		{
			W *= 1.f - FMath::Clamp(ThumbsUp, 0.f, 1.f);   // 竖大拇指自己管右手
		}
		if (G.Mode == EMechGripMode::Support)
		{
			W = FMath::Min(W, LeftSupportWeight);
		}
		if (W <= 0.001f || G.Mode == EMechGripMode::None)
		{
			continue;
		}
		if (G.Mode == EMechGripMode::Fist)
		{
			ApplyFingerPose(CS, s, SolveGrip(s, nullptr), W, false);
			continue;
		}
		// 手转到标准握法握住枪（枪按两只手 / 前臂定朝向，手跟着枪）；换弹冻结时枪跟着手，不转
		if (G.Mode == EMechGripMode::HandToProp && G.Prop >= 0 && G.Prop < NumProps && Props[G.Prop])
		{
			bool bHave = false;
			FQuat PropRot = FQuat::Identity;
			if (PropMount[G.Prop] == EMount::TwoHand && G.Prop == 1 && bTwoHandGunCS && !bTwoHandFreeze[1])
			{
				PropRot = TwoHandGunCS.GetRotation();
				bHave = true;
			}
			else if (PropMount[G.Prop] == EMount::Forearm && PropSide[G.Prop] == s && LowerArm[s] != INDEX_NONE)
			{
				PropRot = ForearmPropRotCS(CS, G.Prop);
				bHave = true;
			}
			if (bHave)
			{
				const FQuat Want = PropRot * GripPropInHand(s, G.Shape).GetRotation().Inverse();
				// 10-31 握持第二版：手转到握把上，手腕比动作里多折了多少（握把的角度和动作设想的不一样就会逼着手腕折，充能步枪的斜杆最明显）。
				// 「顺手」的前臂方向 = 动作里前臂相对手的方向，换到握好以后的手上；长枪（双手端、枪按两只手的位置摆）的右手再把整条手臂
				// 绕「肩—腕」连线转一点（手肘往下 / 往外摆，手腕位置不动、枪不动），让前臂尽量顺着手，手腕少折（最多 MaxWristSwivel 度；默认关，见那里）
				if (LowerArm[s] != INDEX_NONE && UpperArm[s] != INDEX_NONE)
				{
					const FVector Wrist = CS[Hand[s]].GetLocation();
					const FVector ForeNow = (Wrist - CS[LowerArm[s]].GetLocation()).GetSafeNormal();
					const FVector ForeEasy = Want.RotateVector(CS[Hand[s]].GetRotation().UnrotateVector(ForeNow));
					float Swivel = 0.f;
					const float MaxSwivel = MaxWristSwivel();
					if (s == 1 && G.Prop == 1 && PropMount[1] == EMount::TwoHand && MaxSwivel > 0.f)
					{
						const FVector Sh = CS[UpperArm[s]].GetLocation();
						const FVector U = (Wrist - Sh).GetSafeNormal();
						const FVector A0 = ForeNow - U * (ForeNow | U);
						const FVector B0 = ForeEasy - U * (ForeEasy | U);
						if (A0.Size() > 0.2f && B0.Size() > 0.2f)
						{
							Swivel = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(FVector::CrossProduct(A0, B0) | U), static_cast<float>(A0 | B0)));
							Swivel = FMath::Clamp(Swivel, -MaxSwivel, MaxSwivel) * W;
							RotateChain(CS, UpperArm[s], FQuat(U, FMath::DegreesToRadians(Swivel)), Sh);
						}
					}
					const FVector ForeAfter = (CS[Hand[s]].GetLocation() - CS[LowerArm[s]].GetLocation()).GetSafeNormal();
					DebugWrist[s] = FVector(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(ForeNow | ForeEasy), -1.f, 1.f))),
						FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<float>(ForeAfter | ForeEasy), -1.f, 1.f))), Swivel);
				}
				const FQuat Cur = CS[Hand[s]].GetRotation();
				RotateChain(CS, Hand[s], FQuat::Slerp(FQuat::Identity, (Want * Cur.Inverse()).GetNormalized(), W), CS[Hand[s]].GetLocation());
			}
		}
		ApplyFingerPose(CS, s, SolveGrip(s, &G.Shape), W);
	}
}

bool UMechMeshComponent::GetHandAxesWorld(int32 Side, FVector& OutK, FVector& OutN, FVector& OutD) const
{
	if (Side < 0 || Side > 1 || !HandRig[Side].bValid || Hand[Side] == INDEX_NONE || Hand[Side] >= GetNumBones())
	{
		return false;
	}
	const FTransform Bone = GetBoneTransform(Hand[Side]);
	OutK = Bone.TransformVectorNoScale(HandRig[Side].K).GetSafeNormal();
	OutN = Bone.TransformVectorNoScale(HandRig[Side].N).GetSafeNormal();
	OutD = Bone.TransformVectorNoScale(HandRig[Side].D).GetSafeNormal();
	return true;
}

FString UMechMeshComponent::DebugGripText(int32 Side) const
{
	const FGripSolve& S = Solved[Side][0];
	if (!S.bValid)
	{
		return TEXT("（没解过）");
	}
	FString Out;
	static const TCHAR* Cn[5] = {TEXT("拇"), TEXT("食"), TEXT("中"), TEXT("无"), TEXT("小")};
	for (int32 f = 0; f < 5; ++f)
	{
		Out += FString::Printf(TEXT("%s %.0f/%.0f/%.0f "), Cn[f], S.Angle[f][0], S.Angle[f][1], S.Angle[f][2]);
	}
	// 10-31 拇指第二版：分数（第一版是拇指尖到目标 + 穿模罚分）、拇指肚 / 拇指尖各自离目标多远、第一节 β 和扭转、这次解花了几毫秒；握枪的手腕多折了几度
	return Out + FString::Printf(TEXT("拇指离目标 %.1f（拇指肚 %.1f / 尖 %.1f，β %.0f 扭 %.0f，解 %.1f 毫秒）手腕多折 %.0f→%.0f 度（手肘转 %.0f）"), S.ThumbMiss,
		S.ThumbPadMiss, S.ThumbTipMiss, S.ThumbBeta, S.ThumbTwist, S.SolveMs, DebugWrist[Side].X, DebugWrist[Side].Y, DebugWrist[Side].Z);
}

void UMechMeshComponent::OnFrameMeshChanged()
{
	CachedFor = nullptr;  // 下一次骨骼定稿时 CacheBones 按新网格重取（手指、减震也在里面）
	const USkinnedAsset* Asset = GetSkinnedAsset();
	for (int32 p = 0; p < NumProps; ++p)
	{
		bTwoHandValid[p] = false;
		if (PropMount[p] != EMount::Bone)
		{
			continue;
		}
		if (FMath::IsNaN(PropBoneOffset[p].X))
		{
			// SetPropOnBoneLocal 挂的（直接给骨头空间变换）：只重取骨头索引
			PropBone[p] = Asset ? Asset->GetRefSkeleton().FindBoneIndex(PropBoneName[p]) : INDEX_NONE;
		}
		else
		{
			bPropBoneDirty[p] = true;  // SetPropOnBone 挂的：按新网格的绑定姿势重算骨头空间变换
		}
	}
	for (FGripSolve(&HandSolves)[2] : Solved)
	{
		for (FGripSolve& S : HandSolves)
		{
			S.bValid = false;
		}
	}
}

void UMechMeshComponent::AimDampers(TArray<FTransform>& CS) const
{
	if (Dampers.Num() == 0)
	{
		return;  // 没有减震零件的机体（10-31 阵营机体里除新纪轻型以外的八台）
	}
	// 减震的长度比例（以腿伸直时的销距为 1，和 lookdev/robot_lookdev.py 里 COIL_* 对应）：缸筒 0.81/1.43，活塞杆 0.72/1.43。
	// 销距比缸筒还短时缸筒会从活塞杆那头戳出去 → 两段沿轴一起压扁（弹簧看着就是被压缩）；比装好的最长还长时活塞杆拉长，不露缝
	constexpr float BarrelFrac = 0.81f / 1.43f;
	constexpr float RodFrac = 0.72f / 1.43f;
	constexpr float MaxFrac = (0.81f + 0.72f - 0.03f) / 1.43f;
	auto ScaleAxis = [&CS](int32 Bone, int32 Axis, float S)
	{
		if (Axis != INDEX_NONE && !FMath::IsNearlyEqual(S, 1.f))
		{
			FVector Scale = CS[Bone].GetScale3D();
			Scale[Axis] *= S;
			CS[Bone].SetScale3D(Scale);
		}
	};
	// 两根减震骨头都是叶子骨头，直接改各自的组件空间旋转：缸筒指向活塞杆的销孔，活塞杆反过来指向缸筒的销孔
	for (const FDamperPair& P : Dampers)
	{
		if (!CS.IsValidIndex(P.A) || !CS.IsValidIndex(P.B))
		{
			continue;
		}
		const FVector AB = CS[P.B].GetLocation() - CS[P.A].GetLocation();
		if (AB.IsNearlyZero())
		{
			continue;
		}
		const FVector Want = AB.GetSafeNormal();
		const FQuat RotA = CS[P.A].GetRotation();
		CS[P.A].SetRotation((FQuat::FindBetweenNormals(RotA.RotateVector(P.DirA), Want) * RotA).GetNormalized());
		const FQuat RotB = CS[P.B].GetRotation();
		CS[P.B].SetRotation((FQuat::FindBetweenNormals(RotB.RotateVector(P.DirB), -Want) * RotB).GetNormalized());
		if (P.RestLen > KINDA_SMALL_NUMBER)
		{
			const float Len = AB.Size() / P.RestLen;
			if (Len < BarrelFrac)
			{
				ScaleAxis(P.A, P.AxisA, Len / BarrelFrac);
				ScaleAxis(P.B, P.AxisB, Len / BarrelFrac);
			}
			else if (Len > MaxFrac)
			{
				ScaleAxis(P.B, P.AxisB, (Len - BarrelFrac + 0.03f / 1.43f) / RodFrac);
			}
		}
	}
}

void UMechMeshComponent::RotateChain(TArray<FTransform>& CS, int32 Root, const FQuat& Rot, const FVector& Pivot) const
{
	// 骨骼按父在前、子在后排列：从 Root 往后扫，父亲在链上的就也在链上
	TBitArray<> InChain(false, CS.Num());
	InChain[Root] = true;
	for (int32 i = Root; i < CS.Num(); ++i)
	{
		if (i != Root && (Parents[i] == INDEX_NONE || !InChain[Parents[i]]))
		{
			continue;
		}
		InChain[i] = true;
		CS[i].SetLocation(Rot.RotateVector(CS[i].GetLocation() - Pivot) + Pivot);
		CS[i].SetRotation(Rot * CS[i].GetRotation());
	}
}

void UMechMeshComponent::FinalizeBoneTransform()
{
	TArray<FTransform>& CS = GetEditableComponentSpaceTransforms();
	if (bProcedural && CS.Num() > 0)
	{
		CacheBones();
		const FTransform& ToWorld = GetComponentTransform();
		const FVector AimPivot = VisualPivot();
		const FVector AimPoint = AimOffset.IsNearlyZero() ? FVector::ZeroVector : AimPivot + AimOffset;
		if (Spine1 != INDEX_NONE && Parents.Num() == CS.Num())
		{
			// 10-31 展示姿势（结算场景 / 主菜单）：先把上身转正，再加前倾 / 挺胸（TorsoTwist）、手臂
			if (ShowSquare > 0.001f)
			{
				ApplyShowSquare(CS);
			}
			const float Deg = TorsoTwist.Size();
			if (Deg > 0.05f)
			{
				const FVector Axis = ToWorld.InverseTransformVectorNoScale(TorsoTwist / Deg).GetSafeNormal();
				RotateChain(CS, Spine1, FQuat(Axis, FMath::DegreesToRadians(Deg)), CS[Spine1].GetLocation());
			}
			for (int32 s = 0; s < 2; ++s)
			{
				if (ArmWeight[s] <= 0.001f || UpperArm[s] == INDEX_NONE || LowerArm[s] == INDEX_NONE || Hand[s] == INDEX_NONE)
				{
					continue;
				}
				// 以肩为轴整条手臂转过去，让前臂（也就是武器）对准目标方向
				const FVector Fore = (CS[Hand[s]].GetLocation() - CS[LowerArm[s]].GetLocation()).GetSafeNormal();
				FVector Want = ToWorld.InverseTransformVectorNoScale(ArmDir[s]).GetSafeNormal();
				if (Fore.IsNearlyZero() || Want.IsNearlyZero())
				{
					continue;
				}
				if (ArmPointWeight[s] > 0.001f && !AimPoint.IsZero())
				{
					// 视差：从胸口看瞄准点 → 从肩看（手臂伸直时枪管这条线正好过肩）
					const FVector PointCS = ToWorld.InverseTransformPosition(AimPoint);
					const FVector FromPivot = (PointCS - ToWorld.InverseTransformPosition(AimPivot)).GetSafeNormal();
					const FVector FromShoulder = (PointCS - CS[UpperArm[s]].GetLocation()).GetSafeNormal();
					if (!FromPivot.IsNearlyZero() && !FromShoulder.IsNearlyZero())
					{
						Want = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(FromPivot, FromShoulder), FMath::Min(ArmPointWeight[s], 1.f)).RotateVector(Want);
					}
				}
				const FQuat Full = FQuat::FindBetweenNormals(Fore, Want);
				const FQuat Rot = FQuat::Slerp(FQuat::Identity, Full, FMath::Clamp(ArmWeight[s], 0.f, 1.f));
				RotateChain(CS, UpperArm[s], Rot, CS[UpperArm[s]].GetLocation());
			}
			// 长枪：动作只给了大致的瞄准姿势（瞄上 / 瞄下混合），这里把上身转到枪管（从握把右手看出去）正对瞄准点，总共最多转 70 度免得腰拧折。
			// 转完握把位置会跟着变，全对准时按新位置再补一次（第二次只差零点几度）；每帧都从这一帧的动作重新算，不会和上一帧互相追着抖
			if (GunAimWeight > 0.001f && !AimPoint.IsZero() && Spine3 != INDEX_NONE && Hand[0] != INDEX_NONE && Hand[1] != INDEX_NONE)
			{
				const FVector PointCS = ToWorld.InverseTransformPosition(AimPoint);
				const float W = FMath::Clamp(GunAimWeight, 0.f, 1.f);
				float Budget = FMath::DegreesToRadians(70.f);
				DebugGunTurn = 0.f;
				for (int32 Pass = 0; Pass < 2 && Budget > 0.f; ++Pass)
				{
					const FTransform Gun = TwoHandGunFrame(CS);
					const FVector Barrel = Gun.GetUnitAxis(EAxis::X);
					const FVector Want = (PointCS - Gun.TransformPosition(GunMuzzleLocal)).GetSafeNormal();
					if (Barrel.IsNearlyZero() || Want.IsNearlyZero())
					{
						break;
					}
					FVector Axis;
					float Angle;
					FQuat::FindBetweenNormals(Barrel, Want).ToAxisAndAngle(Axis, Angle);
					Angle = FMath::Min(Angle, Budget);
					Budget -= Angle;
					DebugGunTurn += FMath::RadiansToDegrees(Angle) * W;
					RotateChain(CS, Spine3, FQuat::Slerp(FQuat::Identity, FQuat(Axis, Angle), W), CS[Spine3].GetLocation());
					if (W < 0.9f)
					{
						break;  // 平时（没在打，权重 0.6）只大致朝着，不补第二次
					}
				}
			}
			// 10-31 展示姿势：长枪压低（在左手握护木、枪按两只手摆之前做，枪和左手都跟着走）
			if (ShowGunDown >= 0.f && ShowSquare > 0.001f)
			{
				ApplyShowGunDown(CS);
			}
			SolveLeftGrip(CS);
			// 左手去抓浮空无人机（钩爪自拉时，肩部组件设）：在握武器之后做、压过护木握持；长枪已经按 IK 之前的两只手摆好，不跟着左手走
			if (LeftReachWeight > 0.001f && UpperArm[0] != INDEX_NONE && LowerArm[0] != INDEX_NONE && Hand[0] != INDEX_NONE)
			{
				const FVector ReachCS = ToWorld.InverseTransformPosition(AimPivot + VisualActorQuat().RotateVector(LeftReachLocal));
				const float GripWeight = LeftGripWeight;
				const bool bAdaptive = bLeftGripAdaptive;
				LeftGripWeight = LeftReachWeight;
				bLeftGripAdaptive = false;
				SolveLeftArm(CS, ReachCS);
				LeftGripWeight = GripWeight;
				bLeftGripAdaptive = bAdaptive;
			}
			// 10-31 握持：两只手按武器转手、弯手指包住柄（在所有手臂 IK 之后；腿部限位、全身挥刀在动画代理里，早就做完了）
			ApplyHandGrips(CS);
			// 10-31 肩甲擦头：肩上的零件插进头就甩肘 / 歪头推开（手和武器不动，见 MechShoulderClear.cpp）
			ClearShouldersFromHead(CS);
		}
	}
	if (CS.Num() > 0)
	{
		CacheBones();
		AimDampers(CS);
		ApplyThumbsUp(CS);
	}
	Super::FinalizeBoneTransform();
	PlaceProps();
}

void UMechMeshComponent::SolveLeftGrip(TArray<FTransform>& CS)
{
	bTwoHandGunCS = false;
	LeftSupportWeight = 0.f;
	DebugLeftGripDist = -1.f;
	const int32 P = 1;
	if (Hand[0] == INDEX_NONE || Hand[1] == INDEX_NONE || UpperArm[0] == INDEX_NONE || LowerArm[0] == INDEX_NONE)
	{
		return;
	}
	if (PropMount[P] == EMount::TwoHand && !bTwoHandFreeze[P] && Props[P])
	{
		// 长枪按动作里的两只手摆（握把在右手，枪管指向左手）：左手 IK 之前先算好，枪不跟着左手走
		TwoHandGunCS = TwoHandGunFrame(CS);
		bTwoHandGunCS = true;
	}
	// 10-31 握持：左手托 / 握（双手刀的刀柄、长枪的护木）——手转到标准握法，手心空腔对准柄上的点（以前是把手腕拉到那个点）
	const FMechHandGrip& G = Grip[0];
	if (G.Mode == EMechGripMode::Support && HandRig[0].bValid && LeftGripWeight > 0.001f)
	{
		FTransform PropCS;
		bool bHave = false;
		if (G.Prop == 0 && PropMount[0] == EMount::Bone && Props[0])
		{
			if (bPropBoneDirty[0])
			{
				ResolvePropBone(0);
			}
			if (PropBone[0] != INDEX_NONE && CS.IsValidIndex(PropBone[0]))
			{
				PropCS = PropBoneLocal[0] * CS[PropBone[0]];
				bHave = true;
			}
		}
		else if (G.Prop == 1 && bTwoHandGunCS)
		{
			PropCS = TwoHandGunCS;
			bHave = true;
		}
		if (!bHave)
		{
			return;
		}
		FVector Cav, Ax, Wr;
		CanonicalInHand(0, G.Shape, Cav, Ax, Wr);
		const FQuat Cur = CS[Hand[0]].GetRotation();
		// 刀柄是圆的：只把手的柄轴对准刀柄，绕柄的滚转留动作的（手腕不会被拧）；护木按标准握法（手心朝上托着）
		const FQuat Want = G.bKeepRoll
			? (FQuat::FindBetweenNormals(Cur.RotateVector(Ax), PropCS.GetRotation().RotateVector(G.Shape.Axis.GetSafeNormal())) * Cur).GetNormalized()
			: (PropCS.GetRotation() * GripPropInHand(0, G.Shape).GetRotation().Inverse()).GetNormalized();
		const FVector LeftWristWant = PropCS.TransformPosition(G.Shape.Center) - Want.RotateVector(Cav);
		// 调试：动作里的左手腕相对「握到柄上该在的位置」差多少，按道具坐标（X 顺着柄）
		DebugLeftGripDelta = PropCS.InverseTransformVectorNoScale(CS[Hand[0]].GetLocation() - LeftWristWant);
		const float W = SolveLeftArm(CS, LeftWristWant);
		if (W > 0.001f)
		{
			const FQuat Now = CS[Hand[0]].GetRotation();
			RotateChain(CS, Hand[0], FQuat::Slerp(FQuat::Identity, (Want * Now.Inverse()).GetNormalized(), W), CS[Hand[0]].GetLocation());
		}
		LeftSupportWeight = W;
		return;
	}
	if (LeftGripProp == 0)
	{
		// 双手刀：刀挂在右手骨头上，按右手（这一帧动作里的）算出刀在哪，再把左手拉到刀柄上
		if (PropMount[0] != EMount::Bone || !Props[0] || LeftGripWeight <= 0.001f || LeftGripLocal.IsZero())
		{
			return;
		}
		if (bPropBoneDirty[0])
		{
			ResolvePropBone(0);
		}
		if (PropBone[0] == INDEX_NONE || !CS.IsValidIndex(PropBone[0]))
		{
			return;
		}
		SolveLeftArm(CS, (PropBoneLocal[0] * CS[PropBone[0]]).TransformPosition(LeftGripLocal));
		return;
	}
	if (PropMount[P] != EMount::TwoHand || bTwoHandFreeze[P] || !Props[P])
	{
		return;
	}
	// 1. 按动作里的两只手摆枪（握把在右手，枪管指向左手），和 PlaceProps 原来的规则一样，只是在组件空间里先算好
	TwoHandGunCS = TwoHandGunFrame(CS);
	bTwoHandGunCS = true;
	if (LeftGripWeight <= 0.001f || LeftGripLocal.IsZero())
	{
		return;
	}
	// 2. 左手拉到护木上
	SolveLeftArm(CS, TwoHandGunCS.TransformPosition(LeftGripLocal));
}

FTransform UMechMeshComponent::TwoHandGunFrame(const TArray<FTransform>& CS) const
{
	const int32 P = 1;
	const FTransform& ToWorld = GetComponentTransform();
	const FVector R = CS[Hand[1]].GetLocation();
	FVector X = CS[Hand[0]].GetLocation() - R;
	// 组件空间里的「上」和机体前方（网格转了 -90 度、还可能有网络平滑的转角，按世界方向换算）
	const FVector Up = ToWorld.InverseTransformVectorNoScale(FVector::UpVector);
	const FVector Fwd = ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetForwardVector());
	X = X.IsNearlyZero() ? Fwd : X.GetSafeNormal();
	const FVector Hint = FMath::Abs(X | Up) > 0.92f ? Fwd * (X.Z < 0.f ? 1.f : -1.f) : Up;
	const FQuat Rot = FRotationMatrix::MakeFromXZ(X, Hint).ToQuat() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(PropRoll[P])) *
		FQuat(FVector::RightVector, FMath::DegreesToRadians(-PropPitch[P]));
	return FTransform(Rot, R + Rot.RotateVector(PropOffset[P]));  // 组件空间是未缩放的机体尺寸，武器模型厘米数直接对得上
}

float UMechMeshComponent::SolveLeftArm(TArray<FTransform>& CS, const FVector& GripCS)
{
	// 左手两段 IK：肘尖朝向沿用动作里的肘，先转上臂、再转前臂（手和手指跟着前臂一起转，握姿不变）
	const FVector Root = CS[UpperArm[0]].GetLocation();
	const FVector Joint = CS[LowerArm[0]].GetLocation();
	const FVector End = CS[Hand[0]].GetLocation();
	float W = FMath::Clamp(LeftGripWeight, 0.f, 1.f);
	if (bLeftGripAdaptive)
	{
		// 动作里左手离握点 25 厘米内全拉，60 厘米外不管（那是单手挥、左手松开的时候）
		DebugLeftGripDist = static_cast<float>(FVector::Dist(End, GripCS));
		W *= FMath::Clamp(1.f - (FVector::Dist(End, GripCS) - 25.f) / 35.f, 0.f, 1.f);
	}
	if (W <= 0.001f)
	{
		return 0.f;
	}
	const FVector Target = FMath::Lerp(End, GripCS, W);
	FVector OutJoint, OutEnd;
	AnimationCore::SolveTwoBoneIK(Root, Joint, End, Joint + (Joint - (Root + End) * 0.5f), Target, OutJoint, OutEnd, FVector::Dist(Root, Joint),
		FVector::Dist(Joint, End), false, 1.0, 1.0);
	RotateChain(CS, UpperArm[0], FQuat::FindBetweenNormals((Joint - Root).GetSafeNormal(), (OutJoint - Root).GetSafeNormal()), Root);
	const FVector J2 = CS[LowerArm[0]].GetLocation();
	const FVector E2 = CS[Hand[0]].GetLocation();
	RotateChain(CS, LowerArm[0], FQuat::FindBetweenNormals((E2 - J2).GetSafeNormal(), (OutEnd - OutJoint).GetSafeNormal()), J2);
	return W;
}

void UMechMeshComponent::SetPropOnForearm(int32 Prop, int32 Side)
{
	PropMount[Prop] = EMount::Forearm;
	PropSide[Prop] = Side;
}

void UMechMeshComponent::SetPropTwoHand(int32 Prop, bool bFreeze)
{
	if (PropMount[Prop] != EMount::TwoHand)
	{
		bTwoHandValid[Prop] = false;
	}
	PropMount[Prop] = EMount::TwoHand;
	bTwoHandFreeze[Prop] = bFreeze;
}

void UMechMeshComponent::SetPropOnBone(int32 Prop, FName Bone, const FVector& Offset, const FRotator& Rot)
{
	if (PropMount[Prop] == EMount::Bone && PropBoneName[Prop] == Bone && PropBoneOffset[Prop] == Offset && PropBoneRot[Prop] == Rot)
	{
		return;
	}
	PropMount[Prop] = EMount::Bone;
	PropBoneName[Prop] = Bone;
	PropBoneOffset[Prop] = Offset;
	PropBoneRot[Prop] = Rot;
	bPropBoneDirty[Prop] = true;
}

void UMechMeshComponent::SetPropOnBoneLocal(int32 Prop, FName Bone, const FTransform& Local)
{
	if (PropMount[Prop] == EMount::Bone && PropBoneName[Prop] == Bone && !bPropBoneDirty[Prop] && PropBoneLocal[Prop].Equals(Local) && PropBone[Prop] != INDEX_NONE)
	{
		return;
	}
	PropMount[Prop] = EMount::Bone;
	PropBoneName[Prop] = Bone;
	PropBoneOffset[Prop] = FVector(NAN, 0.f, 0.f);  // 和 SetPropOnBone 的参数区分开，切回去时会重算
	PropBoneLocal[Prop] = Local;
	const USkinnedAsset* Asset = GetSkinnedAsset();
	PropBone[Prop] = Asset ? Asset->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
	bPropBoneDirty[Prop] = false;
}

void UMechMeshComponent::ResolvePropBone(int32 Prop)
{
	// 绑定姿势下这根骨头的组件空间变换；想要的道具变换按机体坐标给，换到网格组件空间（网格相对机体转了 -90 度）后求相对骨头的变换
	bPropBoneDirty[Prop] = false;
	const USkinnedAsset* Asset = GetSkinnedAsset();
	if (!Asset)
	{
		PropBone[Prop] = INDEX_NONE;
		return;
	}
	const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
	PropBone[Prop] = Ref.FindBoneIndex(PropBoneName[Prop]);
	if (PropBone[Prop] == INDEX_NONE)
	{
		return;
	}
	FTransform BoneCS = FTransform::Identity;
	for (int32 i = PropBone[Prop]; i != INDEX_NONE; i = Ref.GetParentIndex(i))
	{
		BoneCS = BoneCS * Ref.GetRefBonePose()[i];
	}
	const FQuat ActorToMesh = GetRelativeRotation().Quaternion().Inverse();
	const FTransform Want(ActorToMesh * PropBoneRot[Prop].Quaternion(), BoneCS.GetLocation() + ActorToMesh.RotateVector(PropBoneOffset[Prop]));
	PropBoneLocal[Prop] = Want.GetRelativeTransform(BoneCS);
}

FVector UMechMeshComponent::VisualPivot() const
{
	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	const FVector Capsule = Char ? GetComponentLocation() - VisualActorQuat().RotateVector(Char->GetBaseTranslationOffset()) : GetComponentLocation();
	return Capsule + FVector(0.f, 0.f, 100.f);
}

FQuat UMechMeshComponent::VisualActorQuat() const
{
	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	return Char ? GetComponentQuat() * Char->GetBaseRotationOffset().Inverse() : GetOwner() ? GetOwner()->GetActorQuat() : GetComponentQuat();
}

void UMechMeshComponent::PlaceProps()
{
	const AActor* Owner = GetOwner();
	if (!Owner || Hand[0] == INDEX_NONE)
	{
		return;
	}
	const FQuat Vis = VisualActorQuat();
	const FVector Up = Vis.GetUpVector();
	const FVector Fwd = Vis.GetForwardVector();
	const FVector Scale = GetComponentScale();
	for (int32 p = 0; p < NumProps; ++p)
	{
		USceneComponent* Prop = Props[p];
		if (!Prop || p == 2)
		{
			continue;  // 肩部挂件（10-30 起是浮空无人机）由肩部组件按弹簧阻尼摆，不跟骨头
		}
		if (PropMount[p] == EMount::Bone)
		{
			if (bPropBoneDirty[p])
			{
				ResolvePropBone(p);
			}
			if (PropBone[p] != INDEX_NONE && GetComponentSpaceTransforms().IsValidIndex(PropBone[p]))
			{
				const FTransform W = PropBoneLocal[p] * GetComponentSpaceTransforms()[PropBone[p]] * GetComponentTransform();
				Prop->SetWorldLocationAndRotation(W.GetLocation(), W.GetRotation());
			}
			continue;
		}
		if (PropMount[p] == EMount::TwoHand)
		{
			// 双手端：握把在右手，枪管从右手指向左手（护木）；换弹时冻结在相对右手的位置
			const FTransform HandR = GetBoneTransform(Hand[1]);
			FTransform W;
			if (bTwoHandFreeze[p] && bTwoHandValid[p])
			{
				W = TwoHandRel[p] * HandR;
			}
			else if (p == 1 && bTwoHandGunCS)
			{
				// 骨骼定稿前（左手 IK 之前）已经按两只手算好了
				W = TwoHandGunCS * GetComponentTransform();
				W.SetScale3D(FVector::OneVector);
				TwoHandRel[p] = W.GetRelativeTransform(HandR);
				bTwoHandValid[p] = true;
			}
			else
			{
				FVector X = (GetBoneLocation(GetBoneName(Hand[0])) - HandR.GetLocation()).GetSafeNormal();
				if (X.IsNearlyZero())
				{
					X = Fwd;
				}
				const FVector Hint = FMath::Abs(X | Up) > 0.92f ? Fwd * (X.Z < 0.f ? 1.f : -1.f) : Up;
				const FQuat Rot = FRotationMatrix::MakeFromXZ(X, Hint).ToQuat() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(PropRoll[p])) *
					FQuat(FVector::RightVector, FMath::DegreesToRadians(-PropPitch[p]));
				W = FTransform(Rot, HandR.GetLocation() + Rot.RotateVector(PropOffset[p] * Scale));
				TwoHandRel[p] = W.GetRelativeTransform(HandR);
				bTwoHandValid[p] = true;
			}
			Prop->SetWorldLocationAndRotation(W.GetLocation(), W.GetRotation());
			continue;
		}
		const int32 s = PropSide[p];
		const FVector HandPos = GetBoneLocation(GetBoneName(Hand[s]));
		const FVector Fore = (HandPos - GetBoneLocation(GetBoneName(LowerArm[s]))).GetSafeNormal();
		// 武器顶面尽量朝上；前臂接近竖直时改用朝前做参考，免得翻转
		const FVector Hint = FMath::Abs(Fore | Up) > 0.92f ? Fwd * (Fore.Z < 0.f ? 1.f : -1.f) : Up;
		const FQuat Base = FRotationMatrix::MakeFromXZ(Fore, Hint).ToQuat();
		const FQuat Rot = Base * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(PropRoll[p])) *
			FQuat(FVector::RightVector, FMath::DegreesToRadians(-PropPitch[p]));
		Prop->SetWorldLocationAndRotation(HandPos + Rot.RotateVector(PropOffset[p] * Scale), Rot);
	}
	// 调试统计用：记下摆完时道具在网格局部的位置（每台每帧 5 次逆变换，开销可以忽略）
	PlacedFrame = GFrameCounter;
	const FTransform& ToWorld = GetComponentTransform();
	for (int32 p = 0; p < NumProps; ++p)
	{
		PlacedLocal[p] = Props[p] ? ToWorld.InverseTransformPosition(Props[p]->GetComponentLocation()) : FVector::ZeroVector;
	}
}

FVector UMechMeshComponent::GetPropPoint(int32 Prop, const FVector& LocalPoint) const
{
	const USceneComponent* P = (Prop >= 0 && Prop < NumProps) ? Props[Prop].Get() : nullptr;
	return P ? P->GetComponentTransform().TransformPosition(LocalPoint) : GetComponentLocation();
}

void UMechMeshComponent::ApplyThumbsUp(TArray<FTransform>& CS)
{
	const float W = FMath::Clamp(ThumbsUp, 0.f, 1.f);
	if (W <= 0.001f || !GetOwner() || LowerArm[1] == INDEX_NONE || Hand[1] == INDEX_NONE || Parents.Num() != CS.Num())
	{
		return;
	}
	const FTransform& ToWorld = GetComponentTransform();
	const FVector Up = ToWorld.InverseTransformVectorNoScale(FVector::UpVector);
	const FVector Fwd = ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetForwardVector());
	const FVector Right = ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetRightVector());
	auto Turn = [&](int32 Root, const FVector& From, const FVector& To, const FVector& Pivot) {
		if (!From.IsNearlyZero() && !To.IsNearlyZero())
		{
			RotateChain(CS, Root, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(From.GetSafeNormal(), To.GetSafeNormal()), W), Pivot);
		}
	};
	// 左臂（端着隐藏起来的枪）放下来，自然垂在身侧
	if (UpperArm[0] != INDEX_NONE && Hand[0] != INDEX_NONE)
	{
		Turn(UpperArm[0], CS[Hand[0]].GetLocation() - CS[UpperArm[0]].GetLocation(), -Up * 0.95f + Fwd * 0.12f - Right * 0.18f, CS[UpperArm[0]].GetLocation());
	}
	// 右臂：大臂贴着身体垂下（肘在腰侧），前臂水平朝前、略往外摆（手是直的，手指顺着前臂，拳头横着、拇指朝上才像竖大拇指）
	if (UpperArm[1] != INDEX_NONE)
	{
		Turn(UpperArm[1], CS[LowerArm[1]].GetLocation() - CS[UpperArm[1]].GetLocation(), -Up * 0.88f + Fwd * 0.12f + Right * 0.4f, CS[UpperArm[1]].GetLocation());
	}
	Turn(LowerArm[1], CS[Hand[1]].GetLocation() - CS[LowerArm[1]].GetLocation(), Fwd * 0.55f + Up * 0.28f + Right * 0.78f, CS[LowerArm[1]].GetLocation());
	// 拳头绕前臂转：拇指那一侧朝上
	if (ThumbBone[1] != INDEX_NONE && Middle1[1] != INDEX_NONE)
	{
		const FVector D = (CS[Middle1[1]].GetLocation() - CS[Hand[1]].GetLocation()).GetSafeNormal();
		const FVector Side = FVector::VectorPlaneProject(CS[ThumbBone[1]].GetLocation() - CS[Hand[1]].GetLocation(), D);
		const FVector WantSide = FVector::VectorPlaneProject(Up, D);
		Turn(Hand[1], Side, WantSide, CS[Hand[1]].GetLocation());
		// 拇指：骨头本身朝着手指方向（偏 25 度），找它和手指方向最接近的那根局部轴，转到朝上
		const FQuat R = CS[ThumbBone[1]].GetRotation();
		const FVector D2 = (CS[Middle1[1]].GetLocation() - CS[Hand[1]].GetLocation()).GetSafeNormal();
		FVector Best = R.GetAxisX();
		for (const FVector& A : {R.GetAxisX(), -R.GetAxisX(), R.GetAxisY(), -R.GetAxisY(), R.GetAxisZ(), -R.GetAxisZ()})
		{
			Best = FVector::DotProduct(A, D2) > FVector::DotProduct(Best, D2) ? A : Best;
		}
		const FQuat Q = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Best, (Up * 0.95f + D2 * 0.1f).GetSafeNormal()), W);
		CS[ThumbBone[1]].SetRotation(Q * R);
	}
	// 10-31 可弯手指的手（拇指三节挂 thumb_01–03，thumb_up_r 上没零件了）：四指握拳，拇指伸直（绑定姿势）再整根转到朝上
	if (HandRig[1].bValid && Middle1[1] != INDEX_NONE)
	{
		const FHandRig& H = HandRig[1];
		ApplyFingerPose(CS, 1, SolveGrip(1, nullptr), W, true);
		const FVector Base = CS[H.Bone[0][0]].GetLocation();
		const FVector Mid = CS[H.Bone[0][2]].GetLocation();
		const FVector Tip = Mid + (Mid - CS[H.Bone[0][1]].GetLocation()).GetSafeNormal() * TipLen[0];
		const FVector D2 = (CS[Middle1[1]].GetLocation() - CS[Hand[1]].GetLocation()).GetSafeNormal();
		RotateChain(CS, H.Bone[0][0], FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals((Tip - Base).GetSafeNormal(),
			(Up * 0.95f + D2 * 0.1f).GetSafeNormal()), W), Base);
	}
}

// ---------------------------------------------------------------- 10-31 展示姿势（结算场景 / 主菜单）：上身转正、长枪压低
void UMechMeshComponent::ApplyShowSquare(TArray<FTransform>& CS)
{
	DebugShowTwist = FVector::ZeroVector;
	// 胸口的左右方向：两根锁骨的根（锁骨本身会随手臂前伸转，根的位置只跟着 spine_05 走，和胸腔刚性连着）；没有锁骨就用两肩
	const bool bClav = Clavicle[0] != INDEX_NONE && Clavicle[1] != INDEX_NONE;
	const int32 L = bClav ? Clavicle[0] : UpperArm[0];
	const int32 R = bClav ? Clavicle[1] : UpperArm[1];
	if (L == INDEX_NONE || R == INDEX_NONE || Spine1 == INDEX_NONE)
	{
		return;
	}
	const FTransform& ToWorld = GetComponentTransform();
	const FVector Up = ToWorld.InverseTransformVectorNoScale(FVector::UpVector);
	const FVector Right = FVector::VectorPlaneProject(ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetRightVector()), Up).GetSafeNormal();
	const FVector Flat = FVector::VectorPlaneProject(CS[R].GetLocation() - CS[L].GetLocation(), Up).GetSafeNormal();
	if (Flat.IsNearlyZero() || Right.IsNearlyZero())
	{
		return;
	}
	const float W = FMath::Clamp(ShowSquare, 0.f, 1.f);
	// 偏航：左右方向转到机体的右方，绕竖直轴转整个上身（spine_01 往上：头、手臂、手里的武器一起）
	const FQuat Yaw = FQuat::FindBetweenNormals(Flat, Right);
	DebugShowTwist.X = FMath::RadiansToDegrees(Yaw.GetAngle()) * FMath::Sign(FVector::DotProduct(Yaw.GetRotationAxis(), Up));
	RotateChain(CS, Spine1, FQuat::Slerp(FQuat::Identity, Yaw, W), CS[Spine1].GetLocation());
	// 侧歪：左右方向的上下分量去掉（绕胸口朝前的方向转，支点同样在 spine_01）；前倾 / 后仰不动（结算的前倾、挺胸另外加）
	const FVector Line = (CS[R].GetLocation() - CS[L].GetLocation()).GetSafeNormal();
	const FVector Level = FVector::VectorPlaneProject(Line, Up).GetSafeNormal();
	if (!Line.IsNearlyZero() && !Level.IsNearlyZero())
	{
		DebugShowTwist.Y = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Line, Up), -1.f, 1.f)));
		RotateChain(CS, Spine1, FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Line, Level), W), CS[Spine1].GetLocation());
	}
}

void UMechMeshComponent::ApplyShowGunDown(TArray<FTransform>& CS)
{
	// 只管双手端的长枪（枪按两只手摆：握把在右手、枪管指向左手）。
	// 低姿持枪（low ready）：右手握把在右胯前上方、枪斜着横在身前，枪口朝左前下方；两条手臂各做两段 IK 摆到位（上身用待机动作，
	// 不叠端枪片段——那个片段是侧身抵肩瞄准，枪管比胸口朝向偏左将近 90 度，上身转正以后枪口就横着指向左边；10-31 第一版就是这样）。
	// 左手之后照常按握持握到护木上（SolveLeftGrip）
	if (PropMount[1] != EMount::TwoHand || !Props[1])
	{
		return;
	}
	for (int32 s = 0; s < 2; ++s)
	{
		if (UpperArm[s] == INDEX_NONE || LowerArm[s] == INDEX_NONE || Hand[s] == INDEX_NONE)
		{
			return;
		}
	}
	const FTransform& ToWorld = GetComponentTransform();
	const FVector Up = ToWorld.InverseTransformVectorNoScale(FVector::UpVector);
	const FVector Fwd = ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetForwardVector());
	const FVector Right = ToWorld.InverseTransformVectorNoScale(VisualActorQuat().GetRightVector());
	// 按这台的胳膊长（上臂 + 前臂）定位置：各家外壳高矮不同，骨架一样
	auto ArmLen = [&CS, this](int32 s) {
		return FVector::Dist(CS[UpperArm[s]].GetLocation(), CS[LowerArm[s]].GetLocation()) + FVector::Dist(CS[LowerArm[s]].GetLocation(), CS[Hand[s]].GetLocation());
	};
	const float Arm = ArmLen(1);
	const float ArmLeft = ArmLen(0);
	// 两手间距 = 握把到护木（右手 → 左手握的那一点）；没有护木数据时按 0.65 个胳膊长
	const float Span = LeftGripProp == 1 && !LeftGripLocal.IsZero() ? FMath::Clamp((PropOffset[1] + LeftGripLocal).Size(), 0.4f * Arm, 0.8f * Arm) : 0.65f * Arm;
	// 先定左手（托护木）：左肩往前、往下、往里（约 0.9 个胳膊长，够得着、肘微弯；枪口压得越低左手越低越靠身），右手 = 左手沿枪管往回退一个两手间距
	// （枪托回到右胸前，像人低姿持枪时枪托还抵在肩窝）。第一版先定右手（胯前）再沿枪管往前找左手：机体肩宽，往前往下加起来左手够不着，
	// 枪被拉成水平横在胸前。右手离右肩超过 0.92 个胳膊长（够不着）就把枪口一档 5 度往上抬
	const float YawRad = FMath::DegreesToRadians(ShowGunYaw);
	float DownDeg = FMath::Clamp(ShowGunDown, 0.f, 80.f);
	const float Low = FMath::Clamp((DownDeg - 30.f) / 30.f, 0.f, 1.f);
	const FVector LeftHand = CS[UpperArm[0]].GetLocation() + Fwd * (FMath::Lerp(0.75f, 0.45f, Low) * ArmLeft) - Up * (FMath::Lerp(0.5f, 0.75f, Low) * ArmLeft) +
		Right * (0.2f * ArmLeft);
	FVector Barrel = Fwd;
	FVector RightHand = LeftHand;
	for (int32 k = 0; k < 20; ++k)
	{
		const float Down = FMath::DegreesToRadians(DownDeg);
		Barrel = ((Fwd * FMath::Cos(YawRad) - Right * FMath::Sin(YawRad)) * FMath::Cos(Down) - Up * FMath::Sin(Down)).GetSafeNormal();
		RightHand = LeftHand - Barrel * Span;
		if (DownDeg <= 0.f || FVector::Dist(RightHand, CS[UpperArm[1]].GetLocation()) <= 0.92f * Arm)
		{
			break;
		}
		DownDeg = FMath::Max(0.f, DownDeg - 5.f);
	}
	DebugShowTwist.Z = -DownDeg;
	const FVector Targets[2] = {LeftHand, RightHand};
	// 肘尖：右肘朝右后下，左肘朝左下
	const FVector Poles[2] = {(-Right * 0.75f - Up * 0.55f + Fwd * 0.1f) * Arm, (Right * 0.7f - Up * 0.5f - Fwd * 0.3f) * Arm};
	const float W = FMath::Clamp(ShowSquare, 0.f, 1.f);
	for (int32 s = 1; s >= 0; --s)
	{
		const FVector Root = CS[UpperArm[s]].GetLocation();
		const FVector Joint = CS[LowerArm[s]].GetLocation();
		const FVector End = CS[Hand[s]].GetLocation();
		FVector OutJoint, OutEnd;
		AnimationCore::SolveTwoBoneIK(Root, Joint, End, (Root + Targets[s]) * 0.5f + Poles[s], Targets[s], OutJoint, OutEnd, FVector::Dist(Root, Joint),
			FVector::Dist(Joint, End), false, 1.0, 1.0);
		RotateChain(CS, UpperArm[s], FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals((Joint - Root).GetSafeNormal(), (OutJoint - Root).GetSafeNormal()), W), Root);
		const FVector J2 = CS[LowerArm[s]].GetLocation();
		const FVector E2 = CS[Hand[s]].GetLocation();
		RotateChain(CS, LowerArm[s], FQuat::FindBetweenNormals((E2 - J2).GetSafeNormal(), (FMath::Lerp(E2, OutEnd, W) - J2).GetSafeNormal()), J2);
	}
}
