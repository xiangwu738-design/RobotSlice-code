#include "MechComms.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Sound/SoundBase.h"
#include "MechCharacter.h"
#include "MechCoreDirector.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"
#include "MechMeshComponent.h"
#include "MechPluginPod.h"
#include "MechSettings.h"

namespace MechCommsLocal
{
	/** 按住多久算「按住」出轮盘（再短就容易把点一下误判成按住）、双击间隔 */
	constexpr double HoldTime = 0.22;
	constexpr double DoubleTapTime = 0.35;
	constexpr float DeadDeg = UMechCommsComponent::WheelDead;
	constexpr float MaxDeg = UMechCommsComponent::WheelRange;
	/** 撤掉自己的标点：准星离它的图标多近算「对着」（像素，按 1080p） */
	constexpr float RemoveRadius = 30.f;
}

UMechCommsComponent::UMechCommsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

APlayerController* UMechCommsComponent::GetPC() const
{
	return Cast<APlayerController>(GetOwner());
}

void UMechCommsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	// 机体在的时候记下队伍和名字：被击毁后（团队淘汰里机体可能已经没了）标点、信息区照样按这一队算
	if (const AMechCharacter* M = Cast<AMechCharacter>(PC->GetPawn()))
	{
		LastTeam = M->GetTeam();
		LastName = M->DisplayName;
	}
	if (!PC->IsLocalController())
	{
		return;
	}
	InputTick();
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("MechPingTest"));
	if (bTest)
	{
		TestTick(DeltaTime);
	}
}

// ---------------------------------------------------------------- 队伍 / 名字
int32 UMechCommsComponent::GetServerTeam() const
{
	const APlayerController* PC = GetPC();
	if (const AMechCharacter* M = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr)
	{
		return M->GetTeam();
	}
	if (LastTeam >= 0)
	{
		return LastTeam;
	}
	if (const AMechPlayerController* MPC = Cast<AMechPlayerController>(PC))
	{
		return MPC->IsLocalController() ? MPC->GetMenuTeam() : FMath::Max(0, MPC->AssignedTeam);
	}
	return 0;
}

FString UMechCommsComponent::GetServerName() const
{
	const APlayerController* PC = GetPC();
	if (const AMechCharacter* M = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr)
	{
		return M->DisplayName;
	}
	if (!LastName.IsEmpty())
	{
		return LastName;
	}
	if (const AMechPlayerController* MPC = Cast<AMechPlayerController>(PC))
	{
		return MPC->ProfileName.IsEmpty() ? FString::Printf(TEXT("玩家 %d"), FMath::Max(1, MPC->PlayerNumber)) : MPC->ProfileName;
	}
	return TEXT("?");
}

UMechCommsComponent* MechComms::Get(const APlayerController* PC)
{
	return PC ? PC->FindComponentByClass<UMechCommsComponent>() : nullptr;
}

int32 MechComms::TeamOf(const APlayerController* PC)
{
	if (const UMechCommsComponent* C = Get(PC))
	{
		return C->GetServerTeam();
	}
	const AMechCharacter* M = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	return M ? M->GetTeam() : 0;
}

void MechComms::LookScale(const APlayerController* PC, float& OutYaw, float& OutPitch)
{
	{
		// 「每个鼠标计数转多少度」（带方向）；轮盘和旧版一样按它换算（旧版 = 灵敏度 x 引擎缩放）
		const float Sens = FMechSettings::Get().MouseSensitivity * FMechSettings::DegreesPerCount / FMechSettings::LookEngineExtra;
		float YS = 1.f, PS = 1.f;
		if (GetDefault<UInputSettings>()->bEnableLegacyInputScales && PC)
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			YS = PC->GetDeprecatedInputYawScale();
			PS = PC->GetDeprecatedInputPitchScale();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
		OutYaw = Sens * (YS < 0.f ? -1.f : 1.f);
		OutPitch = Sens * (PS < 0.f ? -1.f : 1.f);
	}
}

bool MechComms::WheelLook(const AMechCharacter* Mech, float Yaw, float Pitch)
{
	APlayerController* PC = Mech ? Cast<APlayerController>(Mech->GetController()) : nullptr;
	UMechCommsComponent* C = PC && PC->IsLocalController() ? Get(PC) : nullptr;
	if (!C || !C->IsWheelOpen())
	{
		return false;
	}
	float YS, PS;
	MechComms::LookScale(PC, YS, PS);
	C->AddWheelInput(Yaw * YS, Pitch * PS);
	return true;
}

// ---------------------------------------------------------------- 标点：按键
bool UMechCommsComponent::CanPingNow() const
{
	const APlayerController* PC = GetPC();
	const UWorld* World = GetWorld();
	if (!PC || !World || !PC->PlayerCameraManager)
	{
		return false;
	}
	if (const AMechPlayerController* MPC = Cast<AMechPlayerController>(PC); MPC && MPC->IsMenuOpen())
	{
		return false;
	}
	const AMechGameState* GS = World->GetGameState<AMechGameState>();
	if (GS && (GS->bMenuMode || GS->Phase == EMechRoundPhase::Draft || GS->Phase == EMechRoundPhase::Lobby))
	{
		return false;
	}
	const AMechCharacter* M = Cast<AMechCharacter>(PC->GetPawn());
	return !(M && M->IsViewerMode());
}

void UMechCommsComponent::InputTick()
{
	APlayerController* PC = GetPC();
	// 真实时间：顿帧、慢放不影响「按住多久」「双击间隔」
	const double Now = FPlatformTime::Seconds();
	if (!CanPingNow())
	{
		if (bWheel)
		{
			CloseWheel(false);
		}
		bHeld = false;
		return;
	}
	const bool bDown = MechKeys::IsDown(PC, EMechKey::Ping);
	if (bDown && !bHeld)
	{
		bHeld = true;
		PressTime = Now;
	}
	else if (bDown && bHeld)
	{
		if (!bWheel && Now - PressTime >= MechCommsLocal::HoldTime)
		{
			OpenWheel();
		}
	}
	else if (!bDown && bHeld)
	{
		bHeld = false;
		if (bWheel)
		{
			CloseWheel(true);
		}
		else
		{
			const bool bDouble = bLastTapPlaced && Now - LastTapTime < MechCommsLocal::DoubleTapTime;
			Tap(bDouble);
			LastTapTime = Now;
		}
	}
}

EMechPing UMechCommsComponent::Resolve(FVector& OutAt, AActor*& OutTarget, int32& OutCoreId, AMechPing*& OutRemove) const
{
	OutTarget = nullptr;
	OutCoreId = 0;
	OutRemove = nullptr;
	APlayerController* PC = GetPC();
	UWorld* World = GetWorld();
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const FVector Dir = PC->PlayerCameraManager->GetCameraRotation().Vector();
	int32 SX = 0, SY = 0;
	PC->GetViewportSize(SX, SY);
	const FVector2D Center(SX * 0.5f, SY * 0.5f);
	const float UI = FMath::Max(SY, 1) / 1080.f;
	const int32 MyTeam = MechComms::TeamOf(PC);
	auto Screen = [&](const FVector& P, FVector2D& Out) {
		return FVector::DotProduct(P - Cam, Dir) > 0.f && UGameplayStatics::ProjectWorldToScreen(PC, P, Out);
	};

	// 准星射线：地面、墙、机体都算（和自由瞄准一样的物体类型）
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechPing), false, PC->GetPawn());
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByObjectType(Hit, Cam, Cam + Dir * 80000.f, Objects, Params);
	OutAt = bHit ? Hit.ImpactPoint : Cam + Dir * 6000.f;
	AActor* HitActor = bHit ? Hit.GetActor() : nullptr;

	// 1) 对着自己的标点：撤掉（看图标的位置，地点类的图钉在地面点上方）
	for (TActorIterator<AMechPing> It(World); It; ++It)
	{
		if (!It->IsMine(PC) || It->IsActorBeingDestroyed())
		{
			continue;
		}
		FVector2D P;
		if (!Screen(It->GetPoint(), P))
		{
			continue;
		}
		const float Lift = MechPing::IsPlace(It->Kind) ? 34.f * UI : 0.f;
		if (FVector2D::Distance(P, Center) < MechCommsLocal::RemoveRadius * UI || FVector2D::Distance(P - FVector2D(0.f, Lift), Center) < MechCommsLocal::RemoveRadius * UI)
		{
			OutRemove = *It;
			return EMechPing::Count;
		}
	}

	// 2) 准星附近的敌人 / 物品：直接打中的优先，否则挑屏幕上离准星最近的（要看得见：敌人不能隔墙标）
	EMechPing BestKind = EMechPing::Count;
	float BestD = TNumericLimits<float>::Max();
	auto Sight = [&](const FVector& P, const AActor* Ignore) {
		FCollisionQueryParams SP(SCENE_QUERY_STAT(MechPingSight), false, PC->GetPawn());
		if (Ignore)
		{
			SP.AddIgnoredActor(Ignore);
		}
		FHitResult H;
		return !World->LineTraceSingleByObjectType(H, Cam, P, FCollisionObjectQueryParams(ECC_WorldStatic), SP);
	};
	auto Consider = [&](AActor* A, const FVector& P, EMechPing K, int32 Core, float Radius) {
		float D;
		if (A && A == HitActor)
		{
			D = -1.f;
		}
		else
		{
			FVector2D S;
			if (!Screen(P, S))
			{
				return;
			}
			D = FVector2D::Distance(S, Center);
			if (D > Radius * UI || D >= BestD || !Sight(P, A))
			{
				return;
			}
		}
		if (D < BestD)
		{
			BestD = D;
			BestKind = K;
			OutTarget = A;
			OutCoreId = Core;
			OutAt = A ? A->GetActorLocation() : P;
		}
	};
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		AMechCharacter* M = *It;
		if (M == PC->GetPawn() || M->GetTeam() == MyTeam || M->GetHealth()->IsDead())
		{
			continue;
		}
		Consider(M, M->GetMechMesh()->VisualPivot(), EMechPing::Enemy, 0, 70.f);
	}
	for (TActorIterator<AMechGrunt> It(World); It; ++It)
	{
		if (It->IsAlive())
		{
			Consider(*It, It->GetActorLocation(), EMechPing::Enemy, 0, 60.f);
		}
	}
	for (TActorIterator<AMechPluginPod> It(World); It; ++It)
	{
		Consider(*It, It->GetActorLocation() + FVector(0.f, 0.f, 60.f), EMechPing::Item, 0, 50.f);
	}
	for (TActorIterator<AMechCoreDirector> It(World); It; ++It)
	{
		for (const FMechCore& C : It->Cores)
		{
			if (C.State != EMechCoreState::Carried)
			{
				Consider(nullptr, C.Location, EMechPing::Item, C.Id, 55.f);
			}
		}
	}
	if (BestKind != EMechPing::Count)
	{
		return BestKind;
	}
	// 3) 别的：标地点（天空什么都不标）
	return bHit ? EMechPing::Go : EMechPing::Count;
}

void UMechCommsComponent::Tap(bool bDouble)
{
	FVector At;
	AActor* Target = nullptr;
	int32 CoreId = 0;
	AMechPing* Remove = nullptr;
	const EMechPing Kind = Resolve(At, Target, CoreId, Remove);
	if (bDouble)
	{
		// 双击：危险 / 有敌人在这——把第一下放的那个换掉，标在指着的位置（指着敌人就是敌人那里）
		if (Kind != EMechPing::Count || Remove)
		{
			ServerPing(EMechPing::Danger, At, nullptr, 0, true);
			PlayPingSound(true);
		}
		bLastTapPlaced = false;  // 第三下算新的一次
		return;
	}
	if (Remove)
	{
		ServerRemovePing(Remove);
		bLastTapPlaced = false;
		return;
	}
	if (Kind == EMechPing::Count)
	{
		bLastTapPlaced = false;
		return;
	}
	ServerPing(Kind, At, Target, CoreId, false);
	PlayPingSound(MechPing::IsThreat(Kind));
	bLastTapPlaced = true;
}

void UMechCommsComponent::OpenWheel()
{
	FVector At;
	AActor* Target = nullptr;
	int32 CoreId = 0;
	AMechPing* Remove = nullptr;
	const EMechPing Kind = Resolve(At, Target, CoreId, Remove);
	// 轮盘标在打开那一刻准星指着的地方（指着敌人就是敌人脚下）；指着天空时只有「需要支援」能用（标在自己身上）
	bWheelPointValid = Kind != EMechPing::Count || Remove;
	WheelPoint = Target ? Target->GetActorLocation() : At;
	bWheel = true;
	WheelTime = FPlatformTime::Seconds();
	WheelCursor = FVector2D::ZeroVector;
	WheelSlot = -1;
}

float UMechCommsComponent::GetWheelAge() const
{
	return bWheel ? float(FPlatformTime::Seconds() - WheelTime) : 0.f;
}

void UMechCommsComponent::AddWheelInput(float Yaw, float Pitch)
{
	WheelCursor += FVector2D(Yaw, Pitch);
	const float Len = WheelCursor.Size();
	if (Len > MechCommsLocal::MaxDeg)
	{
		WheelCursor *= MechCommsLocal::MaxDeg / Len;
	}
	if (WheelCursor.Size() < MechCommsLocal::DeadDeg)
	{
		WheelSlot = -1;
		return;
	}
	// 从正上方顺时针的角度（X 向右、Y 向上）
	const float A = FMath::Atan2(WheelCursor.X, WheelCursor.Y);
	const int32 Slot = FMath::RoundToInt(A / (2.f * PI / MechPing::WheelSlots));
	WheelSlot = ((Slot % MechPing::WheelSlots) + MechPing::WheelSlots) % MechPing::WheelSlots;
}

void UMechCommsComponent::CloseWheel(bool bPlace)
{
	const int32 Slot = WheelSlot;
	bWheel = false;
	WheelSlot = -1;
	if (!bPlace || Slot < 0)
	{
		return;  // 在正中松开 = 取消
	}
	const EMechPing Kind = MechPing::WheelItem(Slot);
	const APlayerController* PC = GetPC();
	if (Kind == EMechPing::Help)
	{
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		ServerPing(Kind, Pawn ? Pawn->GetActorLocation() : WheelPoint, nullptr, 0, false);
	}
	else if (bWheelPointValid)
	{
		ServerPing(Kind, WheelPoint, nullptr, 0, false);
	}
	else
	{
		return;
	}
	PlayPingSound(MechPing::IsThreat(Kind));
	bLastTapPlaced = false;
}

void UMechCommsComponent::PlayPingSound(bool bThreat)
{
	if (!bSoundLoaded)
	{
		bSoundLoaded = true;
		// 按路径加载（/Game/Audio 在打包目录里）：没导入时只是没声音
		PingSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/HUD/S_hud_thrust_ready.S_hud_thrust_ready"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (PingSound)
	{
		// 推进恢复那声是 660 → 990 Hz 两下：升高四成半变成短促清脆的「嘀嘀」（和推进恢复分得开）；敌情类只升一点，听着沉一些
		UGameplayStatics::PlaySound2D(this, PingSound, 0.7f * FMechSettings::Get().AlertVolume, bThreat ? 1.05f : 1.45f);
	}
}

// ---------------------------------------------------------------- 标点：服务器
void UMechCommsComponent::ServerPing_Implementation(EMechPing Kind, FVector_NetQuantize At, AActor* Target, int32 CoreId, bool bReplaceRecent)
{
	APlayerController* PC = GetPC();
	if (!PC || Kind >= EMechPing::Count)
	{
		return;
	}
	// 防刷：2 秒内最多 6 个
	const double Now = FPlatformTime::Seconds();
	RecentPings.RemoveAll([Now](double T) { return Now - T > 2.0; });
	if (RecentPings.Num() >= 6)
	{
		return;
	}
	RecentPings.Add(Now);
	AMechPing::Place(PC, Kind, At, Target, CoreId, bReplaceRecent);
}

void UMechCommsComponent::ServerRemovePing_Implementation(AMechPing* Ping)
{
	if (Ping && Ping->GetOwner() == GetPC())
	{
		UE_LOG(LogTemp, Display, TEXT("[ping] %s 撤掉了「%s」"), *GetServerName(), MechPing::Word(Ping->Kind));
		Ping->Destroy();
	}
}

int32 UMechCommsComponent::CountMyPings() const
{
	int32 N = 0;
	for (TActorIterator<AMechPing> It(GetWorld()); It; ++It)
	{
		N += It->IsMine(GetPC()) && !It->IsActorBeingDestroyed() ? 1 : 0;
	}
	return N;
}

// ---------------------------------------------------------------- 信息区：本机
void UMechCommsComponent::ClientFeed_Implementation(const FMechFeedEvent& E)
{
	++DebugFeedReceived;
	AddLine(E);
}

void UMechCommsComponent::AddLine(const FMechFeedEvent& E)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (E.Kind == EMechFeed::Ping)
	{
		// 同一个人 0.8 秒内又标了（双击换成「危险」）：改写那一行，不另起一行
		for (int32 i = Lines.Num() - 1; i >= 0 && Now - Lines[i].Time < 0.8f; --i)
		{
			if (Lines[i].E.Kind == EMechFeed::Ping && Lines[i].E.A == E.A)
			{
				Lines[i].E = E;
				return;
			}
		}
	}
	FMechFeedLine& L = Lines.AddDefaulted_GetRef();
	L.E = E;
	L.Time = Now;
	while (Lines.Num() > 12)
	{
		Lines.RemoveAt(0);
	}
}

void UMechCommsComponent::OnPingArrived(AMechPing* Ping)
{
	++DebugArrived;
	APlayerController* PC = GetPC();
	if (!Ping || !PC || Ping->GetAge() > 2.f || Ping->IsMine(PC))
	{
		return;  // 自己标的按键时已经响过、信息区也不记；加入晚了补来的旧标点不响
	}
	PlayPingSound(MechPing::IsThreat(Ping->Kind));
	FMechFeedEvent E;
	E.Kind = EMechFeed::Ping;
	E.Icon = static_cast<uint8>(Ping->Kind);
	E.A = Ping->ByName;
	E.TeamA = Ping->Team;
	E.Num = Ping->IsCore() ? 1 : 0;
	if (const AMechPluginPod* Pod = Cast<AMechPluginPod>(Ping->Target))
	{
		E.Plugin = Pod->Plugin;
	}
	AddLine(E);
}
