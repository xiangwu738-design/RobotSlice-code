// -MechDuelTest（10-30 战斗调整加的）：刀剑机制、霰弹压刀、三维突进、狙击吸附、手枪冷却、充能步枪、匕首击退逐项过一遍。
// 用法：UnrealEditor.exe RobotSlice.uproject /Game/Maps/L_Range?Range -game -MechDuelTest（-nullrhi 只看日志；带画面加 -RenderOffscreen，
// 关键时刻截图到 Saved/Screenshots/Duel：弹反 / 格挡 / 破防的 HUD 提示、充能激光、光束、拼刀）。日志搜 [duel]，每项 5 秒，约 190 秒退出；
// 第 15 项起是拼刀和格挡弹刀（11-01），只看这些加 -MechDuelFrom=15，约 115 秒。
// 做法：本机玩家那台和最近的木桩对打——木桩没有控制器，脚本直接调它的战斗组件出招（UMechCombatComponent::RequestLight 等），
// 玩家这台也由脚本按时刻按键（格挡、开枪、闪避、出刀），每项结束前把关键数值写进日志。

#include "MechCombatComponent.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MechCharacter.h"
#include "MechEffectComponent.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechMovementComponent.h"
#include "MechWeapons.h"

// 投掷匕首击退那一项的距离（默认 10 米，15 米内才有加强；-MechDuelDaggerDist=2500 看远处）。
// 表里写 -1、用的时候才读：表是全局对象，构造时读命令行，打包版 main 之前就退出（11-01 踩过，退出码 46、没有日志）
static float DaggerTestDist()
{
	float D = 1000.f;
	FParse::Value(FCommandLine::Get(), TEXT("MechDuelDaggerDist="), D);
	return D;
}

namespace MechDuelState  // 不用匿名命名空间：合并编译时会和别的文件里同名的变量撞
{
	float Clock = 0.f;
	int32 Step = -1;
	bool bQuit = false;
	FVector Home = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	TWeakObjectPtr<AMechCharacter> DummyRef;
	float MyAP = 0.f;
	float Damage = 0.f;
	float Z0 = 0.f;
	float LastShot = -100.f;
	FVector DummyAt = FVector::ZeroVector;
	float SlashFrom = 0.f;
	int32 Parts[3] = {0, 0, 0};

	struct FSetup
	{
		EMechShooter Gun;
		EMechMeleeWeapon MyBlade;
		bool bMyBlade;
		EMechMeleeWeapon ItsBlade;
		float Dist;
		float MyHeight;
		float ItsHeight;
		const TCHAR* Name;
	};
	const FSetup Setups[] = {
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("双手刀弹反")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀（我挡木桩的刀）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Blunt, 450.f, 0.f, 0.f, TEXT("钝器重斩破防")},
		{EMechShooter::Shotgun, EMechMeleeWeapon::Katana, false, EMechMeleeWeapon::Katana, 1100.f, 0.f, 0.f, TEXT("霰弹打断出刀")},
		{EMechShooter::Shotgun, EMechMeleeWeapon::Katana, false, EMechMeleeWeapon::GreatSword, 800.f, 0.f, 0.f, TEXT("大剑霸体重斩")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 600.f, 0.f, 700.f, TEXT("三维突进（向上）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 500.f, 800.f, 0.f, TEXT("三维突进（向下）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::OneHand, true, EMechMeleeWeapon::Katana, 2000.f, 0.f, 0.f, TEXT("单手刀闪避斩")},
		{EMechShooter::Rifle, EMechMeleeWeapon::TwinBlades, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("双刀割裂")},
		{EMechShooter::Rifle, EMechMeleeWeapon::QuickBlade, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("快刀连斩")},
		{EMechShooter::DualPistol, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("双持手枪重斩补满")},
		{EMechShooter::Sniper, EMechMeleeWeapon::Katana, false, EMechMeleeWeapon::Katana, 3000.f, 0.f, 0.f, TEXT("狙击开镜吸附")},
		{EMechShooter::Marksman, EMechMeleeWeapon::Katana, false, EMechMeleeWeapon::Katana, 1500.f, 0.f, 0.f, TEXT("充能步枪")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Dagger, true, EMechMeleeWeapon::Katana, -1.f, 0.f, 0.f, TEXT("投掷匕首击退")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, false, EMechMeleeWeapon::Katana, 1500.f, 0.f, 0.f, TEXT("充能步枪画面（侧面看木桩朝世界 +X 打）")},
		// ---- 拼刀（11-01，第 15 项起，和下面的 ClashCases 一一对应）
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：同把刀轻击（应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::GreatSword, true, EMechMeleeWeapon::QuickBlade, 450.f, 0.f, 0.f, TEXT("拼刀：大剑对快刀（应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Blunt, true, EMechMeleeWeapon::TwinBlades, 450.f, 0.f, 0.f, TEXT("拼刀：钝器重斩对双刀（应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::GreatSword, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：大剑霸体重斩对双手刀（霸体也应弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Dagger, true, EMechMeleeWeapon::GreatSword, 450.f, 0.f, 0.f, TEXT("拼刀：我匕首对木桩大剑（应只弹我）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::GreatSword, true, EMechMeleeWeapon::Dagger, 450.f, 0.f, 0.f, TEXT("拼刀：我大剑对木桩匕首（应只弹木桩，我马上能接招）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Dagger, true, EMechMeleeWeapon::Dagger, 450.f, 0.f, 0.f, TEXT("拼刀：匕首对匕首（应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 800.f, 800.f, TEXT("拼刀：空中（应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：错开 0.08 秒（超出窗口，不算同时出刀，我先砍中）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：错开 0.35 秒（不触发，我先砍中）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：我砍木桩的格挡（应两人弹开，木桩耗能掉血）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：对方背对我（不触发，砍中背后）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：僵直一结束马上出刀")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("拼刀：错开 0.02 秒（窗口内，应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Blunt, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：钝器重斩砍格挡（破防，不弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：对方能量不够（破防，不弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：双手刀刚按下格挡（弹反，我僵直，都不弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Dagger, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：匕首砍格挡（只弹匕首方，木桩耗能掉血）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::GreatSword, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：大剑霸体重斩砍格挡（也应两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, false, EMechMeleeWeapon::Katana, 1500.f, 0.f, 0.f, TEXT("格挡弹刀：子弹打格挡（不弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：砍举着格挡的人的背后（挡不住，普通命中）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::GreatSword, 450.f, 0.f, 0.f, TEXT("格挡弹刀：大剑刚按下格挡（不是双手刀，没有弹反，照样弹刀）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Blunt, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：钝器轻击砍格挡（普通弹刀，两人弹开）")},
		{EMechShooter::Rifle, EMechMeleeWeapon::Katana, true, EMechMeleeWeapon::Katana, 450.f, 0.f, 0.f, TEXT("格挡弹刀：砍举着格挡的人的侧面（挡不住，普通命中）")},
	};

	// ---- 拼刀（11-01）：脚本让两边的刀在同一时刻落下，记下双方掉血、失衡、僵直多久、弹开多远。
	// 弹开距离从拼刀那一帧量到这一项快结束（人已经停了），木桩没有输入，玩家这台也没有
	constexpr int32 ClashFirst = 15;
	struct FClashCase
	{
		bool bMyHeavy;
		bool bItsHeavy;
		float ItsDelay;  // 木桩的刀比我晚多少秒落下
		bool bItsGuard;
		bool bItsBack;
		bool bChain;     // 拼上后马上按下一刀（没被弹开的一方应该能马上接）
		bool bActNow;    // 僵直一结束马上出刀
		float GuardAt = 0.f;     // 木桩第几秒按格挡（0 = 早早按住，第 0.6 秒）
		bool bNoEnergy = false;  // 刀落下前把木桩的能量清零
		bool bFire = false;      // 我不出刀，开枪打它
		bool bItsSide = false;   // 木桩侧身对着我
	};
	const FClashCase ClashCases[] = {
		{false, false, 0.f, false, false, false, false},
		{false, false, 0.f, false, false, false, false},
		{true, false, 0.f, false, false, false, false},
		{true, false, 0.f, false, false, false, false},
		{false, false, 0.f, false, false, false, false},
		{false, false, 0.f, false, false, true, false},
		{false, false, 0.f, false, false, false, false},
		{false, false, 0.f, false, false, false, false},
		{false, false, 0.08f, false, false, false, false},
		{false, false, 0.35f, false, false, false, false},
		{false, false, 0.f, true, false, false, false},
		{false, false, 0.f, false, true, false, false},
		{false, false, 0.f, false, false, false, true},
		{false, false, 0.02f, false, false, false, false},
		{true, false, 0.f, true, false, false, false},
		{false, false, 0.f, true, false, false, false, 0.f, true},
		{false, false, 0.f, true, false, false, false, 1.48f},
		{false, false, 0.f, true, false, false, false},
		{true, false, 0.f, true, false, false, false},
		{false, false, 0.f, true, false, false, false, 0.f, false, true},
		{false, false, 0.f, true, true, false, false},
		{false, false, 0.f, true, false, false, false, 1.48f},
		{false, false, 0.f, true, false, false, false},
		{false, false, 0.f, true, false, false, false, 0.f, false, false, true},
	};
	float MyEnergy0 = 0.f, ItsEnergy0 = 0.f;
	bool bClashSeen = false;
	bool bClashShot1 = false;
	bool bClashShot2 = false;
	bool bClashActed = false;
	float ClashWorld = 0.f;
	FVector MyClashAt = FVector::ZeroVector;
	FVector ItsClashAt = FVector::ZeroVector;
	float MyStunFrom = -1.f, MyStunTo = -1.f, ItsStunFrom = -1.f, ItsStunTo = -1.f;
	float MyAtStunEnd = -1.f, ItsAtStunEnd = -1.f;
	float MyPeak = 0.f, ItsPeak = 0.f;
	float ItsAP0 = 0.f;
	int32 MyClashes0 = 0, ItsClashes0 = 0;
}

void MechCombatTest::DuelTick(AMechCharacter* Me, float DeltaTime)
{
	using namespace MechDuelState;
	if (bQuit)
	{
		return;
	}
	// -MechDuelFrom=N：从第 N 项开始（只看某一项时用）
	static const int32 From = [] {
		int32 N = 0;
		FParse::Value(FCommandLine::Get(), TEXT("MechDuelFrom="), N);
		return N;
	}();
	if (Clock <= 0.f && From > 0)
	{
		Clock = From * 5.f;
	}
	const float PrevClock = Clock;
	Clock += DeltaTime;
	APlayerController* PC = Cast<APlayerController>(Me->GetController());
	if (!PC || Clock < 3.f + From * 5.f)
	{
		return;
	}
	// -MechDuelVsPlayer（11-01 拼刀联机测试，房主跑）：对手不是木桩，是连进来的另一个玩家——服务器直接调他的组件出招，
	// 看他那一端收到组播、弹开有没有被位置纠正拉回（客户端加 -MechCorrectionLog）。等他连上再开始计时
	static const bool bVsPlayer = FParse::Param(FCommandLine::Get(), TEXT("MechDuelVsPlayer"));
	if (!DummyRef.IsValid())
	{
		for (TActorIterator<AMechCharacter> It(Me->GetWorld()); It; ++It)
		{
			const bool bPick = bVsPlayer ? (*It != Me && It->IsPlayerControlled()) : It->IsTrainingDummy();
			if (bPick && (!DummyRef.IsValid() || FVector::Dist(It->GetActorLocation(), Me->GetActorLocation()) < FVector::Dist(DummyRef->GetActorLocation(), Me->GetActorLocation())))
			{
				DummyRef = *It;
			}
		}
		if (!DummyRef.IsValid())
		{
			if (bVsPlayer)
			{
				Clock = PrevClock;
			}
			return;
		}
		if (bVsPlayer)
		{
			DummyRef->SetTeam(Me->GetTeam() + 1);  // 测试场里两个玩家同队，同队的刀不算数
		}
		Home = DummyRef->GetActorLocation();
		Dir = DummyRef->GetActorForwardVector().GetSafeNormal2D();
	}
	AMechCharacter* Dummy = DummyRef.Get();
	UMechCombatComponent* Mine = Me->GetCombat();
	UMechCombatComponent* Its = Dummy->GetCombat();
	const float StepLen = 5.f;
	const int32 S = FMath::FloorToInt((Clock - 3.f) / StepLen);
	const float T = Clock - 3.f - S * StepLen;
	const float T0 = PrevClock - 3.f - S * StepLen;
	auto At = [&](float X) { return T0 < X && T >= X; };
	auto Log = [&](const FString& Msg) { UE_LOG(LogTemp, Display, TEXT("[duel] %02d %s"), S, *Msg); };
	auto Shot = [&](const TCHAR* Tag) {
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Duel") / FString::Printf(TEXT("%02d_%s.png"), S, Tag), true, false);
	};
	auto Face = [](AMechCharacter* Who, const FVector& To) {
		Who->SetActorRotation(FRotator(0.f, (To - Who->GetActorLocation()).Rotation().Yaw, 0.f));
	};
	auto AimAt = [&](const FVector& P) { PC->SetControlRotation((P - PC->PlayerCameraManager->GetCameraLocation()).Rotation()); };
	if (S >= static_cast<int32>(UE_ARRAY_COUNT(Setups)))
	{
		Log(TEXT("全部测完"));
		bQuit = true;
		PC->ConsoleCommand(TEXT("quit"));
		return;
	}
	// ---- 每项开头：换装（装甲能量回满）、摆位置、清效果、互相指定目标
	const FSetup& U = Setups[S];
	if (S != Step)
	{
		Step = S;
		Log(FString::Printf(TEXT("开始：%s"), U.Name));
		Mine->SetTrigger(false);
		Mine->SetAltHeld(false);
		Its->SetAltHeld(false);
		FMechLoadout L = Me->GetLoadout();
		L.Shooter = U.Gun;
		L.Melee = U.MyBlade;
		Me->SetLoadout(L, true);
		FMechLoadout D = Dummy->GetLoadout();
		D.Melee = U.ItsBlade;
		Dummy->SetLoadout(D, true);
		Me->GetEffects()->ClearEffects();
		Dummy->GetEffects()->ClearEffects();
		Dummy->SetActorLocation(Home + FVector(0.f, 0.f, U.ItsHeight), false, nullptr, ETeleportType::TeleportPhysics);
		Me->SetActorLocation(Home + Dir * (U.Dist < 0.f ? DaggerTestDist() : U.Dist) + FVector(0.f, 0.f, U.MyHeight), false, nullptr, ETeleportType::TeleportPhysics);
		Me->GetCharacterMovement()->StopMovementImmediately();
		Dummy->GetCharacterMovement()->StopMovementImmediately();
		// 木桩没有控制器、移动模式是 None：要让它突进 / 被击退得开物理；悬在空中的那台用飞行模式
		Dummy->GetCharacterMovement()->bRunPhysicsWithNoController = true;
		Dummy->GetCharacterMovement()->SetMovementMode(U.ItsHeight > 0.f ? MOVE_Flying : MOVE_Walking);
		Its->SetForcedTarget(Me);
		Mine->SetForcedTarget(Dummy);
		Face(Dummy, Me->GetActorLocation());
		Face(Me, Dummy->GetActorLocation());
		MyAP = Me->GetHealth()->GetAP();
		Damage = Mine->DebugDamage;
		Parts[0] = Mine->DebugPartHits[0];
		Parts[1] = Mine->DebugPartHits[1];
		Parts[2] = Mine->DebugPartHits[2];
	}
	// 姿态：自己按这一项要的，木桩一直拿刀
	const EMechStance MyStance = U.bMyBlade ? EMechStance::Blade : EMechStance::Gun;
	if (T < 0.9f && Mine->GetStance() != MyStance && Mine->GetSwitchProgress() < 0.f && !Mine->IsMeleeActive())
	{
		Mine->SwitchStance();
	}
	const EMechStance ItsStance = S == 14 ? EMechStance::Gun : EMechStance::Blade;
	if (T < 0.9f && Its->GetStance() != ItsStance && Its->GetSwitchProgress() < 0.f)
	{
		Its->SwitchStance();
	}
	if (T < 0.95f)
	{
		Face(Dummy, Me->GetActorLocation());
		Face(Me, Dummy->GetActorLocation());
	}
	if (S != 11)
	{
		AimAt(Dummy->GetMechMesh()->GetBoneLocation(TEXT("spine_03")));
	}
	const float MyDealt = Mine->DebugDamage - Damage;
	const float MyLost = MyAP - Me->GetHealth()->GetAP();
	if (S >= ClashFirst)
	{
		const FClashCase& C = ClashCases[S - ClashFirst];
		const float StrikeT = 1.6f;
		const float Now = Me->GetWorld()->GetTimeSeconds();
		auto Lead = [](const UMechCombatComponent* Who, bool bHeavy) { return (bHeavy ? 0.38f : 0.24f) * Who->GetBlade().TimeScale; };
		auto Swing = [](UMechCombatComponent* Who, bool bHeavy) {
			if (bHeavy)
			{
				Who->RequestHeavy();
			}
			else if (Who->GetBlade().Knives > 0)
			{
				Who->RequestDaggerSlash();  // 匕首的左键是投掷，近身挥刀是右键
			}
			else
			{
				Who->RequestLight();
			}
		};
		if (At(0.05f))
		{
			bClashSeen = bClashShot1 = bClashShot2 = bClashActed = false;
			MyStunFrom = MyStunTo = ItsStunFrom = ItsStunTo = MyAtStunEnd = ItsAtStunEnd = -1.f;
			MyPeak = ItsPeak = 0.f;
			ItsAP0 = Dummy->GetHealth()->GetAP();
			MyClashes0 = Mine->DebugClashes;
			ItsClashes0 = Its->DebugClashes;
		}
		if (At(0.85f))
		{
			// 木桩 4 秒没挨打、离家超过半米就瞬移回原位（量距离时不能回去）：先记一下 0 伤害的命中，同三维突进那项
			Dummy->GetHealth()->ApplyHit(0.f, 0.f, nullptr, Dummy->GetActorLocation());
			ItsAP0 = Dummy->GetHealth()->GetAP();
			if (U.ItsHeight > 0.f)
			{
				Dummy->SetActorLocation(Home + FVector(0.f, 0.f, U.ItsHeight), false, nullptr, ETeleportType::TeleportPhysics);
				Dummy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				Dummy->GetCharacterMovement()->StopMovementImmediately();
			}
		}
		if (C.bItsGuard && At(C.GuardAt > 0.f ? C.GuardAt : 0.6f))
		{
			Its->SetAltHeld(true);
		}
		if (C.bNoEnergy && At(StrikeT - 0.05f))
		{
			Dummy->GetMechMovement()->Energy = 0.f;
		}
		if (At(StrikeT - 0.04f))
		{
			MyEnergy0 = Me->GetMechMovement()->Energy;
			ItsEnergy0 = Dummy->GetMechMovement()->Energy;
		}
		if (C.bFire)
		{
			Mine->SetTrigger(T >= StrikeT && T < StrikeT + 0.25f);
		}
		if (C.bItsSide && At(0.96f))
		{
			// 木桩侧过身去（用户：格挡要正面才有效），我从它正侧面砍
			Its->SetForcedTarget(nullptr);
			Face(Dummy, Dummy->GetActorLocation() + FVector::CrossProduct(FVector::UpVector, (Me->GetActorLocation() - Dummy->GetActorLocation()).GetSafeNormal2D()) * 100.f);
		}
		if (C.bItsBack && At(0.96f))
		{
			// 木桩转过身去、不追我（没有目标、没有控制器时它不转身）
			Its->SetForcedTarget(nullptr);
			Face(Dummy, Dummy->GetActorLocation() * 2.f - Me->GetActorLocation());
		}
		if (U.MyHeight > 0.f && At(StrikeT - 0.32f))
		{
			// 空中那项：出刀前才把自己放到高处（早放就掉回地上了）
			Me->SetActorLocation(Home + Dir * (U.Dist < 0.f ? DaggerTestDist() : U.Dist) + FVector(0.f, 0.f, U.MyHeight), false, nullptr, ETeleportType::TeleportPhysics);
			Me->GetCharacterMovement()->StopMovementImmediately();
			Face(Me, Dummy->GetActorLocation());
		}
		if (!C.bFire && At(StrikeT - Lead(Mine, C.bMyHeavy)))
		{
			Swing(Mine, C.bMyHeavy);
		}
		if (!C.bItsGuard && At(StrikeT + C.ItsDelay - Lead(Its, C.bItsHeavy)))
		{
			Swing(Its, C.bItsHeavy);
		}
		// 每帧记：僵直从什么时候到什么时候（世界时间，含顿帧）、最高速度、僵直结束那一刻走了多远
		const bool bMyStun = Me->GetHealth()->IsStunned();
		const bool bItsStun = Dummy->GetHealth()->IsStunned();
		if (bMyStun)
		{
			MyStunFrom = MyStunFrom < 0.f ? Now : MyStunFrom;
			MyStunTo = Now;
		}
		if (bItsStun)
		{
			ItsStunFrom = ItsStunFrom < 0.f ? Now : ItsStunFrom;
			ItsStunTo = Now;
		}
		if (!bClashSeen && (Mine->DebugClashes > MyClashes0 || Its->DebugClashes > ItsClashes0))
		{
			bClashSeen = true;
			ClashWorld = Now;
			if (U.ItsHeight > 0.f)
			{
				Dummy->GetCharacterMovement()->SetMovementMode(MOVE_Falling);  // 木桩悬空是飞行模式（没有重力、没有刹车）；拼上后按真机体那样下落
			}
			MyClashAt = Me->GetActorLocation();
			ItsClashAt = Dummy->GetActorLocation();
			Log(FString::Printf(TEXT("  拼上了（第 %.2f 秒）：两人相距 %.1f 米；我还在出刀 %d、木桩还在出刀 %d，霸体 我 %d 木桩 %d"), T, FVector::Dist(MyClashAt, ItsClashAt) / 100.f,
				Mine->IsMeleeActive() ? 1 : 0, Its->IsMeleeActive() ? 1 : 0, Mine->IsSuperArmor() ? 1 : 0, Its->IsSuperArmor() ? 1 : 0));
			if (C.bChain)
			{
				Log(FString::Printf(TEXT("  我没被弹开：在弹开 %d、僵直 %d；这一招时间 %.2f，可取消时刻 %.2f（不大于招式时间 = 马上能动），速度被出招接管 %d"), Mine->IsClashKnocked() ? 1 : 0, bMyStun ? 1 : 0,
					Mine->GetMeleeTime(), Mine->GetMeleeCancel(), Mine->IsMeleeDriving() ? 1 : 0));
				Mine->RequestLight();
			}
		}
		if (bClashSeen)
		{
			MyPeak = FMath::Max(MyPeak, Me->GetVelocity().Size2D());
			ItsPeak = FMath::Max(ItsPeak, Dummy->GetVelocity().Size2D());
			const float Since = Now - ClashWorld;
			static const bool bFrames = FParse::Param(FCommandLine::Get(), TEXT("MechClashFrames"));  // 拼刀后逐帧记速度和位置（查弹开距离跟帧率的关系）
			if (bFrames && Since < 0.7f)
			{
				Log(FString::Printf(TEXT("  帧 +%.3f 帧时 %.4f 顿帧 %.2f 速度 %.0f 走了 %.1f 弹开中 %d 僵直 %d 模式 %d"), Since, DeltaTime, Me->CustomTimeDilation, Me->GetVelocity().Size2D(),
					FVector::Dist2D(Me->GetActorLocation(), MyClashAt) / 100.f, Mine->IsClashKnocked() ? 1 : 0, bMyStun ? 1 : 0, static_cast<int32>(Me->GetCharacterMovement()->MovementMode)));
			}
			if (!bClashShot1 && Since >= 0.05f)
			{
				bClashShot1 = true;
				Shot(TEXT("拼刀"));
			}
			if (!bClashShot2 && Since >= 0.24f)
			{
				bClashShot2 = true;
				Shot(TEXT("拼刀_弹开"));
			}
			if (MyStunFrom >= 0.f && !bMyStun && MyAtStunEnd < 0.f)
			{
				MyAtStunEnd = FVector::Dist2D(Me->GetActorLocation(), MyClashAt);
				if (C.bActNow && !bClashActed)
				{
					bClashActed = true;
					Mine->RequestLight();
					Log(FString::Printf(TEXT("  僵直刚结束就按轻击：出招中 %d（应为 1），第 %d 段"), Mine->IsMeleeActive() ? 1 : 0, Mine->GetComboStep()));
				}
			}
			if (ItsStunFrom >= 0.f && !bItsStun && ItsAtStunEnd < 0.f)
			{
				ItsAtStunEnd = FVector::Dist2D(Dummy->GetActorLocation(), ItsClashAt);
			}
			if (C.bChain && Since > 0.16f && Since - DeltaTime <= 0.16f)
			{
				Log(FString::Printf(TEXT("  拼上 0.16 秒后按过轻击：现在第 %d 段（应为 2 = 马上接上了）"), Mine->GetComboStep()));
			}
		}
		if (At(StrikeT + 0.2f))
		{
			Log(FString::Printf(TEXT("  刀落下后：能量 我 %.0f→%.0f 木桩 %.0f→%.0f；破防 我 %d 木桩 %d；僵直 我 %d 木桩 %d；还在出刀 我 %d 木桩 %d；木桩还举着格挡 %d"), MyEnergy0, Me->GetMechMovement()->Energy,
				ItsEnergy0, Dummy->GetMechMovement()->Energy, Mine->IsGuardBroken() ? 1 : 0, Its->IsGuardBroken() ? 1 : 0, bMyStun ? 1 : 0, bItsStun ? 1 : 0,
				Mine->IsMeleeActive() ? 1 : 0, Its->IsMeleeActive() ? 1 : 0, Its->IsGuarding() ? 1 : 0));
		}
		if (At(3.4f))
		{
			const int32 Clashes = FMath::Max(Mine->DebugClashes - MyClashes0, Its->DebugClashes - ItsClashes0);
			Log(FString::Printf(TEXT("结果：拼刀 %d 次；掉血 我 %.0f 木桩 %.0f%s；失衡 我 %.3f 木桩 %.3f；格挡反馈种类 %d（%.1f 秒前）"), Clashes, MyLost, ItsAP0 - Dummy->GetHealth()->GetAP(),
				C.bChain ? TEXT("（这一项拼上后马上接了第二刀，掉的血是第二刀追上打的，作废那一刀的伤害看上一项）") : TEXT(""),
				Me->GetHealth()->GetStaggerFraction(), Dummy->GetHealth()->GetStaggerFraction(), Its->GetBlockFxKind(), Its->GetBlockFxAge()));
			if (bClashSeen)
			{
				auto Span = [](float Begin, float End) { return Begin >= 0.f ? End - Begin : 0.f; };
				Log(FString::Printf(TEXT("  我：弹开 %.1f 米（僵直结束时 %.1f 米），僵直 %.2f 秒（含顿帧），最高速度 %.0f 米/秒，现在高度差 %.1f 米"), FVector::Dist2D(Me->GetActorLocation(), MyClashAt) / 100.f,
					MyAtStunEnd / 100.f, Span(MyStunFrom, MyStunTo), MyPeak / 100.f, (Me->GetActorLocation().Z - MyClashAt.Z) / 100.f));
				Log(FString::Printf(TEXT("  木桩：弹开 %.1f 米（僵直结束时 %.1f 米），僵直 %.2f 秒（含顿帧），最高速度 %.0f 米/秒，现在高度差 %.1f 米"), FVector::Dist2D(Dummy->GetActorLocation(), ItsClashAt) / 100.f,
					ItsAtStunEnd / 100.f, Span(ItsStunFrom, ItsStunTo), ItsPeak / 100.f, (Dummy->GetActorLocation().Z - ItsClashAt.Z) / 100.f));
			}
		}
		return;
	}
	switch (S)
	{
	case 0:  // 弹反：对方出轻击一（0.24 秒砍下），刀落下前 0.12 秒按格挡
		if (At(1.f)) Its->RequestLight();
		if (At(1.12f)) Mine->SetAltHeld(true);
		if (At(1.27f)) Shot(TEXT("弹反"));
		if (At(1.45f))
		{
			Log(FString::Printf(TEXT("我掉血 %.0f，对方短僵直 %d、刀还在出 %d，我弹反加成剩 %.1f 秒，格挡反馈种类 %d（1 = 弹反）"), MyLost,
				Dummy->GetHealth()->IsStunned() ? 1 : 0, Its->IsMeleeActive() ? 1 : 0, Mine->GetHuajingMult(), Mine->GetBlockFxKind()));
			Mine->SetAltHeld(false);
		}
		if (At(1.7f)) Mine->RequestLight();
		if (At(2.4f)) Log(FString::Printf(TEXT("弹反后的下一刀打出 %.0f（轻击一 380 ×1.5 = 570）"), MyDealt));
		break;
	case 1:  // 普通格挡：早早按住
		if (At(0.6f)) Mine->SetAltHeld(true);
		if (At(1.f)) Its->RequestLight();
		if (At(1.27f)) Shot(TEXT("格挡"));
		if (At(1.5f))
		{
			// 11-01 晚：格挡架势挨刀 = 弹刀，不再有「挡住」的反馈，两人都被弹开
			Log(FString::Printf(TEXT("我掉血 %.0f（380 ×0.25 = 95），对方僵直 %d、还在出刀 %d，破防 %d，弹刀 %d 次（应 > 0），我在弹开 %d、木桩在弹开 %d"), MyLost,
				Dummy->GetHealth()->IsStaggered() ? 1 : 0, Its->IsMeleeActive() ? 1 : 0, Mine->IsGuardBroken() ? 1 : 0, Mine->DebugClashes, Mine->IsClashKnocked() ? 1 : 0, Its->IsClashKnocked() ? 1 : 0));
			Mine->SetAltHeld(false);
		}
		break;
	case 2:  // 钝器重斩砍在格挡上：直接破防（钝器出招慢 1.5 倍，0.57 秒砍下；11-01 晚起轻击走普通弹刀，见第 29、37 项）
		if (At(0.6f)) Mine->SetAltHeld(true);
		if (At(1.f)) Its->RequestHeavy();
		if (At(1.6f)) Shot(TEXT("破防"));
		if (At(1.9f))
		{
			Log(FString::Printf(TEXT("我掉血 %.0f（钝器重斩 1400 ×1.1 = 1540 全额），破防 %d，格挡反馈种类 %d（2 = 破防），我在弹开 %d（应为 0）"), MyLost,
				Mine->IsGuardBroken() ? 1 : 0, Mine->GetBlockFxKind(), Mine->IsClashKnocked() ? 1 : 0));
			Mine->SetAltHeld(false);
		}
		break;
	case 3:  // 霰弹打断：对方从 11 米冲过来，突进途中开一枪
		if (At(1.f)) Its->RequestLight();
		if (At(1.08f)) Mine->SetTrigger(true);
		if (At(1.12f)) Mine->SetTrigger(false);
		if (At(1.2f)) Log(FString::Printf(TEXT("开枪后对方还在出刀 %d（应为 0）"), Its->IsMeleeActive() ? 1 : 0));
		if (At(1.8f)) Log(FString::Printf(TEXT("我掉血 %.0f（被打断就是 0），这一枪打出 %.0f，吸血后装甲 %.0f（满 %.0f）"), MyLost, MyDealt, Me->GetHealth()->GetAP(), MyAP));
		break;
	case 4:  // 大剑霸体重斩：重斩出招中开一枪，打不断，伤害 ×0.6
		if (At(1.f)) Its->RequestHeavy();
		if (At(1.1f)) Mine->SetTrigger(true);
		if (At(1.14f)) Mine->SetTrigger(false);
		if (At(1.3f)) Log(FString::Printf(TEXT("中枪后大剑还在出重斩 %d（应为 1）、霸体 %d，这一枪打出 %.0f"), Its->IsMeleeActive() ? 1 : 0, Its->IsSuperArmor() ? 1 : 0, MyDealt));
		if (At(2.3f)) Log(FString::Printf(TEXT("我被重斩砍掉 %.0f（1400 ×1.45 = 2030）"), MyLost));
		break;
	case 5:  // 三维突进向上：对方悬在 7 米高处，地面上出重斩
	case 6:  // 三维突进向下：自己在 8 米高处下落，对方在地面，出轻击一
		if (At(0.9f) && S == 6)
		{
			Me->SetActorLocation(Home + Dir * (U.Dist < 0.f ? DaggerTestDist() : U.Dist) + FVector(0.f, 0.f, U.MyHeight), false, nullptr, ETeleportType::TeleportPhysics);
			Me->GetCharacterMovement()->StopMovementImmediately();
		}
		if (At(0.85f) && S == 5)
		{
			// 木桩 4 秒没挨打、离家超过半米就瞬移回原位：先给它记一下 0 伤害的命中，再挂到空中
			Dummy->GetHealth()->ApplyHit(0.f, 0.f, nullptr, Dummy->GetActorLocation());
			Dummy->SetActorLocation(Home + FVector(0.f, 0.f, U.ItsHeight), false, nullptr, ETeleportType::TeleportPhysics);
			Dummy->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Dummy->GetCharacterMovement()->StopMovementImmediately();
		}
		if (At(1.f))
		{
			Z0 = Me->GetActorLocation().Z;
			if (S == 5)
			{
				Mine->RequestHeavy();
			}
			else
			{
				Mine->RequestLight();
			}
			Log(FString::Printf(TEXT("出招：离对方 %.1f 米（对方比我高 %.1f 米），我在空中 %d"), FVector::Dist(Me->GetActorLocation(), Dummy->GetActorLocation()) / 100.f,
				(Dummy->GetActorLocation().Z - Me->GetActorLocation().Z) / 100.f, Me->GetCharacterMovement()->IsFalling() ? 1 : 0));
		}
		if (At(1.1f) || At(1.2f) || At(1.3f) || At(1.45f))
		{
			Log(FString::Printf(TEXT("  第 %.2f 秒：高度 %+.1f 米，竖直速度 %.0f，空中 %d，离对方 %.1f 米"), T - 1.f, (Me->GetActorLocation().Z - Z0) / 100.f,
				Me->GetVelocity().Z, Me->GetCharacterMovement()->IsFalling() ? 1 : 0, FVector::Dist(Me->GetActorLocation(), Dummy->GetActorLocation()) / 100.f));
		}
		if (At(1.8f)) Log(FString::Printf(TEXT("砍中 %.0f"), MyDealt));
		break;
	case 7:  // 单手刀闪避斩：20 米外朝对方闪避一下，马上轻击
		if (At(1.f)) Me->QuickBoostToward(-Dir);
		if (At(1.1f))
		{
			Log(FString::Printf(TEXT("闪避后能出闪避斩 %d，离对方 %.1f 米"), Mine->CanDashSlash() ? 1 : 0, FVector::Dist(Me->GetActorLocation(), Dummy->GetActorLocation()) / 100.f));
			Mine->RequestLight();
		}
		if (At(1.9f)) Log(FString::Printf(TEXT("闪避斩打出 %.0f（单手刀轻击一 380 ×0.9 ×1.4 = 479），现在离对方 %.1f 米"), MyDealt,
			FVector::Dist(Me->GetActorLocation(), Dummy->GetActorLocation()) / 100.f));
		break;
	case 8:  // 双刀割裂：连砍 2 秒
	case 9:  // 快刀连斩：连砍 2 秒
		if (T > 1.f && T < 3.f && FMath::Fmod(T, 0.1f) < DeltaTime)
		{
			Mine->RequestLight();
		}
		if (S == 9 && Mine->IsMeleeActive() && Mine->GetMeleeTime() <= DeltaTime * 1.01f)
		{
			Log(FString::Printf(TEXT("  出第 %d 下：整招 %.3f 秒，连斩 %d 层"), Mine->GetComboStep(), Mine->GetMeleeDuration(), Me->GetEffects()->GetStacks(TEXT("Flurry"))));
		}
		if (At(3.f))
		{
			if (S == 8)
			{
				Log(FString::Printf(TEXT("对方割裂 %d 层，受到伤害倍数 %.2f；2 秒打出 %.0f"), Dummy->GetEffects()->GetStacks(TEXT("Rend")),
					Dummy->EffectScale(EMechStat::DamageTaken), MyDealt));
			}
			else
			{
				Log(FString::Printf(TEXT("我连斩 %d 层，移速倍数 %.3f；2 秒打出 %.0f"), Me->GetEffects()->GetStacks(TEXT("Flurry")), Me->EffectScale(EMechStat::MoveSpeed), MyDealt));
			}
		}
		break;
	case 10:  // 双持手枪（10-30 晚改）：弹匣空、在换弹 → 切刀重斩砍中 → 立即补满；11-01 闪避也装满（没有冷却）
		if (At(1.f)) Mine->DebugReload();
		if (At(1.2f)) Mine->RequestHeavy();
		if (At(1.62f)) Shot(TEXT("重击_装满"));
		if (At(2.f)) Log(FString::Printf(TEXT("重斩砍中后弹药 %d（应满 24）、还在换弹 %d、打出 %.0f（重斩 1400）、补满图形时间 %.2f 秒前"), Mine->GetAmmo(),
			Mine->IsReloading() ? 1 : 0, MyDealt, Me->GetWorld()->GetTimeSeconds() - Mine->GetLastRefillTime()));
		if (At(2.4f)) Mine->DebugReload();
		if (At(2.6f)) Me->QuickBoostToward(Me->GetActorRightVector());
		if (At(3.f)) Log(FString::Printf(TEXT("闪避后弹药 %d（应满 %d，11-01 闪避装满）、在换弹 %d"), Mine->GetAmmo(), Mine->GetMagazine(), Mine->IsReloading() ? 1 : 0));
		break;
	case 11:  // 狙击开镜：准星在头的侧面 0.9 米（机体胶囊半径约 0.57 米，不吸附就打空）→ 吸附到头；偏 3 米 → 打空
	{
		Mine->SetAltHeld(T > 0.9f);
		const FVector Head = Dummy->GetMechMesh()->GetBoneLocation(TEXT("head")) + FVector(0.f, 0.f, 14.f);
		const FVector Side = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
		AimAt(Head + Side * (T < 2.5f ? 90.f : 300.f));
		Mine->SetTrigger(At(1.6f) || At(3.2f));
		if (At(2.2f) || At(3.8f))
		{
			Log(FString::Printf(TEXT("准星偏 %s：打出 %.0f，部位 躯干 %d 头 %d 四肢 %d（打头 1400 ×1.5 = 2100）"), T < 2.5f ? TEXT("0.9 米") : TEXT("3 米"), MyDealt,
				Mine->DebugPartHits[0] - Parts[0], Mine->DebugPartHits[1] - Parts[1], Mine->DebugPartHits[2] - Parts[2]));
			Damage = Mine->DebugDamage;
			Parts[0] = Mine->DebugPartHits[0];
			Parts[1] = Mine->DebugPartHits[1];
			Parts[2] = Mine->DebugPartHits[2];
		}
		break;
	}
	case 12:  // 充能步枪：按住 1 秒充能（激光照着对方）→ 自动打出光束；再按住一半松开 = 按当时充能打出弱一发（10-30 晚改，以前是取消）
		Mine->SetTrigger((T > 1.f && T < 2.3f) || (T > 2.6f && T < 3.1f));
		if (At(1.5f)) Log(FString::Printf(TEXT("充能到 %.2f，激光已经打出 %.0f"), Mine->GetCharge(), MyDealt));
		if (At(1.7f)) Shot(TEXT("充能激光"));
		if (Mine->GetLastShotTime() > LastShot && T > 1.f && T < 2.5f)
		{
			LastShot = Mine->GetLastShotTime();
			Shot(TEXT("光束"));
			Log(FString::Printf(TEXT("光束打出（按住后 %.2f 秒），连激光一共打出 %.0f（激光约 120 + 光束 1150），弹药 %d"), T - 1.f, MyDealt, Mine->GetAmmo()));
		}
		if (At(3.08f))
		{
			Z0 = Mine->GetCharge();  // 借 Z0 记松开前的充能
			Damage = Mine->DebugDamage;
		}
		if (At(3.6f))
		{
			Log(FString::Printf(TEXT("充到 %.2f 松开：打出 %.0f（按充能应约 %.0f，不算激光），充能 %.2f（应为 0）、弹药 %d（应为 4）"), Z0, MyDealt,
				1150.f * MechWeapons::ChargeShotScale(Z0), Mine->GetCharge(), Mine->GetAmmo()));
		}
		break;
	case 13:  // 投掷匕首：命中轻微击退
		if (At(1.f))
		{
			DummyAt = Dummy->GetActorLocation();
			Mine->RequestLight();
		}
		if (At(1.8f)) Log(FString::Printf(TEXT("匕首打出 %.0f，对方被推开 %.0f 厘米"), MyDealt, FVector::Dist2D(Dummy->GetActorLocation(), DummyAt)));
		// 右键（10-30 改）：匕首不能格挡，右键是近身挥刀——走到 8 米按住右键，应该出轻击一并砍中，而不是举刀格挡
		if (At(2.f))
		{
			Me->SetActorLocation(Dummy->GetActorLocation() + Dir * 800.f, false, nullptr, ETeleportType::TeleportPhysics);
			Face(Me, Dummy->GetActorLocation());
			SlashFrom = MyDealt;
		}
		if (At(2.1f))
		{
			Mine->SetAltHeld(true);
			Mine->RequestDaggerSlash();
		}
		if (At(2.25f)) Log(FString::Printf(TEXT("右键：格挡中 %d（应为 0）、出招中 %d（应为 1）、第 %d 段"), Mine->IsGuarding() ? 1 : 0,
			Mine->IsMeleeActive() ? 1 : 0, Mine->GetComboStep()));
		if (At(2.3f)) Shot(TEXT("匕首右键挥刀"));
		if (At(3.f))
		{
			Mine->SetAltHeld(false);
			Log(FString::Printf(TEXT("右键挥刀打出 %.0f"), MyDealt - SlashFrom));
		}
		break;
	case 14:  // 充能步枪画面：自己站到木桩的 -Y 侧 15 米朝 +Y 看，木桩拿充能步枪朝世界 +X 充能、打光束（激光和光束横穿画面），截图看白天亮不亮
		if (At(0.05f))
		{
			FMechLoadout D = Dummy->GetLoadout();
			D.Shooter = EMechShooter::Marksman;
			Dummy->SetLoadout(D, true);
			Its->SetForcedTarget(nullptr);
			Its->AimOverride = FVector(1.f, 0.f, 0.f);
			Me->SetActorLocation(Home + FVector(0.f, -1500.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Me->SetActorRotation(FRotator(0.f, 90.f, 0.f));
		}
		AimAt(Dummy->GetActorLocation() + FVector(300.f, 0.f, 50.f));
		Its->SetTrigger(T > 1.f && T < 2.4f);
		if (At(1.75f)) Shot(TEXT("侧面_充能激光"));
		if (Its->GetLastShotTime() > LastShot && T > 1.f && T < 2.5f)
		{
			LastShot = Its->GetLastShotTime();
			Shot(TEXT("侧面_光束"));
			Log(TEXT("木桩打出光束（截图 侧面_光束）"));
		}
		if (At(3.f))
		{
			Its->AimOverride = FVector::ZeroVector;
			Its->SetTrigger(false);
		}
		break;
	default:
		break;
	}
}
