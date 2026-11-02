#include "MechSettings.h"

#include "MechLoadout.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* Section = TEXT("MechSettings");

	/** 键位表：ini 里的名字（Key_后缀）、设置页上的名字、默认键。顺序 = EMechKey */
	struct FKeyRow
	{
		const TCHAR* Id;
		const TCHAR* Label;
		FKey Default;
	};
	const FKeyRow& KeyRow(EMechKey Action)
	{
		// 闪避默认 F（10-30 玩家反馈：左 Ctrl 离 WASD 远、按着方向再按很别扭）
		static const FKeyRow Rows[MechKeys::Num] = {
			{TEXT("MoveForward"), TEXT("前进"), EKeys::W},
			{TEXT("MoveBack"), TEXT("后退"), EKeys::S},
			{TEXT("MoveLeft"), TEXT("左移"), EKeys::A},
			{TEXT("MoveRight"), TEXT("右移"), EKeys::D},
			{TEXT("Jump"), TEXT("跳跃 / 喷射 / 蹬墙"), EKeys::SpaceBar},
			{TEXT("Boost"), TEXT("冲刺 / 滑翔"), EKeys::LeftShift},
			{TEXT("QuickBoost"), TEXT("快速闪避"), EKeys::F},
			{TEXT("Stance"), TEXT("切换射击 / 近战"), EKeys::Q},
			{TEXT("Fire"), TEXT("开火 / 攻击"), EKeys::LeftMouseButton},
			{TEXT("Alt"), TEXT("瞄准 / 格挡"), EKeys::RightMouseButton},
			{TEXT("Shoulder"), TEXT("肩部装备"), EKeys::E},
			{TEXT("Reload"), TEXT("换弹"), EKeys::R},
			{TEXT("FirstPerson"), TEXT("第一人称"), EKeys::V},
			{TEXT("Map"), TEXT("大地图（按住）"), EKeys::M},
			{TEXT("Scoreboard"), TEXT("计分板（按住）"), EKeys::Tab},
			{TEXT("ThrowCore"), TEXT("扔核心（夺芯）"), EKeys::G},
			// 标点默认鼠标中键（11-01，仿 Apex）
			{TEXT("Ping"), TEXT("标点"), EKeys::MiddleMouseButton},
		};
		return Rows[FMath::Clamp(static_cast<int32>(Action), 0, MechKeys::Num - 1)];
	}

	/** 键位有重复（ini 手改坏了）就整套恢复默认：一个键给两个操作时增强输入只会触发其中一个，玩家会以为按键失灵 */
	void ValidateKeys(FKey (&Keys)[MechKeys::Num])
	{
		for (int32 i = 0; i < MechKeys::Num; ++i)
		{
			for (int32 j = i + 1; j < MechKeys::Num; ++j)
			{
				if (Keys[i] == Keys[j])
				{
					UE_LOG(LogTemp, Warning, TEXT("[keys] 设置里 %s 和 %s 都是 %s，键位整套恢复默认"), KeyRow((EMechKey)i).Id, KeyRow((EMechKey)j).Id,
						*Keys[i].ToString());
					for (int32 k = 0; k < MechKeys::Num; ++k)
					{
						Keys[k] = KeyRow((EMechKey)k).Default;
					}
					return;
				}
			}
		}
	}
}

FMechSettings& FMechSettings::Get()
{
	static FMechSettings Settings = []
	{
		FMechSettings S;
		S.Load();
		return S;
	}();
	return Settings;
}

void FMechSettings::Load()
{
	bool bSaved[MechKeys::Num] = {};
	for (int32 i = 0; i < MechKeys::Num; ++i)
	{
		Keys[i] = KeyRow((EMechKey)i).Default;
		FString Name;
		if (GConfig->GetString(Section, *(FString(TEXT("Key_")) + KeyRow((EMechKey)i).Id), Name, GGameUserSettingsIni))
		{
			const FKey K(*Name);
			if (MechKeys::CanBind(K))
			{
				Keys[i] = K;
				bSaved[i] = true;
			}
		}
	}
	// 后来新加的操作（ini 里还没有这一项，比如 11-01 的「标点」）：默认键要是已经被玩家改给了别的操作，
	// 不能让下面的查重把整套键位恢复默认（玩家改过的键全丢），给新操作挑一个还空着的键
	for (int32 i = 0; i < MechKeys::Num; ++i)
	{
		if (bSaved[i])
		{
			continue;
		}
		auto Taken = [this, i](const FKey& K) {
			for (int32 j = 0; j < MechKeys::Num; ++j)
			{
				if (j != i && Keys[j] == K)
				{
					return true;
				}
			}
			return false;
		};
		if (!Taken(Keys[i]))
		{
			continue;
		}
		const FKey Spare[] = {EKeys::C, EKeys::X, EKeys::Z, EKeys::B, EKeys::T, EKeys::H, EKeys::K, EKeys::L, EKeys::N, EKeys::Y, EKeys::U, EKeys::I, EKeys::O, EKeys::P};
		for (const FKey& K : Spare)
		{
			if (!Taken(K))
			{
				UE_LOG(LogTemp, Display, TEXT("[keys] 新操作 %s 的默认键 %s 已被占用，改用 %s"), KeyRow((EMechKey)i).Id, *Keys[i].ToString(), *K.ToString());
				Keys[i] = K;
				break;
			}
		}
	}
	ValidateKeys(Keys);
	const bool bHasSens = GConfig->GetFloat(Section, TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("ZoomSensitivityRatio"), ZoomSensitivityRatio, GGameUserSettingsIni);
	{
		// 11-02 旧设置迁移：没有 SensVersion = 旧的 0.1-3 倍率（每计数 = 倍率 x 引擎 InputYawScale 度）→ 换成 CS2 灵敏度，手感不变
		int32 SensVersion = 0;
		GConfig->GetInt(Section, TEXT("SensVersion"), SensVersion, GGameUserSettingsIni);
		if (SensVersion < 2 && bHasSens)
		{
			float OldYawScale = 1.f;
			if (GetDefault<UInputSettings>()->bEnableLegacyInputScales)
			{
				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				OldYawScale = FMath::Abs(GetDefault<APlayerController>()->GetDeprecatedInputYawScale());
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
			const float Old = MouseSensitivity;
			MouseSensitivity = Old * OldYawScale * LookEngineExtra / DegreesPerCount;
			UE_LOG(LogTemp, Display, TEXT("[sens] 旧灵敏度 %.3f 倍率（引擎 yaw 缩放 %.2f）→ CS2 灵敏度 %.3f"), Old, OldYawScale, MouseSensitivity);
			GConfig->SetInt(Section, TEXT("SensVersion"), 2, GGameUserSettingsIni);
			ZoomSensitivityRatio = 0.3f;
		}
	}
	GConfig->GetBool(Section, TEXT("InvertY"), bInvertY, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("FieldOfView"), FieldOfView, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("MasterVolume"), MasterVolume, GGameUserSettingsIni);
	GConfig->GetFloat(Section, TEXT("AlertVolume"), AlertVolume, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("AlertVoice"), AlertVoice, GGameUserSettingsIni);
	if (!GConfig->GetBool(Section, TEXT("AlertVoiceSet"), bAlertVoiceSet, GGameUserSettingsIni))
	{
		bAlertVoiceSet = AlertVoice != 1;  // 10-31 以前的存档：改过（不是默认的冷静女声）就当是手动选的
	}
	GConfig->GetFloat(Section, TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("MenuPalette"), MenuPalette, GGameUserSettingsIni);
	GConfig->GetInt(Section, TEXT("HudTheme"), HudTheme, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("FirstPerson"), bFirstPerson, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("MotionBlur"), bMotionBlur, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("ShowFeed"), bShowFeed, GGameUserSettingsIni);
	GConfig->GetBool(Section, TEXT("DraftBans"), bDraftBans, GGameUserSettingsIni);
	// 对战规则（房主上次在房间里的选择）
	{
		FString ModeText;
		if (GConfig->GetString(Section, TEXT("MatchMode"), ModeText, GGameUserSettingsIni))
		{
			Rules.Mode = ModeText.Equals(TEXT("DM"), ESearchCase::IgnoreCase) ? EMechMode::Deathmatch : EMechMode::Elimination;
		}
		GConfig->GetInt(Section, TEXT("ElimRounds"), Rules.RoundsToWin, GGameUserSettingsIni);
		GConfig->GetBool(Section, TEXT("DMSolo"), Rules.bSolo, GGameUserSettingsIni);
		GConfig->GetInt(Section, TEXT("DMTeamTarget"), Rules.TeamTarget, GGameUserSettingsIni);
		GConfig->GetInt(Section, TEXT("DMSoloTarget"), Rules.SoloTarget, GGameUserSettingsIni);
		GConfig->GetInt(Section, TEXT("DMTime"), Rules.TimeLimit, GGameUserSettingsIni);
		MechRules::Sanitize(Rules);
	}
	FString L;
	if (GConfig->GetString(Section, TEXT("Loadout"), L, GGameUserSettingsIni))
	{
		TArray<FString> Parts;
		L.ParseIntoArray(Parts, TEXT(","));
		// 存的是五类（框架、推进器、射击、近战、肩部）。10-31 到 11-01 的存档多一个数（独立的战术装备，已并入肩部）：多的不读，肩部还是原来选的
		if (Parts.Num() >= 5)
		{
			for (int32 c = 0; c < FMath::Min(Parts.Num(), static_cast<int32>(MechLoadout::NumCategories)); ++c)
			{
				MechLoadout::Set(Loadout, c, FCString::Atoi(*Parts[c]));  // 越界的编号会夹到范围里
			}
		}
	}
	GConfig->GetString(Section, TEXT("PlayerName"), PlayerName, GGameUserSettingsIni);
	FString PaintText;
	if (GConfig->GetString(Section, TEXT("Paint"), PaintText, GGameUserSettingsIni))
	{
		TArray<FString> Parts;
		PaintText.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Paint.Primary = FColor::FromHex(Parts[0]);
			Paint.Secondary = FColor::FromHex(Parts[1]);
			Paint.Accent = FColor::FromHex(Parts[2]);
		}
	}
	MouseSensitivity = FMath::Clamp(MouseSensitivity, 0.01f, 100.f);
	ZoomSensitivityRatio = FMath::Clamp(ZoomSensitivityRatio, 0.05f, 3.f);
	FieldOfView = FMath::Clamp(FieldOfView, 70.f, 110.f);
	MasterVolume = FMath::Clamp(MasterVolume, 0.f, 1.f);
	AlertVolume = FMath::Clamp(AlertVolume, 0.f, 1.f);
	AlertVoice = FMath::Clamp(AlertVoice, 0, 3);
	MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
	MenuPalette = FMath::Clamp(MenuPalette, 0, 4);  // MechUITheme::NumPresets - 1（这里不引那个头文件）
	HudTheme = FMath::Clamp(HudTheme, -1, 3);
}

void FMechSettings::Save() const
{
	GConfig->SetFloat(Section, TEXT("MouseSensitivity"), MouseSensitivity, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("ZoomSensitivityRatio"), ZoomSensitivityRatio, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("SensVersion"), 2, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("InvertY"), bInvertY, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("FieldOfView"), FieldOfView, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("MasterVolume"), MasterVolume, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("AlertVolume"), AlertVolume, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("AlertVoice"), AlertVoice, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("AlertVoiceSet"), bAlertVoiceSet, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("MenuPalette"), MenuPalette, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("HudTheme"), HudTheme, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("FirstPerson"), bFirstPerson, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("MotionBlur"), bMotionBlur, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("ShowFeed"), bShowFeed, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("DraftBans"), bDraftBans, GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("MatchMode"), Rules.IsDeathmatch() ? TEXT("DM") : TEXT("Elim"), GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("ElimRounds"), Rules.RoundsToWin, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("DMSolo"), Rules.bSolo, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("DMTeamTarget"), Rules.TeamTarget, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("DMSoloTarget"), Rules.SoloTarget, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("DMTime"), Rules.TimeLimit, GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("Loadout"), *FString::Printf(TEXT("%d,%d,%d,%d,%d"), (int32)Loadout.Frame, (int32)Loadout.Booster,
		(int32)Loadout.Shooter, (int32)Loadout.Melee, (int32)Loadout.Shoulder), GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("PlayerName"), *PlayerName, GGameUserSettingsIni);
	GConfig->SetString(Section, TEXT("Paint"), *FString::Printf(TEXT("%s,%s,%s"), *Paint.Primary.ToHex(), *Paint.Secondary.ToHex(), *Paint.Accent.ToHex()),
		GGameUserSettingsIni);
	SaveKeys();
}

void FMechSettings::SaveKeys() const
{
	for (int32 i = 0; i < MechKeys::Num; ++i)
	{
		GConfig->SetString(Section, *(FString(TEXT("Key_")) + KeyRow((EMechKey)i).Id), *Keys[i].ToString(), GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

// ---------------------------------------------------------------- 键位
FKey MechKeys::Default(EMechKey Action)
{
	return KeyRow(Action).Default;
}

FKey MechKeys::Key(EMechKey Action)
{
	return FMechSettings::Get().Keys[FMath::Clamp(static_cast<int32>(Action), 0, Num - 1)];
}

FText MechKeys::Label(EMechKey Action)
{
	return FText::FromString(KeyRow(Action).Label);
}

FText MechKeys::KeyName(const FKey& Key)
{
	// 引擎自带的键名是英文（Left Mouse Button、Space Bar），常用的换成中文；字母、数字、F 键照原样
	static const TMap<FKey, const TCHAR*> Names = {
		{EKeys::LeftMouseButton, TEXT("鼠标左键")}, {EKeys::RightMouseButton, TEXT("鼠标右键")}, {EKeys::MiddleMouseButton, TEXT("鼠标中键")},
		{EKeys::ThumbMouseButton, TEXT("鼠标侧键 1")}, {EKeys::ThumbMouseButton2, TEXT("鼠标侧键 2")},
		{EKeys::SpaceBar, TEXT("空格")}, {EKeys::LeftShift, TEXT("左 Shift")}, {EKeys::RightShift, TEXT("右 Shift")},
		{EKeys::LeftControl, TEXT("左 Ctrl")}, {EKeys::RightControl, TEXT("右 Ctrl")}, {EKeys::LeftAlt, TEXT("左 Alt")}, {EKeys::RightAlt, TEXT("右 Alt")},
		{EKeys::Tab, TEXT("Tab")}, {EKeys::Enter, TEXT("回车")}, {EKeys::BackSpace, TEXT("退格")}, {EKeys::CapsLock, TEXT("大写锁定")},
		{EKeys::Up, TEXT("方向键 上")}, {EKeys::Down, TEXT("方向键 下")}, {EKeys::Left, TEXT("方向键 左")}, {EKeys::Right, TEXT("方向键 右")},
		{EKeys::Tilde, TEXT("`")},
	};
	if (const TCHAR* const* Found = Names.Find(Key))
	{
		return FText::FromString(*Found);
	}
	return Key.IsValid() ? Key.GetDisplayName(false) : FText::FromString(TEXT("（无）"));
}

bool MechKeys::CanBind(const FKey& Key)
{
	if (!Key.IsValid() || Key == EKeys::Escape || Key == EKeys::F1 || Key.IsGamepadKey() || Key.IsTouch() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D())
	{
		return false;
	}
	// 滚轮是一下一下的「按下又松开」，按住类（冲刺、喷射、瞄准）用不了，干脆不让绑
	if (Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown || Key == EKeys::MouseWheelAxis)
	{
		return false;
	}
	return Key.IsDigital();
}

EMechKey MechKeys::Rebind(EMechKey Action, const FKey& Key)
{
	FMechSettings& S = FMechSettings::Get();
	const int32 Me = static_cast<int32>(Action);
	EMechKey Swapped = EMechKey::Count;
	for (int32 i = 0; i < Num; ++i)
	{
		if (i != Me && S.Keys[i] == Key)
		{
			S.Keys[i] = S.Keys[Me];  // 冲突：把我原来的键给它，两个操作互换
			Swapped = static_cast<EMechKey>(i);
		}
	}
	S.Keys[Me] = Key;
	S.SaveKeys();
	return Swapped;
}

void MechKeys::ResetAll()
{
	FMechSettings& S = FMechSettings::Get();
	for (int32 i = 0; i < Num; ++i)
	{
		S.Keys[i] = Default(static_cast<EMechKey>(i));
	}
	S.SaveKeys();
}

bool MechKeys::IsDown(const APlayerController* PC, EMechKey Action)
{
	return PC && PC->IsInputKeyDown(Key(Action));
}

bool MechKeys::JustPressed(const APlayerController* PC, EMechKey Action)
{
	return PC && PC->WasInputKeyJustPressed(Key(Action));
}

void FMechSettings::ApplyAudio(const UObject* WorldContext) const
{
	if (UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		if (FAudioDeviceHandle Device = World->GetAudioDevice())
		{
			Device->SetTransientPrimaryVolume(MasterVolume);
		}
	}
}
