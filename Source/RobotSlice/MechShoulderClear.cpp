// 肩甲擦头（10-31 用户：「大剑举刀过顶时，肩甲擦到头」，九种框架都要查）。
// 动作是真人动作重定向来的：真人肩膀窄、头能贴着上臂，机体的肩甲 / 上臂装甲大（维克托轻 / 中的肩甲顶到 157–159 厘米、和下沉的头差不多高），
// 举刀过顶、双手握柄把左臂横到胸前、弯腰歪头时，肩甲就插进头里。Blender 里把大剑五个攻击片段套到九台上（tools/shoulder_head_clip.py，
// 每秒 15 帧）：新纪轻 / 中 / 重 14–20 帧擦头（共 188 帧）、相位轻 / 中 / 重 5–31、维克托重 62、维克托轻 134、维克托中 150。
// 做法（骨骼定稿时、所有手臂 IK 和握持之后，不改模型、不改动作）：
//  · 每台一份简化碰撞体（tools/shoulder_head_proxy.py 从模型算、tools/shoulder_head_table.py 生成 MechShoulderClearData.inl）：
//    头 = 4 个球（头部零件按位置分 4 团、每团一个包住它的球），肩 = 两边各 160 个采样点（锁骨、上臂上的零件按面积撒点再均匀挑）；
//  · 每边算「肩上的点插进头球多深」（留 1.5 厘米），插进去了按三步推，每步都二分找刚好不穿的最小角度：
//    ① 甩肘：整条手臂绕「肩 → 手腕」这条线转（手腕位置不动，手再反着转回来，手里的刀 / 枪和握姿一点不变，只是肘往离头远的那边抬），最多 40 度；
//    ② 还穿（肩甲挂在锁骨上、或者就在肩关节旁边，甩肘带不动）：锁骨把肩往离头远的方向转（最多 20 度），手臂两段 IK 把手腕拉回原处；
//    ③ 还穿（头本来就压在肩上）：头往离最深那一点远的方向转（绕脖子，最多 15 度）。
// 每帧都只按这一帧的姿势算（不记状态，姿势连续推法就连续）；不穿就什么都不动。
// 10-31 用户决定这一项暂时搁置：**默认关闭**，动作和改之前完全一样（关着时这里直接返回，什么都不算）。
// 要打开看效果：命令行 -MechShoulderClear，或控制台 mech.ShoulderClear 1。-MechMeleeFeelFrames 测试时关着也只量插进头多深、不推。
// 游戏里实测（-MechMeleeFeelTest -MechMeleeFeelFrames，九台各打一套大剑四连 + 重斩）见 README「阵营机体」的逐台修正。

#include "MechMeshComponent.h"

#include "HAL/IConsoleManager.h"
#include "TwoBoneIK.h"
#include "MechCatalog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace MechShoulderClearPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的东西撞
{
	struct FShoulderProxy
	{
		const TCHAR* Key;
		const float (*Head)[4];
		int32 NumHead;
		const float (*Arm[2])[4];
		int32 NumArm[2];
	};
#include "MechShoulderClearData.inl"

	TAutoConsoleVariable<int32> CVarShoulderClear(TEXT("mech.ShoulderClear"), 0,
		TEXT("肩甲擦头时甩肘 / 转锁骨 / 歪头推开（10-31，MechShoulderClear.cpp）。默认 0 关（用户暂时搁置），1 开；命令行 -MechShoulderClear 也能开"));
	constexpr float Margin = 1.5f;     // 推到离头球表面留多少（厘米，网格尺寸）
	constexpr float SwivelMax = 40.f;  // 甩肘最多多少度
	constexpr float ClavMax = 20.f;    // 锁骨把肩往外转最多多少度（手臂 IK 把手拉回原处）
	constexpr float HeadMax = 15.f;    // 歪头最多多少度
}

void UMechMeshComponent::ClearShouldersFromHead(TArray<FTransform>& CS)
{
	using namespace MechShoulderClearPriv;
	static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechShoulderClear"));
	static const bool bMeasure = FParse::Param(FCommandLine::Get(), TEXT("MechMeleeFeelFrames"));
	DebugShoulderFix[0] = DebugShoulderFix[1] = FVector::ZeroVector;
	DebugShoulderDepth[0] = DebugShoulderDepth[1] = FVector2D::ZeroVector;
	// 默认关（暂时搁置）：关着时直接返回；只有 -MechMeleeFeelFrames 测试时关着也量插进去多深（改前改后对比用），只是不推
	const bool bFix = bOn || CVarShoulderClear.GetValueOnGameThread() != 0;
	if (!bFix && !bMeasure)
	{
		return;
	}
	// 这台网格是哪一台、要用到的几根骨头的绑定姿势（换网格时重查）
	const USkinnedAsset* Asset = GetSkinnedAsset();
	if (!Asset)
	{
		return;
	}
	if (ShoulderFor != Asset)
	{
		ShoulderFor = Asset;
		ShoulderProxy = INDEX_NONE;
		if (const MechCatalog::FFrameBody* Body = MechCatalog::FrameBodyForMesh(Asset))
		{
			for (int32 i = 0; i < UE_ARRAY_COUNT(ShoulderProxies); ++i)
			{
				if (FCString::Strcmp(Body->Key, ShoulderProxies[i].Key) == 0)
				{
					ShoulderProxy = i;
				}
			}
		}
		const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();
		auto RefCS = [&Ref](int32 B) {
			FTransform T = FTransform::Identity;
			for (; B != INDEX_NONE; B = Ref.GetParentIndex(B))
			{
				T = T * Ref.GetRefBonePose()[B];
			}
			return T;
		};
		HeadBone = Ref.FindBoneIndex(TEXT("head"));
		NeckBone = Ref.FindBoneIndex(TEXT("neck_01"));
		static const TCHAR* Names[5] = {TEXT("head"), TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("clavicle_r"), TEXT("upperarm_r")};
		for (int32 k = 0; k < 5; ++k)
		{
			const int32 B = Ref.FindBoneIndex(Names[k]);
			ShoulderRef[k] = B != INDEX_NONE ? RefCS(B) : FTransform::Identity;
		}
	}
	if (ShoulderProxy == INDEX_NONE || HeadBone == INDEX_NONE || NeckBone == INDEX_NONE || Spine5 == INDEX_NONE || !CS.IsValidIndex(HeadBone))
	{
		return;
	}
	const FShoulderProxy& P = ShoulderProxies[ShoulderProxy];
	// 绑定姿势里的一点 → 这一帧组件空间（跟着骨头 B 走）
	auto Move = [&CS](int32 B, const FTransform& RefB, const FVector& P0) { return CS[B].TransformPosition(RefB.InverseTransformPosition(P0)); };
	for (int32 s = 0; s < 2; ++s)
	{
		if (UpperArm[s] == INDEX_NONE || LowerArm[s] == INDEX_NONE || Hand[s] == INDEX_NONE || Clavicle[s] == INDEX_NONE)
		{
			continue;
		}
		const int32 NH = FMath::Min(P.NumHead, 8);
		FVector HeadC[8];
		float HeadR[8];
		TArray<FVector, TInlineAllocator<192>> Pts;
		TArray<bool, TInlineAllocator<192>> OnUpper;
		// 按这一帧（已经推过的）骨头重新取头球、肩点的位置
		auto Gather = [&]() {
			for (int32 k = 0; k < NH; ++k)
			{
				HeadC[k] = Move(HeadBone, ShoulderRef[0], FVector(P.Head[k][0], P.Head[k][1], P.Head[k][2]));
				HeadR[k] = P.Head[k][3] + Margin;
			}
			Pts.Reset();
			OnUpper.Reset();
			for (int32 i = 0; i < P.NumArm[s]; ++i)
			{
				const float* Q = P.Arm[s][i];
				const bool bUpper = Q[3] > 0.5f;
				Pts.Add(bUpper ? Move(UpperArm[s], ShoulderRef[s * 2 + 2], FVector(Q[0], Q[1], Q[2])) : Move(Clavicle[s], ShoulderRef[s * 2 + 1], FVector(Q[0], Q[1], Q[2])));
				OnUpper.Add(bUpper);
			}
		};
		// 插进去多深（> 0 = 进了留了余量的头球）：XForm 把肩点换到试推之后的位置，HeadQ / HeadPivot 把头球绕脖子转
		auto Depth = [&](TFunctionRef<FVector(int32)> XForm, const FQuat& HeadQ, const FVector& HeadPivot, int32* OutPt = nullptr, int32* OutHead = nullptr) {
			float D = -BIG_NUMBER;
			for (int32 k = 0; k < NH; ++k)
			{
				const FVector C = HeadPivot + HeadQ.RotateVector(HeadC[k] - HeadPivot);
				for (int32 i = 0; i < Pts.Num(); ++i)
				{
					const float Di = HeadR[k] - FVector::Dist(XForm(i), C);
					if (Di > D)
					{
						D = Di;
						if (OutPt) { *OutPt = i; }
						if (OutHead) { *OutHead = k; }
					}
				}
			}
			return D;
		};
		auto Same = [&](int32 i) { return Pts[i]; };
		// 二分：Try(角度) 返回这个角度下插多深，找刚好不穿的最小角度（Max 内；Max 也不够就用 Max）
		auto Solve = [](TFunctionRef<float(float)> Try, float Max, int32 Iter) {
			if (Try(Max) > 0.f)
			{
				return Max;
			}
			float Lo = 0.f, Hi = Max;
			for (int32 It = 0; It < Iter; ++It)
			{
				const float Mid = 0.5f * (Lo + Hi);
				(Try(Mid) > 0.f ? Lo : Hi) = Mid;
			}
			return Hi;
		};
		Gather();
		const FVector N = CS[NeckBone].GetLocation();
		// 调试量的是去掉 1.5 厘米余量的真实插入深度（> 0 = 肩上的点进了头球）
		const float D0 = Depth(Same, FQuat::Identity, N);
		DebugShoulderDepth[s] = FVector2D(FMath::Max(D0 - Margin, 0.f), FMath::Max(D0 - Margin, 0.f));
		if (!bFix || D0 <= 0.f)
		{
			continue;
		}
		const FQuat HandRot0 = CS[Hand[s]].GetRotation();
		const FVector HandAt0 = CS[Hand[s]].GetLocation();
		// ① 甩肘：整条手臂绕肩 → 手腕这条线转，肘往离头远的那边走（肘几乎正对着头 / 背着头时沿用上一帧的方向，免得来回翻）；手腕在轴上不动，手再转回原样
		{
			const FVector S = CS[UpperArm[s]].GetLocation();
			const FVector E = CS[LowerArm[s]].GetLocation();
			const FVector Axis = (HandAt0 - S).GetSafeNormal();
			if (!Axis.IsNearlyZero())
			{
				const float Dir = (Axis ^ (E - S)).GetSafeNormal() | (E - CS[HeadBone].GetLocation()).GetSafeNormal();
				if (FMath::Abs(Dir) > 0.15f || ShoulderSwivelSign[s] == 0.f)
				{
					ShoulderSwivelSign[s] = Dir >= 0.f ? 1.f : -1.f;
				}
				const float Sign = ShoulderSwivelSign[s];
				const float Swing = Solve([&](float Deg) {
					const FQuat R(Axis, FMath::DegreesToRadians(Sign * Deg));
					return Depth([&](int32 i) { return OnUpper[i] ? S + R.RotateVector(Pts[i] - S) : Pts[i]; }, FQuat::Identity, N);
				}, SwivelMax, 9);
				RotateChain(CS, UpperArm[s], FQuat(Axis, FMath::DegreesToRadians(Sign * Swing)), S);
				RotateChain(CS, Hand[s], (HandRot0 * CS[Hand[s]].GetRotation().Inverse()).GetNormalized(), CS[Hand[s]].GetLocation());
				DebugShoulderFix[s].X = Swing;
				Gather();
			}
		}
		// ② 还穿（肩甲挂在锁骨上、或者肩甲就在肩关节旁边，甩肘带不动）：锁骨把肩往离头远的方向转（最多 20 度），手臂再两段 IK 把手腕拉回原处
		int32 Deep = 0, DeepHead = 0;
		if (Depth(Same, FQuat::Identity, N, &Deep, &DeepHead) > 0.f)
		{
			const FVector Cp = CS[Clavicle[s]].GetLocation();
			const FVector S = CS[UpperArm[s]].GetLocation();
			const FVector E = CS[LowerArm[s]].GetLocation();
			const FVector H = CS[Hand[s]].GetLocation();
			const FVector Ac = ((Pts[Deep] - Cp) ^ (Pts[Deep] - HeadC[DeepHead])).GetSafeNormal();
			const float L1 = FVector::Dist(S, E);
			const float L2 = FVector::Dist(E, H);
			// 锁骨转 Deg 度、IK 拉回之后：肩点怎么走（锁骨上的跟着锁骨转，上臂上的再跟着 IK 转上臂）
			auto Arm = [&](float Deg, FQuat& OutRc, FQuat& OutQ2, FVector& OutS1, FVector& OutE2) {
				OutRc = FQuat(Ac, FMath::DegreesToRadians(Deg));
				OutS1 = Cp + OutRc.RotateVector(S - Cp);
				const FVector E1 = Cp + OutRc.RotateVector(E - Cp);
				const FVector H1 = Cp + OutRc.RotateVector(H - Cp);
				FVector OutH2;
				AnimationCore::SolveTwoBoneIK(OutS1, E1, H1, E1 + (E1 - (OutS1 + H1) * 0.5f), HandAt0, OutE2, OutH2, L1, L2, false, 1.0, 1.0);
				OutQ2 = FQuat::FindBetweenNormals((E1 - OutS1).GetSafeNormal(), (OutE2 - OutS1).GetSafeNormal());
			};
			if (!Ac.IsNearlyZero())
			{
				const float Clav = Solve([&](float Deg) {
					FQuat Rc, Q2;
					FVector S1, E2;
					Arm(Deg, Rc, Q2, S1, E2);
					return Depth([&](int32 i) {
						const FVector P1 = Cp + Rc.RotateVector(Pts[i] - Cp);
						return OnUpper[i] ? S1 + Q2.RotateVector(P1 - S1) : P1;
					}, FQuat::Identity, N);
				}, ClavMax, 8);
				FQuat Rc, Q2;
				FVector S1, E2;
				Arm(Clav, Rc, Q2, S1, E2);
				RotateChain(CS, Clavicle[s], Rc, Cp);
				RotateChain(CS, UpperArm[s], Q2, CS[UpperArm[s]].GetLocation());
				const FVector EN = CS[LowerArm[s]].GetLocation();
				RotateChain(CS, LowerArm[s], FQuat::FindBetweenNormals((CS[Hand[s]].GetLocation() - EN).GetSafeNormal(), (HandAt0 - EN).GetSafeNormal()), EN);
				RotateChain(CS, Hand[s], (HandRot0 * CS[Hand[s]].GetRotation().Inverse()).GetNormalized(), CS[Hand[s]].GetLocation());
				DebugShoulderFix[s].Y = Clav;
				Gather();
			}
		}
		// ③ 还穿（头本来就压在肩上）：头往离最深那一点远的方向转（绕脖子，最多 15 度）
		if (Depth(Same, FQuat::Identity, N, &Deep, &DeepHead) > 0.f)
		{
			const FVector Ah = ((HeadC[DeepHead] - N) ^ (HeadC[DeepHead] - Pts[Deep])).GetSafeNormal();
			if (!Ah.IsNearlyZero())
			{
				const float Tilt = Solve([&](float Deg) { return Depth(Same, FQuat(Ah, FMath::DegreesToRadians(Deg)), N); }, HeadMax, 8);
				RotateChain(CS, NeckBone, FQuat(Ah, FMath::DegreesToRadians(Tilt)), N);
				DebugShoulderFix[s].Z = Tilt;
				Gather();
			}
		}
		DebugShoulderDepth[s].Y = FMath::Max(Depth(Same, FQuat::Identity, N) - Margin, 0.f);
	}
}
