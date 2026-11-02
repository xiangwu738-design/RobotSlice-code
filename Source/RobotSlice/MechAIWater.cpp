// 有会损毁的水的地图上的 AI（11-02，石柱山水 L_Pillars、荒野与废墟 L_Nature；规则见 MechMapRules.h）。
// 「AI 不能傻傻飞进水里」：原来的 AI 只管在水平面上绕着目标转，随手跳一下，水在它眼里不存在。这里在它算出移动方向之后再过一道：
//   L_Nature（地面图）：前方几米踩下去会下水就顺着岸绕开；靠近湖不随机跳、不朝湖闪避；万一被打飞到湖上就喷回岸上。
//   L_Pillars（空战图，用户：「你要维持你一直在空中，不要落下去」）：能起飞就起飞，飞着打。用的是和玩家一样的飞行（石林飞行器，UMechMovementComponent::GlideStep）：
//     按住冲刺飞，水平方向跟移动方向，俯仰由这里按想要的高度算（AI 的视线要瞄人，不能拿来飞）。平飞靠回能撑得住，闪避要花能量，所以能量低了不闪避；
//     万一能量见底就飞回最近的落脚点（UMechWater::FindLanding）。前方石柱太高爬不过去就绕；快撞水就不管别的，抬头。
// 只用服务器上的地面格子（AMechGameMode::ComputeArena 建），每帧只查格子，不做射线；绕路才做几次球形扫描，且 0.12 秒才算一次。
// 量化：-MechAutoBattle 跑完数日志里的 [water]（tools/pillars_ai_stats.py）。

#include "MechAIController.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "MechCharacter.h"
#include "MechHealthComponent.h"
#include "MechMapRules.h"
#include "MechMovementComponent.h"

namespace MechAIWater
{
	/** 空战图：脚离水面 / 地面这么高就算危险（厘米，算上刹车距离之后） */
	constexpr float DangerClear = 800.f;
	/** 空战图：站在地上至少有这么多能量才起飞 */
	constexpr float TakeoffEnergy = 45.f;
	/** 空战图：能量低于这个先不冲刺、不闪避（只点喷射保持高度，净回能最快） */
	constexpr float LowEnergy = 24.f;
	/** 空战图：贴着地形飞时留多高 */
	constexpr float TerrainClear = 800.f;
	/** 空战图：一次爬升最多想爬多少（再高就绕） */
	constexpr float MaxClimb = 3800.f;
}

bool AMechAIController::WaterSteerGround(UMechWater* W, const FVector& Me, const FVector& Dir, float Look, FVector& Out) const
{
	// 先小角度偏开，优先往现在横移的那边
	static const float Angles[] = {25.f, 50.f, 75.f, 100.f, 130.f, 160.f};
	for (const float A : Angles)
	{
		for (const float S : {StrafeSign, -StrafeSign})
		{
			const FVector D = Dir.RotateAngleAxis(A * S, FVector::UpVector);
			if (W->IsSafeLine(Me, D, Look))
			{
				Out = D;
				return true;
			}
		}
	}
	return false;
}

bool AMechAIController::WaterAirFree(AMechCharacter* Self, UMechWater* W, const FVector& Me, const FVector& Feet, const FVector& Dir, float Look, float Climb) const
{
	// 格子：沿途的地面比「现在 + 能爬的高度」还高 = 过不去
	for (const float F : {0.35f, 0.7f, 1.f})
	{
		if (W->GroundTop(Me + Dir * (Look * F)) > Feet.Z + Climb)
		{
			return false;
		}
	}
	// 球形扫描：石柱的横向凸起格子看不出来
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechAIWaterSteer), false, Self);
	return !GetWorld()->SweepTestByObjectType(Me, Me + Dir * FMath::Min(Look, 2500.f), FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic),
		FCollisionShape::MakeSphere(90.f), Params);
}

bool AMechAIController::WaterThink(AMechCharacter* Self, AMechCharacter* Target, bool bSee, float Dist, float Range, bool bBlade, float DeltaSeconds, FVector& InOutMove, FWaterOrder& Out)
{
	UWorld* World = GetWorld();
	UMechWater* W = World->GetSubsystem<UMechWater>();
	if (!W || !W->HasWater() || !W->IsGridReady())
	{
		return false;
	}
	Out.bActive = true;
	using namespace MechAIWater;
	const MechMapRules::FFlight& Flight = MechMapRules::FlightFor(World);
	UMechMovementComponent* M = Self->GetMechMovement();
	const float HalfH = Self->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Me = Self->GetActorLocation();
	const FVector Feet = Me - FVector(0.f, 0.f, HalfH);
	const FVector Vel = Self->GetVelocity();
	const float HSpeed = Vel.Size2D();
	const bool bGround = M->IsMovingOnGround();
	const FMechWaterZone* Zone = W->ZoneAt(Me);
	const float WaterZ = Zone ? Zone->Z : -1.e9f;
	const bool bUnsafeHere = !W->IsSafeStep(Me);
	const float Top = W->GroundTop(Me);
	const float Clear = Feet.Z - FMath::Max(Top, WaterZ);
	const float Energy = Self->GetEnergy();
	// 重力：冲刺时 980 × AirBoostGravity（见 GetGravityZ）；喷射净加速度；从现在的下落速度刹住要多远（含按下喷射到起作用的 0.3 秒）
	const float GBoost = FMath::Max(980.f * M->AirBoostGravity, 50.f);
	const float Net = FMath::Max(M->ThrustUpAccel - GBoost, 300.f);
	const float StopDist = Vel.Z < 0.f ? Vel.Z * Vel.Z / (2.f * Net) - Vel.Z * 0.3f : 0.f;
	const FVector Dir0 = InOutMove.GetSafeNormal2D();

	// 最近的落脚点（0.35 秒找一次）：能量多就可以往上找，能量少只找不比自己高的
	WaterHomeClock -= DeltaSeconds;
	if (WaterHomeClock <= 0.f)
	{
		WaterHomeClock = 0.35f;
		bWaterHasHome = W->FindLanding(Feet, Energy > 40.f ? 1500.f : 150.f, 40000.f, WaterHome);
	}

	// ---------------------------------------------------------------- 地面图（L_Nature）：绕着水走
	if (!Flight.bAirMap)
	{
		const float Look = 700.f + HSpeed * 0.75f;
		if (bUnsafeHere)
		{
			// 脚下这一格会下水：被打飞 / 闪出去了，或者站在深一点的浅滩上，不管别的先回岸上
			Out.bUrgent = true;
			if (bWaterHasHome)
			{
				InOutMove = (WaterHome - Me).GetSafeNormal2D();
			}
			Out.bCtrlBoost = true;
			Out.bBoost = true;
			Out.bCtrlThrust = true;
			Out.bThrust = !bGround && Clear - StopDist < 500.f;
			if (Out.bThrust || bGround)
			{
				++WaterStat.Rescues;
			}
			return true;
		}
		if (!Dir0.IsNearlyZero() && !W->IsSafeLine(Me, Dir0, Look))
		{
			FVector D;
			if (WaterSteerGround(W, Me, Dir0, Look, D))
			{
				InOutMove = D;
			}
			else
			{
				InOutMove = bWaterHasHome ? (WaterHome - Me).GetSafeNormal2D() : FVector::ZeroVector;
			}
			++WaterStat.Steers;
		}
		// 朝水的方向跳 / 正在往水那边冲：这段时间不随机跳；前面就是水不冲刺（刹不住）
		const FVector VelDir = HSpeed > 100.f ? Vel.GetSafeNormal2D() : InOutMove.GetSafeNormal2D();
		Out.bNoJump = !W->IsSafeLine(Me, VelDir, 2200.f, 4);
		if (!W->IsSafeLine(Me, VelDir, 300.f + HSpeed * 0.9f, 2))
		{
			Out.bCtrlBoost = true;
			Out.bBoost = false;
		}
		return true;
	}

	// ---------------------------------------------------------------- 空战图（L_Pillars）
	WaterTakeoffCd -= DeltaSeconds;
	WaterThrustMin -= DeltaSeconds;
	const float AirSpeed = M->GliderRules ? M->GliderRules->Cruise * M->GlideSpeedScale : FMath::Max(M->BoostSpeed * 0.85f, 500.f);
	const FVector ToHome = bWaterHasHome ? WaterHome - Feet : FVector::ZeroVector;
	const float DistHome = bWaterHasHome ? ToHome.Size2D() : 40000.f;
	// 能量账：回能（推进时也不停）和平飞的消耗对比。撑得住就只留一点余量，撑不住按飞回去的时间算；要爬升也算上
	const float Regen = M->bRegenWhileFlying ? M->RegenPerSecond * M->AirRegenScale * Self->EffectScale(EMechStat::EnergyRegen) : 0.f;
	const float TimeHome = DistHome / AirSpeed;
	const float RiseHome = FMath::Max(0.f, ToHome.Z + 200.f);
	const float Need = 14.f + FMath::Max(0.f, M->BoostDrain - Regen) * TimeHome + RiseHome / 800.f * FMath::Max(0.f, M->BoostDrain * 2.f - Regen);
	WaterNeed = Need;

	if (bGround)
	{
		bWaterHoming = false;
		// 站在地上：能量够就起飞（这张图飞着打）；只有拿刀贴着地上的对手时才留在地上
		FVector Move = Dir0;
		const float Look = 600.f + HSpeed * 0.7f;
		const bool bEdge = !Dir0.IsNearlyZero() && !W->IsSafeLine(Me, Dir0, Look);
		const bool bHigh = Target && Target->GetActorLocation().Z - Me.Z > 1500.f;
		const bool bWantFly = Target && (!bBlade || Dist > Tier.MeleeKeep * 1.4f || bHigh);
		const bool bReady = Energy >= TakeoffEnergy && !M->bOverheated && M->bCanThrust && WaterTakeoffCd <= 0.f;
		Out.bCtrlThrust = true;
		Out.bThrust = false;
		Out.bCtrlBoost = true;
		Out.bBoost = false;
		if (bUnsafeHere)
		{
			// 站到水下的地形上了（被打下来、从岸边滑下去）：再往里走就落水。有能量就马上飞起来，没有就往最近的落脚点走
			Out.bUrgent = true;
			Move = bWaterHasHome ? (WaterHome - Me).GetSafeNormal2D() : Move;
			if (!M->bOverheated && Energy > 5.f)
			{
				Out.bThrust = true;
				bWaterThrust = true;
				WaterThrustMin = 0.7f;
				Out.bBoost = true;
			}
			++WaterStat.Rescues;
		}
		else if ((bWantFly || bEdge) && bReady)
		{
			// 起飞：按住喷射（先跳一下，0.18 秒后开始喷）
			Out.bThrust = true;
			bWaterThrust = true;
			WaterThrustMin = 0.7f;
			WaterTakeoffCd = FMath::FRandRange(0.5f, 1.5f);
			Out.bBoost = true;
			++WaterStat.Takeoffs;
		}
		else if (bEdge)
		{
			FVector D;
			Move = WaterSteerGround(W, Me, Dir0, Look, D) ? D : FVector::ZeroVector;
			++WaterStat.Steers;
		}
		else if (!Dir0.IsNearlyZero())
		{
			// 岛上照常走（能量够才冲刺），边上别冲
			Out.bBoost = Energy > 45.f && W->IsSafeLine(Me, Dir0, 900.f + HSpeed * 0.9f, 2);
		}
		InOutMove = Move;
		return true;
	}

	// ---- 在空中
	// 脚下是水，或者正朝水面飞、一秒内就到水上（从岛上贴地飞出去的时候不看这一下会直接下水）
	const bool bOverWater = bUnsafeHere || !W->IsSafeStep(Me + Vel.GetSafeNormal2D() * FMath::Min(HSpeed, 3000.f));
	// 快撞水：离水面太近，或者正往下冲（按这个下落速度抬头要掉一段）
	const float DiveLoss = Vel.Z < 0.f ? -Vel.Z * 0.6f : 0.f;
	const bool bDanger = bOverWater && Clear - DiveLoss < DangerClear;
	if (bOverWater && Energy < Need)
	{
		if (!bWaterHoming)
		{
			++WaterStat.Homings;
		}
		bWaterHoming = true;
	}
	else if (bWaterHoming && Energy > Need + 30.f && Clear > 1500.f)
	{
		bWaterHoming = false;
	}
	if (!Target)
	{
		bWaterHoming = true;  // 没有目标（都死了 / 对局结束）：先落地
	}
	if (bDanger)
	{
		++WaterStat.Rescues;
		Out.bUrgent = true;
	}
	// 目标方向：往家飞 / 照脑子里算的
	FVector Dir = Dir0;
	const bool bGoHome = bWaterHoming || bDanger;
	if (bGoHome && bWaterHasHome)
	{
		Dir = ToHome.GetSafeNormal2D();
	}
	// 想飞多高
	float WantZ = (Zone ? WaterZ : Feet.Z) + FMath::Max(WaterCruise, 4500.f);
	if (bGoHome)
	{
		// 回落脚点：水面上方留够高度，快到了才往下落（贴水飞一歪就下去了）
		const float GoHomeFloor = (Zone ? WaterZ : Feet.Z) + (DistHome > 1500.f ? 2000.f : 800.f);
		WantZ = bWaterHasHome ? FMath::Max(WaterHome.Z + 300.f, GoHomeFloor) : WantZ;
	}
	else if (Target)
	{
		const float TZ = Target->GetActorLocation().Z - HalfH;
		// 水面上方留够高度：被打失衡要掉两秒多（约 40 米），飞太低一下就下水了
		WantZ = FMath::Max(TZ + (bBlade ? 150.f : WaterZOffset), (Zone ? WaterZ : Feet.Z) + 4500.f);
	}
	// 前方地形
	const float Look = 900.f + HSpeed * 1.1f;
	if (!Dir.IsNearlyZero())
	{
		for (const float F : {0.3f, 0.6f, 1.f, 1.5f})
		{
			const float T = W->GroundTop(Me + Dir * (Look * F));
			if (T > -1.e8f)
			{
				const float Next = T + TerrainClear;
				if (Next - Feet.Z < MaxClimb)
				{
					WantZ = FMath::Max(WantZ, Next);
				}
			}
		}
		// 绕路：前方爬不过去 / 撞得上（0.12 秒算一次）
		WaterSteerClock -= DeltaSeconds;
		if (WaterSteerClock <= 0.f)
		{
			WaterSteerClock = 0.12f;
			const float Climb = MaxClimb * FMath::Clamp(Energy / 60.f, 0.25f, 1.f);
			WaterSteerDir = Dir;
			if (!WaterAirFree(Self, W, Me, Feet, Dir, Look, Climb))
			{
				bool bFound = false;
				static const float Angles[] = {30.f, 60.f, 90.f, 125.f, 160.f};
				for (const float A : Angles)
				{
					for (const float S : {StrafeSign, -StrafeSign})
					{
						const FVector D = Dir.RotateAngleAxis(A * S, FVector::UpVector);
						if (WaterAirFree(Self, W, Me, Feet, D, Look, Climb))
						{
							WaterSteerDir = D;
							bFound = true;
							break;
						}
					}
					if (bFound)
					{
						break;
					}
				}
				if (!bFound)
				{
					WaterSteerDir = FVector::ZeroVector;  // 四面都堵：原地往上
					WantZ = FMath::Max(WantZ, Feet.Z + 1500.f);
				}
				++WaterStat.Steers;
			}
		}
		Dir = WaterSteerDir;
	}
	// 快到家了：松手落下去
	const bool bLanding = bWaterHoming && !bDanger && bWaterHasHome && DistHome < 450.f;  // 只有真要回去落地才松手；快撞水时不能松（离岸近也不行）
	if (bLanding)
	{
		Dir = FVector::ZeroVector;
		WantZ = WaterHome.Z;
	}
	InOutMove = Dir;
	// 飞行：冲刺一直按着，俯仰按想要的高度算；爬升角有上限，高度差大时机头抬满
	const float Speed3 = FMath::Max(Vel.Size(), 800.f);
	const float Err = WantZ - Feet.Z;
	const float WantVz = bDanger ? 1600.f : FMath::Clamp(Err * 0.7f, -900.f, 1200.f);
	const float PitchDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(WantVz / Speed3, -0.6f, 0.85f)));
	M->bGlideAI = true;
	M->GlideAIPitch = PitchDeg;
	Out.bCtrlThrust = true;
	Out.bThrust = WaterThrustMin > 0.f && !M->bOverheated;  // 只有起飞那一下按喷射
	Out.bCtrlBoost = true;
	Out.bBoost = !bLanding;
	return true;
}

bool AMechAIController::WaterDodgeOk(AMechCharacter* Self, const FVector& Dir) const
{
	UMechWater* W = GetWorld()->GetSubsystem<UMechWater>();
	if (!W || !W->HasWater() || !W->IsGridReady())
	{
		return true;
	}
	const UMechMovementComponent* M = Self->GetMechMovement();
	const FVector Me = Self->GetActorLocation();
	const FVector D = Dir.GetSafeNormal2D();
	if (!MechMapRules::FlightFor(GetWorld()).bAirMap)
	{
		// 地面图：闪出去约 15 米，落点和路上都不能下水
		return W->IsSafeLine(Me, D, 1600.f, 3);
	}
	if (M->IsMovingOnGround())
	{
		return W->IsSafeLine(Me, D, 1400.f, 3) || Self->GetEnergy() > MechAIWater::TakeoffEnergy;
	}
	// 空中：闪避要留够保持飞行的能量
	return Self->GetEnergy() > M->QuickBoostCost + FMath::Max(WaterNeed, MechAIWater::LowEnergy);
}

bool AMechAIController::WaterSwingOk(AMechCharacter* Self, AMechCharacter* Target) const
{
	UMechWater* W = GetWorld()->GetSubsystem<UMechWater>();
	if (!W || !W->HasWater() || !W->IsGridReady() || !Target)
	{
		return true;
	}
	if (!MechMapRules::FlightFor(GetWorld()).bAirMap)
	{
		// 地面图：目标在水面上方不追着砍（刀的突进会把自己送进湖里）
		return W->IsSafeStep(Target->GetActorLocation());
	}
	// 空战图：出刀期间喷不了，脚下是水时要留点能量和高度
	return W->IsSafeStep(Self->GetActorLocation()) || Self->GetEnergy() > 25.f;
}
