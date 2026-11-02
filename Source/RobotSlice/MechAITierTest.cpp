// -MechAITierTest（11-01 人机强度）。用户：「四个档位也没有什么区别」——先量化再调，以后改 MechAIController.cpp 开头的 MechAITier 表就跑这个。
// 用法（不要画面，定帧跑得比实时快）：
//   UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Dreamfield?Range -game -nullrhi -unattended -nosplash -nosound -BENCHMARK -FPS=60 -MechAITierTest -abslog=...
// （用开阔的 L_Dreamfield；L_Range 障碍多，看得见目标的时间短、结果抖得厉害。定帧 60 约 5 倍速，全量约 6 分钟）
// 日志搜 [aitier]，最后几行是汇总表，跑完自动退出。可选：
//   -MechAITierLegacy        跑调整前的数值（对比用，四档都走旧表）
//   -MechAITierOnly=target   只打移动靶；-MechAITierOnly=duel 只跑 AI 对 AI
//   -MechAITierLevels=03     只量这几档（移动靶）
//   -MechAITierSecs=30       每轮打多少秒（默认 30）
//   -MechAITierReps=2        每档枪、刀各几轮（默认 2，每轮新抽性格）
//   -MechAITierRounds=6      AI 对 AI 每对打几局（默认 6）
// 做法：
//  一、移动靶：测试场里去掉木桩、把玩家挪到天上，刷一台靶机（中型，步枪 + 双手刀，没人操控但有控制器）按固定矩形路线走，每 8 秒冲刺 3 秒，
//      每 2.6 秒左右交替闪避一次，能量不耗尽，不会失衡（失衡了站着不动就不算移动靶了），刀的那几轮靶机每 3.2 秒砍一刀（看 AI 格挡、弹反）。
//      再刷一台指定档位的 AI（步枪 + 双手刀，拿枪 / 拿刀锁死，分开量）打它固定时间。打穿（装甲见底）就回满再计。
//      枪命中率 = 打中发数 / 打出发数；近战命中率 = 砍中过人的刀数 / 出刀数；打穿时间 = 中型装甲 / 每秒伤害。
//  二、AI 对 AI：一对档位同配装（每局换一套枪和刀）对打，打死为止，90 秒没分出来按剩余装甲比例判。

#include "MechAITierTest.h"

#include "AIController.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "MechAIController.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechMovementComponent.h"
#include "MechWeapons.h"
#include "Misc/CommandLine.h"

namespace MechAITierState  // 不用匿名命名空间：合并编译时别的文件里同名的局部变量会和它撞
{
	const TCHAR* const TierNames[4] = {TEXT("新兵"), TEXT("普通"), TEXT("老练"), TEXT("王牌")};

	struct FAcc
	{
		float Time = 0.f;
		float See = 0.f;
		float DistSum = 0.f;
		int32 Frames = 0;
		float Damage = 0.f;
		int32 Shots = 0;
		int32 Hits = 0;
		int32 Swings = 0;
		int32 SwingHits = 0;
		int32 Dodges = 0;
		int32 Guards = 0;
		int32 ParryTries = 0;
		int32 Whiffs = 0;
		int32 NoLock = 0;
		int32 Idles = 0;
		int32 Shoulders = 0;
		int32 Breaks = 0;
		int32 Parried = 0;
		int32 Clashes = 0;
		int32 TgtSwings = 0;
		int32 TgtHits = 0;
	};

	struct FCase
	{
		int32 Tier = 0;
		bool bBlade = false;
	};
	struct FPair
	{
		int32 Hi = 3;
		int32 Lo = 0;
		int32 HiWins = 0;
		int32 LoWins = 0;
		int32 TimeoutRounds = 0;
		float TimeSum = 0.f;
		int32 Done = 0;
	};

	FAcc Results[4][2];  // [档位][0 枪 / 1 刀]
	TArray<FCase> Cases;
	TArray<FPair> Pairs;

	float Secs = 30.f;
	int32 Reps = 2;
	int32 Rounds = 6;
	bool bLegacy = false;
	const float Warm = 3.f;       // 每轮开头多久不计（等拔刀 / 换枪）
	const float DuelLimit = 90.f;

	bool bReady = false;
	bool bFinished = false;
	float Clock = 0.f;
	float CaseClock = 0.f;
	int32 CaseIndex = -1;
	int32 PairIndex = 0;
	int32 RoundIndex = -1;
	FVector Center = FVector::ZeroVector;
	float BuiltAt = 0.f;

	TWeakObjectPtr<AMechCharacter> Bot;
	TWeakObjectPtr<AMechCharacter> Dummy;
	TWeakObjectPtr<AMechCharacter> DuelA;
	TWeakObjectPtr<AMechCharacter> DuelB;

	// 靶机路线和节奏
	int32 WayIndex = 0;
	float DodgeClock = 0.f;
	float DodgeSide = 1.f;
	float TargetSwingClock = 0.f;
	float RouteClock = 0.f;

	// 这一轮的轮询状态
	int32 LastAmmo = 0;
	int32 LastHits = 0;
	float LastDamage = 0.f;
	bool bWasMelee = false;
	float LastMeleeTime = 0.f;
	bool bSwingHit = false;
	float LastBlockAt = -1000.f;
	int32 LastClashes = 0;
	bool bTgtWasMelee = false;
	float TgtLastMeleeTime = 0.f;
	float TgtLastDamage = 0.f;
	bool bTgtSwingHit = false;

	FVector GroundAt(UWorld* World, const FVector& P)
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechAITierGround), false);
		if (World->LineTraceSingleByObjectType(Hit, P + FVector(0.f, 0.f, 3000.f), P - FVector(0.f, 0.f, 6000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
		{
			return Hit.ImpactPoint;
		}
		return P;
	}

	AMechCharacter* SpawnMech(UWorld* World, int32 Team, const FVector& Foot, float Yaw, const FString& Name, bool bAI, const FMechLoadout& L)
	{
		const FTransform At(FRotator(0.f, Yaw, 0.f), Foot + FVector(0.f, 0.f, 96.f * 1.35f + 30.f));
		AMechCharacter* E = World->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), At, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!E)
		{
			return nullptr;
		}
		if (!bAI)
		{
			E->AutoPossessAI = EAutoPossessAI::Disabled;
		}
		E->SetTeam(Team);
		E->DisplayName = Name;
		E->FinishSpawning(At);
		E->SetLoadout(L, true);
		if (bAI)
		{
			if (AMechAIController* AI = Cast<AMechAIController>(E->GetController()))
			{
				AI->ArenaCenter = Center;
				AI->ArenaExtent = FVector2D(9000.f, 9000.f);
			}
		}
		else
		{
			AAIController* C = World->SpawnActor<AAIController>();
			C->bSetControlRotationFromPawnOrientation = false;
			C->Possess(E);
		}
		return E;
	}

	void KillMech(TWeakObjectPtr<AMechCharacter>& M)
	{
		if (AMechCharacter* E = M.Get())
		{
			if (AController* C = E->GetController())
			{
				C->Destroy();
			}
			E->Destroy();
		}
		M = nullptr;
	}

	/** 靶机走固定路线：矩形四个角（相对场地中心），每 8 秒冲刺 3 秒，每 2.6 秒左右交替闪避 */
	void DriveDummy(AMechCharacter* T, AMechCharacter* Foe, bool bBladeCase, float Dt)
	{
		static const FVector2D Way[4] = {{-2200.f, -1300.f}, {2200.f, -1300.f}, {2200.f, 1300.f}, {-2200.f, 1300.f}};
		RouteClock += Dt;
		UMechMovementComponent* Move = T->GetMechMovement();
		Move->Energy = Move->MaxEnergy;
		const FVector Goal = Center + FVector(Way[WayIndex].X, Way[WayIndex].Y, 0.f);
		FVector Dir = (Goal - T->GetActorLocation()).GetSafeNormal2D();
		if (FVector::Dist2D(T->GetActorLocation(), Goal) < 350.f)
		{
			WayIndex = (WayIndex + 1) % 4;
		}
		T->AddMovementInput(Dir, 1.f);
		T->SetBoostHeld(FMath::Fmod(RouteClock, 8.f) < 3.f);
		DodgeClock -= Dt;
		if (DodgeClock <= 0.f)
		{
			DodgeClock = 2.6f;
			DodgeSide = -DodgeSide;
			T->QuickBoostToward(FVector::CrossProduct(FVector::UpVector, Dir) * DodgeSide);
		}
		UMechCombatComponent* Combat = T->GetCombat();
		Combat->SetForcedTarget(Foe);
		if (AController* C = T->GetController())
		{
			if (Foe)
			{
				C->SetControlRotation((Foe->GetActorLocation() - T->GetActorLocation()).Rotation());
			}
		}
		const EMechStance Want = bBladeCase ? EMechStance::Blade : EMechStance::Gun;
		if (Combat->GetStance() != Want && Combat->GetSwitchProgress() < 0.f && !Combat->IsMeleeActive())
		{
			Combat->SwitchStance();
		}
		// 刀的那几轮：靶机隔 3.2 秒砍一刀（够得着才砍），让 AI 有东西可挡
		if (bBladeCase)
		{
			TargetSwingClock -= Dt;
			if (TargetSwingClock <= 0.f && Foe && FVector::Dist(Foe->GetActorLocation(), T->GetActorLocation()) < 900.f)
			{
				TargetSwingClock = 3.2f;
				Combat->RequestLight();
			}
		}
	}

	void StartCase(UWorld* World, const FCase& C)
	{
		KillMech(Bot);
		AMechCharacter* T = Dummy.Get();
		if (!T)
		{
			FMechLoadout L;
			L.Frame = EMechFrame::Medium;
			L.Shooter = EMechShooter::Rifle;
			L.Melee = EMechMeleeWeapon::Katana;
			L.Shoulder = EMechShoulder::Shield;
			T = SpawnMech(World, 1, GroundAt(World, Center + FVector(-2200.f, -1300.f, 0.f)), 0.f, TEXT("移动靶"), false, L);
			Dummy = T;
			// 不死、不失衡（一直在动才算移动靶）
			UMechHealthComponent* H = T->GetHealth();
			H->bNoDeath = true;
			H->SetMaxValues(H->MaxAP, 1.e8f, true);
		}
		T->GetHealth()->ResetHealth();
		T->GetCombat()->InterruptMelee();
		WayIndex = 0;
		RouteClock = 0.f;
		DodgeClock = 2.6f;
		TargetSwingClock = 3.2f;
		T->TeleportTo(GroundAt(World, Center + FVector(-2200.f, -1300.f, 0.f)) + FVector(0.f, 0.f, 96.f * 1.35f + 30.f), FRotator::ZeroRotator);

		FMechLoadout L;
		L.Frame = EMechFrame::Medium;
		L.Shooter = EMechShooter::Rifle;
		L.Melee = EMechMeleeWeapon::Katana;
		L.Shoulder = C.bBlade ? EMechShoulder::Shield : EMechShoulder::Missiles;  // 刀的那几轮别让导弹把伤害算到刀上
		AMechCharacter* B = SpawnMech(World, 0, GroundAt(World, Center + FVector(0.f, 3000.f, 0.f)), -90.f,
			FString::Printf(TEXT("%s AI"), TierNames[C.Tier]), true, L);
		Bot = B;
		AMechAIController* AI = Cast<AMechAIController>(B->GetController());
		AI->ApplyTier(C.Tier, bLegacy);
		AI->TestStance = C.bBlade ? 1 : 0;
		B->GetHealth()->bNoDeath = true;
		CaseClock = 0.f;
		UMechCombatComponent* BC = B->GetCombat();
		LastAmmo = BC->GetAmmo();
		LastHits = 0;
		LastDamage = 0.f;
		bWasMelee = false;
		LastMeleeTime = 0.f;
		bSwingHit = false;
		LastBlockAt = -1000.f;
		LastClashes = 0;
		bTgtWasMelee = false;
		TgtLastMeleeTime = 0.f;
		TgtLastDamage = T->GetCombat()->DebugDamage;
		bTgtSwingHit = false;
	}

	void SampleCase(UWorld* World, const FCase& C, float Dt)
	{
		AMechCharacter* B = Bot.Get();
		AMechCharacter* T = Dummy.Get();
		if (!B || !T)
		{
			return;
		}
		DriveDummy(T, B, C.bBlade, Dt);
		UMechCombatComponent* BC = B->GetCombat();
		UMechCombatComponent* TC = T->GetCombat();
		AMechAIController* AI = Cast<AMechAIController>(B->GetController());
		// 打穿了（装甲见底）：回满接着打；AI 自己也别被打残（测试里它不死）
		if (T->GetHealth()->GetAP() <= 1.5f)
		{
			if (CaseClock >= Warm)
			{
				++Results[C.Tier][C.bBlade ? 1 : 0].Breaks;
			}
			T->GetHealth()->ResetHealth();
		}
		if (B->GetHealth()->GetAPFraction() < 0.2f)
		{
			B->GetHealth()->ResetHealth();
		}
		CaseClock += Dt;
		const bool bCount = CaseClock >= Warm;
		if (CaseClock >= Warm && CaseClock - Dt < Warm)
		{
			// 开始计：所有累计清零
			BC->DebugDamage = 0.f;
			BC->DebugBaseDamage = 0.f;
			BC->DebugPartHits[0] = BC->DebugPartHits[1] = BC->DebugPartHits[2] = 0;
			AI->TierStats = AMechAIController::FTierStats();
			LastAmmo = BC->GetAmmo();
			LastHits = 0;
			LastDamage = 0.f;
			LastClashes = BC->DebugClashes;
			TgtLastDamage = TC->DebugDamage;
		}
		if (!bCount)
		{
			bWasMelee = BC->IsMeleeActive();
			LastMeleeTime = BC->GetMeleeTime();
			bTgtWasMelee = TC->IsMeleeActive();
			TgtLastMeleeTime = TC->GetMeleeTime();
			return;
		}
		FAcc& A = Results[C.Tier][C.bBlade ? 1 : 0];
		A.Time += Dt;
		A.Frames += 1;
		A.DistSum += FVector::Dist(B->GetActorLocation(), T->GetActorLocation());
		{
			FHitResult Hit;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(MechAITierSee), false, B);
			Q.AddIgnoredActor(T);
			if (!World->LineTraceSingleByObjectType(Hit, B->GetActorLocation() + FVector(0.f, 0.f, 80.f), T->GetActorLocation() + FVector(0.f, 0.f, 40.f),
				FCollisionObjectQueryParams(ECC_WorldStatic), Q))
			{
				A.See += Dt;
			}
		}
		// 枪：弹匣每少一发算打出一发；打中按服务器结算的部位计数
		const int32 Ammo = BC->GetAmmo();
		if (Ammo < LastAmmo)
		{
			A.Shots += LastAmmo - Ammo;
		}
		LastAmmo = Ammo;
		const int32 Hits = BC->DebugPartHits[0] + BC->DebugPartHits[1] + BC->DebugPartHits[2];
		A.Hits += Hits - LastHits;
		LastHits = Hits;
		// 刀：招式时间倒回去 = 新的一刀；这一刀期间打出了伤害 = 砍中
		const bool bMelee = BC->IsMeleeActive();
		const float MT = BC->GetMeleeTime();
		if (bMelee && (!bWasMelee || MT < LastMeleeTime - 1.e-4f))
		{
			++A.Swings;
			bSwingHit = false;
		}
		const float Dmg = BC->DebugDamage;
		if (Dmg > LastDamage && bMelee && !bSwingHit)
		{
			bSwingHit = true;
			++A.SwingHits;
		}
		A.Damage += Dmg - LastDamage;
		LastDamage = Dmg;
		bWasMelee = bMelee;
		LastMeleeTime = MT;
		// AI 挡下 / 弹开靶机的刀
		const float BlockAt = World->GetTimeSeconds() - BC->GetBlockFxAge();
		if (BlockAt > LastBlockAt + 0.01f && BC->GetBlockFxAge() < 3.f && BC->IsBlockFxDefender())
		{
			A.Parried += BC->GetBlockFxKind() == 1 ? 1 : 0;
		}
		LastBlockAt = BlockAt;
		A.Clashes += BC->DebugClashes - LastClashes;
		LastClashes = BC->DebugClashes;
		// 靶机的刀：出了几刀、几刀砍到 AI 身上（AI 格挡了算没砍到）
		const bool bTgtMelee = TC->IsMeleeActive();
		const float TMT = TC->GetMeleeTime();
		if (bTgtMelee && (!bTgtWasMelee || TMT < TgtLastMeleeTime - 1.e-4f))
		{
			++A.TgtSwings;
			bTgtSwingHit = false;
		}
		if (TC->DebugDamage > TgtLastDamage && bTgtMelee && !bTgtSwingHit)
		{
			bTgtSwingHit = true;
			++A.TgtHits;
		}
		TgtLastDamage = TC->DebugDamage;
		bTgtWasMelee = bTgtMelee;
		TgtLastMeleeTime = TMT;
		if (CaseClock >= Warm + Secs)
		{
			const AMechAIController::FTierStats& S = AI->TierStats;
			A.Dodges += S.Dodges;
			A.Guards += S.Guards;
			A.ParryTries += S.ParryTries;
			A.Whiffs += S.Whiffs;
			A.NoLock += S.NoLock;
			A.Idles += S.Idles;
			A.Shoulders += S.Shoulders;
		}
	}

	void PrintTable()
	{
		const float MidAP = GetDefault<UMechHealthComponent>()->MaxAP;
		UE_LOG(LogTemp, Display, TEXT("[aitier] ==== 汇总（%s，中型装甲 %.0f）===="), bLegacy ? TEXT("调整前的数值") : TEXT("现在的表"), MidAP);
		for (int32 i = 0; i < 4; ++i)
		{
			const FAcc& G = Results[i][0];
			const FAcc& B = Results[i][1];
			if (G.Frames > 0)
			{
				const float Min = G.Time / 60.f;
				UE_LOG(LogTemp, Display, TEXT("[aitier] %s 枪：命中率 %.0f%%（%d/%d 发）| 每秒伤害 %.0f | 打穿中型 %.1f 秒（实际打穿 %d 次）| 闪避 %.1f 次/分 | 肩部 %.1f 次/分 | 发呆 %.1f 次/分 | 平均距离 %.0f 米 看得见 %.0f%%"),
					TierNames[i], 100.f * G.Hits / FMath::Max(1, G.Shots), G.Hits, G.Shots, G.Damage / FMath::Max(G.Time, 1.f), MidAP / FMath::Max(G.Damage / FMath::Max(G.Time, 1.f), 1.f), G.Breaks,
					G.Dodges / FMath::Max(Min, 0.01f), G.Shoulders / FMath::Max(Min, 0.01f), G.Idles / FMath::Max(Min, 0.01f), G.DistSum / FMath::Max(1, G.Frames) / 100.f, 100.f * G.See / FMath::Max(G.Time, 1.f));
			}
			if (B.Frames > 0)
			{
				const float Min = B.Time / 60.f;
				UE_LOG(LogTemp, Display, TEXT("[aitier] %s 刀：命中率 %.0f%%（%d/%d 刀）出刀 %.1f 次/分（其中冲动 %d、没对准 %d）| 每秒伤害 %.0f | 打穿中型 %.1f 秒（实际打穿 %d 次）| 闪避 %.1f 次/分 | 格挡决定 %d 次（卡弹反 %d）| 靶机出刀 %d：弹反 %d 拼刀或格挡弹开 %d 打到 %d（含被挡掉血）| 发呆 %.1f 次/分 | 平均距离 %.0f 米"),
					TierNames[i], 100.f * B.SwingHits / FMath::Max(1, B.Swings), B.SwingHits, B.Swings, B.Swings / FMath::Max(Min, 0.01f), B.Whiffs, B.NoLock,
					B.Damage / FMath::Max(B.Time, 1.f), MidAP / FMath::Max(B.Damage / FMath::Max(B.Time, 1.f), 1.f), B.Breaks, B.Dodges / FMath::Max(Min, 0.01f), B.Guards, B.ParryTries,
					B.TgtSwings, B.Parried, B.Clashes, B.TgtHits, B.Idles / FMath::Max(Min, 0.01f), B.DistSum / FMath::Max(1, B.Frames) / 100.f);
			}
		}
		for (const FPair& P : Pairs)
		{
			if (P.Done > 0)
			{
				UE_LOG(LogTemp, Display, TEXT("[aitier] 对战 %s 对 %s：%d 胜 %d 负（%d 局，其中 %d 局超时按血量判，平均 %.0f 秒）"),
					TierNames[P.Hi], TierNames[P.Lo], P.HiWins, P.LoWins, P.Done, P.TimeoutRounds, P.TimeSum / P.Done);
			}
		}
	}

	void StartRound(UWorld* World)
	{
		KillMech(DuelA);
		KillMech(DuelB);
		const FPair& P = Pairs[PairIndex];
		// 同配装，每局换一套（枪和刀轮着来）；肩部用导弹（双方一样）
		static const EMechShooter Guns[] = {EMechShooter::Rifle, EMechShooter::SMG, EMechShooter::Shotgun, EMechShooter::Marksman, EMechShooter::Sniper, EMechShooter::DualPistol};
		static const EMechMeleeWeapon Blades[] = {EMechMeleeWeapon::Katana, EMechMeleeWeapon::QuickBlade, EMechMeleeWeapon::GreatSword, EMechMeleeWeapon::OneHand, EMechMeleeWeapon::TwinBlades, EMechMeleeWeapon::Blunt};
		FMechLoadout L;
		L.Frame = EMechFrame::Medium;
		L.Shooter = Guns[RoundIndex % UE_ARRAY_COUNT(Guns)];
		L.Melee = Blades[RoundIndex % UE_ARRAY_COUNT(Blades)];
		L.Shoulder = EMechShoulder::Missiles;
		AMechCharacter* A = SpawnMech(World, 0, GroundAt(World, Center + FVector(0.f, -3000.f, 0.f)), 90.f, FString::Printf(TEXT("%s A"), TierNames[P.Hi]), true, L);
		AMechCharacter* B = SpawnMech(World, 1, GroundAt(World, Center + FVector(0.f, 3000.f, 0.f)), -90.f, FString::Printf(TEXT("%s B"), TierNames[P.Lo]), true, L);
		Cast<AMechAIController>(A->GetController())->ApplyTier(P.Hi, bLegacy);
		Cast<AMechAIController>(B->GetController())->ApplyTier(P.Lo, bLegacy);
		DuelA = A;
		DuelB = B;
		CaseClock = 0.f;
	}

	void TickDuel(UWorld* World, float Dt)
	{
		if (!DuelA.IsValid() || !DuelB.IsValid())
		{
			StartRound(World);
			return;
		}
		CaseClock += Dt;
		AMechCharacter* A = DuelA.Get();
		AMechCharacter* B = DuelB.Get();
		const bool bADead = A->GetHealth()->IsDead();
		const bool bBDead = B->GetHealth()->IsDead();
		if (!bADead && !bBDead && CaseClock < DuelLimit)
		{
			return;
		}
		FPair& P = Pairs[PairIndex];
		const bool bTimeout = !bADead && !bBDead;
		const bool bHiWin = bBDead || (bTimeout && A->GetHealth()->GetAPFraction() >= B->GetHealth()->GetAPFraction());
		P.HiWins += bHiWin ? 1 : 0;
		P.LoWins += bHiWin ? 0 : 1;
		P.TimeoutRounds += bTimeout ? 1 : 0;
		P.TimeSum += CaseClock;
		P.Done += 1;
		UE_LOG(LogTemp, Display, TEXT("[aitier] 对战 %s 对 %s 第 %d 局（%s + %s）：%s 胜，%.0f 秒%s，剩余装甲 %.0f%% / %.0f%%"), TierNames[P.Hi], TierNames[P.Lo], RoundIndex + 1,
			MechWeapons::Gun(A->GetLoadout().Shooter).Name, MechWeapons::Blade(A->GetLoadout().Melee).Name, bHiWin ? TierNames[P.Hi] : TierNames[P.Lo], CaseClock, bTimeout ? TEXT("（超时按血量判）") : TEXT(""),
			100.f * A->GetHealth()->GetAPFraction(), 100.f * B->GetHealth()->GetAPFraction());
		KillMech(DuelA);
		KillMech(DuelB);
		++RoundIndex;
		if (RoundIndex >= Rounds)
		{
			RoundIndex = 0;
			++PairIndex;
		}
		if (PairIndex < Pairs.Num())
		{
			StartRound(World);
		}
	}
}

bool UMechAITierTest::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("MechAITierTest"));
}

bool UMechAITierTest::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMechAITierTest::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMechAITierTest, STATGROUP_Tickables);
}

void UMechAITierTest::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	using namespace MechAITierState;
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay() || bFinished)
	{
		return;
	}
	Clock += DeltaTime;
	APlayerController* PC = World->GetFirstPlayerController();
	AMechCharacter* Me = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	if (!bReady)
	{
		if (Clock < 3.f || !Me)
		{
			return;  // 等测试场把玩家和木桩刷好
		}
		bReady = true;
		BuiltAt = FPlatformTime::Seconds();
		bLegacy = FParse::Param(FCommandLine::Get(), TEXT("MechAITierLegacy"));
		FParse::Value(FCommandLine::Get(), TEXT("MechAITierSecs="), Secs);
		FParse::Value(FCommandLine::Get(), TEXT("MechAITierReps="), Reps);
		FParse::Value(FCommandLine::Get(), TEXT("MechAITierRounds="), Rounds);
		FString Only;
		FParse::Value(FCommandLine::Get(), TEXT("MechAITierOnly="), Only);
		FString Levels = TEXT("0123");
		FParse::Value(FCommandLine::Get(), TEXT("MechAITierLevels="), Levels);
		if (Only != TEXT("duel"))
		{
			for (int32 i = 0; i < 4; ++i)
			{
				if (!Levels.Contains(FString::FromInt(i)))
				{
					continue;
				}
				for (int32 r = 0; r < Reps; ++r)
				{
					Cases.Add({i, false});
				}
				for (int32 r = 0; r < Reps; ++r)
				{
					Cases.Add({i, true});
				}
			}
		}
		if (Only != TEXT("target"))
		{
			Pairs.Add({3, 0});
			Pairs.Add({2, 1});
			Pairs.Add({3, 2});
			Pairs.Add({2, 0});
			Pairs.Add({1, 0});
		}
		// 去掉测试场的木桩；玩家挪到天上不动（AI 只挑最近的敌人，不会去打它）
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (It->IsTrainingDummy())
			{
				It->Destroy();
			}
		}
		Center = GroundAt(World, Me->GetActorLocation() + Me->GetActorForwardVector().GetSafeNormal2D() * 5000.f);
		Me->GetHealth()->bNoDeath = true;
		Me->GetCharacterMovement()->DisableMovement();
		Me->SetActorLocation(Center + FVector(0.f, 0.f, 60000.f), false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Display, TEXT("[aitier] 开始（%s）：移动靶 %d 轮、每轮 %.0f 秒；AI 对 AI %d 对、每对 %d 局"), bLegacy ? TEXT("调整前的数值") : TEXT("现在的表"), Cases.Num(), Secs, Pairs.Num(), Rounds);
	}
	if (Me)
	{
		Me->GetMechMovement()->Energy = Me->GetMechMovement()->MaxEnergy;
	}
	// ---- 移动靶
	if (CaseIndex < Cases.Num())
	{
		if (CaseIndex < 0)
		{
			CaseIndex = 0;
			if (Cases.Num() > 0)
			{
				StartCase(World, Cases[0]);
				return;
			}
		}
		if (CaseIndex < Cases.Num())
		{
			const FCase C = Cases[CaseIndex];
			SampleCase(World, C, DeltaTime);
			if (CaseClock >= Warm + Secs)
			{
				const FAcc& A = Results[C.Tier][C.bBlade ? 1 : 0];
				UE_LOG(LogTemp, Display, TEXT("[aitier] 第 %d/%d 轮完（%s %s）：累计 枪 %d/%d 刀 %d/%d 伤害 %.0f（真实耗时 %.0f 秒）"), CaseIndex + 1, Cases.Num(), TierNames[C.Tier], C.bBlade ? TEXT("刀") : TEXT("枪"),
					A.Hits, A.Shots, A.SwingHits, A.Swings, A.Damage, FPlatformTime::Seconds() - BuiltAt);
				++CaseIndex;
				if (CaseIndex < Cases.Num())
				{
					StartCase(World, Cases[CaseIndex]);
				}
				else
				{
					KillMech(Bot);
					KillMech(Dummy);
					PrintTable();
				}
			}
			return;
		}
	}
	// ---- AI 对 AI
	if (PairIndex < Pairs.Num())
	{
		if (RoundIndex < 0)
		{
			RoundIndex = 0;
		}
		TickDuel(World, DeltaTime);
		if (PairIndex < Pairs.Num())
		{
			return;
		}
	}
	PrintTable();
	UE_LOG(LogTemp, Display, TEXT("[aitier] 全部测完（真实耗时 %.0f 秒）"), FPlatformTime::Seconds() - BuiltAt);
	bFinished = true;
	if (PC)
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
}
