// -MechAlertTest（10-31 战机式告警）：测试场里本机玩家那台挨个触发各项告警，每项截图到 Saved/Screenshots/Alerts，日志搜 [alert]，约 48 秒后退出。
// 用法：UnrealEditor.exe RobotSlice.uproject "/Game/Maps/L_Range?Range" -game -MechAlertTest -RenderOffscreen -ForceRes -ResX=1600 -ResY=900 -windowed -MechHour=13
// 做法和 -MechDuelTest 一样：挑一根离出生点约 40 米的木桩当「敌人」，脚本直接调它的组件（换配装、指定目标、发导弹、钩爪、充能、开镜），
// 自己身上的状态（低装甲、推进耗尽、失衡、网、EMP、重击、击毁）直接在服务器上设。
// 只查「该响的响了、图形出来了、不崩」：每张截图时记一行当前循环告警和告警灯，最后汇总每个声音响了几次。手感和音量由用户试玩判断。

#include "MechHudAlerts.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechHUD.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "MechShoulderComponent.h"

namespace MechAlertTestState  // 不用匿名命名空间：合并编译时会和别的文件里同名的变量撞
{
	float Clock = 0.f;
	bool bStarted = false;
	bool bQuit = false;
	TWeakObjectPtr<AMechCharacter> DummyRef;
	FVector Away = FVector::ForwardVector;
	float AP0 = 0.f;
}

const TCHAR* MechAlertTest::LoopName(EMechAlertLoop Loop)
{
	switch (Loop)
	{
	case EMechAlertLoop::Caution:
		return TEXT("低装甲");
	case EMechAlertLoop::Track:
		return TEXT("跟踪");
	case EMechAlertLoop::Lock:
		return TEXT("锁定");
	case EMechAlertLoop::Missile:
		return TEXT("导弹来袭");
	default:
		return TEXT("无");
	}
}

void MechAlertTest::Tick(AMechHUD& Hud, UMechHudAlerts& Alerts, AMechCharacter* Me, float Dt)
{
	using namespace MechAlertTestState;
	if (bQuit || !Me || !Me->HasAuthority())
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
		// 挑离自己约 40 米、和自己差不多高的木桩（导弹从这个距离飞过来约 1 秒，来得及听出哔声变快；
		// 站在 10 米高台上的木桩不要：导弹从上往下扎，第一轮测试全砸在自己 6–9 米外的地上）
		AMechCharacter* Best = nullptr;
		float BestErr = 1.e9f;
		for (TActorIterator<AMechCharacter> It(Me->GetWorld()); It; ++It)
		{
			if (!It->IsTrainingDummy() || FMath::Abs(It->GetActorLocation().Z - Me->GetActorLocation().Z) > 300.f)
			{
				continue;
			}
			const float Err = FMath::Abs(FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) - 4000.f);
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
		Clock = -3.f;  // 先等 3 秒：开局加载、着色器编译那几秒帧很不稳
		UE_LOG(LogTemp, Display, TEXT("[alert] 测试开始：木桩 %s 离自己 %.0f 米"), *Best->DisplayName, FVector::Dist(Best->GetActorLocation(), Me->GetActorLocation()) / 100.f);
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
	UMechMovementComponent* Move = Me->GetMechMovement();
	UMechCombatComponent* Its = D->GetCombat();

	auto State = [&](const TCHAR* Tag) {
		static const TCHAR* Lamps[6] = {TEXT("MSL"), TEXT("LCK"), TEXT("AP"), TEXT("EN"), TEXT("BAL"), TEXT("SYS")};
		FString L;
		for (int32 i = 0; i < 6; ++i)
		{
			L += FString::Printf(TEXT(" %s%d"), Lamps[i], Alerts.GetLamp(static_cast<EMechAlertLamp>(i)));
		}
		UE_LOG(LogTemp, Display, TEXT("[alert] %5.2f %s：循环 %s | 灯%s | 装甲 %.0f%% 能量 %.0f 失衡 %.0f%% 网 %d EMP %d"), T, Tag, LoopName(Alerts.GetLoop()), *L,
			HP->GetAPFraction() * 100.f, Me->GetEnergy(), HP->GetStaggerFraction() * 100.f, Me->IsNetted() ? 1 : 0, Me->IsEmped() ? 1 : 0);
	};
	auto Gun = [&](const TCHAR* Tag) {
		// 木桩的枪现在对着自己的程度（充能 / 开镜 / 枪线擦过自己多远 / 中间有没有墙），对不上时查原因用
		FHitResult Hit;
		FCollisionQueryParams P(SCENE_QUERY_STAT(MechAlertTestLos), false, D);
		P.AddIgnoredActor(Me);
		const bool bWall = Me->GetWorld()->LineTraceSingleByObjectType(Hit, D->GetActorLocation() + FVector(0.f, 0.f, 100.f), Me->GetActorLocation() + FVector(0.f, 0.f, 60.f),
			FCollisionObjectQueryParams(ECC_WorldStatic), P);
		UE_LOG(LogTemp, Display, TEXT("[alert] %s 木桩：姿态 %d 充能 %.2f 开镜 %d 目标是自己 %d 锁定威胁 %.2f 枪线擦过 %.0f 厘米 中间有墙 %d（%s）"), Tag,
			static_cast<int32>(Its->GetStance()), Its->GetCharge(), Its->IsAiming() ? 1 : 0, Its->GetTarget() == Me ? 1 : 0, Its->GetLockThreat(),
			UMechHudAlerts::AimMiss(D, Me), bWall ? 1 : 0, bWall && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"));
	};
	// -MechAlertNoShots：只看日志（换语音风格再跑一遍时用，截图和第一遍一样）
	static const bool bNoShots = FParse::Param(FCommandLine::Get(), TEXT("MechAlertNoShots"));
	auto Shot = [&](const TCHAR* Tag) {
		if (!bNoShots)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Alerts") / FString::Printf(TEXT("%s.png"), Tag), true, false);
		}
		State(Tag);
	};
	auto StandAt = [&](float Dist, float YawOff) {
		const FVector P = D->GetActorLocation() + Away * Dist + FVector(0.f, 0.f, 20.f);
		Me->SetActorLocation(P, false, nullptr, ETeleportType::TeleportPhysics);
		Me->GetCharacterMovement()->StopMovementImmediately();
		Me->SetActorRotation(FRotator(0.f, (-Away).Rotation().Yaw + YawOff, 0.f));
	};
	auto LookAtDummy = [&](float YawOff) {
		const FRotator R = (D->GetActorLocation() + FVector(0.f, 0.f, 60.f) - PC->PlayerCameraManager->GetCameraLocation()).Rotation();
		PC->SetControlRotation(FRotator(FMath::Clamp(R.Pitch, -20.f, 10.f), R.Yaw + YawOff, 0.f));
	};
	auto Equip = [&](EMechShooter Gun, EMechShoulder Shoulder) {
		FMechLoadout L = D->GetLoadout();
		L.Shooter = Gun;
		L.Shoulder = Shoulder;
		D->SetLoadout(L, true);
	};
	auto Hit = [&](float Damage, float Impact, float StaggerScale) {
		HP->ApplyHit(Damage, Impact, D, Me->GetActorLocation() + FVector(0.f, 0.f, 60.f), StaggerScale);
	};
	auto HealFull = [&]() { HP->Heal(1.e6f); };
	auto MaxAP = [&]() { return HP->GetAP() / FMath::Max(HP->GetAPFraction(), 0.001f); };

	// ---- 0 上电（第三人称）：HUD 从下往上逐块亮、告警灯自检
	if (At(0.5f))
	{
		StandAt(4000.f, 0.f);
		Alerts.DebugPowerUp(Hud);
	}
	if (T > 0.5f && T < 2.f)
	{
		LookAtDummy(0.f);
	}
	if (At(0.62f))
	{
		Shot(TEXT("00_上电_a_仪表盘"));
	}
	if (At(0.9f))
	{
		Shot(TEXT("00_上电_b_细条_告警灯自检"));
	}
	if (At(1.2f))
	{
		Shot(TEXT("00_上电_c_上方"));
	}
	// ---- 1 被导弹锁定：木桩换导弹、指定自己为目标 → 锁定在走（跟踪）→ 锁上（锁定）；转身看屏幕外的方向光带
	if (At(2.f))
	{
		Equip(EMechShooter::Rifle, EMechShoulder::Missiles);
		Its->SetForcedTarget(Me);
		State(TEXT("木桩开始锁定"));
	}
	if (T > 2.f && T < 4.f)
	{
		LookAtDummy(0.f);
	}
	if (At(2.35f))
	{
		Shot(TEXT("01_被跟踪_屏幕内"));
	}
	if (At(3.3f))
	{
		Shot(TEXT("02_被锁定_屏幕内"));
		Gun(TEXT("导弹锁定"));
	}
	if (T > 4.f && T < 7.5f)
	{
		LookAtDummy(140.f);  // 背对木桩偏右：威胁在左后方，屏幕边出光带
	}
	if (At(4.7f))
	{
		Shot(TEXT("03_被锁定_屏幕外"));
	}
	// ---- 2 导弹来袭（背对着）：发射告警一声，之后越近哔得越快
	if (At(5.6f))
	{
		AP0 = HP->GetAP();
		Its->FireMissiles();
		State(TEXT("木桩发射导弹"));
	}
	if (At(5.85f))
	{
		Shot(TEXT("04_导弹来袭_刚发射"));
	}
	if (At(6.25f))
	{
		Shot(TEXT("05_导弹来袭_逼近"));
	}
	if (At(6.5f))
	{
		Shot(TEXT("06_导弹来袭_贴近"));
	}
	if (At(7.5f))
	{
		Its->SetForcedTarget(nullptr);
		UE_LOG(LogTemp, Display, TEXT("[alert] 导弹打掉装甲 %.0f"), AP0 - HP->GetAP());
		State(TEXT("导弹段结束"));
	}
	// ---- 3 低装甲（< 25%）→ 危急（< 12%）
	if (T > 7.5f && T < 15.f)
	{
		LookAtDummy(25.f);
	}
	if (At(8.f))
	{
		HealFull();
		Hit(HP->GetAP() - 0.2f * MaxAP(), 0.f, 0.f);  // 装甲打到 20%（冲击 0：不抖、不积累失衡）
	}
	if (At(8.45f))
	{
		Shot(TEXT("07_低装甲"));
	}
	if (At(12.5f))
	{
		Hit(HP->GetAP() - 0.09f * MaxAP(), 0.f, 0.f);
	}
	if (At(12.8f))
	{
		Shot(TEXT("08_装甲危急"));
	}
	if (At(14.5f))
	{
		HealFull();
		State(TEXT("回满装甲"));
	}
	// ---- 4 推进耗尽 → 恢复
	if (At(15.f))
	{
		Move->Energy = 0.f;
		Move->bOverheated = true;
	}
	if (At(15.25f))
	{
		Shot(TEXT("09_推进耗尽"));
	}
	if (At(17.5f))
	{
		State(TEXT("推进恢复后"));
	}
	// ---- 5 失衡将满（一下冲击把失衡槽打到 80%）
	if (At(18.f))
	{
		Hit(1.f, 0.8f * HP->MaxStagger, 1.f);
	}
	if (At(18.25f))
	{
		Shot(TEXT("10_失衡将满"));
	}
	// ---- 6 受重击（冲击 900、不积累失衡）：HUD 抖 + 撕一道
	if (At(21.f))
	{
		Hit(300.f, 900.f, 0.f);
	}
	if (At(21.03f))
	{
		Shot(TEXT("11_重击抖动_a"));
	}
	if (At(21.12f))
	{
		Shot(TEXT("11_重击抖动_b"));
	}
	// ---- 7 被网住
	if (At(23.f))
	{
		HealFull();
		Me->GetShoulder()->ApplyNet(D, nullptr);
	}
	if (At(23.3f))
	{
		Shot(TEXT("12_被网住"));
	}
	// ---- 8 被钩爪拖拽：木桩换钩爪钩自己（侧身站，木桩在屏幕外）
	if (At(26.f))
	{
		Equip(EMechShooter::Rifle, EMechShoulder::Grapple);
		Its->SetForcedTarget(Me);
		StandAt(2500.f, 0.f);
	}
	if (T > 26.f && T < 30.f)
	{
		LookAtDummy(-95.f);
	}
	if (At(26.3f))
	{
		D->GetShoulder()->Pressed();
	}
	if (At(26.35f))
	{
		D->GetShoulder()->Released();
	}
	if (At(26.75f))
	{
		Shot(TEXT("13_被钩爪拖拽"));
	}
	if (At(27.05f))
	{
		Shot(TEXT("13_被钩爪拖拽_b"));
	}
	if (At(28.5f))
	{
		Its->SetForcedTarget(nullptr);
		StandAt(3000.f, 0.f);
		UE_LOG(LogTemp, Display, TEXT("[alert] 钩爪：木桩钩住 %d 次"), D->GetShoulder()->DebugHooks);
	}
	// ---- 9 中 EMP（4 秒）
	if (At(30.f))
	{
		Me->GetShoulder()->ApplyEmp(4.f);
	}
	if (T > 28.5f && T < 41.f)
	{
		LookAtDummy(0.f);
	}
	if (At(30.05f))
	{
		Shot(TEXT("14_EMP_a_刚中"));
	}
	if (At(30.3f))
	{
		Shot(TEXT("14_EMP_b"));
	}
	if (At(31.2f))
	{
		Shot(TEXT("14_EMP_c"));
	}
	if (At(33.f))
	{
		Shot(TEXT("14_EMP_d"));
	}
	// ---- 10 充能步枪照着自己（木桩不带导弹，免得锁定告警混进来）
	if (At(35.f))
	{
		Equip(EMechShooter::Marksman, EMechShoulder::Shield);
		Its->SetForcedTarget(Me);
	}
	if (At(35.3f))
	{
		Its->SetTrigger(true);
	}
	if (At(36.f))
	{
		Shot(TEXT("15_充能步枪照射"));
		Gun(TEXT("充能"));
	}
	if (At(36.4f))
	{
		Its->SetTrigger(false);
	}
	// ---- 11 狙击开镜对着自己
	if (At(38.f))
	{
		HealFull();
		Equip(EMechShooter::Sniper, EMechShoulder::Shield);
		Its->SetForcedTarget(Me);
		Its->SetAltHeld(true);
	}
	if (At(38.8f))
	{
		Shot(TEXT("16_狙击开镜瞄着"));
		Gun(TEXT("狙击"));
	}
	if (At(40.f))
	{
		Its->SetAltHeld(false);
		Its->SetForcedTarget(nullptr);
	}
	// ---- 12 被击毁：HUD 闪两下、收成一条线熄灭
	if (At(42.f))
	{
		State(TEXT("击毁前"));
		HP->bNoDeath = false;
		Hit(HP->GetAP() + 500.f, 0.f, 0.f);
	}
	if (At(42.06f))
	{
		Shot(TEXT("17_断电_a_闪"));
	}
	if (At(42.25f))
	{
		Shot(TEXT("17_断电_b_压扁"));
	}
	if (At(42.38f))
	{
		Shot(TEXT("17_断电_c_一条线"));
	}
	if (At(42.55f))
	{
		Shot(TEXT("17_断电_d_熄灭"));
	}
	if (At(45.f))
	{
		FString Counts;
		for (const TPair<FString, int32>& P : Alerts.SoundCounts)
		{
			Counts += FString::Printf(TEXT(" %s×%d"), *P.Key, P.Value);
		}
		UE_LOG(LogTemp, Display, TEXT("[alert] 汇总 响过的声音：%s"), *Counts);
		static const TCHAR* Want[] = {TEXT("上电"), TEXT("跟踪"), TEXT("锁定"), TEXT("导弹发射"), TEXT("导弹来袭"), TEXT("低装甲"), TEXT("推进耗尽"),
			TEXT("推进恢复"), TEXT("失衡将满"), TEXT("被网住"), TEXT("被拖拽"), TEXT("EMP"), TEXT("断电")};
		FString Missing;
		for (const TCHAR* W : Want)
		{
			if (!Alerts.SoundCounts.Contains(W))
			{
				Missing += FString::Printf(TEXT(" %s"), W);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[alert] 汇总 没响的：%s"), Missing.IsEmpty() ? TEXT("（都响了）") : *Missing);
		// 语音（-MechAlertVoice=1/2/3 选风格；0 关时这一行全是没说的）
		static const TCHAR* WantVoice[] = {TEXT("语音 online"), TEXT("语音 lock"), TEXT("语音 missile"), TEXT("语音 armor_low"), TEXT("语音 armor_crit"),
			TEXT("语音 thrust"), TEXT("语音 balance"), TEXT("语音 net"), TEXT("语音 drag"), TEXT("语音 emp"), TEXT("语音 sniper"), TEXT("语音 lost")};
		FString MissingVoice;
		for (const TCHAR* W : WantVoice)
		{
			if (!Alerts.SoundCounts.Contains(W))
			{
				MissingVoice += FString::Printf(TEXT(" %s"), W);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[alert] 汇总 语音风格 %d，没说的：%s"), UMechHudAlerts::VoiceStyle(), MissingVoice.IsEmpty() ? TEXT("（都说了）") : *MissingVoice);
		bQuit = true;
		PC->ConsoleCommand(TEXT("quit"));
	}
}
