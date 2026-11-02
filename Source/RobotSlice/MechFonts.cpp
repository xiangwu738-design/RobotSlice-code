#include "MechFonts.h"

#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UObject/Package.h"

namespace
{
	TSharedPtr<const FCompositeFont> LoadSystemFont(const TCHAR* File)
	{
		const FString Path = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts"), File);
		if (!FPaths::FileExists(Path))
		{
			return nullptr;
		}
		// .ttc 取第 0 个字体（微软雅黑本体）；按需从文件流式读，不整个读进内存
		return MakeShared<FCompositeFont>(TEXT("Regular"), Path, EFontHinting::Default, EFontLoadingPolicy::Stream);
	}

	const TSharedPtr<const FCompositeFont>& Composite(bool bBold)
	{
		static const TSharedPtr<const FCompositeFont> Regular = LoadSystemFont(TEXT("msyh.ttc"));
		static const TSharedPtr<const FCompositeFont> Bold = LoadSystemFont(TEXT("msyhbd.ttc"));
		return bBold ? Bold : Regular;
	}

	// HUD：Bahnschrift（Windows 自带的 DIN 风格窄体，数字像仪表读数），没有的字（队友名字里的中文）退回微软雅黑
	const TSharedPtr<const FCompositeFont>& HudComposite()
	{
		static const TSharedPtr<const FCompositeFont> Font = []() -> TSharedPtr<const FCompositeFont> {
			const FString Dir = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts"));
			const FString Main = FPaths::Combine(Dir, TEXT("bahnschrift.ttf"));
			if (!FPaths::FileExists(Main))
			{
				return Composite(false);
			}
			TSharedRef<FCompositeFont> Made = MakeShared<FCompositeFont>(TEXT("Regular"), Main, EFontHinting::Default, EFontLoadingPolicy::Stream);
			const FString Back = FPaths::Combine(Dir, TEXT("msyh.ttc"));
			if (FPaths::FileExists(Back))
			{
				Made->FallbackTypeface.Typeface.AppendFont(TEXT("Regular"), Back, EFontHinting::Default, EFontLoadingPolicy::Stream);
			}
			return Made;
		}();
		return Font;
	}

	UFont* CanvasFont(bool bBold)
	{
		static TWeakObjectPtr<UFont> Cached[2];
		TWeakObjectPtr<UFont>& Slot = Cached[bBold ? 1 : 0];
		if (!Slot.IsValid())
		{
			UFont* Font = nullptr;
			if (const TSharedPtr<const FCompositeFont>& C = HudComposite())
			{
				Font = NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient);
				Font->FontCacheType = EFontCacheType::Runtime;
				Font->CompositeFont = *C;
				Font->LegacyFontSize = 16;
				Font->AddToRoot();  // 整个进程一直用
			}
			else
			{
				Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			}
			Slot = Font;
		}
		return Slot.Get();
	}
}

namespace MechFontsHud  // HUD 主题字体（10-31）
{
	struct FFace
	{
		const TCHAR* File;
		/** 换算成和 Bahnschrift 差不多宽的字号 */
		float Scale;
	};
	// 顺序和 EMechHudFace 一致：Bahnschrift、Segoe UI 粗体、Stencil、Consolas 粗体、Segoe UI 细体、Segoe UI 半细
	const FFace Faces[] = {
		{TEXT("bahnschrift.ttf"), 1.f},
		{TEXT("segoeuib.ttf"), 0.9f},
		{TEXT("STENCIL.TTF"), 0.84f},
		{TEXT("consolab.ttf"), 0.86f},
		{TEXT("segoeuil.ttf"), 0.98f},
		{TEXT("segoeuisl.ttf"), 0.95f},
	};
	constexpr int32 Num = UE_ARRAY_COUNT(Faces);

	UFont* FaceFont(int32 Face)
	{
		static TWeakObjectPtr<UFont> Cached[Num];
		static bool bTried[Num] = {};
		Face = FMath::Clamp(Face, 0, Num - 1);
		if (!Cached[Face].IsValid() && !bTried[Face])
		{
			bTried[Face] = true;
			const FString Dir = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("WINDIR")), TEXT("Fonts"));
			const FString Main = FPaths::Combine(Dir, Faces[Face].File);
			if (Face > 0 && FPaths::FileExists(Main))
			{
				TSharedRef<FCompositeFont> Made = MakeShared<FCompositeFont>(TEXT("Regular"), Main, EFontHinting::Default, EFontLoadingPolicy::Stream);
				const FString Back = FPaths::Combine(Dir, TEXT("msyh.ttc"));
				if (FPaths::FileExists(Back))
				{
					Made->FallbackTypeface.Typeface.AppendFont(TEXT("Regular"), Back, EFontHinting::Default, EFontLoadingPolicy::Stream);
				}
				UFont* Font = NewObject<UFont>(GetTransientPackage(), NAME_None, RF_Transient);
				Font->FontCacheType = EFontCacheType::Runtime;
				Font->CompositeFont = *Made;
				Font->LegacyFontSize = 16;
				Font->AddToRoot();  // 整个进程一直用
				Cached[Face] = Font;
			}
		}
		return Cached[Face].Get();
	}
}

FSlateFontInfo MechFonts::GetHudFace(int32 Face, float Size)
{
	const int32 F = FMath::Clamp(Face, 0, MechFontsHud::Num - 1);
	if (UFont* Font = F > 0 ? MechFontsHud::FaceFont(F) : nullptr)
	{
		return FSlateFontInfo(Font, FMath::Max(6.f, FMath::RoundToFloat(Size * MechFontsHud::Faces[F].Scale)), FName(TEXT("Regular")));
	}
	return GetCanvas(Size, false);  // Bahnschrift（原来那份）
}

FSlateFontInfo MechFonts::Get(float Size, bool bBold)
{
	if (const TSharedPtr<const FCompositeFont>& Font = Composite(bBold))
	{
		return FSlateFontInfo(Font, Size, TEXT("Regular"));
	}
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

FSlateFontInfo MechFonts::GetLight(float Size)
{
	static const TSharedPtr<const FCompositeFont> Light = LoadSystemFont(TEXT("msyhl.ttc"));
	if (Light.IsValid())
	{
		return FSlateFontInfo(Light, Size, TEXT("Regular"));
	}
	return Get(Size, false);
}

FSlateFontInfo MechFonts::GetLatin(float Size, int32 LetterSpacing)
{
	// 直接用组合字体（不包 UFont）：Slate 画字用不着 UFont，那是 Canvas 才要的
	FSlateFontInfo F = HudComposite().IsValid() ? FSlateFontInfo(HudComposite(), Size, TEXT("Regular")) : Get(Size, false);
	F.LetterSpacing = LetterSpacing;
	return F;
}

FSlateFontInfo MechFonts::GetCanvas(float Size, bool bBold)
{
	UFont* Font = CanvasFont(bBold);
	return FSlateFontInfo(Font, Size, HudComposite().IsValid() ? FName(TEXT("Regular")) : NAME_None);
}
