// -MechHudThemeShots（10-31 HUD 主题）：测试场里同一状态下四套主题（联赛标准 / 新纪 / 维克托 / 相位）轮流截图，拼对比图用。
// 用法：UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechHudThemeShots=day -RenderOffscreen -ForceRes -ResX=1920 -ResY=1080 -windowed -MechHour=13
//      夜里：-MechHudThemeShots=night -MechHour=22。截图在 Saved/Screenshots/HudTheme/<day|night>/<序号>_<状态>_<主题>.png，日志 [hudtheme]，约 22 秒后退出。
// 做法和 -MechAlertTest 一样：挑一根约 35 米外、和自己同高的木桩当敌人，脚本直接调双方的战斗组件（指定目标、开火、开镜、切姿态、锁定），
// 自己的装甲直接在服务器上设。每个状态摆好后，四套主题每 0.3 秒换一套、换完 0.15 秒截一张（主题由 MechHudTheme::SetTestOverride 强制）。
// 状态：0 交战中（对着木桩连射、锁定框在收）/ 1 低装甲告警（20%）/ 2 被锁定（木桩导弹锁上自己）/ 3 开镜（狙击枪）/ 4 近战姿态（10 米内，突进距离弧亮）

#include "MechHudTheme.h"

#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechHUD.h"
#include "MechLoadout.h"

namespace MechHudThemeTestState  // 不用匿名命名空间：合并编译时会和别的测试文件里同名的变量撞
{
	float Clock = 0.f;
	bool bStarted = false;
	bool bQuit = false;
	TWeakObjectPtr<AMechCharacter> DummyRef;
	FVector Away = FVector::ForwardVector;
	FString Tag = TEXT("day");
	int32 Shots = 0;
}

void MechHudThemeTest::Tick(AMechHUD& Hud, AMechCharacter* Me, float Dt)
{
	using namespace MechHudThemeTestState;
	if (bQuit || !Me || !Me->HasAuthority() || Me != Hud.GetOwningPawn())
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	if (!bStarted)
	{
		AMechCharacter* Best = nullptr;
		float BestErr = 1.e9f;
		for (TActorIterator<AMechCharacter> It(Me->GetWorld()); It; ++It)
		{
			if (!It->IsTrainingDummy() || FMath::Abs(It->GetActorLocation().Z - Me->GetActorLocation().Z) > 300.f)
			{
				continue;
			}
			const float Err = FMath::Abs(FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) - 3500.f);
			if (Err < BestErr)
			{
				BestErr = Err;
				Best = *It;
			}
		}
		if (!Best)
		{
			return;
		}
		bStarted = true;
		DummyRef = Best;
		Away = Best->GetActorForwardVector().GetSafeNormal2D();
		FParse::Value(FCommandLine::Get(), TEXT("MechHudThemeShots="), Tag);
		if (Tag.IsEmpty())
		{
			Tag = TEXT("day");
		}
		Clock = -3.f;  // 先等 3 秒：开局加载、着色器编译、上电动画
		UE_LOG(LogTemp, Display, TEXT("[hudtheme] 测试开始（%s）：木桩 %s 离自己 %.0f 米"), *Tag, *Best->DisplayName, FVector::Dist(Best->GetActorLocation(), Me->GetActorLocation()) / 100.f);
	}
	AMechCharacter* D = DummyRef.Get();
	if (!D)
	{
		return;
	}
	const float T0 = Clock;
	Clock += Dt;
	const float T = Clock;
	auto At = [&](float S) { return T0 < S && T >= S; };
	UMechHealthComponent* HP = Me->GetHealth();
	UMechCombatComponent* Mine = Me->GetCombat();
	UMechCombatComponent* Its = D->GetCombat();

	auto StandAt = [&](float Dist) {
		const FVector P = D->GetActorLocation() + Away * Dist + FVector(0.f, 0.f, 20.f);
		Me->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		Me->GetCharacterMovement()->StopMovementImmediately();
		Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw, 0.f));
	};
	auto LookAtDummy = [&](float YawOff, float PitchOff) {
		const FRotator R = (D->GetActorLocation() + FVector(0.f, 0.f, 60.f) - PC->PlayerCameraManager->GetCameraLocation()).Rotation();
		PC->SetControlRotation(FRotator(FMath::Clamp(R.Pitch + PitchOff, -20.f, 10.f), R.Yaw + YawOff, 0.f));
	};
	auto EquipMe = [&](EMechShooter Gun, EMechShoulder Shoulder) {
		FMechLoadout L = Me->GetLoadout();
		L.Shooter = Gun;
		L.Shoulder = Shoulder;
		Me->SetLoadout(L, false);
	};
	auto EquipIt = [&](EMechShooter Gun, EMechShoulder Shoulder) {
		FMechLoadout L = D->GetLoadout();
		L.Shooter = Gun;
		L.Shoulder = Shoulder;
		D->SetLoadout(L, true);
	};
	auto MaxAP = [&]() { return HP->GetAP() / FMath::Max(HP->GetAPFraction(), 0.001f); };
	// 一个状态从 S 秒起拍四套：每 Every 秒换一套主题（同时调 OnSwitch 重摆状态），换完 Delay 秒截图
	auto Cycle = [&](float S, int32 Index, const TCHAR* Name, float Every = 0.3f, float Delay = 0.15f, TFunction<void(int32)> OnSwitch = nullptr) {
		for (int32 k = 0; k < 4; ++k)
		{
			const float At0 = S + k * Every;
			if (At(At0))
			{
				MechHudTheme::SetTestOverride(k);
				if (OnSwitch)
				{
					OnSwitch(k);
				}
			}
			if (At(At0 + Delay))
			{
				const TCHAR* Code = MechHudTheme::Code(static_cast<EMechHudThemeId>(k));
				const FString File = FPaths::ProjectSavedDir() / TEXT("Screenshots/HudTheme") / Tag / FString::Printf(TEXT("%d_%s_%s.png"), Index, Name, Code);
				FScreenshotRequest::RequestScreenshot(File, true, false);
				++Shots;
				UE_LOG(LogTemp, Display, TEXT("[hudtheme] %5.2f 截图 %d_%s_%s（装甲 %.0f%% 姿态 %d 开镜 %d 目标 %s）"), T, Index, Name, Code, HP->GetAPFraction() * 100.f,
					static_cast<int32>(Mine->GetStance()), Mine->IsAiming() ? 1 : 0, Mine->GetTarget() ? *Mine->GetTarget()->DisplayName : TEXT("-"));
			}
		}
	};

	// ---- 0 交战中：对着木桩连射（命中提示、飘字、弹药在掉），自己的导弹锁定框在收
	if (At(0.1f))
	{
		EquipMe(EMechShooter::Rifle, EMechShoulder::Missiles);
		StandAt(3500.f);
		Mine->SetForcedTarget(D);
	}
	if (T > 0.1f && T < 2.8f)
	{
		LookAtDummy(0.f, 0.f);
	}
	if (At(0.5f))
	{
		Mine->SetTrigger(true);
	}
	Cycle(1.3f, 0, TEXT("交战中"));
	if (At(2.6f))
	{
		Mine->SetTrigger(false);
	}
	// ---- 1 低装甲告警：装甲打到 20%（冲击 0：不抖、不积累失衡）。四边暗红呼吸跟着告警音一下一下、响过三轮还会变暗，
	//      所以每套主题都先回满再打一次（告警从头响起），隔 1.3 秒（受击方向弧淡掉、呼吸在同一个相位）再拍
	if (At(3.f))
	{
		Mine->SetForcedTarget(nullptr);
	}
	if (T > 3.f && T < 10.f)
	{
		LookAtDummy(18.f, 0.f);
	}
	if (At(3.f + 1.7f * 3.f + 1.6f))
	{
		HP->Heal(1.e6f);
	}
	Cycle(3.f, 1, TEXT("低装甲告警"), 1.7f, 1.3f, [&](int32) {
		HP->Heal(1.e6f);
		HP->ApplyHit(HP->GetAP() - 0.2f * MaxAP(), 0.f, D, Me->GetActorLocation() + FVector(0.f, 0.f, 60.f), 0.f);
	});
	// 后面三段整体往后挪 5.1 秒（原来低装甲只占 1.4 秒）
	const float O = 5.1f;
	// ---- 2 被锁定：木桩换导弹、锁自己（锁上后告警灯 LCK 红、木桩身上套菱形）
	if (At(O + 5.8f))
	{
		HP->Heal(1.e6f);
		EquipIt(EMechShooter::Rifle, EMechShoulder::Missiles);
		Its->SetForcedTarget(Me);
	}
	if (T > O + 5.8f && T < O + 8.9f)
	{
		LookAtDummy(-12.f, 0.f);
	}
	Cycle(O + 7.6f, 2, TEXT("被锁定"));
	// ---- 3 开镜：自己换狙击枪、按住右键
	if (At(O + 9.f))
	{
		Its->SetForcedTarget(nullptr);
		EquipMe(EMechShooter::Sniper, EMechShoulder::Missiles);
	}
	if (At(O + 9.3f))
	{
		Mine->SetAltHeld(true);
	}
	if (T > O + 9.f && T < O + 11.f)
	{
		LookAtDummy(0.f, 0.f);
	}
	Cycle(O + 10.f, 3, TEXT("开镜"));
	// ---- 4 近战姿态：10 米内（突进距离弧亮），指定木桩为目标
	if (At(O + 11.2f))
	{
		Mine->SetAltHeld(false);
		EquipMe(EMechShooter::Rifle, EMechShoulder::Missiles);
		StandAt(1000.f);
		Mine->SetForcedTarget(D);
	}
	if (At(O + 11.4f) && Mine->GetStance() != EMechStance::Blade)
	{
		Mine->SwitchStance();
	}
	if (T > O + 11.2f && T < O + 14.f)
	{
		LookAtDummy(0.f, -4.f);
	}
	Cycle(O + 12.6f, 4, TEXT("近战姿态"));
	if (At(O + 14.6f))
	{
		MechHudTheme::SetTestOverride(-1);
		UE_LOG(LogTemp, Display, TEXT("[hudtheme] 拍完 %d 张（%s）"), Shots, *Tag);
		bQuit = true;
		PC->ConsoleCommand(TEXT("quit"));
	}
}
