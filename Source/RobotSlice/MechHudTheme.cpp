#include "MechHudTheme.h"

#include "MechCatalog.h"
#include "MechCharacter.h"
#include "MechSettings.h"
#include "Misc/CommandLine.h"

namespace MechHudThemeData  // 不用匿名命名空间：合并编译时会和别的文件里同名的小函数撞
{
	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A)
	{
		FLinearColor C(FColor(R, G, B));
		C.A = A;
		return C;
	}

	/** 联赛标准：原来那套（数值一个没改，对比时就是改前的样子） */
	FMechHudTheme MakeStandard()
	{
		FMechHudTheme T;
		T.Id = EMechHudThemeId::Standard;
		T.Code = TEXT("STD");
		T.Ink = Srgb(241, 239, 232, 0.95f);
		T.Dim = Srgb(168, 166, 158, 0.85f);
		T.Faint = Srgb(168, 166, 158, 0.3f);
		T.Accent = Srgb(93, 202, 165, 0.95f);
		T.Track = Srgb(12, 92, 76, 0.6f);
		T.Ally = Srgb(133, 183, 235, 0.95f);
		T.Enemy = Srgb(226, 75, 74, 0.95f);
		T.Warn = Srgb(239, 159, 39, 0.95f);
		T.Heal = Srgb(151, 196, 89, 0.95f);
		T.Gold = Srgb(250, 199, 117, 1.f);
		T.Guard = Srgb(226, 232, 240, 0.9f);
		T.Plate = FLinearColor(0.f, 0.005f, 0.01f, 0.45f);
		T.Shade = FLinearColor(0.f, 0.005f, 0.01f, 1.f);
		T.DashUp = FLinearColor(0.f, 0.01f, 0.015f, 0.22f);
		T.DashDown = FLinearColor(0.f, 0.01f, 0.015f, 0.55f);
		T.Caution = Srgb(239, 159, 39, 1.f);
		T.Danger = Srgb(240, 64, 52, 1.f);
		T.LampText = FLinearColor(0.01f, 0.012f, 0.015f, 0.92f);
		T.Glow = FLinearColor(0.55f, 0.95f, 0.85f, 1.f);
		T.Hazard = Srgb(255, 196, 0, 1.f);
		return T;
	}

	/** 新纪建设：工程机械仪表。琥珀橙为主、提醒用警示黄，线粗、条粗、条分段，读数是七段数码管，仪表盘两角黄黑斜纹 */
	FMechHudTheme MakeNewEra()
	{
		FMechHudTheme T = MakeStandard();
		T.Id = EMechHudThemeId::NewEra;
		T.Code = TEXT("NEC");
		T.Ink = Srgb(246, 236, 216, 0.95f);
		T.Dim = Srgb(184, 164, 130, 0.85f);
		T.Faint = Srgb(184, 164, 130, 0.32f);
		T.Accent = Srgb(255, 156, 26, 0.95f);
		T.Track = Srgb(96, 54, 8, 0.62f);
		T.Ally = Srgb(120, 182, 242, 0.95f);
		T.Enemy = Srgb(234, 60, 42, 0.95f);
		T.Warn = Srgb(255, 218, 20, 0.95f);
		T.Heal = Srgb(170, 206, 80, 0.95f);
		T.Gold = Srgb(255, 210, 112, 1.f);
		T.Guard = Srgb(240, 232, 214, 0.9f);
		T.Plate = FLinearColor(0.012f, 0.007f, 0.f, 0.5f);
		T.Shade = FLinearColor(0.012f, 0.007f, 0.f, 1.f);
		T.DashUp = FLinearColor(0.014f, 0.008f, 0.f, 0.3f);
		T.DashDown = FLinearColor(0.014f, 0.008f, 0.f, 0.64f);
		T.Caution = Srgb(255, 204, 0, 1.f);
		T.Danger = Srgb(242, 56, 38, 1.f);
		T.LampText = FLinearColor(0.02f, 0.012f, 0.f, 0.94f);
		T.Glow = FLinearColor(1.f, 0.72f, 0.3f, 1.f);
		T.LineScale = 1.5f;
		T.BarScale = 1.75f;
		T.CalmAlpha = 0.62f;  // 工程仪表：平时也看得清
		T.LabelFace = EMechHudFace::SegoeBold;
		T.NumberFace = EMechHudFace::SegoeBold;
		T.bSegmentDigits = true;
		T.GhostAlpha = 0.09f;
		T.bSegmentBars = true;
		T.bHazard = true;
		T.DefaultVoice = 1;    // 冷静女声
		T.AlertPitch = 0.86f;  // 低一点、闷一点，像大机器的蜂鸣
		return T;
	}

	/** 维克托重工：军用 MFD。单色荧光绿、琥珀提醒，模板字体读数、等宽小字、补零，方位刻度带、刻度尺、方框仪表盘 */
	FMechHudTheme MakeVictor()
	{
		FMechHudTheme T = MakeStandard();
		T.Id = EMechHudThemeId::Victor;
		T.Code = TEXT("VHI");
		T.Ink = Srgb(196, 248, 192, 0.95f);
		T.Dim = Srgb(112, 176, 112, 0.85f);
		T.Faint = Srgb(112, 176, 112, 0.32f);
		T.Accent = Srgb(72, 236, 106, 0.95f);
		T.Track = Srgb(14, 84, 34, 0.62f);
		T.Ally = Srgb(112, 206, 246, 0.95f);
		T.Enemy = Srgb(246, 72, 58, 0.95f);
		T.Warn = Srgb(255, 178, 0, 0.95f);
		T.Heal = Srgb(170, 240, 110, 0.95f);
		T.Gold = Srgb(240, 222, 112, 1.f);
		T.Guard = Srgb(212, 244, 210, 0.9f);
		T.Plate = FLinearColor(0.f, 0.008f, 0.002f, 0.5f);
		T.Shade = FLinearColor(0.f, 0.008f, 0.002f, 1.f);
		T.DashUp = FLinearColor(0.f, 0.009f, 0.003f, 0.42f);
		T.DashDown = FLinearColor(0.f, 0.009f, 0.003f, 0.6f);
		T.Caution = Srgb(255, 178, 0, 1.f);
		T.Danger = Srgb(246, 60, 46, 1.f);
		T.LampText = FLinearColor(0.f, 0.015f, 0.004f, 0.94f);
		T.Glow = FLinearColor(0.45f, 1.f, 0.55f, 1.f);
		T.CalmAlpha = 0.55f;
		T.LabelFace = EMechHudFace::Mono;
		T.NumberFace = EMechHudFace::Stencil;
		T.bPadDigits = true;
		T.Dash = EMechDashStyle::Frame;
		T.bExtraLabels = true;
		T.bHeadingTape = true;
		T.bRulers = true;
		T.DefaultVoice = 2;    // 机械合成：军用简短报读
		T.AlertPitch = 1.12f;  // 尖一点、短一点
		return T;
	}

	/** 相位电子：最克制。白色细线、大量留白，不铺仪表盘底、不写单位和空槽，细体字，四角极简细线框 */
	FMechHudTheme MakePhase()
	{
		FMechHudTheme T = MakeStandard();
		T.Id = EMechHudThemeId::Phase;
		T.Code = TEXT("PHASE");
		T.Ink = Srgb(250, 251, 253, 0.96f);
		T.Dim = Srgb(196, 202, 210, 0.82f);
		T.Faint = Srgb(220, 226, 234, 0.24f);
		T.Accent = Srgb(244, 248, 255, 0.95f);
		T.Track = Srgb(255, 255, 255, 0.16f);
		T.Ally = Srgb(140, 196, 255, 0.95f);
		T.Enemy = Srgb(255, 84, 96, 0.95f);
		T.Warn = Srgb(255, 192, 96, 0.95f);
		T.Heal = Srgb(176, 232, 176, 0.95f);
		T.Gold = Srgb(255, 226, 160, 1.f);
		T.Guard = Srgb(236, 240, 248, 0.9f);
		T.Plate = FLinearColor(0.f, 0.f, 0.005f, 0.32f);
		T.Shade = FLinearColor(0.f, 0.f, 0.005f, 1.f);
		T.DashUp = FLinearColor(0.f, 0.f, 0.f, 0.f);
		T.DashDown = FLinearColor(0.f, 0.f, 0.f, 0.f);
		T.Caution = Srgb(255, 192, 96, 1.f);
		T.Danger = Srgb(255, 78, 92, 1.f);
		T.LampText = FLinearColor(0.01f, 0.01f, 0.015f, 0.92f);
		T.Glow = FLinearColor(1.f, 1.f, 1.f, 1.f);
		T.LineScale = 0.7f;
		T.BarScale = 0.6f;
		T.CalmAlpha = 0.45f;
		T.BarShadow = 0.35f;
		T.LabelFace = EMechHudFace::SegoeSemilight;
		T.NumberFace = EMechHudFace::SegoeLight;
		T.Dash = EMechDashStyle::Hairline;
		T.bDashTicks = false;
		T.bUnits = false;
		T.bEmptySlots = false;
		T.bHairFrame = true;
		T.DefaultVoice = 3;   // 企业客服
		T.AlertGain = 0.85f;  // 提示音轻一点
		return T;
	}

	const FMechHudTheme& PresetRef(EMechHudThemeId Id)
	{
		static const FMechHudTheme Presets[] = {MakeStandard(), MakeNewEra(), MakeVictor(), MakePhase()};
		return Presets[FMath::Clamp(static_cast<int32>(Id), 0, 3)];
	}

	FMechHudTheme& Mutable()
	{
		static FMechHudTheme Theme = MakeStandard();
		return Theme;
	}

	int32 TestOverride = -1;
}

const FMechHudTheme& MechHudTheme::Get()
{
	return MechHudThemeData::Mutable();
}

const FMechHudTheme& MechHudTheme::Preset(EMechHudThemeId Id)
{
	return MechHudThemeData::PresetRef(Id);
}

void MechHudTheme::Use(EMechHudThemeId Id)
{
	FMechHudTheme& T = MechHudThemeData::Mutable();
	if (T.Id != Id)
	{
		T = MechHudThemeData::PresetRef(Id);
	}
}

EMechHudThemeId MechHudTheme::Current()
{
	return MechHudThemeData::Mutable().Id;
}

EMechHudThemeId MechHudTheme::ForMaker(EMechMaker Maker)
{
	switch (Maker)
	{
	case EMechMaker::NewEra:
		return EMechHudThemeId::NewEra;
	case EMechMaker::Victor:
		return EMechHudThemeId::Victor;
	case EMechMaker::Phase:
		return EMechHudThemeId::Phase;
	default:
		return EMechHudThemeId::Standard;
	}
}

EMechHudThemeId MechHudTheme::Resolve(const AMechCharacter* Focus, bool bOwnMech)
{
	if (MechHudThemeData::TestOverride >= 0)
	{
		return static_cast<EMechHudThemeId>(FMath::Clamp(MechHudThemeData::TestOverride, 0, 3));
	}
	static const int32 Forced = [] {
		FString S;
		EMechHudThemeId Id;
		return FParse::Value(FCommandLine::Get(), TEXT("MechHudTheme="), S) && Parse(S, Id) ? static_cast<int32>(Id) : -1;
	}();
	if (Forced >= 0)
	{
		return static_cast<EMechHudThemeId>(Forced);
	}
	// 11-01 设置「界面」页里固定了一套（不是「跟随机体」）：观战、模型查看也一直用这套
	const int32 Fixed = FMechSettings::Get().HudTheme;
	if (Fixed >= 0)
	{
		return static_cast<EMechHudThemeId>(FMath::Clamp(Fixed, 0, 3));
	}
	// 观战（看的不是自己操控的那台）、模型查看之外没有机体：联赛标准
	if (!Focus || !bOwnMech)
	{
		return EMechHudThemeId::Standard;
	}
	// 制造商接口（阵营导入代理加的）：AMechCharacter::GetFrameMaker() = MechCatalog::FrameMaker(配装框架编号)
	return ForMaker(Focus->GetFrameMaker());
}

void MechHudTheme::SetTestOverride(int32 Id)
{
	MechHudThemeData::TestOverride = Id;
}

const TCHAR* MechHudTheme::Code(EMechHudThemeId Id)
{
	return MechHudThemeData::PresetRef(Id).Code;
}

bool MechHudTheme::Parse(const FString& S, EMechHudThemeId& Out)
{
	static const TCHAR* Alias[4][2] = {{TEXT("STD"), TEXT("Standard")}, {TEXT("NEC"), TEXT("NewEra")}, {TEXT("VHI"), TEXT("Victor")}, {TEXT("PHASE"), TEXT("Phase")}};
	for (int32 i = 0; i < 4; ++i)
	{
		if (S.Equals(Alias[i][0], ESearchCase::IgnoreCase) || S.Equals(Alias[i][1], ESearchCase::IgnoreCase))
		{
			Out = static_cast<EMechHudThemeId>(i);
			return true;
		}
	}
	return false;
}
