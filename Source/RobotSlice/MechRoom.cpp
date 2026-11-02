#include "MechRoom.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MechGameState.h"
#include "MechMaps.h"
#include "MechSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"

namespace MechRoomPriv  // 不用匿名命名空间：合并编译时会和别的文件里同名的小函数撞
{
	// 房间里的地图 / 保卫战 / 人机名单 / 倍数 / 友伤单独存一节（规则和禁用开关早就在 FMechSettings 里，照旧存那边）
	const TCHAR* RoomSection = TEXT("MechRoom");

	bool IsTestRun()
	{
		return FString(FCommandLine::Get()).Contains(TEXT("-Mech"));
	}

	/** 没存过时的人机名单：真人 + 1 台 对 2 台（一个人开房时就是以前默认的 2v2） */
	void DefaultAIs(FMechRoomSettings& S)
	{
		S.AIs.Reset();
		for (const int32 Team : {0, 1, 1})
		{
			FMechRoomAI A;
			A.Team = Team;
			A.Level = 1;
			S.AIs.Add(A);
		}
	}
}

namespace MechRoom
{
	FString RoomURL()
	{
		return FString(RoomMapPath()) + TEXT("?Room?listen");
	}

	bool IsCore(const FMechRoomSettings& S)
	{
		return MechMaps::Get(S.Map).IsCore();
	}

	bool IsTraining(const FMechRoomSettings& S)
	{
		return MechMaps::Get(S.Map).IsTraining();
	}

	bool UsesTeams(const FMechRoomSettings& S)
	{
		return !S.bDefense && !IsTraining(S);
	}

	bool UsesAI(const FMechRoomSettings& S)
	{
		return UsesTeams(S);
	}

	bool IsSolo(const FMechRoomSettings& S, bool bRoomMap, bool bCoreNow)
	{
		const bool bCore = bRoomMap ? IsCore(S) : bCoreNow;
		return !bCore && UsesTeams(S) && S.Rules.IsDeathmatch() && S.Rules.bSolo;
	}

	int32 FirstMapFor(uint8 Flag)
	{
		for (int32 m = 0; m < MechMaps::Num(); ++m)
		{
			if (MechMaps::Get(m).Supports(Flag))
			{
				return m;
			}
		}
		return 0;
	}

	int32 CountOf(const FMechRoomSettings& S, const AMechGameState& GS, int32 Team)
	{
		int32 N = 0;
		for (const FMechLobbyPlayer& P : GS.Lobby)
		{
			N += Team < 0 || P.Team == Team ? 1 : 0;
		}
		if (UsesAI(S))
		{
			for (const FMechRoomAI& A : S.AIs)
			{
				N += Team < 0 || A.Team == Team ? 1 : 0;
			}
		}
		return N;
	}

	FText StartProblem(const FMechRoomSettings& S, const AMechGameState& GS)
	{
		if (!UsesTeams(S))
		{
			return FText::GetEmpty();
		}
		if (IsSolo(S, GS.bRoomMode, GS.bCoreMode))
		{
			return CountOf(S, GS, -1) < 2 ? INVTEXT("个人战至少要两台：加一个人机") : FText::GetEmpty();
		}
		if (CountOf(S, GS, 0) == 0)
		{
			return INVTEXT("蓝队还没有人：加一个人机，或者有人换过去");
		}
		if (CountOf(S, GS, 1) == 0)
		{
			return INVTEXT("红队还没有人：加一个人机，或者有人换过去");
		}
		return FText::GetEmpty();
	}

	float RecommendedChallenge(int32 CountA, int32 CountB)
	{
		const int32 Few = FMath::Min(CountA, CountB);
		const int32 Many = FMath::Max(CountA, CountB);
		if (Few <= 0 || Few == Many)
		{
			return 1.f;
		}
		return FMath::RoundToFloat(FMath::Pow(static_cast<float>(Many) / Few, 0.6f) * 20.f) / 20.f;
	}

	void EffectiveChallenge(const FMechRoomSettings& S, bool bSolo, int32 Count0, int32 Count1, int32& OutTeam, float& OutArmor, float& OutDamage)
	{
		OutTeam = -1;
		OutArmor = 1.f;
		OutDamage = 1.f;
		if (!S.bChallenge || !UsesTeams(S) || bSolo || Count0 == Count1 || Count0 <= 0 || Count1 <= 0)
		{
			return;
		}
		OutTeam = Count0 < Count1 ? 0 : 1;
		OutArmor = S.bChallengeCustom ? S.ChallengeArmor : RecommendedChallenge(Count0, Count1);
		OutDamage = S.bChallengeCustom ? S.ChallengeDamage : OutArmor;
	}

	void Sanitize(FMechRoomSettings& S)
	{
		S.Map = FMath::Clamp(S.Map, 0, MechMaps::Num() - 1);
		const MechMaps::FMap& M = MechMaps::Get(S.Map);
		S.bDefense = S.bDefense && M.Supports(MechMaps::Defense);
		if (!M.IsCore() && !M.IsTraining() && !M.Supports(MechMaps::Elim) && !M.Supports(MechMaps::DM))
		{
			S.bDefense = M.Supports(MechMaps::Defense);  // 只能守的图
		}
		MechRules::Sanitize(S.Rules);
		if (!M.IsCore() && !M.IsTraining() && !S.bDefense && !M.Supports(S.Rules.IsDeathmatch() ? MechMaps::DM : MechMaps::Elim))
		{
			S.Rules.Mode = M.Supports(MechMaps::Elim) ? EMechMode::Elimination : EMechMode::Deathmatch;
		}
		if (S.AIs.Num() > MaxMechs)
		{
			S.AIs.SetNum(MaxMechs);
		}
		for (FMechRoomAI& A : S.AIs)
		{
			A.Team = FMath::Clamp(A.Team, 0, 1);
			A.Level = FMath::Clamp(A.Level, 0, 3);
		}
		S.AILevel = FMath::Clamp(S.AILevel, 0, 3);
		S.ChallengeArmor = FMath::Clamp(S.ChallengeArmor, 1.f, 10.f);
		S.ChallengeDamage = FMath::Clamp(S.ChallengeDamage, 1.f, 10.f);
	}

	FString MatchURL(const FMechRoomSettings& In)
	{
		FMechRoomSettings S = In;
		Sanitize(S);
		const MechMaps::FMap& M = MechMaps::Get(S.Map);
		FString Options;
		if (M.IsTraining())
		{
			Options = TEXT("Range");
		}
		else if (S.bDefense)
		{
			Options = TEXT("Defense");
		}
		else if (M.IsCore())
		{
			Options = TEXT("Core");
		}
		else
		{
			Options = FString::Printf(TEXT("Match?Mode=%s"), S.Rules.IsDeathmatch() ? TEXT("DM") : TEXT("Elim"));
		}
		return FString::Printf(TEXT("%s?%s?listen"), M.Path, *Options);
	}

	FText ModeName(const FMechRoomSettings& S)
	{
		if (IsTraining(S))
		{
			return INVTEXT("训练场");
		}
		if (S.bDefense)
		{
			return INVTEXT("保卫战");
		}
		return MechRules::ModeName(S.Rules, IsCore(S));
	}

	FString DescribeRules(const FMechRoomSettings& S)
	{
		if (IsTraining(S))
		{
			return S.bFriendlyFire ? TEXT("训练场 · 自由练习 · 友伤开") : TEXT("训练场 · 自由练习");
		}
		if (S.bDefense)
		{
			return TEXT("保卫战 · 一起守指挥所，10 波");
		}
		return MechRules::Describe(S.Rules, IsCore(S));
	}

	FString Describe(const FMechRoomSettings& S)
	{
		if (IsTraining(S))
		{
			return DescribeRules(S);
		}
		return FString::Printf(TEXT("%s · %s"), MechMaps::Get(S.Map).Name, *DescribeRules(S));
	}

	FMechRoomSettings LoadSaved()
	{
		FMechRoomSettings S;
		MechRoomPriv::DefaultAIs(S);
		if (!MechRoomPriv::IsTestRun() && GConfig)
		{
			const TCHAR* Section = MechRoomPriv::RoomSection;
			S.Rules = FMechSettings::Get().Rules;
			S.bBans = FMechSettings::Get().bDraftBans;
			GConfig->GetInt(Section, TEXT("Map"), S.Map, GGameUserSettingsIni);
			GConfig->GetBool(Section, TEXT("Defense"), S.bDefense, GGameUserSettingsIni);
			GConfig->GetInt(Section, TEXT("AILevel"), S.AILevel, GGameUserSettingsIni);
			GConfig->GetBool(Section, TEXT("Challenge"), S.bChallenge, GGameUserSettingsIni);
			GConfig->GetBool(Section, TEXT("ChallengeCustom"), S.bChallengeCustom, GGameUserSettingsIni);
			GConfig->GetFloat(Section, TEXT("ChallengeArmor"), S.ChallengeArmor, GGameUserSettingsIni);
			GConfig->GetFloat(Section, TEXT("ChallengeDamage"), S.ChallengeDamage, GGameUserSettingsIni);
			GConfig->GetBool(Section, TEXT("FriendlyFire"), S.bFriendlyFire, GGameUserSettingsIni);
			GConfig->GetBool(Section, TEXT("Easter"), S.bEaster, GGameUserSettingsIni);
			// 人机名单：「队伍:强度,队伍:强度」；存过空串 = 房主把人机都删了
			FString Text;
			if (GConfig->GetString(Section, TEXT("AIs"), Text, GGameUserSettingsIni))
			{
				S.AIs.Reset();
				TArray<FString> Parts;
				Text.ParseIntoArray(Parts, TEXT(","));
				for (const FString& Part : Parts)
				{
					FString TeamText, LevelText;
					if (Part.Split(TEXT(":"), &TeamText, &LevelText))
					{
						FMechRoomAI A;
						A.Team = FCString::Atoi(*TeamText);
						A.Level = FCString::Atoi(*LevelText);
						S.AIs.Add(A);
					}
				}
			}
		}
		Sanitize(S);
		return S;
	}

	void Save(const FMechRoomSettings& S)
	{
		if (MechRoomPriv::IsTestRun() || !GConfig)
		{
			return;
		}
		FMechSettings& Saved = FMechSettings::Get();
		Saved.Rules = S.Rules;
		Saved.bDraftBans = S.bBans;
		Saved.Save();
		const TCHAR* Section = MechRoomPriv::RoomSection;
		GConfig->SetInt(Section, TEXT("Map"), S.Map, GGameUserSettingsIni);
		GConfig->SetBool(Section, TEXT("Defense"), S.bDefense, GGameUserSettingsIni);
		GConfig->SetInt(Section, TEXT("AILevel"), S.AILevel, GGameUserSettingsIni);
		GConfig->SetBool(Section, TEXT("Challenge"), S.bChallenge, GGameUserSettingsIni);
		GConfig->SetBool(Section, TEXT("ChallengeCustom"), S.bChallengeCustom, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("ChallengeArmor"), S.ChallengeArmor, GGameUserSettingsIni);
		GConfig->SetFloat(Section, TEXT("ChallengeDamage"), S.ChallengeDamage, GGameUserSettingsIni);
		GConfig->SetBool(Section, TEXT("FriendlyFire"), S.bFriendlyFire, GGameUserSettingsIni);
		GConfig->SetBool(Section, TEXT("Easter"), S.bEaster, GGameUserSettingsIni);
		TArray<FString> Parts;
		for (const FMechRoomAI& A : S.AIs)
		{
			Parts.Add(FString::Printf(TEXT("%d:%d"), A.Team, A.Level));
		}
		GConfig->SetString(Section, TEXT("AIs"), *FString::Join(Parts, TEXT(",")), GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void UMechRoomSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
}

UMechRoomSubsystem* UMechRoomSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMechRoomSubsystem>() : nullptr;
}

void UMechRoomSubsystem::BeginRoom()
{
	bActive = true;
	bMatchPending = false;
	Members.Reset();
	Expected.Reset();
	NextNumber = 0;
	Settings = MechRoom::LoadSaved();
	UE_LOG(LogTemp, Display, TEXT("[room] 开房：%s，人机 %d 台"), *MechRoom::Describe(Settings), Settings.AIs.Num());
}

void UMechRoomSubsystem::EndRoom()
{
	if (bActive)
	{
		UE_LOG(LogTemp, Display, TEXT("[room] 房间结束"));
	}
	bActive = false;
	bMatchPending = false;
	Members.Reset();
	Expected.Reset();
}
