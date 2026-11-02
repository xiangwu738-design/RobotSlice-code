#include "MechRules.h"

#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

namespace MechRules
{
	const TArray<int32>& RoundOptions()
	{
		static const TArray<int32> V = {1, 2, 3, 4};
		return V;
	}

	const TArray<int32>& TeamTargetOptions()
	{
		static const TArray<int32> V = {10, 20, 30, 40, 50};
		return V;
	}

	const TArray<int32>& SoloTargetOptions()
	{
		static const TArray<int32> V = {5, 10, 15, 20, 25};
		return V;
	}

	const TArray<int32>& TimeOptions()
	{
		static const TArray<int32> V = {300, 600, 900, 1200};
		return V;
	}

	FText RoundsName(int32 RoundsToWin)
	{
		switch (RoundsToWin)
		{
		case 1:
			return INVTEXT("一局定胜负");
		case 2:
			return INVTEXT("三局两胜");
		case 3:
			return INVTEXT("五局三胜");
		case 4:
			return INVTEXT("七局四胜");
		default:
			return FText::FromString(FString::Printf(TEXT("先赢 %d 局"), RoundsToWin));
		}
	}

	FText ModeName(const FMechRules& Rules, bool bCore)
	{
		if (bCore)
		{
			return INVTEXT("夺芯");
		}
		if (Rules.IsDeathmatch())
		{
			return Rules.bSolo ? INVTEXT("个人死斗") : INVTEXT("团队死斗");
		}
		return INVTEXT("团队淘汰");
	}

	FString Describe(const FMechRules& Rules, bool bCore)
	{
		if (bCore)
		{
			return TEXT("夺芯");
		}
		if (Rules.IsDeathmatch())
		{
			return FString::Printf(TEXT("%s · 先到 %d 击毁 · %d 分钟"), *ModeName(Rules, false).ToString(), Rules.KillTarget(), Rules.TimeLimit / 60);
		}
		return FString::Printf(TEXT("团队淘汰 · %s"), *RoundsName(Rules.RoundsToWin).ToString());
	}

	void Sanitize(FMechRules& Rules)
	{
		Rules.RoundsToWin = FMath::Clamp(Rules.RoundsToWin, 1, 9);
		Rules.TeamTarget = FMath::Clamp(Rules.TeamTarget, 1, 999);
		Rules.SoloTarget = FMath::Clamp(Rules.SoloTarget, 1, 999);
		Rules.TimeLimit = FMath::Clamp(Rules.TimeLimit, 30, 3600);
		if (Rules.Mode != EMechMode::Elimination && Rules.Mode != EMechMode::Deathmatch)
		{
			Rules.Mode = EMechMode::Elimination;
		}
	}

	void ApplyOptions(FMechRules& Rules, const FString& Options, const TCHAR* CommandLine)
	{
		auto ParseMode = [&Rules](const FString& Value) {
			if (Value.Equals(TEXT("DM"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Deathmatch"), ESearchCase::IgnoreCase))
			{
				Rules.Mode = EMechMode::Deathmatch;
			}
			else if (Value.Equals(TEXT("Elim"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("Elimination"), ESearchCase::IgnoreCase))
			{
				Rules.Mode = EMechMode::Elimination;
			}
		};
		// 地址参数（主菜单开局、联机开房走这条）
		if (UGameplayStatics::HasOption(Options, TEXT("Mode")))
		{
			ParseMode(UGameplayStatics::ParseOption(Options, TEXT("Mode")));
		}
		if (UGameplayStatics::HasOption(Options, TEXT("Solo")))
		{
			Rules.bSolo = true;
		}
		if (UGameplayStatics::HasOption(Options, TEXT("DMTeam")))
		{
			Rules.bSolo = false;
		}
		Rules.RoundsToWin = UGameplayStatics::GetIntOption(Options, TEXT("Rounds"), Rules.RoundsToWin);
		if (UGameplayStatics::HasOption(Options, TEXT("Target")))
		{
			(Rules.bSolo ? Rules.SoloTarget : Rules.TeamTarget) = UGameplayStatics::GetIntOption(Options, TEXT("Target"), Rules.KillTarget());
		}
		Rules.TimeLimit = UGameplayStatics::GetIntOption(Options, TEXT("Time"), Rules.TimeLimit);
		// 命令行（测试开关）
		FString Value;
		if (FParse::Value(CommandLine, TEXT("MechMode="), Value))
		{
			ParseMode(Value);
		}
		if (FParse::Param(CommandLine, TEXT("MechDMSolo")))
		{
			Rules.Mode = EMechMode::Deathmatch;
			Rules.bSolo = true;
		}
		if (FParse::Param(CommandLine, TEXT("MechDMTeam")))
		{
			Rules.Mode = EMechMode::Deathmatch;
			Rules.bSolo = false;
		}
		int32 Target = 0;
		if (FParse::Value(CommandLine, TEXT("MechDMTarget="), Target))
		{
			(Rules.bSolo ? Rules.SoloTarget : Rules.TeamTarget) = Target;
		}
		FParse::Value(CommandLine, TEXT("MechDMTime="), Rules.TimeLimit);
		FParse::Value(CommandLine, TEXT("MechWinRounds="), Rules.RoundsToWin);
		Sanitize(Rules);
	}
}
