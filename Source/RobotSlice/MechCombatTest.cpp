// -MechTTKTest：测试场里量每把枪 / 每把刀的每秒伤害、打穿三种框架要多久、刀把中型框架打进失衡要几秒（10-30 调 TTK 时加的）。
// 用法：UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Range?Range -game -nullrhi -MechTTKTest -abslog=...（不要画面也能跑）
// 可选 -MechTTKOnly=gun / blade 只测一类。日志搜 [ttk]。
// 做法：本机玩家这台由脚本操作——换装，站到最近的木桩前（枪 10 / 25 / 45 米、刀贴身、匕首 15 米），对着木桩胸口开火 / 连砍 10 秒，
// 伤害按服务器结算记（UMechCombatComponent::DebugDamage；打失衡中的目标有 ×1.9 直击加成，另记一份折回去的基础值），然后歇 4.5 秒等木桩回满。

#include "MechCombatComponent.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "MechCharacter.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "MechWeapons.h"

namespace MechCombatTestState  // 不用匿名命名空间：合并编译时别的文件里同名的局部变量（Clock 之类）会和它撞
{
	struct FTTKCase
	{
		bool bGun = true;
		int32 Index = 0;
		float Dist = 1000.f;
	};

	const float SetupTime = 1.2f;
	const float FireTime = 10.f;
	const float RestTime = 4.5f;

	TArray<FTTKCase> Cases;
	float Clock = 0.f;
	int32 Current = -1;
	float CaseStart = 0.f;
	float StaggerAt = -1.f;
	float MashTimer = 0.f;
	int32 LastStep = 0;
	FVector Spot = FVector::ZeroVector;
	bool bLogged = false;
	bool bQuit = false;

	void BuildCases()
	{
		FString Only;
		FParse::Value(FCommandLine::Get(), TEXT("MechTTKOnly="), Only);
		if (Only != TEXT("blade"))
		{
			for (int32 g = 0; g < MechWeapons::NumGuns(); ++g)
			{
				for (const float D : {1000.f, 2500.f, 4500.f})
				{
					Cases.Add({true, g, D});
				}
			}
		}
		if (Only != TEXT("gun"))
		{
			for (int32 b = 0; b < MechWeapons::NumBlades(); ++b)
			{
				Cases.Add({false, b, MechWeapons::Blade(static_cast<EMechMeleeWeapon>(b)).Knives > 0 ? 1500.f : 450.f});
			}
		}
	}
}

void MechCombatTest::Tick(AMechCharacter* Me, float DeltaTime)
{
	using namespace MechCombatTestState;
	if (bQuit)
	{
		return;
	}
	if (Cases.IsEmpty())
	{
		BuildCases();
	}
	Clock += DeltaTime;
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	if (!PC || Clock < 3.f)
	{
		return;  // 等木桩生成
	}
	AMechCharacter* Dummy = nullptr;
	for (TActorIterator<AMechCharacter> It(Me->GetWorld()); It; ++It)
	{
		if (It->IsTrainingDummy() && (!Dummy || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(Dummy->GetActorLocation(), Me->GetActorLocation())))
		{
			Dummy = *It;
		}
	}
	if (!Dummy)
	{
		return;
	}
	UMechCombatComponent* Combat = Me->GetCombat();
	const float Slot = SetupTime + FireTime + RestTime;
	const int32 Index = FMath::FloorToInt((Clock - 3.f) / Slot);
	const float L = Clock - 3.f - Index * Slot;
	if (Index >= Cases.Num())
	{
		Combat->SetTrigger(false);
		UE_LOG(LogTemp, Display, TEXT("[ttk] 全部测完"));
		bQuit = true;
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	const FTTKCase& C = Cases[Index];
	const FString Name = C.bGun ? MechWeapons::Gun(static_cast<EMechShooter>(C.Index)).Name : MechWeapons::Blade(static_cast<EMechMeleeWeapon>(C.Index)).Name;
	if (Index != Current)
	{
		// 新的一项：换装（装甲能量回满），站到木桩正前方
		Current = Index;
		bLogged = false;
		StaggerAt = -1.f;
		LastStep = 0;
		Combat->SetTrigger(false);
		Combat->SetAltHeld(false);
		FMechLoadout Want = Me->GetLoadout();
		if (C.bGun)
		{
			Want.Shooter = static_cast<EMechShooter>(C.Index);
		}
		else
		{
			Want.Melee = static_cast<EMechMeleeWeapon>(C.Index);
		}
		Me->SetLoadout(Want, true);
		const FVector Away = Dummy->GetActorForwardVector().GetSafeNormal2D();
		Spot = Dummy->GetActorLocation() + Away * C.Dist + FVector(0.f, 0.f, 20.f);
		Me->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
	}
	// 姿态：枪 / 刀
	const EMechStance WantStance = C.bGun ? EMechStance::Gun : EMechStance::Blade;
	if (Combat->GetStance() != WantStance && Combat->GetSwitchProgress() < 0.f && !Combat->IsMeleeActive())
	{
		Combat->SwitchStance();
	}
	// 一直对着木桩胸口
	const FVector AimAt = Dummy->GetMechMesh()->GetBoneLocation(TEXT("spine_03"));
	PC->SetControlRotation((AimAt - PC->PlayerCameraManager->GetCameraLocation()).Rotation());
	const bool bFiring = L >= SetupTime && L < SetupTime + FireTime;
	if (L >= SetupTime && L - DeltaTime < SetupTime)
	{
		Combat->DebugDamage = 0.f;
		Combat->DebugBaseDamage = 0.f;
		Combat->DebugPartHits[0] = Combat->DebugPartHits[1] = Combat->DebugPartHits[2] = 0;
		CaseStart = Clock;
	}
	if (bFiring && StaggerAt < 0.f && Dummy->GetHealth()->IsStaggered() && !Dummy->GetHealth()->IsStunned())
	{
		StaggerAt = Clock - CaseStart;
	}
	if (C.bGun)
	{
		const MechWeapons::FGun& G = Combat->GetGun();
		Me->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);  // 站定（闪避、后坐都不让走位）
		Combat->SetTrigger(bFiring);
		Combat->SetAltHeld(bFiring && G.bScopeMagnet);  // 狙击枪开镜（吸附），其他腰射
		// 双持手枪：10-30 晚起闪避不再装满（改成重斩砍中补满），这里只量纯开枪、照常换弹
	}
	else if (bFiring)
	{
		// 刀：狂按轻击打四连，四连打完重斩好了就接重斩；匕首每 0.4 秒投一把（玩家按住走满一圈是 0.4 秒）
		const MechWeapons::FBlade& B = Combat->GetBlade();
		const int32 Step = Combat->GetComboStep();
		if (Step > 0)
		{
			LastStep = Step;
		}
		else if (Combat->IsMeleeActive())
		{
			LastStep = 0;  // 重斩中
		}
		MashTimer -= DeltaTime;
		if (MashTimer <= 0.f)
		{
			MashTimer = B.Knives > 0 ? 0.4f : 0.1f;
			if (B.Knives > 0)
			{
				Combat->RequestLight();
			}
			else if (!Combat->IsMeleeActive() && LastStep == 4 && Combat->GetHeavyReady() >= 1.f)
			{
				Combat->RequestHeavy();
			}
			else
			{
				Combat->RequestLight();
			}
		}
	}
	if (L >= SetupTime + FireTime && !bLogged)
	{
		bLogged = true;
		Combat->SetTrigger(false);
		Combat->SetAltHeld(false);
		const float DPS = Combat->DebugDamage / FireTime;
		const float Base = Combat->DebugBaseDamage / FireTime;
		const float Mid = GetDefault<UMechHealthComponent>()->MaxAP;
		UE_LOG(LogTemp, Display, TEXT("[ttk] %s %s %.0f 米：每秒伤害 %.0f（不算直击加成 %.0f）| 打穿 中型 %.0f 约 %.1f 秒、轻型 7650 约 %.1f 秒、重型 13200 约 %.1f 秒 | 木桩失衡 %s | 部位 躯干 %d 头 %d 四肢 %d"),
			C.bGun ? TEXT("枪") : TEXT("刀"), *Name, C.Dist / 100.f, DPS, Base, Mid, Mid / FMath::Max(Base, 1.f), 7650.f / FMath::Max(Base, 1.f),
			13200.f / FMath::Max(Base, 1.f), StaggerAt >= 0.f ? *FString::Printf(TEXT("第 %.1f 秒"), StaggerAt) : TEXT("没有"),
			Combat->DebugPartHits[0], Combat->DebugPartHits[1], Combat->DebugPartHits[2]);
	}
}
