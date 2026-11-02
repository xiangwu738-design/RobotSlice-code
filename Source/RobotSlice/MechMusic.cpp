#include "MechMusic.h"

#include "CoreGlobals.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "MechCharacter.h"
#include "MechDefense.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"
#include "MechMatchIntro.h"
#include "MechResultsStage.h"
#include "MechSettings.h"

namespace MechMusicLocal
{
	int32 Enabled = 1;
	int32 Debug = 0;
	FAutoConsoleVariableRef CVarEnabled(TEXT("mech.Music"), Enabled, TEXT("背景音乐（0 关）"));
	FAutoConsoleVariableRef CVarDebug(TEXT("mech.Music.Debug"), Debug, TEXT("背景音乐：每秒打一行现在在放什么"));

	/** 每个位置：资源名（/Game/Audio/Music/S_mus_*，tools/ue_import_music.py 导入）、音量倍数。原曲已统一到 -16 LUFS */
	struct FSlot
	{
		const TCHAR* Asset;
		float Gain;
		const TCHAR* Label;
	};
	const FSlot& Slot(EMechMusic M)
	{
		static const FSlot Table[(int32)EMechMusic::Count] = {
			{nullptr, 0.f, TEXT("无")},
			{TEXT("S_mus_menu"), 0.8f, TEXT("主菜单 Color Your Night")},
			{TEXT("S_mus_room"), 0.8f, TEXT("房间 巌戸台分寮")},
			{TEXT("S_mus_draft"), 0.7f, TEXT("选配 tartarus_0d04")},
			{TEXT("S_mus_intro"), 0.9f, TEXT("开场 Full Moon Full Life")},
			// 回合里压低：团队淘汰 / 夺芯要听锁定、导弹来袭、脚步
			{TEXT("S_mus_round"), 0.42f, TEXT("回合 避けられぬ戦い")},
			{TEXT("S_mus_clutch"), 0.6f, TEXT("赛点 It's Going Down Now")},
			{TEXT("S_mus_battle"), 0.55f, TEXT("战斗 Mass Destruction")},
			{TEXT("S_mus_defprep"), 0.6f, TEXT("保卫战准备 tartarus_0d05")},
			{TEXT("S_mus_boss1"), 0.7f, TEXT("首领 Master of Shadow")},
			{TEXT("S_mus_boss2"), 0.75f, TEXT("最终首领 Burn My Dread")},
			{TEXT("S_mus_results"), 0.8f, TEXT("结算 戦いのあと")},
			{TEXT("S_mus_defwin"), 0.85f, TEXT("保卫战胜利 キミの記憶")},
			{TEXT("S_mus_range"), 0.45f, TEXT("测试场 Basement")},
		};
		return Table[FMath::Clamp((int32)M, 0, (int32)EMechMusic::Count - 1)];
	}

	// ---------------------------------------------------------------- 音乐包（11-02）
	// 用户：「最好使用音乐包机制，以后打包了也能改」。游戏目录里放一个 MusicPack 文件夹，里面的 .wav 按文件名对号入座，
	// 优先于打进包里的曲子，打包后直接换文件就行（下一次换曲 / 下一张图生效，不用重新打包）：
	//   <位置>_<地图名去掉 L_>.wav（这张图专用，如 battle_OldStreet.wav）> <位置>.wav（所有图）> 包里的 S_mus_<位置>_<地图> > 包里的 S_mus_<位置>。
	//   位置 = menu room draft intro round clutch battle defprep boss1 boss2 results defwin range。
	// 找的地方：工程目录 /MusicPack（编辑器 D:/RobotSlice/MusicPack，打包版 <解压目录>/RobotSlice/MusicPack），再找根目录 /MusicPack（打包版 = exe 旁边）。
	// 只认 WAV（PCM 8/16/24/32 位或 32 位浮点，任意采样率，单声道 / 立体声；多声道只取前两个）。运行时自己解码成 16 位，
	// 用 USoundWaveProcedural 一次排进去放。音量照样乘这个位置的倍数，所以新曲最好也统一到 -16 LUFS（tools/music_pack.py 可以转）。
	struct FPackPcm
	{
		TArray<uint8> Pcm;  // 16 位交错
		int32 Rate = 0;
		int32 Channels = 0;
		FDateTime Stamp;
		int64 Size = 0;
	};

	TArray<FString> PackDirs()
	{
		TArray<FString> Out;
		Out.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("MusicPack")));
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::RootDir() / TEXT("MusicPack"));
		if (!Out.Contains(Root))
		{
			Out.Add(Root);
		}
		return Out;
	}

	bool DecodeWav(const TArray<uint8>& Bytes, FPackPcm& Out, FString& Why)
	{
		const int64 N = Bytes.Num();
		const uint8* D = Bytes.GetData();
		auto U16 = [&](int64 At) { return (uint32)D[At] | ((uint32)D[At + 1] << 8); };
		auto U32 = [&](int64 At) { return U16(At) | (U16(At + 2) << 16); };
		if (N < 12 || FMemory::Memcmp(D, "RIFF", 4) != 0 || FMemory::Memcmp(D + 8, "WAVE", 4) != 0)
		{
			Why = TEXT("不是 RIFF/WAVE（只认 .wav）");
			return false;
		}
		int32 Format = 0, Ch = 0, Rate = 0, Bits = 0;
		int64 DataAt = -1, DataLen = 0;
		for (int64 At = 12; At + 8 <= N;)
		{
			const uint32 Len = U32(At + 4);
			const int64 Body = At + 8;
			if (FMemory::Memcmp(D + At, "fmt ", 4) == 0 && Len >= 16 && Body + 16 <= N)
			{
				Format = U16(Body);
				Ch = U16(Body + 2);
				Rate = U32(Body + 4);
				Bits = U16(Body + 14);
				if (Format == 0xFFFE && Len >= 26 && Body + 26 <= N)
				{
					Format = U16(Body + 24);  // WAVE_FORMAT_EXTENSIBLE：子格式 GUID 的头两个字节
				}
			}
			else if (FMemory::Memcmp(D + At, "data", 4) == 0)
			{
				DataAt = Body;
				DataLen = FMath::Min<int64>(Len, N - Body);  // 有的软件写的长度不对，按文件实际长度截
				break;
			}
			At = Body + Len + (Len & 1);
		}
		if (DataAt < 0 || Ch <= 0 || Rate <= 0)
		{
			Why = TEXT("找不到 fmt / data 块");
			return false;
		}
		const bool bFloat = Format == 3;
		if (!((Format == 1 && (Bits == 8 || Bits == 16 || Bits == 24 || Bits == 32)) || (bFloat && Bits == 32)))
		{
			Why = FString::Printf(TEXT("不支持的格式（格式 %d，%d 位）：请存成 16 / 24 位 PCM WAV"), Format, Bits);
			return false;
		}
		const int32 Bps = Bits / 8;
		const int32 OutCh = FMath::Min(Ch, 2);
		const int64 Frames = DataLen / (Bps * Ch);
		Out.Rate = Rate;
		Out.Channels = OutCh;
		Out.Pcm.SetNumUninitialized(Frames * OutCh * 2);
		int16* W = reinterpret_cast<int16*>(Out.Pcm.GetData());
		const uint8* P = D + DataAt;
		for (int64 F = 0; F < Frames; ++F)
		{
			for (int32 C = 0; C < OutCh; ++C)
			{
				const uint8* S = P + (F * Ch + C) * Bps;
				int32 V = 0;
				if (bFloat)
				{
					float X;
					FMemory::Memcpy(&X, S, 4);
					V = FMath::RoundToInt(FMath::Clamp(X, -1.f, 1.f) * 32767.f);
				}
				else if (Bps == 1)
				{
					V = ((int32)S[0] - 128) << 8;
				}
				else if (Bps == 2)
				{
					V = (int16)(S[0] | (S[1] << 8));
				}
				else if (Bps == 3)
				{
					V = (int16)(S[1] | (S[2] << 8));
				}
				else
				{
					V = (int16)(S[2] | (S[3] << 8));
				}
				*W++ = (int16)FMath::Clamp(V, -32768, 32767);
			}
		}
		return Frames > 0;
	}

	/** 音乐包里这个名字的 wav（解码过的留着，文件改了重新读；最多留 4 首） */
	TSharedPtr<FPackPcm> FindPack(const FString& Name, FString& OutPath)
	{
		static TMap<FString, TSharedPtr<FPackPcm>> Cache;
		static TArray<FString> Order;
		IFileManager& FM = IFileManager::Get();
		for (const FString& Dir : PackDirs())
		{
			const FString Path = Dir / (Name + TEXT(".wav"));
			const FFileStatData St = FM.GetStatData(*Path);
			if (!St.bIsValid || St.bIsDirectory)
			{
				continue;
			}
			OutPath = Path;
			if (const TSharedPtr<FPackPcm>* Hit = Cache.Find(Path); Hit && (*Hit)->Stamp == St.ModificationTime && (*Hit)->Size == St.FileSize)
			{
				return *Hit;
			}
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *Path))
			{
				UE_LOG(LogTemp, Warning, TEXT("[music] 音乐包：读不了 %s"), *Path);
				return nullptr;
			}
			TSharedPtr<FPackPcm> Pcm = MakeShared<FPackPcm>();
			FString Why;
			if (!DecodeWav(Bytes, *Pcm, Why))
			{
				UE_LOG(LogTemp, Warning, TEXT("[music] 音乐包：%s 用不了：%s"), *Path, *Why);
				return nullptr;
			}
			Pcm->Stamp = St.ModificationTime;
			Pcm->Size = St.FileSize;
			Cache.Add(Path, Pcm);
			Order.Remove(Path);
			Order.Add(Path);
			while (Order.Num() > 4)
			{
				Cache.Remove(Order[0]);
				Order.RemoveAt(0);
			}
			return Pcm;
		}
		return nullptr;
	}

	USoundBase* FromPack(const FString& Name, UObject* Outer)
	{
		FString Path;
		const TSharedPtr<FPackPcm> Pcm = FindPack(Name, Path);
		if (!Pcm.IsValid())
		{
			return nullptr;
		}
		USoundWaveProcedural* W = NewObject<USoundWaveProcedural>(Outer);
		W->SetSampleRate(Pcm->Rate);
		W->NumChannels = Pcm->Channels;
		W->Duration = (float)(Pcm->Pcm.Num() / (2 * Pcm->Channels)) / Pcm->Rate;
		W->SoundGroup = SOUNDGROUP_Music;
		W->bLooping = false;
		W->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;  // 淡入从 0.001 起，别被当成静音停掉（程序化的声音停了不能从原处接着放）
		W->QueueAudio(Pcm->Pcm.GetData(), Pcm->Pcm.Num());
		UE_LOG(LogTemp, Display, TEXT("[music] 音乐包：%s（%d:%02d，%d Hz，%s）"), *Path, (int32)W->Duration / 60, (int32)W->Duration % 60, Pcm->Rate,
			Pcm->Channels == 2 ? TEXT("立体声") : TEXT("单声道"));
		return W;
	}

	USoundBase* Load(EMechMusic M, const UWorld* World, UObject* Outer)
	{
		const FSlot& S = Slot(M);
		if (!S.Asset)
		{
			return nullptr;
		}
		FString MapName;
		if (World)
		{
			MapName = World->GetMapName();
			MapName.RemoveFromStart(World->StreamingLevelsPrefix);
			MapName.RemoveFromStart(TEXT("L_"));
		}
		FString Short = S.Asset;
		Short.RemoveFromStart(TEXT("S_mus_"));
		// 1、2：音乐包（这张图专用 → 通用）
		if (!MapName.IsEmpty())
		{
			if (USoundBase* P = FromPack(Short + TEXT("_") + MapName, Outer))
			{
				return P;
			}
		}
		if (USoundBase* P = FromPack(Short, Outer))
		{
			return P;
		}
		// 3：包里这张图专用的 S_mus_<位置>_<地图>（如 S_mus_battle_OldStreet）
		if (!MapName.IsEmpty())
		{
			const FString Own = FString::Printf(TEXT("%s_%s"), S.Asset, *MapName);
			if (USoundBase* Mine = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/Music/%s.%s"), *Own, *Own), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				UE_LOG(LogTemp, Display, TEXT("[music] 用这张图专用的 %s"), *Own);
				return Mine;
			}
		}
		// 4：包里通用的
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/Music/%s.%s"), S.Asset, S.Asset), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Sound)
		{
			UE_LOG(LogTemp, Warning, TEXT("[music] 没有 /Game/Audio/Music/%s（tools/ue_import_music.py 导入），音乐包里也没有 %s.wav"), S.Asset, *Short);
		}
		return Sound;
	}

	/** 曲子快放完时提前几秒从头交叉淡入 */
	constexpr float LoopOverlap = 4.f;
}

bool UMechMusic::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && !FParse::Param(FCommandLine::Get(), TEXT("MechNoMusic")) && Super::ShouldCreateSubsystem(Outer);
}

bool UMechMusic::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMechMusic::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMechMusic, STATGROUP_Tickables);
}

void UMechMusic::Deinitialize()
{
	MuteStageMusic(false);
	for (UAudioComponent* C : Comps)
	{
		if (IsValid(C))
		{
			C->Stop();
			C->DestroyComponent();
		}
	}
	Comps.Reset();
	Voices.Reset();
	Super::Deinitialize();
}

EMechMusic UMechMusic::Pick()
{
	UWorld* World = GetWorld();
	const AMechGameState* GS = World ? World->GetGameState<AMechGameState>() : nullptr;
	if (!World || !GS)
	{
		return Current;  // 刚换图、GameState 还没复制过来：先别动
	}
	// 保卫战：守满放胜利曲（排在结算前面），输了就安静下来，交给结算
	const AMechDefenseDirector* Defense = nullptr;
	for (TActorIterator<AMechDefenseDirector> It(World); It; ++It)
	{
		Defense = *It;
		break;
	}
	if (Defense && Defense->Phase == EMechDefensePhase::Won)
	{
		return EMechMusic::DefWin;
	}
	if (AMechResultsStage::IsActive(World) || GS->MatchEndTime >= 0.f)
	{
		return EMechMusic::Results;
	}
	if (AMechMatchIntro::IsPlayingLocally(World))
	{
		return EMechMusic::Intro;
	}
	if (World->URL.HasOption(TEXT("Viewer")))
	{
		return EMechMusic::Range;
	}
	if (GS->bMenuMode)
	{
		return EMechMusic::Menu;
	}
	if (GS->bRoomMode || GS->Phase == EMechRoundPhase::Lobby)
	{
		return EMechMusic::Room;
	}
	if (GS->Phase == EMechRoundPhase::Draft || (GS->bRoomMatch && GS->Phase == EMechRoundPhase::Waiting))
	{
		return EMechMusic::Draft;
	}
	if (Defense)
	{
		switch (Defense->Phase)
		{
		case EMechDefensePhase::Prep:
			return EMechMusic::DefPrep;
		case EMechDefensePhase::Fight:
			// 第 5 波中途首领、最后一波最终首领（设计稿 docs/保卫战_设计.md）
			return Defense->Wave >= Defense->NumWaves ? EMechMusic::Boss2 : Defense->Wave == 5 ? EMechMusic::Boss1 : EMechMusic::Battle;
		default:
			return EMechMusic::None;
		}
	}
	if (GS->bRangeMode)
	{
		return EMechMusic::Range;
	}
	if (GS->IsDeathmatch())
	{
		return GS->Phase == EMechRoundPhase::Waiting ? Current : EMechMusic::Battle;
	}
	// 团队淘汰 / 夺芯
	if (GS->Phase == EMechRoundPhase::Waiting)
	{
		return Current;
	}
	if (GS->Round != ClutchRound)
	{
		bClutchHeld = false;
		ClutchRound = GS->Round;
	}
	if (GS->Phase == EMechRoundPhase::Fight && !bClutchHeld)
	{
		bool bClutch = false;
		if (GS->bCoreMode)
		{
			bClutch = GS->bOvertime || GS->TeamScore[0] >= GS->CoreTarget - 1 || GS->TeamScore[1] >= GS->CoreTarget - 1;
		}
		else
		{
			// 赛点回合，或者有一队只剩最后一台
			bClutch = GS->RoundsToWin > 1 && (GS->TeamScore[0] >= GS->RoundsToWin - 1 || GS->TeamScore[1] >= GS->RoundsToWin - 1);
			int32 Alive[2] = {0, 0};
			int32 Total[2] = {0, 0};
			for (TActorIterator<AMechCharacter> It(World); It; ++It)
			{
				const int32 T = It->GetTeam();
				if (T == 0 || T == 1)
				{
					++Total[T];
					Alive[T] += It->GetHealth() && !It->GetHealth()->IsDead() ? 1 : 0;
				}
			}
			bClutch |= (Total[0] > 1 && Alive[0] == 1) || (Total[1] > 1 && Alive[1] == 1);
		}
		bClutchHeld = bClutch;
	}
	if (bClutchHeld)
	{
		return EMechMusic::Clutch;
	}
	// 回合之间（结束、倒计时）接着放回合曲
	return EMechMusic::Round;
}

void UMechMusic::StartVoice(EMechMusic SlotId, float FadeIn)
{
	USoundBase* Sound = MechMusicLocal::Load(SlotId, GetWorld(), this);
	UWorld* World = GetWorld();
	if (!Sound || !World)
	{
		return;
	}
	UAudioComponent* C = UGameplayStatics::CreateSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, false, false);
	if (!C)
	{
		return;
	}
	C->bIsUISound = true;  // 单机暂停时也接着放
	C->bIsMusic = true;
	C->SetVolumeMultiplier(0.001f);
	C->Play();
	Comps.Add(C);
	FVoice& V = Voices.AddDefaulted_GetRef();
	V.Comp = C;
	V.Slot = SlotId;
	V.Duration = Sound->GetDuration();
	V.Fade = 0.f;
	V.FadeTarget = 1.f;
	V.FadeRate = 1.f / FMath::Max(0.05f, FadeIn);
}

void UMechMusic::Switch(EMechMusic To, float FadeOut, float FadeIn)
{
	UE_LOG(LogTemp, Display, TEXT("[music] %s → %s"), MechMusicLocal::Slot(Current).Label, MechMusicLocal::Slot(To).Label);
	for (FVoice& V : Voices)
	{
		V.FadeTarget = 0.f;
		V.FadeRate = 1.f / FMath::Max(0.05f, FadeOut);
	}
	Current = To;
	MuteStageMusic(To == EMechMusic::Results);
	if (To != EMechMusic::None)
	{
		StartVoice(To, FadeIn);
	}
}

void UMechMusic::MuteStageMusic(bool bMute)
{
	if (bMute == bStageMuted)
	{
		return;
	}
	if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("mech.Results.Music")))
	{
		if (bMute)
		{
			StageMusicBefore = Var->GetFloat();
			Var->Set(0.f, ECVF_SetByCode);
		}
		else
		{
			Var->Set(StageMusicBefore, ECVF_SetByCode);
		}
	}
	bStageMuted = bMute;
}

void UMechMusic::Tick(float DeltaTime)
{
	// 按真实时间走（慢动作、暂停都不影响音乐的淡入淡出和换遍）
	const float Dt = FMath::Min((float)FApp::GetDeltaTime(), 0.25f);
	const EMechMusic Want = MechMusicLocal::Enabled ? Pick() : EMechMusic::None;
	if (Want != Current)
	{
		// 开场运镜结束进倒计时、结算这些切得慢一点；进赛点要快
		const float Out = Want == EMechMusic::Clutch || Want == EMechMusic::Boss1 || Want == EMechMusic::Boss2 ? 0.8f : 1.8f;
		const float In = Want == EMechMusic::Clutch ? 0.4f : 1.2f;
		Switch(Want, Out, In);
	}
	const float UserVolume = FMath::Clamp(FMechSettings::Get().MusicVolume, 0.f, 1.f);
	float Lift = 1.f;
	if (Current == EMechMusic::Battle)
	{
		// 死斗最后 60 秒提一档
		const AMechGameState* GS = GetWorld() ? GetWorld()->GetGameState<AMechGameState>() : nullptr;
		if (GS && GS->IsDeathmatch() && GS->Phase == EMechRoundPhase::Fight)
		{
			const float Left = GS->GetPhaseRemaining();
			Lift = Left > 0.f && Left < 60.f ? 1.3f : 1.f;
		}
	}
	bool bLoopStarted = false;
	for (int32 i = Voices.Num() - 1; i >= 0; --i)
	{
		FVoice& V = Voices[i];
		UAudioComponent* C = V.Comp.Get();
		V.Age += Dt;
		V.Fade = FMath::FInterpConstantTo(V.Fade, V.FadeTarget, Dt, V.FadeRate);
		const bool bDone = !IsValid(C) || (V.FadeTarget <= 0.f && V.Fade <= 0.f) || (V.Duration > 0.f && V.Age > V.Duration + 0.5f);
		if (bDone)
		{
			if (IsValid(C))
			{
				C->Stop();
				C->DestroyComponent();
				Comps.Remove(C);
			}
			Voices.RemoveAt(i);
			continue;
		}
		// 现在这首快放完了：从头交叉淡入一遍（一次只起一个）
		if (V.Slot == Current && V.FadeTarget > 0.f && V.Duration > MechMusicLocal::LoopOverlap * 3.f && V.Age > V.Duration - MechMusicLocal::LoopOverlap && !bLoopStarted)
		{
			V.FadeTarget = 0.f;
			V.FadeRate = 1.f / MechMusicLocal::LoopOverlap;
			bLoopStarted = true;
		}
		const float Gain = MechMusicLocal::Slot(V.Slot).Gain * (V.Slot == Current ? Lift : 1.f);
		C->SetVolumeMultiplier(FMath::Max(0.001f, UserVolume * Gain * V.Fade));
	}
	if (bLoopStarted)
	{
		StartVoice(Current, MechMusicLocal::LoopOverlap * 0.75f);
	}
	if (MechMusicLocal::Debug)
	{
		static float Acc = 0.f;
		Acc += Dt;
		if (Acc > 1.f)
		{
			Acc = 0.f;
			UE_LOG(LogTemp, Display, TEXT("[music] 在放 %s（%d 个声部）"), MechMusicLocal::Slot(Current).Label, Voices.Num());
		}
	}
}
