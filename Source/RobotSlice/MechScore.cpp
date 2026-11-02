// 结算积分（11-01，见 MechScore.h、docs/结算积分规则.md）：服务器上的记分（AMechGameMode 的几个成员函数放在这里，免得在 MechGameMode.cpp 里铺一大段）
// 和各端都用的显示（挑 MVP、MVP 理由、日志分项）。

#include "MechScore.h"

#include "Engine/World.h"
#include "MechCharacter.h"
#include "MechGameMode.h"
#include "MechGameState.h"

// 不用匿名命名空间：合并编译（unity）时会和别的文件里同名的东西撞
namespace MechScorePriv
{
	AMechGameMode* ModeOf(const AActor* A)
	{
		UWorld* W = A ? A->GetWorld() : nullptr;
		return W ? W->GetAuthGameMode<AMechGameMode>() : nullptr;  // 客户端没有 GameMode：什么都不记
	}

	FString FormatDamage(float V)
	{
		return V >= 100000.f ? FString::Printf(TEXT("%.0fK"), V / 1000.f) : V >= 10000.f ? FString::Printf(TEXT("%.1fK"), V / 1000.f) : FString::Printf(TEXT("%d"), FMath::RoundToInt(V));
	}

	struct FPartInfo
	{
		const TCHAR* Label;  // 界面（英文缩写，和铭牌的 KO / DTH / DMG 一个写法；界面不写中文提示）
		const TCHAR* Name;   // 日志
	};
	const FPartInfo Infos[MechScore::NumParts] = {
		{TEXT("DMG"), TEXT("伤害")},
		{TEXT("KO"), TEXT("击毁")},
		{TEXT("AST"), TEXT("助攻")},
		{TEXT("BRK"), TEXT("打出失衡")},
		{TEXT("CTRL"), TEXT("控制")},
		{TEXT("SAVE"), TEXT("救人")},
		{TEXT("CORE"), TEXT("交芯")},
		{TEXT("CARRIER"), TEXT("截杀带芯")},
		{TEXT("UNITS"), TEXT("杂兵")},
		{TEXT("MORTAR"), TEXT("迫击炮")},
		{TEXT("BOSS"), TEXT("首领")},
		{TEXT("DEF"), TEXT("防守")},
	};
	static_assert(UE_ARRAY_COUNT(Infos) == MechScore::NumParts, "EMechScorePart 加了新项，这张表也要加");

	/** 这一项后面跟的数：有计数的写次数（伤害写伤害量），只有分的写「+分」 */
	FString PartValue(const FMechResultRow& R, EMechScorePart P)
	{
		switch (P)
		{
		case EMechScorePart::Damage:
			return FormatDamage(R.Dealt);
		case EMechScorePart::Kill:
			return FString::FromInt(R.Kills);
		case EMechScorePart::Assist:
			return FString::FromInt(R.Assists);
		case EMechScorePart::Break:
			return FString::FromInt(R.Breaks);
		case EMechScorePart::Control:
			return FString::FromInt(R.Controls);
		case EMechScorePart::Rescue:
			return FString::FromInt(R.Rescues);
		case EMechScorePart::Core:
			return FString::FromInt(R.Cores);
		case EMechScorePart::Carrier:
			return FString::FromInt(R.CarrierKills);
		case EMechScorePart::Grunt:
			return FString::FromInt(R.Grunts);
		case EMechScorePart::Mortar:
			return FString::FromInt(R.Mortars);
		default:
			return FString::Printf(TEXT("+%d"), MechScore::PartOf(R, P));
		}
	}

	/** 日志里这一项有没有「几次」可写（伤害 / 首领 / 防守只有分） */
	int32 PartCount(const FMechResultRow& R, EMechScorePart P)
	{
		switch (P)
		{
		case EMechScorePart::Kill:
			return R.Kills;
		case EMechScorePart::Assist:
			return R.Assists;
		case EMechScorePart::Break:
			return R.Breaks;
		case EMechScorePart::Control:
			return R.Controls;
		case EMechScorePart::Rescue:
			return R.Rescues;
		case EMechScorePart::Core:
			return R.Cores;
		case EMechScorePart::Carrier:
			return R.CarrierKills;
		case EMechScorePart::Grunt:
			return R.Grunts;
		case EMechScorePart::Mortar:
			return R.Mortars;
		default:
			return -1;
		}
	}
}

// ================================================================ 玩法代码的入口（服务器）
void MechScore::NoteControl(AMechCharacter* By, AMechCharacter* Victim)
{
	if (AMechGameMode* GM = MechScorePriv::ModeOf(By))
	{
		GM->RecordControl(By, Victim);
	}
}

void MechScore::NoteRescue(AMechCharacter* By, bool bCore)
{
	if (AMechGameMode* GM = MechScorePriv::ModeOf(By))
	{
		GM->RecordRescue(By, bCore);
	}
}

void MechScore::NoteGruntDamage(AMechCharacter* By, float Damage)
{
	if (AMechGameMode* GM = MechScorePriv::ModeOf(By))
	{
		GM->RecordGruntDamage(By, Damage);
	}
}

void MechScore::NoteDefenseKill(AMechCharacter* By, EMechDefenseKill Kind, const FString& VictimName, bool bNearCore)
{
	if (AMechGameMode* GM = MechScorePriv::ModeOf(By))
	{
		GM->RecordDefenseKill(By, static_cast<uint8>(Kind), VictimName, bNearCore);
	}
}

// ================================================================ AMechGameMode：记分（服务器）
void AMechGameMode::RecordControl(AMechCharacter* By, AMechCharacter* Victim)
{
	if (!By || !Victim || By == Victim || By->GetTeam() == Victim->GetTeam() || Victim->IsTrainingDummy())
	{
		return;
	}
	Stats.FindOrAdd(Victim->DisplayName).Team = Victim->GetTeam();
	FMechStats& S = Stats.FindOrAdd(By->DisplayName);
	S.Team = By->GetTeam();
	++S.Controls;
	S.Points[static_cast<int32>(EMechScorePart::Control)] += MechScore::Control;
	NoteContribution(By, Victim, 0.f, true);
}

void AMechGameMode::RecordRescue(AMechCharacter* By, bool bCore)
{
	if (!By)
	{
		return;
	}
	FMechStats& S = Stats.FindOrAdd(By->DisplayName);
	S.Team = By->GetTeam();
	++S.Rescues;
	S.Points[static_cast<int32>(EMechScorePart::Rescue)] += bCore ? MechScore::CoreRescue : MechScore::NetRescue;
	UE_LOG(LogTemp, Display, TEXT("[score] %s 救人（%s）+%d"), *By->DisplayName, bCore ? TEXT("捡回队友核心") : TEXT("钩爪拉出被网住的队友"),
		bCore ? MechScore::CoreRescue : MechScore::NetRescue);
}

void AMechGameMode::RecordGruntDamage(AMechCharacter* By, float Damage)
{
	if (!By || By->GetTeam() != 0 || Damage <= 0.f)
	{
		return;
	}
	FMechStats& S = Stats.FindOrAdd(By->DisplayName);
	S.Team = 0;
	S.Dealt += Damage;  // 杂兵不走机体的受击（RecordHit），输出统计也在这里补上
	S.ScoreDamage += Damage;
}

void AMechGameMode::RecordDefenseKill(AMechCharacter* By, uint8 Kind, const FString& VictimName, bool bNearCore)
{
	if (!By || By->GetTeam() != 0 || Kind > static_cast<uint8>(EMechDefenseKill::Boss))
	{
		return;
	}
	const EMechDefenseKill K = static_cast<EMechDefenseKill>(Kind);
	const int32 Base = MechScore::DefenseKill[Kind];
	{
		FMechStats& S = Stats.FindOrAdd(By->DisplayName);
		S.Team = 0;
		++S.Kills;
		EMechScorePart Part = EMechScorePart::Boss;
		if (K == EMechDefenseKill::Mortar)
		{
			++S.Mortars;
			Part = EMechScorePart::Mortar;
		}
		else if (K <= EMechDefenseKill::Tank)
		{
			++S.Grunts;
			Part = EMechScorePart::Grunt;
		}
		S.Points[static_cast<int32>(Part)] += Base;
		if (bNearCore)
		{
			S.Points[static_cast<int32>(EMechScorePart::Defend)] += FMath::RoundToInt(Base * MechScore::DefendBonus);
		}
	}
	if (!VictimName.IsEmpty())
	{
		AwardAssists(VictimName, 1, By->DisplayName);  // 精英 / 首领机甲：一起打的人记助攻（杂兵太多，不记）
	}
}

void AMechGameMode::NoteContribution(AMechCharacter* By, AMechCharacter* Victim, float Damage, bool bControl)
{
	if (!By || !Victim)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	FMechAssistMark& M = AssistMarks.FindOrAdd(Victim->DisplayName).FindOrAdd(By->DisplayName);
	if (Damage > 0.f)
	{
		// 隔了一整个窗口没再打它：之前的伤害不算了（一直在打就一直累计）
		if (Now - M.DamageTime > MechScore::AssistWindow)
		{
			M.Damage = 0.f;
		}
		M.Damage += Damage;
		M.DamageTime = Now;
	}
	if (bControl)
	{
		M.ControlTime = Now;
	}
}

void AMechGameMode::AwardAssists(const FString& VictimName, int32 VictimTeam, const FString& KillerName)
{
	TMap<FString, FMechAssistMark>* Marks = AssistMarks.Find(VictimName);
	if (!Marks)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	for (const auto& Pair : *Marks)
	{
		if (Pair.Key == KillerName || Pair.Key == VictimName)
		{
			continue;
		}
		FMechStats* S = Stats.Find(Pair.Key);
		if (!S || S->Team == VictimTeam)
		{
			continue;
		}
		const FMechAssistMark& M = Pair.Value;
		const bool bByDamage = Now - M.DamageTime <= MechScore::AssistWindow && M.Damage >= MechScore::AssistMinDamage;
		const bool bByControl = Now - M.ControlTime <= MechScore::AssistWindow;
		if (bByDamage || bByControl)
		{
			++S->Assists;
			S->Points[static_cast<int32>(EMechScorePart::Assist)] += MechScore::Assist;
			UE_LOG(LogTemp, Display, TEXT("[score] %s 助攻击毁 %s（击毁者 %s；%s%s）"), *Pair.Key, *VictimName, KillerName.IsEmpty() ? TEXT("无") : *KillerName,
				bByDamage ? *FString::Printf(TEXT("%.0f 秒内伤害 %.0f"), MechScore::AssistWindow, M.Damage) : TEXT(""),
				bByControl ? (bByDamage ? TEXT("、控制 / 失衡") : TEXT("控制 / 失衡")) : TEXT(""));
		}
	}
	AssistMarks.Remove(VictimName);
}

void AMechGameMode::FillScore(FMechResultRow& Row, const FMechStats& S) const
{
	Row.Parts.SetNumZeroed(MechScore::NumParts);
	for (int32 i = 0; i < MechScore::NumParts; ++i)
	{
		Row.Parts[i] = S.Points[i];
	}
	Row.Parts[static_cast<int32>(EMechScorePart::Damage)] = FMath::FloorToInt(S.ScoreDamage / MechScore::DamagePerPoint);
	Row.Parts[static_cast<int32>(EMechScorePart::Boss)] += FMath::FloorToInt(S.BossDamage / MechScore::DamagePerPoint) * MechScore::BossDamageBonus;
	Row.Assists = S.Assists;
	Row.Breaks = S.Breaks;
	Row.Controls = S.Controls;
	Row.Rescues = S.Rescues;
	Row.CarrierKills = S.CarrierKills;
	Row.Grunts = S.Grunts;
	Row.Mortars = S.Mortars;
	Row.Deaths = S.Deaths;
	MechScore::Finish(Row);
}

void AMechGameMode::LogScores() const
{
	const AMechGameState* GS = GetMechGameState();
	TArray<FMechResultRow> Rows;
	for (const auto& Pair : Stats)
	{
		if (bDefenseMode && Pair.Value.Team != 0)
		{
			continue;
		}
		FMechResultRow& R = Rows.AddDefaulted_GetRef();
		R.Name = Pair.Key;
		R.Team = Pair.Value.Team;
		R.Kills = Pair.Value.Kills;
		R.Dealt = FMath::RoundToInt(Pair.Value.Dealt);
		R.Cores = Pair.Value.Cores;
		FillScore(R, Pair.Value);
	}
	if (Rows.Num() == 0)
	{
		return;
	}
	Rows.Sort([](const FMechResultRow& A, const FMechResultRow& B) { return A.Score > B.Score; });
	for (const FMechResultRow& R : Rows)
	{
		UE_LOG(LogTemp, Display, TEXT("[score]   %s（%s）%s"), *R.Name, R.Team == 0 ? TEXT("蓝队") : R.Team == 1 ? TEXT("红队") : TEXT("个人"), *MechScore::Breakdown(R));
	}
	// MVP 和结算场景同一个规则：团队 = 胜方里积分最高（平局全场）；个人死斗 = 全场最高（名次照旧按击毁）；保卫战 = 玩家里最高
	const bool bSolo = GS && GS->IsSoloDM();
	const int32 Team = bDefenseMode ? 0 : (bSolo || !GS || GS->MatchWinner < 0) ? -1 : GS->MatchWinner;
	const int32 Mvp = MechScore::PickMVP(Rows, Team);
	// 对照：按以前的规矩（击毁多、阵亡少）会是谁
	const FMechResultRow* OldWay = nullptr;
	for (const FMechResultRow& R : Rows)
	{
		if ((Team < 0 || R.Team == Team) && (!OldWay || R.Kills > OldWay->Kills || (R.Kills == OldWay->Kills && R.Deaths < OldWay->Deaths)))
		{
			OldWay = &R;
		}
	}
	if (Rows.IsValidIndex(Mvp))
	{
		const FMechResultRow& M = Rows[Mvp];
		UE_LOG(LogTemp, Display, TEXT("[score] MVP：%s（%s）积分 %d，理由「%s」；按击毁 / 阵亡排会是 %s（%d / %d）%s"), *M.Name,
			bDefenseMode ? TEXT("保卫战玩家里最高") : bSolo ? TEXT("个人死斗全场最高，名次另按击毁") : Team < 0 ? TEXT("平局，全场最高") : TEXT("胜方里最高"), M.Score,
			*MechScore::Reason(M, Rows), OldWay ? *OldWay->Name : TEXT("?"), OldWay ? OldWay->Kills : 0, OldWay ? OldWay->Deaths : 0,
			OldWay && OldWay->Name != M.Name ? TEXT("——不是同一个人") : TEXT(""));
	}
}

// ================================================================ 显示（各端）
int32 MechScore::PartOf(const FMechResultRow& Row, EMechScorePart Part)
{
	const int32 i = static_cast<int32>(Part);
	return Row.Parts.IsValidIndex(i) ? Row.Parts[i] : 0;
}

const TCHAR* MechScore::PartLabel(EMechScorePart Part)
{
	return Part < EMechScorePart::Num ? MechScorePriv::Infos[static_cast<int32>(Part)].Label : TEXT("?");
}

const TCHAR* MechScore::PartName(EMechScorePart Part)
{
	return Part < EMechScorePart::Num ? MechScorePriv::Infos[static_cast<int32>(Part)].Name : TEXT("?");
}

void MechScore::Finish(FMechResultRow& Row)
{
	int32 Sum = Row.Deaths * Death;
	for (const int32 P : Row.Parts)
	{
		Sum += P;
	}
	Row.Score = FMath::Max(0, Sum);
}

void MechScore::FromCounts(FMechResultRow& Row)
{
	Row.Parts.SetNumZeroed(NumParts);
	Row.Parts[static_cast<int32>(EMechScorePart::Damage)] = FMath::FloorToInt(Row.Dealt / DamagePerPoint);
	Row.Parts[static_cast<int32>(EMechScorePart::Kill)] = Row.Kills * Kill;
	Row.Parts[static_cast<int32>(EMechScorePart::Assist)] = Row.Assists * Assist;
	Row.Parts[static_cast<int32>(EMechScorePart::Break)] = Row.Breaks * Break;
	Row.Parts[static_cast<int32>(EMechScorePart::Control)] = Row.Controls * Control;
	Row.Parts[static_cast<int32>(EMechScorePart::Rescue)] = Row.Rescues * NetRescue;
	Row.Parts[static_cast<int32>(EMechScorePart::Core)] = Row.Cores * CoreDeliver;
	Row.Parts[static_cast<int32>(EMechScorePart::Carrier)] = Row.CarrierKills * CarrierKill;
	Finish(Row);
}

int32 MechScore::PickMVP(const TArray<FMechResultRow>& Rows, int32 Team)
{
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const FMechResultRow& R = Rows[i];
		if (Team >= 0 && R.Team != Team)
		{
			continue;
		}
		if (Best == INDEX_NONE)
		{
			Best = i;
			continue;
		}
		const FMechResultRow& B = Rows[Best];
		if (R.Score != B.Score ? R.Score > B.Score : R.Deaths != B.Deaths ? R.Deaths < B.Deaths : R.Dealt > B.Dealt)
		{
			Best = i;
		}
	}
	return Best;
}

FString MechScore::Reason(const FMechResultRow& Row, const TArray<FMechResultRow>& All, int32 Items)
{
	// 「最突出」：先挑这一项全场最多的（他在这一项上压过所有人），再按这一项的分数高低
	struct FCand
	{
		EMechScorePart Part;
		int32 Points;
		bool bTop;
	};
	TArray<FCand> Cands;
	for (int32 i = 0; i < NumParts; ++i)
	{
		const EMechScorePart P = static_cast<EMechScorePart>(i);
		const int32 Mine = PartOf(Row, P);
		if (Mine <= 0)
		{
			continue;
		}
		bool bTop = true;
		for (const FMechResultRow& O : All)
		{
			bTop &= O.Name == Row.Name || PartOf(O, P) <= Mine;
		}
		Cands.Add({P, Mine, bTop});
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.bTop != B.bTop ? A.bTop : A.Points > B.Points; });
	FString Out = FString::Printf(TEXT("SCORE %d"), Row.Score);
	for (int32 i = 0; i < FMath::Min(Items, Cands.Num()); ++i)
	{
		Out += FString::Printf(TEXT("  ·  %s %s"), PartLabel(Cands[i].Part), *MechScorePriv::PartValue(Row, Cands[i].Part));
	}
	return Out;
}

FString MechScore::Breakdown(const FMechResultRow& Row)
{
	FString Out = FString::Printf(TEXT("积分 %d ="), Row.Score);
	bool bFirst = true;
	for (int32 i = 0; i < NumParts; ++i)
	{
		const EMechScorePart P = static_cast<EMechScorePart>(i);
		const int32 Pts = PartOf(Row, P);
		if (Pts == 0)
		{
			continue;
		}
		const int32 N = MechScorePriv::PartCount(Row, P);
		Out += FString::Printf(TEXT("%s%s %d%s"), bFirst ? TEXT(" ") : TEXT(" + "), PartName(P), Pts, N >= 0 ? *FString::Printf(TEXT("（%d）"), N) : TEXT(""));
		bFirst = false;
	}
	if (bFirst)
	{
		Out += TEXT(" 0");
	}
	Out += FString::Printf(TEXT("；击毁 %d 阵亡 %d 输出 %d"), Row.Kills, Row.Deaths, Row.Dealt);
	if constexpr (Death != 0)
	{
		if (Row.Deaths > 0)
		{
			Out += FString::Printf(TEXT("（阵亡 %+d）"), Row.Deaths * Death);
		}
	}
	return Out;
}
