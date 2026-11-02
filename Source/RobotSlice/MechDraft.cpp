#include "MechDraft.h"

#include "MechLoadout.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

int32 FMechDraft::FindPlayer(int32 PlayerId) const
{
	if (PlayerId < 0)
	{
		return INDEX_NONE;
	}
	return Slots.IndexOfByPredicate([PlayerId](const FMechDraftSlot& S) { return S.PlayerId == PlayerId; });
}

const FMechDraftSlot* FMechDraft::FindSlot(int32 Team, int32 Index) const
{
	return Slots.FindByPredicate([Team, Index](const FMechDraftSlot& S) { return S.Team == Team && S.Index == Index; });
}

bool MechDraft::IsBannable(int32 Category)
{
	// 11-01 战术装备并入肩部（用户：「禁用阶段可以禁它们，和其他肩部装备一样」）：以前能禁的是射击、近战、战术三类，
	// 肩部整类不能禁；现在肩部整类都能禁，导弹、火炮这些也能禁了（没做好的部件 IsAllowed / AutoBan 按 bReady 跳过）
	return Category == MechLoadout::Shooter || Category == MechLoadout::Melee || Category == MechLoadout::Shoulder;
}

bool MechDraft::IsBannedFor(const FMechDraft& Draft, int32 Team, int32 Category, int32 Index)
{
	return Draft.Bans.ContainsByPredicate([&](const FMechDraftBan& B) { return B.Team != Team && B.Category == Category && B.Index == Index; });
}

bool MechDraft::HasBanned(const FMechDraft& Draft, int32 Team, int32 Category, int32 Index)
{
	return Draft.Bans.ContainsByPredicate([&](const FMechDraftBan& B) { return B.Team == Team && B.Category == Category && B.Index == Index; });
}

bool MechDraft::IsAllowed(const FMechDraft& Draft, int32 Team, int32 Category, int32 Index)
{
	return Index >= 0 && Index < MechLoadout::NumItems(Category) && MechLoadout::Item(Category, Index).bReady && !IsBannedFor(Draft, Team, Category, Index);
}

FMechLoadout MechDraft::Sanitize(const FMechDraft& Draft, int32 Team, const FMechLoadout& Wanted)
{
	FMechLoadout Out = Wanted;
	for (int32 c = 0; c < MechLoadout::NumCategories; ++c)
	{
		if (IsAllowed(Draft, Team, c, MechLoadout::Get(Out, c)))
		{
			continue;
		}
		for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
		{
			if (IsAllowed(Draft, Team, c, i))
			{
				MechLoadout::Set(Out, c, i);
				break;
			}
		}
	}
	return Out;
}

FMechLoadout MechDraft::AutoPick(const FMechDraft& Draft, int32 Slot, FRandomStream& Rand)
{
	FMechLoadout Out;
	if (!Draft.Slots.IsValidIndex(Slot))
	{
		return Out;
	}
	const int32 Team = Draft.Slots[Slot].Team;
	for (int32 c = 0; c < MechLoadout::NumCategories; ++c)
	{
		// 能用的里面随机；武器和肩部装备避开队友已经定下的（队里各拿各的，配合更有意思；11-01 肩部也算，AI 不会全带同一件）
		TArray<int32> Fresh;
		TArray<int32> Any;
		for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
		{
			if (!IsAllowed(Draft, Team, c, i))
			{
				continue;
			}
			Any.Add(i);
			const bool bTaken = (c == MechLoadout::Shooter || c == MechLoadout::Melee || c == MechLoadout::Shoulder) && Draft.Slots.ContainsByPredicate([&](const FMechDraftSlot& S) {
				return S.Team == Team && S.bLocked && MechLoadout::Get(S.Loadout, c) == i;
			});
			if (!bTaken)
			{
				Fresh.Add(i);
			}
		}
		const TArray<int32>& From = Fresh.Num() ? Fresh : Any;
		if (From.Num())
		{
			MechLoadout::Set(Out, c, From[Rand.RandRange(0, From.Num() - 1)]);
		}
	}
	// 测试：-MechForceShoulder=34 让代选的肩部装备按位轮流取（3 钩爪、4 捕获网），自动对战里测肩部装备。
	// 11-01 肩部 11 件，一位数字放不下：字母 a 表示 10（地雷），例如 -MechForceShoulder=789a 是紧急脱离、弹射升空、震荡波、地雷
	FString Force;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechForceShoulder="), Force) && Force.Len() > 0)
	{
		const TCHAR Ch = FChar::ToLower(Force[Slot % Force.Len()]);
		if (FChar::IsDigit(Ch))
		{
			MechLoadout::Set(Out, MechLoadout::Shoulder, Ch - TEXT('0'));
		}
		else if (Ch >= TEXT('a') && Ch <= TEXT('z'))
		{
			MechLoadout::Set(Out, MechLoadout::Shoulder, 10 + (Ch - TEXT('a')));
		}
	}
	// 测试：-MechForceMelee=0 让代选的近战武器按位轮流取（0 化境、1 快刀 ...），自动对战里测某把刀
	FString ForceMelee;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechForceMelee="), ForceMelee) && ForceMelee.Len() > 0)
	{
		const TCHAR Ch = ForceMelee[Slot % ForceMelee.Len()];
		if (FChar::IsDigit(Ch))
		{
			MechLoadout::Set(Out, MechLoadout::Melee, Ch - TEXT('0'));
		}
	}
	// 测试：-MechForceTactical=0123 是上一条的老写法（0 紧急脱离、1 弹射升空、2 震荡波、3 地雷），并入肩部后还认，老命令不用改
	FString ForceTac;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechForceTactical="), ForceTac) && ForceTac.Len() > 0)
	{
		const TCHAR Ch = ForceTac[Slot % ForceTac.Len()];
		if (FChar::IsDigit(Ch))
		{
			MechLoadout::Set(Out, MechLoadout::Shoulder, static_cast<int32>(EMechShoulder::Burst) + (Ch - TEXT('0')));
		}
	}
	// 测试：-MechForceFrame=258 让代选的框架按位轮流取（10-31 阵营机体：2 新纪重型、5 维克托重型、8 相位重型）
	if (const int32 Frame = ForcedFrame(Slot); Frame >= 0)
	{
		MechLoadout::Set(Out, MechLoadout::Frame, Frame);
	}
	return Out;
}

int32 MechDraft::ForcedFrame(int32 Index)
{
	static const FString Force = [] {
		FString F;
		FParse::Value(FCommandLine::Get(), TEXT("MechForceFrame="), F);
		return F;
	}();
	if (Force.Len() == 0)
	{
		return -1;
	}
	const TCHAR Ch = Force[FMath::Abs(Index) % Force.Len()];
	return FChar::IsDigit(Ch) ? FMath::Min(Ch - TEXT('0'), MechNumFrames - 1) : -1;
}

FMechDraftBan MechDraft::AutoBan(const FMechDraft& Draft, int32 Team, FRandomStream& Rand)
{
	TArray<FMechDraftBan> Options;
	for (const int32 c : {static_cast<int32>(MechLoadout::Shooter), static_cast<int32>(MechLoadout::Melee), static_cast<int32>(MechLoadout::Shoulder)})
	{
		for (int32 i = 0; i < MechLoadout::NumItems(c); ++i)
		{
			if (MechLoadout::Item(c, i).bReady && !HasBanned(Draft, Team, c, i))
			{
				FMechDraftBan B;
				B.Team = Team;
				B.Category = static_cast<uint8>(c);
				B.Index = static_cast<uint8>(i);
				Options.Add(B);
			}
		}
	}
	return Options.Num() ? Options[Rand.RandRange(0, Options.Num() - 1)] : FMechDraftBan();
}
