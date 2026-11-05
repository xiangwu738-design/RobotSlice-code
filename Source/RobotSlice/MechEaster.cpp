// 彩蛋调度（11-02，说明见 MechEaster.h）

#include "MechEaster.h"

#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechEasterBot.h"
#include "MechEasterProp.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechLoop.h"
#include "MechRoom.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------- 公用
namespace MechEaster
{
	UObject* LoadQuiet(const TCHAR* Path, UClass* Class)
	{
		// 缺的资源记下来，不每次都去磁盘上找（狗群一次生成三十几只，每只都要查动作片段）
		static TSet<FString> Missing;
		FString Full(Path);
		if (!Full.Contains(TEXT(".")))
		{
			Full += TEXT(".") + FPackageName::GetShortName(Full);
		}
		if (Missing.Contains(Full))
		{
			return nullptr;
		}
		UObject* Obj = LoadObject<UObject>(nullptr, *Full, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Obj && Class && !Obj->IsA(Class))
		{
			Obj = nullptr;
		}
		if (!Obj)
		{
			Missing.Add(Full);
		}
		return Obj;
	}

	bool PlaceholderOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechEasterPlaceholder"));
		return bOn;
	}

	bool DroneReady()
	{
		if (Load<UStaticMesh>(DroneBodyPath()))
		{
			return true;
		}
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogTemp, Display, TEXT("[easter] 缺无人机模型 %s%s"), DroneBodyPath(), PlaceholderOn() ? TEXT("，用方块代替（-MechEasterPlaceholder）") : TEXT("，不生成无人机"));
		}
		return PlaceholderOn();
	}

	bool DogReady()
	{
		if (Load<USkeletalMesh>(DogMeshPath()))
		{
			return true;
		}
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogTemp, Display, TEXT("[easter] 缺机械狗模型 %s%s"), DogMeshPath(), PlaceholderOn() ? TEXT("，用方块代替（-MechEasterPlaceholder）") : TEXT("，不生成机械狗"));
		}
		return PlaceholderOn();
	}

	bool Enabled(const UWorld* World)
	{
		static const bool bOff = FParse::Param(FCommandLine::Get(), TEXT("MechEasterOff"));
		if (bOff || !World)
		{
			return false;
		}
		// 从房间开的这一场：看房主的开关（默认开）；直接进图（快速对战、测试）一律开
		if (const UMechRoomSubsystem* Sub = UMechRoomSubsystem::Get(World); Sub && Sub->bActive)
		{
			return Sub->Settings.bEaster;
		}
		return true;
	}

	EMechEasterMap MapOf(const UWorld* World)
	{
		if (!World)
		{
			return EMechEasterMap::None;
		}
		const FString Name = World->GetMapName();
		if (Name.EndsWith(TEXT("L_Nature")))
		{
			return EMechEasterMap::Nature;
		}
		if (Name.EndsWith(TEXT("L_Pillars")))
		{
			return EMechEasterMap::Pillars;
		}
		if (Name.EndsWith(TEXT("L_Soviet")))
		{
			return EMechEasterMap::Soviet;
		}
		if (Name.EndsWith(TEXT("L_OldStreet")) || Name.EndsWith(TEXT("L_MillenniumStreet")))
		{
			return EMechEasterMap::OldStreet;
		}
		return EMechEasterMap::None;
	}

	bool FightOn(const UWorld* World)
	{
		const AMechGameState* GS = World ? World->GetGameState<AMechGameState>() : nullptr;
		return GS && !GS->bMenuMode && !GS->bRoomMode && (GS->bRangeMode || GS->Phase == EMechRoundPhase::Fight);
	}

	float ServerNow(const UWorld* World)
	{
		if (!World)
		{
			return 0.f;
		}
		const AGameStateBase* GS = World->GetGameState();
		return GS ? GS->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	}

	bool LogOn()
	{
		static const bool bOn = FParse::Param(FCommandLine::Get(), TEXT("MechEasterLog"));
		return bOn;
	}

	const FString& TestWhat()
	{
		static const FString What = [] {
			FString V;
			FParse::Value(FCommandLine::Get(), TEXT("MechEasterTest="), V);
			return V.ToLower();
		}();
		return What;
	}

	bool IsLockable(const AActor* A)
	{
		const AMechEasterBot* B = Cast<AMechEasterBot>(A);
		return B && B->IsAlive() && B->IsHostile();
	}

	bool IsTargetMech(const AMechCharacter* M)
	{
		return M && M->GetHealth() && !M->GetHealth()->IsDead() && !M->IsTrainingDummy();
	}

	bool SteerMissile(AActor* Missile, AMechCharacter* Shooter, UProjectileMovementComponent* Movement, TWeakObjectPtr<AActor>& RunTarget)
	{
		if (!Missile || !Movement || !Missile->HasAuthority())
		{
			return false;
		}
		UWorld* World = Missile->GetWorld();
		AMechEasterBot* Bot = Cast<AMechEasterBot>(RunTarget.Get());
		if (!Bot)
		{
			// 刚发射（1 秒内）、玩家打的、没被肉鸽占用：找准星附近的彩蛋敌人（同锁定：放宽的锥、射程、看得见）
			if (RunTarget.IsValid() || !Shooter || !Shooter->IsPlayerControlled() || !Shooter->GetCombat() || Missile->GetGameTimeSinceCreation() > 1.f)
			{
				return false;
			}
			const FVector Eye = Shooter->GetActorLocation() + FVector(0.f, 0.f, 120.f);
			const FVector Aim = (Shooter->GetCombat()->GetFreeAimPoint() - Eye).GetSafeNormal();
			const float Range = Shooter->GetCombat()->GetLockRange();
			float Best = LockKeepCone;
			for (TActorIterator<AMechEasterBot> It(World); It; ++It)
			{
				if (!IsLockable(*It))
				{
					continue;
				}
				const FVector P = MechLoop::Near(World, It->GetActorLocation(), Eye);
				const float D = FVector::Dist(P, Eye);
				const float Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Aim | (P - Eye).GetSafeNormal(), -1.f, 1.f)));
				if (D > Range || Ang >= Best)
				{
					continue;
				}
				FHitResult Hit;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterMissileLOS), false, Shooter);
				if (World->LineTraceSingleByObjectType(Hit, Eye, P, FCollisionObjectQueryParams(ECC_WorldStatic), Q))
				{
					continue;
				}
				Best = Ang;
				Bot = *It;
			}
			if (!Bot)
			{
				return false;
			}
			RunTarget = Bot;
			if (LogOn())
			{
				UE_LOG(LogTemp, Display, TEXT("[easter] 导弹追 %s（%s 打的，偏 %.0f 度）"), *Bot->GetName(), *Shooter->DisplayName, Best);
			}
		}
		if (!Bot->IsAlive())
		{
			RunTarget = nullptr;
			Movement->bIsHomingProjectile = false;
			return false;
		}
		// 同肉鸽追杂兵：转得急一点、飞得慢一点（默认的转向是给能闪的机体调的，小目标会绕着转）
		Movement->HomingTargetComponent = Bot->GetRootComponent();
		Movement->bIsHomingProjectile = true;
		Movement->HomingAccelerationMagnitude = 30000.f;
		Movement->MaxSpeed = 4500.f;
		return MechLoop::Dist(World, Missile->GetActorLocation(), Bot->GetActorLocation()) < 300.f;
	}
}

// ---------------------------------------------------------------- 控制台（房主 / 单机）
namespace MechEasterCmd
{
	AMechEasterDirector* Dir(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogTemp, Display, TEXT("[easter] 控制台命令只能在房主 / 单机上用"));
			return nullptr;
		}
		return AMechEasterDirector::Get(World, true);
	}

	APawn* Me(UWorld* World)
	{
		APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	FVector Front(UWorld* World, float Dist)
	{
		const APawn* P = Me(World);
		if (!P)
		{
			return FVector::ZeroVector;
		}
		const FVector Fwd = P->GetControlRotation().Vector().GetSafeNormal2D();
		return P->GetActorLocation() + Fwd * Dist;
	}

	int32 WeaponArg(const TArray<FString>& Args, int32 Index)
	{
		if (!Args.IsValidIndex(Index))
		{
			return -1;
		}
		const FString W = Args[Index].ToLower();
		return W == TEXT("gun") ? 0 : W == TEXT("rocket") ? 1 : W == TEXT("grenade") ? 2 : W == TEXT("cannon") ? 3 : -1;
	}

	int32 CountArg(const TArray<FString>& Args, int32 Default)
	{
		return Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 60) : Default;
	}

	FAutoConsoleCommandWithWorldAndArgs GDrones(TEXT("mech.Easter.Drones"),
		TEXT("在面前刷一群无人机：mech.Easter.Drones [台数] [gun|rocket|grenade|cannon|mix]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->SpawnDrones(Front(World, 1500.f), CountArg(Args, 6), WeaponArg(Args, 1), MechEaster::DroneLife);
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs GDogs(TEXT("mech.Easter.Dogs"), TEXT("在面前放几只友好的机械狗（苏联那种）：mech.Easter.Dogs [只数]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->SpawnFriendlyDogs(Front(World, 800.f), CountArg(Args, 3));
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs GStampede(TEXT("mech.Easter.Stampede"), TEXT("放狗群（老街那种，沿 X 跑）：mech.Easter.Stampede [只数]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->SpawnStampede(Front(World, 0.f), CountArg(Args, MechEaster::StampedeDogs));
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs GPlate(TEXT("mech.Easter.Plate"), TEXT("当作这张图的压力板被踩下（荒野 / 石林）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->TriggerPlate(Me(World));
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs GDoor(TEXT("mech.Easter.Door"), TEXT("老街：打破门、打中按钮"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->DebugDoor();
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs GClear(TEXT("mech.Easter.Clear"), TEXT("清掉无人机、狗群和它们的子弹"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->ClearHostiles();
			}
		}));
	FAutoConsoleCommandWithWorldAndArgs GInfo(TEXT("mech.Easter.Info"), TEXT("日志里列出彩蛋的位置和场上的无人机 / 狗"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World) {
			if (AMechEasterDirector* D = Dir(World))
			{
				D->LogInfo();
			}
		}));
}

// ---------------------------------------------------------------- 子系统
void UMechEasterSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || InWorld.GetNetMode() == NM_Client)
	{
		return;
	}
	const EMechEasterMap Map = MechEaster::MapOf(&InWorld);
	if (Map != EMechEasterMap::None && !MechEaster::Enabled(&InWorld))
	{
		UE_LOG(LogTemp, Display, TEXT("[easter] 这张图有彩蛋，但房间里关掉了（或 -MechEasterOff）"));
		return;
	}
	if (Map != EMechEasterMap::None || !MechEaster::TestWhat().IsEmpty())
	{
		AMechEasterDirector::Get(&InWorld, true);
	}
}

// ---------------------------------------------------------------- 调度
namespace MechEasterLocal
{
	template <typename T>
	T* Find(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> F(Path);
		return F.Object;
	}

	/** 标记上的数字：Key=123 */
	float TagValue(const AActor* A, const TCHAR* Key, float Default)
	{
		const FString Prefix = FString(Key) + TEXT("=");
		for (const FName& T : A->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(Prefix))
			{
				return FCString::Atof(*S.Mid(Prefix.Len()));
			}
		}
		return Default;
	}

	FString TagString(const AActor* A, const TCHAR* Key)
	{
		const FString Prefix = FString(Key) + TEXT("=");
		for (const FName& T : A->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(Prefix))
			{
				return S.Mid(Prefix.Len());
			}
		}
		return FString();
	}

	/** 标记自己带看得见的模型（不用画占位） */
	bool HasVisibleMesh(const AActor* A)
	{
		if (!A || A->IsHidden())
		{
			return false;
		}
		TArray<UStaticMeshComponent*> Comps;
		A->GetComponents<UStaticMeshComponent>(Comps);
		for (const UStaticMeshComponent* C : Comps)
		{
			if (C && C->GetStaticMesh() && C->IsVisible())
			{
				return true;
			}
		}
		return false;
	}

	const TCHAR* WeaponName(EMechEasterWeapon W)
	{
		switch (W)
		{
		case EMechEasterWeapon::Gun:
			return TEXT("机枪");
		case EMechEasterWeapon::Rocket:
			return TEXT("火箭");
		case EMechEasterWeapon::Grenade:
			return TEXT("榴弹");
		case EMechEasterWeapon::Cannon:
			return TEXT("火炮");
		}
		return TEXT("?");
	}

	// 各种武器（无人机的「肩部武器」）：伤害按机体 1 万装甲的量级给，一群打一个人十几秒能打穿
	constexpr float GunSpeed = 9000.f;
	constexpr float GunDamage = 80.f;
	constexpr float RocketSpeed = 3600.f;
	constexpr float RocketDamage = 650.f;
	constexpr float RocketRadius = 450.f;
	constexpr float RocketTurn = 35.f;  // 度/秒：侧闪甩得掉
	constexpr float GrenadeGravity = 1600.f;
	constexpr float GrenadeDamage = 720.f;
	constexpr float GrenadeRadius = 520.f;
	constexpr float CannonSpeed = 11000.f;
	constexpr float CannonGravity = 300.f;
	constexpr float CannonDamage = 520.f;
	constexpr float CannonRadius = 320.f;
}

using namespace MechEasterLocal;

AMechEasterDirector::AMechEasterDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	auto MakeISM = [this, Root](const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Mat) {
		UInstancedStaticMeshComponent* C = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		C->SetupAttachment(Root);
		C->SetStaticMesh(Mesh);
		if (Mat)
		{
			C->SetMaterial(0, Mat);
		}
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetHasPerInstancePrevTransforms(true);
		return C;
	};
	UMaterialInterface* Tracer = Find<UMaterialInterface>(TEXT("/Game/Weapons/Materials/MI_Tracer"));
	TracerISM = MakeISM(TEXT("EasterTracers"), Find<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder")), Tracer);
	ShellISM = MakeISM(TEXT("EasterShells"), Find<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere")), Tracer);
	RocketISM = MakeISM(TEXT("EasterRockets"), Find<UStaticMesh>(TEXT("/Game/Weapons/Meshes/SM_Missile")), nullptr);
	HitFX = Find<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	BlastFX = Find<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01"));
	BlastSound = Find<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small"));
	ShotSound = Find<USoundBase>(TEXT("/Game/Audio/Combat/S_rifle_fire_a"));
	RocketSound = Find<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small"));
}

AMechEasterDirector* AMechEasterDirector::Get(UWorld* World, bool bCreate)
{
	if (!World)
	{
		return nullptr;
	}
	UMechEasterSubsystem* Sub = World->GetSubsystem<UMechEasterSubsystem>();
	if (Sub && Sub->Director.IsValid())
	{
		return Sub->Director.Get();
	}
	for (TActorIterator<AMechEasterDirector> It(World); It; ++It)
	{
		if (Sub)
		{
			Sub->Director = *It;
		}
		return *It;
	}
	if (!bCreate || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMechEasterDirector* D = World->SpawnActor<AMechEasterDirector>(AMechEasterDirector::StaticClass(), FTransform::Identity, P);
	if (Sub)
	{
		Sub->Director = D;
	}
	UE_LOG(LogTemp, Display, TEXT("[easter] 彩蛋调度开在 %s"), *World->GetMapName());
	return D;
}

void AMechEasterDirector::BeginPlay()
{
	Super::BeginPlay();
	if (UMechEasterSubsystem* Sub = GetWorld()->GetSubsystem<UMechEasterSubsystem>())
	{
		Sub->Director = this;
	}
	// 子弹颜色（各端自己建）：红色曳光（维克托的红）、橙色炮弹
	for (int32 k = 0; k < 2; ++k)
	{
		UInstancedStaticMeshComponent* C = k == 0 ? TracerISM.Get() : ShellISM.Get();
		if (UMaterialInterface* M = C ? C->GetMaterial(0) : nullptr)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(M, this);
			MID->SetVectorParameterValue(TEXT("Emissive"), k == 0 ? FLinearColor(1.f, 0.08f, 0.04f) : FLinearColor(1.f, 0.5f, 0.12f));
			MID->SetScalarParameterValue(TEXT("EmissiveStrength"), k == 0 ? 50.f : 70.f);
			C->SetMaterial(0, MID);
		}
	}
}

AActor* AMechEasterDirector::FindTagged(FName Tag) const
{
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(Tag))
		{
			return *It;
		}
	}
	return nullptr;
}

bool AMechEasterDirector::TraceDown(const FVector& XY, float FromZ, FHitResult& Out, const AActor* OnlyActor) const
{
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterGround), false);
	FVector From(XY.X, XY.Y, FromZ);
	const FVector To(XY.X, XY.Y, FromZ - 300000.f);
	for (int32 Try = 0; Try < 6; ++Try)
	{
		if (!GetWorld()->LineTraceSingleByChannel(Out, From, To, ECC_Visibility, Q))
		{
			return false;
		}
		AActor* A = Out.GetActor();
		const UPrimitiveComponent* C = Out.GetComponent();
		// 隐形墙、隐藏的碰撞方块（老街树干、楼的碰撞盒）、彩蛋自己的东西都跳过，接着往下找
		const bool bSkip = (A && (A->IsHidden() || A->IsA<AMechEasterBot>() || A->IsA<AMechEasterProp>() || A->IsA<AMechCharacter>())) || (C && !C->IsVisible());
		if (!bSkip && (!OnlyActor || A == OnlyActor))
		{
			return true;
		}
		if (A)
		{
			Q.AddIgnoredActor(A);
		}
		From = Out.ImpactPoint - FVector(0.f, 0.f, 5.f);
	}
	return false;
}

bool AMechEasterDirector::HighestFlat(const FVector& Center, const FVector2D& Half, int32 N, float FromZ, const AActor* OnlyActor, FVector& Out) const
{
	N = FMath::Max(N, 3);
	TArray<float> H;
	TArray<FVector> P;
	H.Init(-1.e9f, N * N);
	P.Init(FVector::ZeroVector, N * N);
	for (int32 i = 0; i < N; ++i)
	{
		for (int32 j = 0; j < N; ++j)
		{
			const FVector XY(Center.X + Half.X * (2.f * i / (N - 1) - 1.f), Center.Y + Half.Y * (2.f * j / (N - 1) - 1.f), 0.f);
			FHitResult Hit;
			if (TraceDown(XY, FromZ, Hit, OnlyActor) && Hit.ImpactNormal.Z > 0.8f)
			{
				H[i * N + j] = Hit.ImpactPoint.Z;
				P[i * N + j] = Hit.ImpactPoint;
			}
		}
	}
	// 平地：自己和上下左右四个点都打到、高度差都在 1.5 米内（窄墙头、树尖、柱子尖不算）
	int32 Best = INDEX_NONE;
	int32 BestAny = INDEX_NONE;
	for (int32 i = 0; i < N; ++i)
	{
		for (int32 j = 0; j < N; ++j)
		{
			const float Z = H[i * N + j];
			if (Z < -1.e8f)
			{
				continue;
			}
			if (BestAny == INDEX_NONE || Z > H[BestAny])
			{
				BestAny = i * N + j;
			}
			bool bFlat = i > 0 && j > 0 && i < N - 1 && j < N - 1;
			for (const FIntPoint& D : {FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)})
			{
				if (!bFlat)
				{
					break;
				}
				const float ZN = H[(i + D.X) * N + (j + D.Y)];
				bFlat = ZN > -1.e8f && FMath::Abs(ZN - Z) < 150.f;
			}
			if (bFlat && (Best == INDEX_NONE || Z > H[Best]))
			{
				Best = i * N + j;
			}
		}
	}
	const int32 Pick = Best != INDEX_NONE ? Best : BestAny;
	if (Pick == INDEX_NONE)
	{
		return false;
	}
	Out = P[Pick];
	return true;
}

bool AMechEasterDirector::FindNaturePlate(FVector& Out) const
{
	// 塔 = 最高的那栋废墟（ue_build_nature.py 摆的，带 Nature 标签的静态网格）。优先场地范围里的，没有就离场地中心最近的一栋
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const bool bArena = GS && !GS->MapExtent.IsNearlyZero();
	const FVector Center = bArena ? GS->MapCenter : FVector::ZeroVector;
	AActor* Best = nullptr;
	float BestH = 0.f;
	AActor* Nearest = nullptr;
	float NearestD = 1.e12f;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("Nature")))
		{
			continue;
		}
		FVector O, E;
		It->GetActorBounds(false, O, E);
		const bool bIn = !bArena || (FMath::Abs(O.X - Center.X) < GS->MapExtent.X && FMath::Abs(O.Y - Center.Y) < GS->MapExtent.Y);
		if (bIn && E.Z * 2.f > BestH)
		{
			BestH = E.Z * 2.f;
			Best = *It;
		}
		const float D = FVector::Dist2D(O, Center);
		if (D < NearestD)
		{
			NearestD = D;
			Nearest = *It;
		}
	}
	AActor* Tower = Best ? Best : Nearest;
	if (!Tower)
	{
		return false;
	}
	FVector O, E;
	Tower->GetActorBounds(false, O, E);
	UE_LOG(LogTemp, Display, TEXT("[easter] 荒野：塔 = %s（高 %.0f 米，%s）"), *Tower->GetName(), E.Z / 50.f, Best ? TEXT("场地里最高的废墟") : TEXT("场地里没有废墟，用离中心最近的一栋"));
	return HighestFlat(O, FVector2D(E.X, E.Y) * 0.8f, 9, O.Z + E.Z + 1000.f, Tower, Out);
}

bool AMechEasterDirector::FindPillarsPlate(FVector& Out) const
{
	// 山顶 = 场地范围里最高的一块平地：先粗找（40×40），再在最高点周围 30 米细找
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const bool bArena = GS && !GS->MapExtent.IsNearlyZero();
	const FVector Center = bArena ? GS->MapCenter : FVector::ZeroVector;
	const FVector2D Half = bArena ? GS->MapExtent : FVector2D(30000.f, 30000.f);
	FVector Rough;
	if (!HighestFlat(Center, Half * 0.95f, 40, Center.Z + 200000.f, nullptr, Rough))
	{
		return false;
	}
	if (!HighestFlat(Rough, FVector2D(3000.f, 3000.f), 11, Rough.Z + 20000.f, nullptr, Out))
	{
		Out = Rough;
	}
	return true;
}

void AMechEasterDirector::FindSovietSpots()
{
	Spots.Reset();
	// 1) 用户摆的标记：EasterDogSpot（Kind=Slide|Seesaw|Sit、Len=、Height=，朝向 = 滑下去 / 板子的方向）
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("EasterDogSpot")))
		{
			continue;
		}
		FMechEasterSpot S;
		const FString K = TagString(*It, TEXT("Kind")).ToLower();
		S.Kind = static_cast<uint8>(K == TEXT("slide") ? 0 : K == TEXT("seesaw") ? 1 : 2);
		S.At = It->GetActorLocation();
		S.Dir = It->GetActorForwardVector().GetSafeNormal2D();
		S.Len = TagValue(*It, TEXT("Len"), S.Kind == 0 ? 450.f : 380.f);
		S.Height = TagValue(*It, TEXT("Height"), S.Kind == 0 ? 220.f : 60.f);
		Spots.Add(S);
	}
	// 2) 没有标记：按网格名找中院的滑梯、跷跷板、长椅、秋千（tools/soviet_plan.py 的 SVS_Slide / SVS_Seesaw / SVS_Bench / SVS_Swings）
	if (Spots.IsEmpty())
	{
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* C = It->GetStaticMeshComponent();
			const UStaticMesh* M = C ? C->GetStaticMesh() : nullptr;
			if (!M)
			{
				continue;
			}
			const FString Name = M->GetName();
			const bool bSlide = Name.Contains(TEXT("SVS_Slide"));
			const bool bSeesaw = Name.Contains(TEXT("SVS_Seesaw"));
			const bool bSit = Name.Contains(TEXT("SVS_Bench")) || Name.Contains(TEXT("SVS_Swings"));
			if (!bSlide && !bSeesaw && !bSit)
			{
				continue;
			}
			// 模型自己的包围盒：长的那条水平轴当滑道 / 板子方向
			const FBox B = M->GetBoundingBox();
			const FTransform& Xf = It->GetActorTransform();
			const FVector Ext = B.GetExtent() * Xf.GetScale3D().GetAbs();
			const bool bAlongX = Ext.X >= Ext.Y;
			const FVector LocalAxis = bAlongX ? FVector::ForwardVector : FVector::RightVector;
			FMechEasterSpot S;
			S.Dir = Xf.TransformVectorNoScale(LocalAxis).GetSafeNormal2D();
			if (bSlide)
			{
				// 梯子在哪头看不出来：先当 +轴那头是梯子顶（不对的话摆一个 EasterDogSpot 标记，见 README）
				S.Kind = 0;
				FVector Top = B.GetCenter();
				Top.Z = B.Min.Z + (B.Max.Z - B.Min.Z) * 0.8f;
				if (bAlongX)
				{
					Top.X = B.Max.X - (B.Max.X - B.Min.X) * 0.1f;
				}
				else
				{
					Top.Y = B.Max.Y - (B.Max.Y - B.Min.Y) * 0.1f;
				}
				S.At = Xf.TransformPosition(Top);
				S.Dir = -S.Dir;
				S.Len = (bAlongX ? Ext.X : Ext.Y) * 2.f * 0.8f;
				S.Height = (B.Max.Z - B.Min.Z) * Xf.GetScale3D().Z * 0.8f;
			}
			else if (bSeesaw)
			{
				S.Kind = 1;
				S.At = Xf.TransformPosition(B.GetCenter());
				S.Len = (bAlongX ? Ext.X : Ext.Y) * 2.f * 0.85f;
				S.Height = (B.Max.Z - B.Min.Z) * Xf.GetScale3D().Z;
			}
			else
			{
				// 长椅、秋千旁边蹲着（这些小品没有碰撞，狗不上去）
				S.Kind = 2;
				const FVector Side = FVector::CrossProduct(FVector::UpVector, S.Dir);
				S.At = Xf.TransformPosition(B.GetCenter()) + Side * ((bAlongX ? Ext.Y : Ext.X) + 120.f);
			}
			Spots.Add(S);
		}
	}
	// 3) 都没有：按 soviet_plan.py 的坐标（Blender 米，UE = (x, -y)）
	if (Spots.IsEmpty())
	{
		FMechEasterSpot Slide;
		Slide.Kind = 0;
		Slide.At = FVector(2000.f, -1300.f, 220.f);
		Slide.Dir = FVector(0.f, 1.f, 0.f);
		Spots.Add(Slide);
		FMechEasterSpot Seesaw;
		Seesaw.Kind = 1;
		Seesaw.At = FVector(-1400.f, 1300.f, 30.f);
		Seesaw.Dir = FRotator(0.f, -40.f, 0.f).Vector();
		Spots.Add(Seesaw);
		for (const FVector& P : {FVector(1500.f, -900.f, 0.f), FVector(-1500.f, 900.f, 0.f), FVector(2800.f, 0.f, 0.f)})
		{
			FMechEasterSpot Sit;
			Sit.Kind = 2;
			Sit.At = P;
			Spots.Add(Sit);
		}
		UE_LOG(LogTemp, Display, TEXT("[easter] 苏联：没找到滑梯 / 跷跷板，按 soviet_plan.py 的坐标"));
	}
	// 地面高度、狗的活动中心和范围
	FVector Sum = FVector::ZeroVector;
	for (FMechEasterSpot& S : Spots)
	{
		const float G = GroundZ(FVector(S.At.X, S.At.Y, S.At.Z + 300.f), nullptr);
		if (S.Kind == 2)
		{
			S.At.Z = G;
		}
		else
		{
			S.Height = FMath::Max(S.Kind == 0 ? 120.f : 30.f, S.At.Z - G);
			S.At.Z = G;  // 存地面高度，Height 另算
		}
		Sum += S.At;
	}
	DogHome = Sum / FMath::Max(1, Spots.Num());
	DogRoam = 1200.f;
	for (const FMechEasterSpot& S : Spots)
	{
		DogRoam = FMath::Max(DogRoam, FVector::Dist2D(S.At, DogHome) + 500.f);
	}
	DogRoam = FMath::Min(DogRoam, 4500.f);
	for (const FMechEasterSpot& S : Spots)
	{
		UE_LOG(LogTemp, Display, TEXT("[easter] 苏联：狗玩的地方 %s 在 %s 方向 %s 长 %.0f 高 %.0f"), S.Kind == 0 ? TEXT("滑梯") : S.Kind == 1 ? TEXT("跷跷板") : TEXT("蹲点"),
			*S.At.ToCompactString(), *S.Dir.ToCompactString(), S.Len, S.Height);
	}
}

void AMechEasterDirector::SetupOldStreet()
{
	// 街沿 X（循环），中线 Y = 场地中心；门在街边的楼面上
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const float CY = GS ? GS->MapCenter.Y : 0.f;
	const float HalfY = GS && GS->MapExtent.Y > 100.f ? GS->MapExtent.Y : 4000.f;
	UMechLoopSubsystem* LoopSub = GetWorld()->GetSubsystem<UMechLoopSubsystem>();
	const float Period = MechLoop::Period(GetWorld());
	const float CX = Period > 0.f && LoopSub ? LoopSub->GetCenter() : (GS ? GS->MapCenter.X : 0.f);

	AActor* DoorTag = FindTagged(TEXT("EasterDoor"));
	AActor* ButtonTag = FindTagged(TEXT("EasterButton"));
	FVector DoorAt;
	FVector Out;  // 门朝街的方向
	FVector DoorSize(40.f, 200.f, 320.f);
	if (DoorTag)
	{
		FVector O, E;
		DoorTag->GetActorBounds(false, O, E);
		Out = FVector(0.f, O.Y > CY ? -1.f : 1.f, 0.f);
		DoorAt = O + Out * (FMath::Min(E.X, E.Y) + 15.f);
		DoorSize = FVector(40.f, 2.f * FMath::Max(E.X, E.Y) + 30.f, 2.f * E.Z + 30.f);
	}
	else
	{
		// 没有标记：从街中线往两边打水平射线找楼面（隐形墙不挡可见性），门贴在楼面上，底边落地
		const float X = CX + (Period > 0.f ? Period * 0.27f : 2700.f);
		const float G = GroundZ(FVector(X, CY, 3000.f), nullptr);
		bool bFound = false;
		for (const float Side : {1.f, -1.f})
		{
			FHitResult Hit;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterFacade), false);
			const FVector From(X, CY, G + 160.f);
			const FVector To(X, CY + Side * (HalfY + 3000.f), G + 160.f);
			if (GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Q) && Hit.GetActor() && !Hit.GetActor()->IsHidden())
			{
				Out = FVector(0.f, -Side, 0.f);
				DoorAt = FVector(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, G + DoorSize.Z * 0.5f) + Out * 20.f;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			Out = FVector(0.f, -1.f, 0.f);
			DoorAt = FVector(X, CY + HalfY - 300.f, G + DoorSize.Z * 0.5f);
		}
		UE_LOG(LogTemp, Display, TEXT("[easter] 老街：地图里没有 EasterDoor 标记，%s，门放在 %s"), bFound ? TEXT("在街边楼面上找了一处") : TEXT("没找到楼面，放在街边"), *DoorAt.ToCompactString());
	}
	const FRotator Rot = Out.Rotation();
	Door = SpawnProp(MechEasterProp::Door, DoorAt, Rot, DoorSize, DoorTag);
	// 按钮：标记的位置，没有就放在门后面（门板中间往里一点）
	const FVector ButtonAt = ButtonTag ? ButtonTag->GetActorLocation() : DoorAt - Out * 30.f;
	Button = SpawnProp(MechEasterProp::Button, ButtonAt, Rot, FVector(30.f, 70.f, 70.f), ButtonTag);
}

AMechEasterProp* AMechEasterDirector::SpawnProp(uint8 PropKind, const FVector& At, const FRotator& Rot, const FVector& Size, AActor* Linked)
{
	AMechEasterProp* Prop = GetWorld()->SpawnActorDeferred<AMechEasterProp>(AMechEasterProp::StaticClass(), FTransform(Rot, At), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Prop)
	{
		return nullptr;
	}
	FName Tag = NAME_None;
	if (Linked)
	{
		Tag = PropKind == MechEasterProp::Plate ? FName(TEXT("EasterPlate")) : PropKind == MechEasterProp::Door ? FName(TEXT("EasterDoor")) : FName(TEXT("EasterButton"));
		Prop->Radius = TagValue(Linked, TEXT("Radius"), MechEaster::PlateRadius);
	}
	Prop->InitProp(PropKind, Size, !HasVisibleMesh(Linked), Tag, this);
	Prop->FinishSpawning(FTransform(Rot, At));
	UE_LOG(LogTemp, Display, TEXT("[easter] %s 在 %s%s"), PropKind == MechEasterProp::Plate ? TEXT("压力板") : PropKind == MechEasterProp::Door ? TEXT("门") : TEXT("按钮"),
		*At.ToCompactString(), Linked ? *FString::Printf(TEXT("（地图标记 %s）"), *Linked->GetName()) : TEXT("（占位）"));
	return Prop;
}

void AMechEasterDirector::Setup()
{
	bSetup = true;
	MapKind = MechEaster::Enabled(GetWorld()) ? MechEaster::MapOf(GetWorld()) : EMechEasterMap::None;
	switch (MapKind)
	{
	case EMechEasterMap::Nature:
	case EMechEasterMap::Pillars:
	{
		AActor* Tag = FindTagged(TEXT("EasterPlate"));
		FVector At = FVector::ZeroVector;
		bool bOk = Tag != nullptr;
		if (Tag)
		{
			At = Tag->GetActorLocation();
		}
		else
		{
			bOk = MapKind == EMechEasterMap::Nature ? FindNaturePlate(At) : FindPillarsPlate(At);
			if (!bOk)
			{
				UE_LOG(LogTemp, Display, TEXT("[easter] %s：没找到放压力板的地方，这张图的压力板关掉（可以摆一个 EasterPlate 标记）"), MapKind == EMechEasterMap::Nature ? TEXT("荒野") : TEXT("石林"));
			}
		}
		if (bOk)
		{
			Plate = SpawnProp(MechEasterProp::Plate, At + FVector(0.f, 0.f, 3.f), FRotator::ZeroRotator, FVector(260.f, 260.f, 10.f), Tag);
		}
		break;
	}
	case EMechEasterMap::Soviet:
		FindSovietSpots();
		break;
	case EMechEasterMap::OldStreet:
		SetupOldStreet();
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------- 每帧（服务器管触发和行为，各端画子弹）
void AMechEasterDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateShots(DeltaTime);
	if (!HasAuthority())
	{
		return;
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (!bSetup)
	{
		// 等 GameMode 算好场地（ComputeArena）再找位置
		if (!GS || (GS->MapExtent.IsNearlyZero() && GetGameTimeSinceCreation() < 3.f))
		{
			return;
		}
		Setup();
	}
	// 回合结束：清掉无人机、狗群和子弹（苏联的狗留着）
	const uint8 Phase = GS ? static_cast<uint8>(GS->Phase) : static_cast<uint8>(255);
	if (LastPhase == static_cast<uint8>(EMechRoundPhase::Fight) && Phase != LastPhase && GS && !GS->bRangeMode)
	{
		ClearHostiles();
	}
	LastPhase = Phase;
	const bool bFight = MechEaster::FightOn(GetWorld());
	// 苏联的狗：开局放（选配阶段也在院子里玩）
	if (MapKind == EMechEasterMap::Soviet && !bDogsSpawned && GS && !GS->bMenuMode)
	{
		bDogsSpawned = true;
		SpawnFriendlyDogs(DogHome, MechEaster::SovietDogs);
	}
	// 压力板：有机体站上去（打的时候才算）
	if (bFight && !bPlateUsed && Plate.IsValid())
	{
		const FVector P = Plate->GetActorLocation();
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			AMechCharacter* M = *It;
			if (!MechEaster::IsTargetMech(M))
			{
				continue;
			}
			const UCapsuleComponent* Cap = M->GetCapsuleComponent();
			const float Feet = M->GetActorLocation().Z - Cap->GetScaledCapsuleHalfHeight();
			if (FVector::Dist2D(M->GetActorLocation(), P) < Plate->Radius && FMath::Abs(Feet - P.Z) < 200.f)
			{
				TriggerPlate(M);
				break;
			}
		}
	}
	// 按钮打中后过一会儿放狗
	if (PendingStampede > 0.f)
	{
		PendingStampede -= DeltaTime;
		if (PendingStampede <= 0.f)
		{
			SpawnStampede(StampedeFrom, MechEaster::StampedeDogs);
		}
	}
	TestTick(DeltaTime);
	if (MechEaster::LogOn())
	{
		LogClock += DeltaTime;
		if (LogClock > 2.f)
		{
			LogClock = 0.f;
			int32 Counts[3] = {0, 0, 0};
			for (TActorIterator<AMechEasterBot> It(GetWorld()); It; ++It)
			{
				if (It->IsAlive())
				{
					++Counts[static_cast<int32>(It->GetBotRole())];
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[easter] 场上：无人机 %d，友好的狗 %d，狗群 %d，子弹 %d，压力板%s，门%s"), Counts[0], Counts[1], Counts[2], Shots.Num(),
				bPlateUsed ? TEXT("已触发") : Plate.IsValid() ? TEXT("没踩") : TEXT("无"), Door.IsValid() ? (Door->IsDone() ? TEXT("已破") : TEXT("完好")) : TEXT("无"));
		}
	}
}

void AMechEasterDirector::TestTick(float DeltaTime)
{
	const FString& What = MechEaster::TestWhat();
	if (What.IsEmpty() || bTestDone || !MechEaster::FightOn(GetWorld()))
	{
		return;
	}
	TestClock = FMath::Max(TestClock, 0.f) + DeltaTime;
	if (TestClock < 4.f)
	{
		return;
	}
	bTestDone = true;
	// 找一台机体（优先真人）当参照
	AMechCharacter* Ref = nullptr;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (MechEaster::IsTargetMech(*It) && (!Ref || It->IsPlayerControlled()))
		{
			Ref = *It;
		}
	}
	const FVector At = Ref ? Ref->GetActorLocation() + Ref->GetActorForwardVector() * 1500.f : GetActorLocation();
	UE_LOG(LogTemp, Display, TEXT("[easter] 测试 -MechEasterTest=%s，参照 %s"), *What, Ref ? *Ref->DisplayName : TEXT("无"));
	if (What == TEXT("drones"))
	{
		SpawnDrones(At, 6, -1, MechEaster::DroneLife);
	}
	else if (What == TEXT("guns"))
	{
		SpawnDrones(At, 6, 0, MechEaster::DroneLife);
	}
	else if (What == TEXT("dogs"))
	{
		SpawnFriendlyDogs(Ref ? Ref->GetActorLocation() + Ref->GetActorForwardVector() * 700.f : At, 3);
	}
	else if (What == TEXT("stampede"))
	{
		SpawnStampede(Ref ? Ref->GetActorLocation() : At, MechEaster::StampedeDogs);
	}
	else if (What == TEXT("plate"))
	{
		TriggerPlate(Ref);
	}
	else if (What == TEXT("door"))
	{
		DebugDoor();
	}
	LogInfo();
}

// ---------------------------------------------------------------- 触发
void AMechEasterDirector::TriggerPlate(AActor* By)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!bSetup)
	{
		Setup();
	}
	if (bPlateUsed)
	{
		UE_LOG(LogTemp, Display, TEXT("[easter] 压力板这一局已经踩过了"));
		return;
	}
	const AMechCharacter* M = Cast<AMechCharacter>(By);
	const FString Who = M ? M->DisplayName : FString(TEXT("有人"));
	const FVector At = Plate.IsValid() ? Plate->GetActorLocation() : By ? By->GetActorLocation() : GetActorLocation();
	bPlateUsed = true;
	if (Plate.IsValid())
	{
		Plate->SetDone(By);
	}
	if (MapKind == EMechEasterMap::Pillars)
	{
		Announce(FString::Printf(TEXT("%s 踩下了山顶的压力板——无人机群来了"), *Who));
		SpawnDrones(At, MechEaster::PillarsDrones, static_cast<int32>(EMechEasterWeapon::Gun), MechEaster::DroneLife);
	}
	else
	{
		Announce(FString::Printf(TEXT("%s 踩下了塔顶的压力板——维克托无人机群出动"), *Who));
		SpawnDrones(At, MechEaster::NatureDrones, -1, MechEaster::DroneLife);
	}
}

void AMechEasterDirector::OnDoorBroken(AActor* By)
{
	UE_LOG(LogTemp, Display, TEXT("[easter] 老街：门被打破了（%s），按钮露出来"), *GetNameSafe(By));
	if (Button.IsValid())
	{
		Button->SetArmed(true);
	}
}

void AMechEasterDirector::OnButtonShot(AActor* By)
{
	if (bStampedeUsed)
	{
		return;
	}
	bStampedeUsed = true;
	const AMechCharacter* M = Cast<AMechCharacter>(By);
	Announce(FString::Printf(TEXT("%s 按下了门后的按钮——狗群冲出来了"), M ? *M->DisplayName : TEXT("有人")));
	StampedeFrom = Button.IsValid() ? Button->GetActorLocation() : By ? By->GetActorLocation() : GetActorLocation();
	PendingStampede = 1.5f;
}

void AMechEasterDirector::DebugDoor()
{
	if (!bSetup)
	{
		Setup();
	}
	if (Door.IsValid() && !Door->IsDone())
	{
		Door->SetDone(nullptr);
	}
	if (Button.IsValid() && !Button->IsDone())
	{
		Button->SetArmed(true);
		Button->SetDone(nullptr);
	}
	else if (!Button.IsValid())
	{
		// 这张图没有门：当场放狗（从第一个本机玩家那里）
		bStampedeUsed = false;
		AMechCharacter* Ref = nullptr;
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			Ref = *It;
			break;
		}
		OnButtonShot(Ref);
	}
}

// ---------------------------------------------------------------- 生成
int32 AMechEasterDirector::SpawnDrones(const FVector& Around, int32 Count, int32 Weapon, float Life)
{
	if (!HasAuthority() || !MechEaster::DroneReady())
	{
		return 0;
	}
	int32 N = 0;
	const float Base = FMath::FRandRange(0.f, 2.f * PI);
	for (int32 i = 0; i < Count; ++i)
	{
		const float A = Base + 2.f * PI * i / FMath::Max(1, Count) + FMath::FRandRange(-0.2f, 0.2f);
		const float R = FMath::FRandRange(4500.f, 7000.f);
		FVector P = Around + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, FMath::FRandRange(1500.f, 3000.f));
		P.Z = FMath::Max(P.Z, GroundZ(P + FVector(0.f, 0.f, 5000.f), nullptr) + 1200.f);
		const EMechEasterWeapon W = Weapon >= 0 ? static_cast<EMechEasterWeapon>(FMath::Clamp(Weapon, 0, 3)) : static_cast<EMechEasterWeapon>(FMath::RandRange(0, 3));
		AMechEasterBot* Bot = GetWorld()->SpawnActorDeferred<AMechEasterBot>(AMechEasterBot::StaticClass(), FTransform(P), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Bot)
		{
			continue;
		}
		Bot->InitBot(EMechEasterKind::Drone, EMechEasterRole::Hostile, W, FMath::Rand(), this);
		Bot->SetLife(Life + FMath::FRandRange(0.f, 8.f));
		Bot->FinishSpawning(FTransform(P));
		++N;
	}
	UE_LOG(LogTemp, Display, TEXT("[easter] 生成无人机 %d 架（%s），在 %s 周围"), N, Weapon >= 0 ? WeaponName(static_cast<EMechEasterWeapon>(FMath::Clamp(Weapon, 0, 3))) : TEXT("各种武器"), *Around.ToCompactString());
	return N;
}

int32 AMechEasterDirector::SpawnFriendlyDogs(const FVector& Around, int32 Count)
{
	if (!HasAuthority() || !MechEaster::DogReady())
	{
		return 0;
	}
	if (Spots.IsEmpty())
	{
		DogHome = Around;
		DogRoam = 1500.f;
	}
	int32 N = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const float A = FMath::FRandRange(0.f, 2.f * PI);
		FVector P = Around + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * FMath::FRandRange(300.f, 800.f);
		P.Z = GroundZ(P + FVector(0.f, 0.f, 500.f), nullptr) + 60.f;
		const FTransform Xf(FRotator(0.f, FMath::FRandRange(-180.f, 180.f), 0.f), P);
		AMechEasterBot* Bot = GetWorld()->SpawnActorDeferred<AMechEasterBot>(AMechEasterBot::StaticClass(), Xf, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Bot)
		{
			continue;
		}
		Bot->InitBot(EMechEasterKind::Dog, EMechEasterRole::Friendly, EMechEasterWeapon::Gun, FMath::Rand(), this);
		Bot->FinishSpawning(Xf);
		++N;
	}
	UE_LOG(LogTemp, Display, TEXT("[easter] 放了 %d 只友好的机械狗，在 %s 周围"), N, *Around.ToCompactString());
	return N;
}

int32 AMechEasterDirector::SpawnStampede(const FVector& From, int32 Count)
{
	if (!HasAuthority() || !MechEaster::DogReady())
	{
		return 0;
	}
	// 沿街（X）跑；两条车道之间随机排开，从按钮上游 25–55 米处起跑（循环街：跑出接缝就从另一头回来）
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const float CY = GS && GS->MapExtent.Y > 100.f ? GS->MapCenter.Y : From.Y;
	const float HalfY = GS && GS->MapExtent.Y > 100.f ? GS->MapExtent.Y : 3000.f;
	const FVector Dir = FMath::RandBool() ? FVector::ForwardVector : -FVector::ForwardVector;
	const float Start = MechEaster::ServerNow(GetWorld()) + 0.6f;
	int32 N = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		FVector O(From.X - Dir.X * FMath::FRandRange(2500.f, 5500.f), CY + FMath::FRandRange(-HalfY + 400.f, HalfY - 400.f), From.Z);
		if (MechLoop::IsActive(GetWorld()))
		{
			O.X += MechLoop::WrapShift(GetWorld(), O.X, 0.f);
		}
		O.Z = GroundZ(FVector(O.X, O.Y, From.Z + 2000.f), nullptr);
		const FTransform Xf(Dir.Rotation(), O + FVector(0.f, 0.f, 60.f));
		AMechEasterBot* Bot = GetWorld()->SpawnActorDeferred<AMechEasterBot>(AMechEasterBot::StaticClass(), Xf, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Bot)
		{
			continue;
		}
		Bot->InitBot(EMechEasterKind::Dog, EMechEasterRole::Stampede, EMechEasterWeapon::Gun, FMath::Rand(), this);
		Bot->InitStampede(Start + FMath::FRandRange(0.f, 0.8f), O, Dir, FMath::FRandRange(0.f, 2.f * PI));
		Bot->FinishSpawning(Xf);
		++N;
	}
	UE_LOG(LogTemp, Display, TEXT("[easter] 狗群 %d 只，往 %s 跑，从 %.1f 秒（服务器时间）起跑"), N, Dir.X > 0.f ? TEXT("+X") : TEXT("-X"), Start);
	return N;
}

void AMechEasterDirector::ClearHostiles()
{
	int32 N = 0;
	for (TActorIterator<AMechEasterBot> It(GetWorld()); It; ++It)
	{
		if (It->IsHostile())
		{
			It->Remove();
			++N;
		}
	}
	for (FShot& S : Shots)
	{
		S.bLive = false;
		S.Life = 0.f;
	}
	PendingStampede = -1.f;
	if (N > 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[easter] 清场：拿掉 %d 个"), N);
	}
}

void AMechEasterDirector::LogInfo() const
{
	UE_LOG(LogTemp, Display, TEXT("[easter] 地图 %s，彩蛋%s，压力板 %s%s，门 %s，按钮 %s，狗玩的地方 %d 处"), *GetWorld()->GetMapName(),
		MechEaster::Enabled(GetWorld()) ? TEXT("开") : TEXT("关"),
		Plate.IsValid() ? *Plate->GetActorLocation().ToCompactString() : TEXT("无"), bPlateUsed ? TEXT("（已触发）") : TEXT(""),
		Door.IsValid() ? *Door->GetActorLocation().ToCompactString() : TEXT("无"), Button.IsValid() ? *Button->GetActorLocation().ToCompactString() : TEXT("无"), Spots.Num());
	for (TActorIterator<AMechEasterBot> It(GetWorld()); It; ++It)
	{
		UE_LOG(LogTemp, Display, TEXT("[easter]   %s"), *It->DebugInfo());
	}
}

// ---------------------------------------------------------------- 给无人机 / 狗用
AMechCharacter* AMechEasterDirector::NearestTarget(const FVector& From, float MaxDist) const
{
	AMechCharacter* Best = nullptr;
	float BestD = MaxDist;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		if (!MechEaster::IsTargetMech(*It))
		{
			continue;
		}
		const float D = MechLoop::Dist(GetWorld(), From, It->GetActorLocation());
		if (D < BestD)
		{
			BestD = D;
			Best = *It;
		}
	}
	return Best;
}

float AMechEasterDirector::GroundZ(const FVector& From, const AActor* Ignore) const
{
	FHitResult Hit;
	if (TraceDown(From, From.Z, Hit, nullptr))
	{
		return Hit.ImpactPoint.Z;
	}
	return From.Z - 10000.f;
}

void AMechEasterDirector::Announce(const FString& Text)
{
	if (HasAuthority())
	{
		MulticastAnnounce(Text);
	}
}

void AMechEasterDirector::MulticastAnnounce_Implementation(const FString& Text)
{
	UE_LOG(LogTemp, Display, TEXT("[easter] 全场提示：%s"), *Text);
	if (UMechEasterSubsystem* Sub = GetWorld()->GetSubsystem<UMechEasterSubsystem>())
	{
		Sub->AnnounceText = Text;
		Sub->AnnounceTime = GetWorld()->GetTimeSeconds();
	}
}

void AMechEasterDirector::FireWeapon(AMechEasterBot* From, EMechEasterWeapon Weapon, const FVector& Muzzle, AActor* Target)
{
	if (!HasAuthority() || !Target)
	{
		return;
	}
	const FVector TP = MechLoop::Near(GetWorld(), Target->GetActorLocation() + FVector(0.f, 0.f, 40.f), Muzzle);
	const FVector TV = Target->GetVelocity();
	const float Dist = FVector::Dist(TP, Muzzle);
	FShot S;
	S.Pos = Muzzle;
	S.bLive = true;
	S.Source = From;
	switch (Weapon)
	{
	case EMechEasterWeapon::Gun:
	{
		const FVector Aim = TP + TV * (Dist / GunSpeed) * 0.7f;
		S.Vel = FMath::VRandCone((Aim - Muzzle).GetSafeNormal(), FMath::DegreesToRadians(1.4f)) * GunSpeed;
		S.Damage = GunDamage;
		S.Life = 2.5f;
		S.Style = 0;
		break;
	}
	case EMechEasterWeapon::Rocket:
		S.Vel = (TP - Muzzle).GetSafeNormal() * RocketSpeed;
		S.Damage = RocketDamage;
		S.Radius = RocketRadius;
		S.Life = 6.f;
		S.Style = 1;
		S.Homing = Target;
		break;
	case EMechEasterWeapon::Grenade:
	{
		// 抛物线：按落地时间算初速度（远了飞得久），落点按目标速度提前一点
		const float T = FMath::Clamp(Dist / 2600.f, 0.9f, 2.2f);
		const FVector Aim = TP - FVector(0.f, 0.f, 40.f) + TV * T * 0.5f;
		S.Vel = (Aim - Muzzle) / T + FVector(0.f, 0.f, 0.5f * GrenadeGravity * T);
		S.Gravity = GrenadeGravity;
		S.Damage = GrenadeDamage;
		S.Radius = GrenadeRadius;
		S.Life = T + 1.5f;
		S.Style = 2;
		break;
	}
	case EMechEasterWeapon::Cannon:
	{
		const FVector Aim = TP + TV * (Dist / CannonSpeed) + FVector(0.f, 0.f, 0.5f * CannonGravity * FMath::Square(Dist / CannonSpeed));
		S.Vel = (Aim - Muzzle).GetSafeNormal() * CannonSpeed;
		S.Gravity = CannonGravity;
		S.Damage = CannonDamage;
		S.Radius = CannonRadius;
		S.Life = 3.f;
		S.Style = 2;
		break;
	}
	}
	Shots.Add(S);
	MulticastShot(S.Style, S.Pos, S.Vel, S.Gravity, S.Homing.Get());
}

void AMechEasterDirector::MulticastShot_Implementation(uint8 Style, FVector_NetQuantize From, FVector_NetQuantize Velocity, float Gravity, AActor* Homing)
{
	if (!HasAuthority())
	{
		FShot S;
		S.Pos = From;
		S.Vel = Velocity;
		S.Gravity = Gravity;
		S.Style = Style;
		S.Homing = Homing;
		S.Life = Style == 1 ? 6.f : 3.5f;
		Shots.Add(S);  // 客户端只模拟画面
	}
	USoundBase* Snd = Style == 0 ? ShotSound.Get() : RocketSound.Get();
	if (Snd)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Snd, From, FRotator::ZeroRotator, Style == 0 ? 0.25f : 0.35f, Style == 0 ? 1.7f : 1.9f);
	}
}

void AMechEasterDirector::UpdateShots(float DeltaTime)
{
	TArray<FTransform> Tracers;
	TArray<FTransform> Shells;
	TArray<FTransform> Rockets;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	UWorld* World = GetWorld();
	const bool bLoop = MechLoop::IsActive(World);
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		FShot& S = Shots[i];
		S.Life -= DeltaTime;
		if (S.Life <= 0.f)
		{
			Shots.RemoveAtSwap(i);
			continue;
		}
		if (bLoop)
		{
			// 老街循环：飞出接缝半个周期再挪回来（同 MechLoop.h 第 4 条）
			S.Pos.X += MechLoop::WrapShift(World, S.Pos.X, MechLoop::ProjectileMargin(World));
		}
		// 火箭慢慢拐向目标（每秒最多 RocketTurn 度）
		if (S.Style == 1 && S.Homing.IsValid())
		{
			const FVector Cur = S.Vel.GetSafeNormal();
			const FVector Want = (MechLoop::Near(World, S.Homing->GetActorLocation(), S.Pos) - S.Pos).GetSafeNormal();
			const float Ang = FMath::Acos(FMath::Clamp(Cur | Want, -1.f, 1.f));
			if (Ang > KINDA_SMALL_NUMBER)
			{
				const float Step = FMath::Min(1.f, FMath::DegreesToRadians(RocketTurn) * DeltaTime / Ang);
				S.Vel = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Cur, Want), Step).RotateVector(Cur) * S.Vel.Size();
			}
		}
		const FVector Prev = S.Pos;
		S.Vel.Z -= S.Gravity * DeltaTime;
		S.Pos += S.Vel * DeltaTime;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEasterShot), false, S.Source.Get());
		if (World->LineTraceSingleByObjectType(Hit, Prev, S.Pos, Objects, Q))
		{
			HitShot(S, Hit.ImpactPoint, Hit.GetActor());
			if (S.Style == 0 && HitFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitFX, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVector(0.5f), true, true, ENCPoolMethod::AutoRelease);
			}
			Shots.RemoveAtSwap(i);
			continue;
		}
		// 火箭在目标身边近炸
		if (S.Style == 1 && S.Homing.IsValid() && MechLoop::Dist(World, S.Pos, S.Homing->GetActorLocation()) < 220.f)
		{
			HitShot(S, S.Pos, nullptr);
			Shots.RemoveAtSwap(i);
			continue;
		}
		// 画在离镜头最近的那一份
		const FVector Draw = bLoop ? MechLoop::Near(World, S.Pos, MechLoop::ViewLocation(World)) : S.Pos;
		const FVector Dir = S.Vel.GetSafeNormal();
		if (S.Style == 0)
		{
			const float Len = 350.f;
			Tracers.Add(FTransform(FRotationMatrix::MakeFromZ(Dir).ToQuat(), Draw - Dir * (Len * 0.5f), FVector(0.07f, 0.07f, Len / 100.f)));
		}
		else if (S.Style == 1)
		{
			Rockets.Add(FTransform(FRotationMatrix::MakeFromX(Dir).ToQuat(), Draw, FVector(1.f)));
			Shells.Add(FTransform(FQuat::Identity, Draw - Dir * 70.f, FVector(0.25f)));  // 尾焰
		}
		else
		{
			Shells.Add(FTransform(FQuat::Identity, Draw, FVector(0.4f)));
		}
	}
	auto Rebuild = [](UInstancedStaticMeshComponent* Into, const TArray<FTransform>& Xf) {
		if (!Into || (Into->GetInstanceCount() == 0 && Xf.IsEmpty()))
		{
			return;
		}
		Into->ClearInstances();
		if (Xf.Num())
		{
			Into->AddInstances(Xf, false, true);
			Into->BatchUpdateInstancesTransforms(0, Xf, Xf, true, true, true);
		}
	};
	Rebuild(TracerISM, Tracers);
	Rebuild(ShellISM, Shells);
	Rebuild(RocketISM, Rockets);
}

void AMechEasterDirector::HitShot(FShot& S, const FVector& At, AActor* HitActor)
{
	if (!S.bLive || !HasAuthority())
	{
		return;
	}
	S.bLive = false;
	if (S.Radius > 0.f)
	{
		Blast(At, S.Radius, S.Damage, 400.f, S.Source.Get());
	}
	else if (AMechCharacter* M = Cast<AMechCharacter>(HitActor); M && MechEaster::IsTargetMech(M))
	{
		M->ReceiveHit(S.Damage, 60.f, S.Source.Get(), At, EMechHitKind::Bullet);
	}
}

void AMechEasterDirector::Blast(const FVector& At, float Radius, float Damage, float Impact, AActor* Source)
{
	if (!HasAuthority())
	{
		return;
	}
	if (Radius > 0.f)
	{
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			AMechCharacter* M = *It;
			if (!MechEaster::IsTargetMech(M))
			{
				continue;
			}
			// 按离体表的距离：贴身满额，到半径边上降到一半（同保卫战、导弹）
			const UCapsuleComponent* Cap = M->GetCapsuleComponent();
			const FVector C = M->GetActorLocation();
			const FVector Near = MechLoop::Near(GetWorld(), At, C);
			const float Half = Cap->GetScaledCapsuleHalfHeight() - Cap->GetScaledCapsuleRadius();
			const float Skin = FMath::Max(0.f, FVector::Dist(Near, FMath::ClosestPointOnSegment(Near, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half))) - Cap->GetScaledCapsuleRadius());
			if (Skin > Radius)
			{
				continue;
			}
			const float Falloff = 1.f - 0.5f * FMath::Clamp(Skin / Radius, 0.f, 1.f);
			M->ReceiveHit(Damage * Falloff, Impact * Falloff, Source, Near, EMechHitKind::Missile);
		}
	}
	MulticastBlastFX(At, Radius);
}

void AMechEasterDirector::MulticastBlastFX_Implementation(FVector_NetQuantize At, float Radius)
{
	const FVector P = MechLoop::IsActive(GetWorld()) ? MechLoop::Near(GetWorld(), At, MechLoop::ViewLocation(GetWorld())) : FVector(At);
	const float S = FMath::Clamp(Radius / 700.f, 0.4f, 1.2f);
	if (BlastFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BlastFX, P, FRotator::ZeroRotator, FVector(S), true, true, ENCPoolMethod::AutoRelease);
	}
	if (BlastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BlastSound, P, FRotator::ZeroRotator, 0.7f, 1.f);
	}
}
