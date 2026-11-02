#include "MechGameState.h"

#include "Engine/World.h"
#include "MechCharacter.h"
#include "Net/UnrealNetwork.h"

float AMechGameState::GetPhaseRemaining() const
{
	return FMath::Max(0.f, PhaseEndTime - GetServerWorldTimeSeconds());
}

const FMechRespawn* AMechGameState::RespawnOf(const AMechCharacter* Mech) const
{
	return Mech ? Respawns.FindByPredicate([Mech](const FMechRespawn& R) { return R.Mech.Get() == Mech; }) : nullptr;
}

void AMechGameState::MulticastFeed_Implementation(const FString& Text, int32 Team)
{
	FFeedLine& Line = Feed.AddDefaulted_GetRef();
	Line.Text = Text;
	Line.Team = Team;
	Line.Time = GetWorld()->GetTimeSeconds();
	if (Feed.Num() > 6)
	{
		Feed.RemoveAt(0);
	}
}

void AMechGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechGameState, Phase);
	DOREPLIFETIME(AMechGameState, Round);
	DOREPLIFETIME(AMechGameState, TeamScore);
	DOREPLIFETIME(AMechGameState, PhaseEndTime);
	DOREPLIFETIME(AMechGameState, RoundWinner);
	DOREPLIFETIME(AMechGameState, MatchWinner);
	DOREPLIFETIME(AMechGameState, TeamSize);
	DOREPLIFETIME(AMechGameState, RoundsToWin);
	DOREPLIFETIME(AMechGameState, bMenuMode);
	DOREPLIFETIME(AMechGameState, bRangeMode);
	DOREPLIFETIME(AMechGameState, bCoreMode);
	DOREPLIFETIME(AMechGameState, CoreTarget);
	DOREPLIFETIME(AMechGameState, bOvertime);
	DOREPLIFETIME(AMechGameState, Results);
	DOREPLIFETIME(AMechGameState, Draft);
	DOREPLIFETIME(AMechGameState, Lobby);
	DOREPLIFETIME(AMechGameState, AILevel);
	DOREPLIFETIME(AMechGameState, bDraftBans);
	DOREPLIFETIME(AMechGameState, MapCenter);
	DOREPLIFETIME(AMechGameState, MapExtent);
	DOREPLIFETIME(AMechGameState, MapSpawn);
	DOREPLIFETIME(AMechGameState, Rules);
	DOREPLIFETIME(AMechGameState, Respawns);
	DOREPLIFETIME(AMechGameState, WinnerName);
	DOREPLIFETIME(AMechGameState, MatchEndTime);
	DOREPLIFETIME(AMechGameState, bRoomMode);
	DOREPLIFETIME(AMechGameState, bRoomMatch);
	DOREPLIFETIME(AMechGameState, RoomArrived);
	DOREPLIFETIME(AMechGameState, RoomExpected);
	DOREPLIFETIME(AMechGameState, bRoomTravelling);
	DOREPLIFETIME(AMechGameState, Room);
	DOREPLIFETIME(AMechGameState, TeamCount);
	DOREPLIFETIME(AMechGameState, ChallengeTeam);
	DOREPLIFETIME(AMechGameState, ChallengeArmor);
	DOREPLIFETIME(AMechGameState, ChallengeDamage);
	DOREPLIFETIME(AMechGameState, bFriendlyFire);
}
