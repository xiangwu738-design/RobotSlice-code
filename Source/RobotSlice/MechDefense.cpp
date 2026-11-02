#include "MechDefense.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MechAIController.h"
#include "MechCharacter.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechGrunt.h"
#include "MechHealthComponent.h"
#include "MechLoadout.h"
#include "MechRun.h"
#include "MechWeapons.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace MechDefenseLocal
{
	template <typename T>
	T* Load(const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<T> Finder(Path);
		return Finder.Object;
	}

	constexpr float FirstPrep = 12.f;
	constexpr float PrepTime = 20.f;
	constexpr float RespawnDelay = 10.f;
	constexpr float WaveHeal = 0.4f;         // 每波结束回 40% 装甲
	constexpr int32 MaxDrones = 40;          // 同屏无人机（压力测试：60–80 架都扛得住，先保守）
	constexpr int32 MaxTanks = 6;
	constexpr int32 MaxElites = 3;           // 同屏敌方机甲（压力测试：每台 1.4 毫秒，玩家最多 4 台，合计 7 台以内）
	constexpr float SpawnRadius = 17000.f;   // 刷怪点离指挥所多远
	constexpr float MortarRadiusRing = 22000.f;

	// 一个人时的数量；人多按 1 + 0.5 × (人数 − 1) 加杂兵，3 人以上多 1 台精英。第 5、10 波的「精英 ×血量」是首领占位
	struct FWave
	{
		int32 Kamikaze, Gunner, Tank, Mortar, Elite;
		float EliteHP;
	};
	const FWave Waves[] = {
		{8, 4, 0, 0, 0, 1.f},
		{10, 6, 1, 0, 0, 1.f},
		{12, 8, 2, 0, 1, 1.f},
		{14, 10, 3, 1, 0, 1.f},
		{10, 8, 2, 0, 2, 2.5f},   // 首领占位：两台加强精英
		{18, 12, 3, 2, 1, 1.f},
		{20, 14, 4, 2, 0, 1.f},
		{24, 16, 4, 2, 2, 1.f},
		{26, 18, 5, 3, 2, 1.f},
		{20, 16, 4, 2, 3, 2.f},   // 最终首领占位：三台加强精英
	};
	constexpr int32 NumWaveDefs = UE_ARRAY_COUNT(Waves);
}

using namespace MechDefenseLocal;

// ================================================================ 指挥所
AMechDefenseCore::AMechDefenseCore()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	Block = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Block"));
	RootComponent = Block;
	Mast = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mast"));
	Mast->SetupAttachment(Block);
	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(Block);
	UStaticMesh* Cube = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube"));
	UStaticMesh* Cylinder = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder"));
	UStaticMesh* Sphere = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere"));
	UMaterialInterface* Concrete = Load<UMaterialInterface>(TEXT("/Game/Env/Materials/MI_ConcreteWall"));
	UMaterialInterface* Metal = Load<UMaterialInterface>(TEXT("/Game/Robot/Materials/MI_Robot_PaintDark"));
	UMaterialInterface* Glow = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/MI_Tracer"));
	// 占位：12 × 12 × 7 米的混凝土块（能站上去），顶上一根 10 米的天线杆、一颗信标灯
	Block->SetStaticMesh(Cube);
	Block->SetMaterial(0, Concrete);
	Block->SetWorldScale3D(FVector(1.f));
	Block->SetCollisionProfileName(TEXT("BlockAll"));
	Mast->SetStaticMesh(Cylinder);
	Mast->SetMaterial(0, Metal);
	Mast->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetStaticMesh(Sphere);
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCastShadow(false);
	BeaconGlow = Glow;
}

void AMechDefenseCore::BeginPlay()
{
	Super::BeginPlay();
	// 方块按 12 × 12 × 7 米缩放（根组件缩放会带着子组件，子组件按「根坐标」反算）
	Block->SetRelativeScale3D(FVector(12.f, 12.f, 7.f));
	Mast->SetRelativeLocation(FVector(0.f, 0.f, 50.f + 500.f / 7.f));
	Mast->SetRelativeScale3D(FVector(0.4f / 12.f, 0.4f / 12.f, 10.f / 7.f));
	Beacon->SetRelativeLocation(FVector(0.f, 0.f, 50.f + 1000.f / 7.f));
	Beacon->SetRelativeScale3D(FVector(1.2f / 12.f, 1.2f / 12.f, 1.2f / 7.f));
	if (BeaconGlow)
	{
		BeaconMID = UMaterialInstanceDynamic::Create(BeaconGlow, this);
		BeaconMID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.2f, 0.75f, 1.f));
		Beacon->SetMaterial(0, BeaconMID);
	}
}

void AMechDefenseCore::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechDefenseCore, HP);
	DOREPLIFETIME(AMechDefenseCore, MaxHP);
}

void AMechDefenseCore::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 信标：平时慢呼吸的青光；刚挨打闪红；血少了（三成以下）一直红闪
	if (BeaconMID)
	{
		const float T = GetWorld()->GetTimeSeconds();
		const bool bHurt = T - LastHitTime < 0.4f || GetHPFraction() < 0.3f;
		BeaconMID->SetVectorParameterValue(TEXT("Emissive"), bHurt ? FLinearColor(1.f, 0.15f, 0.06f) : FLinearColor(0.2f, 0.75f, 1.f));
		BeaconMID->SetScalarParameterValue(TEXT("EmissiveStrength"), bHurt ? 60.f * (0.5f + 0.5f * FMath::Sin(T * 14.f)) : 25.f * (0.7f + 0.3f * FMath::Sin(T * 2.f)));
	}
}

void AMechDefenseCore::ApplyEnemyDamage(float Damage)
{
	if (!HasAuthority() || HP <= 0.f || Damage <= 0.f)
	{
		return;
	}
	HP = FMath::Max(0.f, HP - Damage);
	MulticastHit();
}

void AMechDefenseCore::MulticastHit_Implementation()
{
	LastHitTime = GetWorld()->GetTimeSeconds();
}

// ================================================================ 波次调度
AMechDefenseDirector::AMechDefenseDirector()
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
		C->SetMaterial(0, Mat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetHasPerInstancePrevTransforms(true);
		return C;
	};
	UMaterialInterface* Tracer = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/MI_Tracer"));
	TracerISM = MakeISM(TEXT("EnemyTracers"), Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder")), Tracer);
	ShellISM = MakeISM(TEXT("EnemyShells"), Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere")), Tracer);
	PlaneMesh = Load<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane"));
	RingMat = Load<UMaterialInterface>(TEXT("/Game/Weapons/Materials/M_ShockRing"));
	HitFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_03_Hit_01"));
	BlastFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_04/Particles/Ex_02_Exp_01"));
	DustFX = Load<UNiagaraSystem>(TEXT("/Game/AAAVFX/Course_00/Particle/Ex_02_Projectile/Projectile_Exp_01"));
	BlastSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_explosion_small"));
	ShotSound = Load<USoundBase>(TEXT("/Game/Audio/Combat/S_rifle_fire_a"));
}

void AMechDefenseDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechDefenseDirector, Wave);
	DOREPLIFETIME(AMechDefenseDirector, NumWaves);
	DOREPLIFETIME(AMechDefenseDirector, Phase);
	DOREPLIFETIME(AMechDefenseDirector, PhaseEndTime);
	DOREPLIFETIME(AMechDefenseDirector, EnemiesLeft);
	DOREPLIFETIME(AMechDefenseDirector, Core);
}

float AMechDefenseDirector::Now() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AMechDefenseDirector::BeginPlay()
{
	Super::BeginPlay();
	// 敌人子弹 / 炮弹的颜色：红色曳光、橙色炮弹（各端自己建）
	for (int32 k = 0; k < 2; ++k)
	{
		UInstancedStaticMeshComponent* C = k == 0 ? TracerISM.Get() : ShellISM.Get();
		if (UMaterialInterface* M = C->GetMaterial(0))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(M, this);
			MID->SetVectorParameterValue(TEXT("Emissive"), k == 0 ? FLinearColor(1.f, 0.1f, 0.05f) : FLinearColor(1.f, 0.45f, 0.1f));
			MID->SetScalarParameterValue(TEXT("EmissiveStrength"), k == 0 ? 50.f : 70.f);
			C->SetMaterial(0, MID);
		}
	}
	if (HasAuthority())
	{
		SetupServer();
	}
}

void AMechDefenseDirector::SetupServer()
{
	AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>();
	FTransform Start = GM ? GM->RangeStart : GetActorTransform();
	// 指挥所放在出生点身后 25 米（玩家出生在它前面）
	FVector At = Start.GetLocation() - Start.GetRotation().GetForwardVector() * 2500.f;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechDefenseGround), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, 20000.f), At - FVector(0.f, 0.f, 20000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
	{
		At = Hit.ImpactPoint;
	}
	CoreAt = At;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Core = GetWorld()->SpawnActor<AMechDefenseCore>(AMechDefenseCore::StaticClass(), FTransform(Start.Rotator(), At + FVector(0.f, 0.f, 350.f - 50.f)), P);
	BuildSpawnPoints();
	Wave = 0;
	NumWaves = NumWaveDefs;
	Phase = EMechDefensePhase::Prep;
	PhaseEndTime = Now() + FirstPrep;
	bSetup = true;
	UE_LOG(LogTemp, Display, TEXT("[defense] 开始：指挥所 %s，刷怪点 %d 个（迫击炮位 %d 个），%d 波"), *CoreAt.ToCompactString(), SpawnRing.Num(), MortarRing.Num(), NumWaves);
}

void AMechDefenseDirector::BuildSpawnPoints()
{
	// 绕指挥所一圈每 7.5 度一个点：打到地面、不是水面、和指挥所高差 35 米以内才要
	// 有场地标记（带 MechArena 标签的点，HalfX= / HalfY= 厘米，和 AMechGameMode::ComputeArena 同一个约定）的图（梦核旷野）：
	// 只要场地框里、出生点正前方 90 米以外的点——指挥所在墙脚的平台上，不加这条有一半点落在墙后面、还有的刷在守方平台上；敌人只从旷野那边来
	bool bArena = false;
	FBox2D Arena(ForceInit);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (!It->ActorHasTag(TEXT("MechArena")))
		{
			continue;
		}
		FVector2D Half(0.f, 0.f);
		for (const FName& T : It->Tags)
		{
			const FString S = T.ToString();
			if (S.StartsWith(TEXT("HalfX=")))
			{
				Half.X = FCString::Atof(*S.Mid(6));
			}
			else if (S.StartsWith(TEXT("HalfY=")))
			{
				Half.Y = FCString::Atof(*S.Mid(6));
			}
		}
		const FVector2D C(It->GetActorLocation());
		Half -= FVector2D(2000.f, 2000.f);  // 离边界的隐形墙 / 巨墙留 20 米
		if (Half.X > 0.f && Half.Y > 0.f)
		{
			Arena = FBox2D(C - Half, C + Half);
			bArena = true;
		}
		break;
	}
	const AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>();
	const FVector Front = GM ? GM->RangeStart.GetRotation().GetForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechDefenseSpawn), false);
	auto Ring = [&](float Radius, TArray<FVector>& Out, bool bStrict) {
		for (int32 i = 0; i < 48; ++i)
		{
			const float A = i * 2.f * PI / 48.f;
			const FVector P = CoreAt + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * Radius;
			if (bArena && (!Arena.IsInside(FVector2D(P)) || FVector::DotProduct(P - CoreAt, Front) < 9000.f))
			{
				continue;
			}
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0.f, 0.f, 30000.f), P - FVector(0.f, 0.f, 30000.f), ECC_Visibility, Q))
			{
				continue;
			}
			const bool bWater = Hit.GetActor() && Hit.GetActor()->GetClass()->GetName().Contains(TEXT("Water"));
			if (bStrict && (bWater || FMath::Abs(Hit.ImpactPoint.Z - CoreAt.Z) > 3500.f))
			{
				continue;
			}
			Out.Add(Hit.ImpactPoint);
		}
	};
	Ring(SpawnRadius, SpawnRing, true);
	Ring(MortarRadiusRing, MortarRing, true);
	if (SpawnRing.IsEmpty())
	{
		Ring(SpawnRadius, SpawnRing, false);
	}
	if (MortarRing.IsEmpty())
	{
		MortarRing = SpawnRing;
	}
}

FVector AMechDefenseDirector::PickSpawn(bool bAir, float Radius) const
{
	const TArray<FVector>& From = Radius > SpawnRadius + 1.f ? MortarRing : SpawnRing;
	if (From.IsEmpty())
	{
		return CoreAt + FVector(SpawnRadius, 0.f, bAir ? 2500.f : 200.f);
	}
	const FVector P = From[FMath::RandHelper(From.Num())];
	return P + FVector(0.f, 0.f, bAir ? 2500.f : 0.f);
}

int32 AMechDefenseDirector::PlayerCount() const
{
	int32 N = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->Get())
		{
			++N;
		}
	}
	return FMath::Max(1, N);
}

void AMechDefenseDirector::GetPlayers(TArray<AMechCharacter*>& Out) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMechCharacter* M = It->Get() ? Cast<AMechCharacter>(It->Get()->GetPawn()) : nullptr;
		if (M && !M->GetHealth()->IsDead() && M->GetTeam() == 0)
		{
			Out.Add(M);
		}
	}
}

int32 AMechDefenseDirector::NumAliveGrunts() const
{
	int32 N = 0;
	for (const TWeakObjectPtr<AMechGrunt>& G : Grunts)
	{
		if (G.IsValid() && G->IsAlive())
		{
			++N;
		}
	}
	return N;
}

// ---------------------------------------------------------------- 每帧
void AMechDefenseDirector::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateShots(DeltaTime);
	UpdateRings();
	if (!HasAuthority() || !bSetup)
	{
		return;
	}
	PlayerTick(DeltaTime);
	if (Phase == EMechDefensePhase::Won || Phase == EMechDefensePhase::Lost)
	{
		return;
	}
	if (Core && Core->IsDestroyed())
	{
		Phase = EMechDefensePhase::Lost;
		UE_LOG(LogTemp, Display, TEXT("[defense] 指挥所被摧毁：第 %d 波失败"), Wave);
		if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
		{
			GM->LogStats();  // 11-01 积分制：每人积分分项、MVP（玩家里积分最高），马上抄给 HUD 的结束画面
			GM->PublishStats();
		}
		return;
	}
	if (Phase == EMechDefensePhase::Prep)
	{
		EnemiesLeft = 0;
		if (Now() >= PhaseEndTime)
		{
			StartWave();
		}
		return;
	}
	WaveTime += DeltaTime;
	SpawnTick(DeltaTime);
	Grunts.RemoveAll([](const TWeakObjectPtr<AMechGrunt>& G) { return !G.IsValid() || !G->IsAlive(); });
	Elites.RemoveAll([](const TWeakObjectPtr<AMechCharacter>& E) { return !E.IsValid() || E->GetHealth()->IsDead(); });
	const int32 Queued = Queue[0] + Queue[1] + Queue[2] + Queue[3] + EliteQueue;
	EnemiesLeft = Queued + Grunts.Num() + Elites.Num();
	if (EnemiesLeft == 0)
	{
		EndWave();
	}
}

void AMechDefenseDirector::StartWave()
{
	++Wave;
	const FWave& W = Waves[FMath::Clamp(Wave - 1, 0, NumWaveDefs - 1)];
	const int32 N = PlayerCount();
	const float Scale = 1.f + 0.5f * (N - 1);
	Queue[(int32)EMechGrunt::Kamikaze] = FMath::RoundToInt(W.Kamikaze * Scale);
	Queue[(int32)EMechGrunt::Gunner] = FMath::RoundToInt(W.Gunner * Scale);
	Queue[(int32)EMechGrunt::Tank] = FMath::RoundToInt(W.Tank * Scale);
	Queue[(int32)EMechGrunt::Mortar] = W.Mortar + (N >= 3 && W.Mortar > 0 ? 1 : 0);
	EliteQueue = W.Elite + (N >= 3 && W.Elite > 0 ? 1 : 0);
	EliteHPScale = W.EliteHP;
	GruntHPScale = 1.f + 0.06f * (Wave - 1);  // 后面的波次杂兵稍微耐打一点
	Phase = EMechDefensePhase::Fight;
	WaveTime = 0.f;
	DronePacketClock = 1.f;
	TankClock = 3.f;
	// 迫击炮开打就全摆好（远处固定不动）
	while (Queue[(int32)EMechGrunt::Mortar] > 0)
	{
		SpawnGrunt(EMechGrunt::Mortar, PickSpawn(false, MortarRadiusRing));
	}
	UE_LOG(LogTemp, Display, TEXT("[defense] 第 %d 波开始（%d 人）：自爆 %d 机枪 %d 炮车 %d 迫击炮 %d 精英 %d（血量 ×%.1f）"), Wave, N,
		Queue[0], Queue[1], Queue[2], W.Mortar, EliteQueue, EliteHPScale);
}

void AMechDefenseDirector::EndWave()
{
	UE_LOG(LogTemp, Display, TEXT("[defense] 第 %d 波打完（用时 %.0f 秒），指挥所 %.0f%%"), Wave, WaveTime, Core ? Core->GetHPFraction() * 100.f : 0.f);
	if (Wave >= NumWaves)
	{
		Phase = EMechDefensePhase::Won;
		UE_LOG(LogTemp, Display, TEXT("[defense] 守满 %d 波：胜利"), NumWaves);
		if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
		{
			GM->LogStats();  // 11-01 积分制：同上
			GM->PublishStats();
		}
		return;
	}
	Phase = EMechDefensePhase::Prep;
	PhaseEndTime = Now() + PrepTime;
	ReviveAll(WaveHeal);
	// 11-01 肉鸽：守波经验，人人一份（打完最后一波就不用了）
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (UMechRunComponent* Run = UMechRunComponent::Of(It->Get()))
		{
			Run->OnWaveEnd(Wave);
		}
	}
}

void AMechDefenseDirector::SpawnTick(float DeltaTime)
{
	int32 Alive[4] = {0, 0, 0, 0};
	for (const TWeakObjectPtr<AMechGrunt>& G : Grunts)
	{
		if (G.IsValid() && G->IsAlive())
		{
			++Alive[(int32)G->GetKind()];
		}
	}
	// 无人机：每 1.4 秒从一个方向来一小股（2–3 架，自爆和机枪按剩下的比例混着）
	const int32 Drones = Alive[0] + Alive[1];
	DronePacketClock -= DeltaTime;
	if (DronePacketClock <= 0.f && Drones < MaxDrones && Queue[0] + Queue[1] > 0)
	{
		DronePacketClock = 1.4f;
		const FVector At = PickSpawn(true, SpawnRadius);
		const int32 Packet = FMath::Min(FMath::RandRange(2, 3), Queue[0] + Queue[1]);
		for (int32 i = 0; i < Packet; ++i)
		{
			const bool bKamikaze = Queue[1] == 0 || (Queue[0] > 0 && FMath::FRand() < float(Queue[0]) / (Queue[0] + Queue[1]));
			SpawnGrunt(bKamikaze ? EMechGrunt::Kamikaze : EMechGrunt::Gunner, At + FMath::VRand() * 800.f);
		}
	}
	TankClock -= DeltaTime;
	if (TankClock <= 0.f && Queue[2] > 0 && Alive[2] < MaxTanks)
	{
		TankClock = 6.f;
		SpawnGrunt(EMechGrunt::Tank, PickSpawn(false, SpawnRadius) + FVector(0.f, 0.f, 150.f));
	}
	// 精英：开打 8 秒后一台一台来，间隔 6 秒
	if (EliteQueue > 0 && WaveTime > 8.f && Elites.Num() < MaxElites && FMath::Fmod(WaveTime, 6.f) < DeltaTime)
	{
		--EliteQueue;
		SpawnElite(PickSpawn(false, SpawnRadius), EliteHPScale);
	}
}

void AMechDefenseDirector::SpawnGrunt(EMechGrunt Kind, const FVector& At)
{
	const int32 K = (int32)Kind;
	Queue[K] = FMath::Max(0, Queue[K] - 1);
	const FRotator Face(0.f, (CoreAt - At).Rotation().Yaw, 0.f);
	const FTransform T(Face, At);
	AMechGrunt* G = GetWorld()->SpawnActorDeferred<AMechGrunt>(AMechGrunt::StaticClass(), T, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!G)
	{
		return;
	}
	G->Init(Kind, this, GruntHPScale);
	G->FinishSpawning(T);
	Grunts.Add(G);
}

void AMechDefenseDirector::SpawnElite(const FVector& Ground, float HPScale)
{
	static int32 Serial = 0;
	const FTransform At(FRotator(0.f, (CoreAt - Ground).Rotation().Yaw, 0.f), Ground + FVector(0.f, 0.f, 96.f * 1.35f + 30.f));
	AMechCharacter* E = GetWorld()->SpawnActorDeferred<AMechCharacter>(AMechCharacter::StaticClass(), At, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!E)
	{
		return;
	}
	++Serial;
	E->SetTeam(1);
	E->DisplayName = HPScale > 1.f ? FString::Printf(TEXT("首领 %d"), Serial) : FString::Printf(TEXT("精英 %d"), Serial);
	E->FinishSpawning(At);
	FMechLoadout L = E->GetLoadout();
	L.Frame = static_cast<EMechFrame>(FMath::RandHelper(MechNumFrames));
	L.Shooter = static_cast<EMechShooter>(FMath::RandHelper(MechWeapons::NumGuns()));
	L.Melee = static_cast<EMechMeleeWeapon>(FMath::RandHelper(MechWeapons::NumBlades()));
	E->SetLoadout(L, true);
	if (HPScale > 1.f)
	{
		UMechHealthComponent* H = E->GetHealth();
		H->SetMaxValues(H->MaxAP * HPScale, H->MaxStagger * FMath::Sqrt(HPScale), true);
		E->Tags.Add(TEXT("MechBoss"));  // 11-01 积分制：打首领的伤害另加分（AMechGameMode::RecordHit 按这个标签认）
	}
	if (AMechAIController* AI = Cast<AMechAIController>(E->GetController()))
	{
		AI->ArenaCenter = CoreAt;
		AI->ArenaExtent = FVector2D(22000.f, 22000.f);
	}
	TWeakObjectPtr<AMechCharacter> Weak(E);
	const bool bBoss = HPScale > 1.f;
	E->GetHealth()->OnDied.AddWeakLambda(this, [this, Weak, bBoss](AActor* Killer) {
		++DebugKills;
		AMechCharacter* K = Cast<AMechCharacter>(Killer);
		if (!K)
		{
			if (const AController* C = Cast<AController>(Killer))
			{
				K = Cast<AMechCharacter>(C->GetPawn());
			}
		}
		if (K && K->GetTeam() == 0)
		{
			++DebugKillsByPlayers;
			// 11-01 积分制：精英 / 首领击毁分（指挥所附近另加），一起打的人记助攻
			const bool bNear = Weak.IsValid() && Core && FVector::Dist2D(Weak->GetActorLocation(), Core->GetActorLocation()) < MechScore::DefendRadius;
			MechScore::NoteDefenseKill(K, bBoss ? EMechDefenseKill::Boss : EMechDefenseKill::Elite, Weak.IsValid() ? Weak->DisplayName : FString(), bNear);
			if (UMechRunComponent* Run = K->GetRun())
			{
				Run->OnKill(bBoss ? EMechDefenseKill::Boss : EMechDefenseKill::Elite, Weak.Get());  // 11-01 肉鸽：谁杀的经验算谁的
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[defense] %s 被击毁"), Weak.IsValid() ? *Weak->DisplayName : TEXT("精英"));
		// 尸体过一会儿清掉（不然一波波堆在场上）
		if (Weak.IsValid())
		{
			Weak->SetLifeSpan(8.f);
		}
	});
	Elites.Add(E);
	UE_LOG(LogTemp, Display, TEXT("[defense] 刷出 %s（装甲 %.0f）"), *E->DisplayName, E->GetHealth()->MaxAP);
}

void AMechDefenseDirector::OnGruntDied(AMechGrunt* Grunt, AActor* Killer)
{
	++DebugKills;
	if (AMechCharacter* K = Cast<AMechCharacter>(Killer); K && K->GetTeam() == 0)
	{
		++DebugKillsByPlayers;
		// 11-01 积分制：按种类给分（迫击炮单独一项「拆迫击炮」），离指挥所 DefendRadius 内击毁的另加
		const bool bNear = Grunt && Core && FVector::Dist2D(Grunt->GetActorLocation(), Core->GetActorLocation()) < MechScore::DefendRadius;
		MechScore::NoteDefenseKill(K, Grunt ? static_cast<EMechDefenseKill>(Grunt->GetKind()) : EMechDefenseKill::Kamikaze, FString(), bNear);
		if (UMechRunComponent* Run = K->GetRun())
		{
			Run->OnKill(Grunt ? static_cast<EMechDefenseKill>(Grunt->GetKind()) : EMechDefenseKill::Kamikaze, Grunt);  // 11-01 肉鸽：谁杀的经验算谁的
		}
	}
}

void AMechDefenseDirector::MulticastRunFx_Implementation(uint8 Kind, FVector_NetQuantize At, FVector_NetQuantize Vec, float A, float B, float C)
{
	if (AMechRunFx* Fx = AMechRunFx::Get(GetWorld()))
	{
		Fx->Play(static_cast<EMechRunFxKind>(Kind), At, Vec, A, B, C);
	}
}

void AMechDefenseDirector::MulticastRunBullet_Implementation(AMechCharacter* Mech, FVector_NetQuantize From, FVector_NetQuantizeNormal Dir, AActor* Target)
{
	if (!HasAuthority())
	{
		UMechRunComponent::AddVisualHoming(Mech, From, Dir, Target);
	}
}

AMechGrunt* AMechDefenseDirector::DebugSpawnGrunt(EMechGrunt Kind, const FVector& At, float HPScale, bool bFrozen)
{
	const FTransform T(FRotator::ZeroRotator, At);
	AMechGrunt* G = GetWorld()->SpawnActorDeferred<AMechGrunt>(AMechGrunt::StaticClass(), T, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (G)
	{
		G->Init(Kind, this, HPScale);
		G->FinishSpawning(T);
		Grunts.Add(G);
		if (bFrozen)
		{
			G->RunStun(100000.f);
		}
	}
	return G;
}

void AMechDefenseDirector::DebugKillAll()
{
	for (const TWeakObjectPtr<AMechGrunt>& G : Grunts)
	{
		if (G.IsValid())
		{
			G->DebugKill();
		}
	}
	for (const TWeakObjectPtr<AMechCharacter>& E : Elites)
	{
		if (E.IsValid() && !E->GetHealth()->IsDead())
		{
			E->GetHealth()->ApplyHit(1e7f, 0.f, nullptr, E->GetActorLocation());
		}
	}
	Queue[0] = Queue[1] = Queue[2] = Queue[3] = 0;
	EliteQueue = 0;
}

// ---------------------------------------------------------------- 玩家：阵亡重生
void AMechDefenseDirector::PlayerTick(float DeltaTime)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		// 11-01 肉鸽成长（MechRun.h）：只有保卫战的玩家控制器才有这个组件，对战里根本不存在；放控制器上重生不丢，复制给本人
		if (PC && !UMechRunComponent::Of(PC))
		{
			UMechRunComponent* Run = NewObject<UMechRunComponent>(PC, TEXT("MechRun"));
			Run->RegisterComponent();
			Run->SetIsReplicated(true);
		}
		AMechCharacter* M = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
		if (!M || BoundPlayers.Contains(M))
		{
			continue;
		}
		BoundPlayers.Add(M);
		TWeakObjectPtr<APlayerController> WeakPC(PC);
		TWeakObjectPtr<AMechCharacter> WeakMech(M);
		M->GetHealth()->OnDied.AddWeakLambda(this, [this, WeakPC, WeakMech](AActor*) {
			Respawns.Add({WeakPC, Now() + RespawnDelay});
			// 11-01 积分制：保卫战玩家的阵亡也记进统计（测试场那条路不经过 AMechGameMode::OnMechDied）
			AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>();
			if (GM && WeakMech.IsValid())
			{
				++GM->Stats.FindOrAdd(WeakMech->DisplayName).Deaths;
			}
			UE_LOG(LogTemp, Display, TEXT("[defense] 玩家阵亡，%.0f 秒后在指挥所旁重生"), RespawnDelay);
		});
	}
	for (int32 i = Respawns.Num() - 1; i >= 0; --i)
	{
		if (Now() >= Respawns[i].At && Phase != EMechDefensePhase::Lost)
		{
			if (APlayerController* PC = Respawns[i].PC.Get())
			{
				RespawnPlayer(PC);
			}
			Respawns.RemoveAt(i);
		}
	}
}

void AMechDefenseDirector::RespawnPlayer(APlayerController* PC)
{
	if (AMechGameMode* GM = GetWorld()->GetAuthGameMode<AMechGameMode>())
	{
		GM->SpawnRangeMech(PC);
	}
}

void AMechDefenseDirector::ReviveAll(float HealFraction)
{
	for (const FRespawn& R : Respawns)
	{
		if (APlayerController* PC = R.PC.Get())
		{
			RespawnPlayer(PC);
		}
	}
	Respawns.Reset();
	TArray<AMechCharacter*> Players;
	GetPlayers(Players);
	for (AMechCharacter* M : Players)
	{
		M->GetHealth()->Heal(M->GetHealth()->MaxAP * HealFraction);
	}
}

// ---------------------------------------------------------------- 子弹 / 炮弹 / 爆炸 / 红圈
void AMechDefenseDirector::FireShot(const FVector& From, const FVector& Velocity, float Gravity, float Damage, float Radius, float CoreDamage, AActor* Source, uint8 Style)
{
	FShot S;
	S.Pos = From;
	S.Vel = Velocity;
	S.Gravity = Gravity;
	S.Damage = Damage;
	S.Radius = Radius;
	S.CoreDamage = CoreDamage;
	S.Style = Style;
	S.bLive = true;
	S.Instigator = Source;
	Shots.Add(S);
	MulticastShot(From, Velocity, Gravity, Style);
}

void AMechDefenseDirector::MulticastShot_Implementation(FVector_NetQuantize From, FVector_NetQuantize Velocity, float Gravity, uint8 Style)
{
	if (!HasAuthority())
	{
		FShot S;
		S.Pos = From;
		S.Vel = Velocity;
		S.Gravity = Gravity;
		S.Style = Style;
		Shots.Add(S);  // 客户端只模拟画面
	}
	if (ShotSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ShotSound, From, FRotator::ZeroRotator, Style == 1 ? 0.7f : 0.3f, Style == 1 ? 0.55f : 1.6f);
	}
}

void AMechDefenseDirector::UpdateShots(float DeltaTime)
{
	TArray<FTransform> Tracers;
	TArray<FTransform> Shells;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	for (int32 i = Shots.Num() - 1; i >= 0; --i)
	{
		FShot& S = Shots[i];
		const FVector Prev = S.Pos;
		S.Vel.Z -= S.Gravity * DeltaTime;
		S.Pos += S.Vel * DeltaTime;
		S.Life -= DeltaTime;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(MechEnemyShot), false, S.Instigator.Get());
		if (GetWorld()->LineTraceSingleByObjectType(Hit, Prev, S.Pos, Objects, Q))
		{
			AActor* A = Hit.GetActor();
			if (S.bLive)
			{
				if (S.Radius > 0.f)
				{
					Blast(Hit.ImpactPoint, S.Radius, S.Damage, 400.f, S.CoreDamage, S.Instigator.Get(), false);
				}
				else if (AMechCharacter* M = Cast<AMechCharacter>(A); M && M->GetTeam() == 0 && !M->GetHealth()->IsDead())
				{
					const float Dealt = M->ReceiveHit(S.Damage, 60.f, S.Instigator.Get(), Hit.ImpactPoint, EMechHitKind::Bullet);
					const AMechGrunt* By = Cast<AMechGrunt>(S.Instigator.Get());
					DebugDamage.FindOrAdd(By ? AMechGrunt::KindName(By->GetKind()) : TEXT("?")) += Dealt;
				}
				else if (AMechDefenseCore* C = Cast<AMechDefenseCore>(A))
				{
					C->ApplyEnemyDamage(S.CoreDamage);
					DebugDamage.FindOrAdd(TEXT("指挥所")) += S.CoreDamage;
				}
			}
			if (S.Style == 0 && HitFX)
			{
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitFX, Hit.ImpactPoint, Hit.ImpactNormal.Rotation(), FVector(0.5f), true, true, ENCPoolMethod::AutoRelease);
			}
			Shots.RemoveAtSwap(i);
			continue;
		}
		if (S.Life <= 0.f)
		{
			Shots.RemoveAtSwap(i);
			continue;
		}
		const FVector Dir = S.Vel.GetSafeNormal();
		if (S.Style == 0)
		{
			const float Len = 350.f;
			Tracers.Add(FTransform(FRotationMatrix::MakeFromZ(Dir).ToQuat(), S.Pos - Dir * (Len * 0.5f), FVector(0.07f, 0.07f, Len / 100.f)));
		}
		else
		{
			Shells.Add(FTransform(FQuat::Identity, S.Pos, FVector(0.4f)));
		}
	}
	auto Rebuild = [](UInstancedStaticMeshComponent* Into, const TArray<FTransform>& Xf) {
		if (Into->GetInstanceCount() == 0 && Xf.IsEmpty())
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
}

void AMechDefenseDirector::Blast(const FVector& At, float Radius, float Damage, float Impact, float CoreDamage, AActor* Source, bool bBig)
{
	if (Radius > 0.f)
	{
		const AMechGrunt* By = Cast<AMechGrunt>(Source);
		const FString Who = By ? AMechGrunt::KindName(By->GetKind()) : TEXT("?");
		TArray<AMechCharacter*> Players;
		GetPlayers(Players);
		for (AMechCharacter* M : Players)
		{
			// 按离体表的距离：贴身满额，到半径边上降到一半（和导弹一样）
			const UCapsuleComponent* Cap = M->GetCapsuleComponent();
			const FVector C = M->GetActorLocation();
			const float Half = Cap->GetScaledCapsuleHalfHeight() - Cap->GetScaledCapsuleRadius();
			const float Skin = FMath::Max(0.f, FVector::Dist(At, FMath::ClosestPointOnSegment(At, C - FVector(0.f, 0.f, Half), C + FVector(0.f, 0.f, Half))) - Cap->GetScaledCapsuleRadius());
			if (Skin > Radius)
			{
				continue;
			}
			const float Falloff = 1.f - 0.5f * FMath::Clamp(Skin / Radius, 0.f, 1.f);
			DebugDamage.FindOrAdd(Who) += M->ReceiveHit(Damage * Falloff, Impact * Falloff, Source, At, EMechHitKind::Missile);
		}
		if (Core && FVector::Dist2D(At, Core->GetActorLocation()) < Radius + Core->GetRadius() && FMath::Abs(At.Z - Core->GetActorLocation().Z) < Radius + 500.f)
		{
			Core->ApplyEnemyDamage(CoreDamage);
			DebugDamage.FindOrAdd(TEXT("指挥所")) += CoreDamage;
		}
	}
	MulticastBlastFX(At, Radius, bBig);
}

void AMechDefenseDirector::MulticastBlastFX_Implementation(FVector_NetQuantize At, float Radius, bool bBig)
{
	// 半径 0 = 迫击炮的炮口火光（小、不伤人）
	const float S = Radius <= 0.f ? 0.35f : FMath::Clamp(Radius / 700.f, 0.5f, 1.4f) * (bBig ? 1.3f : 1.f);
	if (BlastFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BlastFX, At, FRotator::ZeroRotator, FVector(S), true, true, ENCPoolMethod::AutoRelease);
	}
	if (DustFX && Radius > 0.f)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, DustFX, At, FRotator::ZeroRotator, FVector(S * 0.8f), true, true, ENCPoolMethod::AutoRelease);
	}
	if (BlastSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BlastSound, At, FRotator::ZeroRotator, Radius <= 0.f ? 0.4f : 0.8f, Radius <= 0.f ? 1.4f : 0.9f);
	}
}

void AMechDefenseDirector::Warn(const FVector& At, float Radius, float Duration)
{
	MulticastWarn(At, Radius, Duration);
}

void AMechDefenseDirector::MulticastWarn_Implementation(FVector_NetQuantize At, float Radius, float Duration)
{
	if (!PlaneMesh || !RingMat)
	{
		return;
	}
	const float T = GetWorld()->GetTimeSeconds();
	FWarnRing* R = Rings.FindByPredicate([T](const FWarnRing& X) { return X.Comp && T - X.Born > X.Duration + 0.2f; });
	if (!R)
	{
		FWarnRing New;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(PlaneMesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		New.Comp = C;
		New.MID = UMaterialInstanceDynamic::Create(RingMat, this);
		New.MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.15f, 0.08f));
		C->SetMaterial(0, New.MID);
		R = &Rings.Add_GetRef(New);
	}
	R->Born = T;
	R->Duration = Duration;
	R->Comp->SetWorldLocationAndRotation(FVector(At) + FVector(0.f, 0.f, 20.f), FRotator::ZeroRotator);
	R->Comp->SetWorldScale3D(FVector(Radius * 2.f / 100.f));
	R->Comp->SetVisibility(true);
}

void AMechDefenseDirector::UpdateRings()
{
	const float T = GetWorld()->GetTimeSeconds();
	for (FWarnRing& R : Rings)
	{
		if (!R.Comp)
		{
			continue;
		}
		const float K = (T - R.Born) / FMath::Max(R.Duration, 0.01f);
		if (K > 1.05f)
		{
			if (R.Comp->IsVisible())
			{
				R.Comp->SetVisibility(false);
			}
			continue;
		}
		// 样式 2 = 预警圈：外圈一直在，里面一片从中心涨到边（涨满就落下来）
		R.MID->SetVectorParameterValue(TEXT("Prm"), FLinearColor(FMath::Clamp(K, 0.f, 1.f), 0.03f, 1.4f, 2.f));
	}
}
