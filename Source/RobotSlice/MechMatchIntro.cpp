#include "MechMatchIntro.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundBase.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/SLeafWidget.h"

#include "MechCharacter.h"
#include "MechFonts.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHealthComponent.h"

// ================================================================ 数据（Content/Intro/MechIntro.txt + MechIntroVoice.txt）
namespace
{
	/** 句间留白：一句台词那段镜头至少 = 说话时长 + 这么多 */
	constexpr float LineGap = 0.25f;
	/** 最后一句后多留一点再交回游戏镜头 */
	constexpr float LastLineTail = 0.4f;
	/** 运镜结束 → 游戏镜头的过渡 */
	constexpr float BlendOut = 0.9f;

	enum EAnchor : int32
	{
		World = -1,
		Mid = 0,   // C：两队中间，x 指向红方
		Spawn0 = 1,
		Spawn1 = 2,
		Ally = 3,  // A：自己那队
		Enemy = 4  // E：对面那队
	};

	struct FIntroPoint
	{
		int32 Anchor = World;
		bool bScaled = false;
		/** G 前缀：高度按这个水平位置的地面（从上往下第一个挡住的东西：地形、树、楼顶）+ V.Z 厘米 */
		bool bGround = false;
		FVector V = FVector::ZeroVector;
	};

	struct FIntroKey
	{
		float U = 0.f;
		FIntroPoint Pos;
		FIntroPoint Look;
		float Fov = 60.f;
	};

	bool ParsePoint(const FString& Tok, FIntroPoint& Out)
	{
		FString Body = Tok;
		FString Head;
		if (Tok.Split(TEXT(":"), &Head, &Body))
		{
			Out.bScaled = Head.EndsWith(TEXT("*"));
			Head.RemoveFromEnd(TEXT("*"));
			Out.bGround = Head.StartsWith(TEXT("G"));
			if (Out.bGround)
			{
				Head.RightChopInline(1);
			}
			Out.Anchor = Head == TEXT("C") ? Mid : Head == TEXT("S0") ? Spawn0 : Head == TEXT("S1") ? Spawn1 : Head == TEXT("A") ? Ally : Head == TEXT("E") ? Enemy : World;
		}
		TArray<FString> P;
		Body.ParseIntoArray(P, TEXT(","));
		if (P.Num() != 3)
		{
			return false;
		}
		Out.V = FVector(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]));
		return true;
	}
}

/** 一镜 */
struct FMechIntroShot
{
	float Nominal = 3.f;
	bool bLine = false;
	/** 0 路径关键帧；1 自动取景对面一队；2 自动取景自己这队 */
	int32 Kind = 0;
	float Ease = 0.5f;
	TArray<FIntroKey> Keys;
	/** 自动队伍镜头：机位高度（相对机体胶囊中心，厘米）、距离倍数 */
	float TeamH = 30.f;
	float TeamDist = 1.f;
};

/** 一张地图 */
struct FMechIntroMap
{
	FString Key;
	FString Match;
	bool bCoreOnly = false;
	FString Url;
	FString Title;
	FString Sub;
	TArray<FString> Variants;
	FString ShotsFrom;
	TArray<FMechIntroShot> Shots;
};

namespace
{
	struct FIntroData
	{
		TArray<FMechIntroMap> Maps;
		/** 编号 → 说话时长 */
		TMap<FString, float> Speech;
		TArray<FString> Openers;
		TArray<FString> Counts;

		const FMechIntroMap* FindKey(const FString& Key) const
		{
			return Maps.FindByPredicate([&Key](const FMechIntroMap& M) { return M.Key == Key; });
		}

		const FMechIntroMap* FindForWorld(const FString& MapName, bool bCore) const
		{
			// 带 core 的先比（L_Range?Core 灰盒和普通测试场同一张图）
			for (const FMechIntroMap& M : Maps)
			{
				if (M.bCoreOnly && bCore && !M.Match.IsEmpty() && MapName.Contains(M.Match))
				{
					return &M;
				}
			}
			for (const FMechIntroMap& M : Maps)
			{
				if (!M.bCoreOnly && !M.Match.IsEmpty() && MapName.Contains(M.Match))
				{
					return &M;
				}
			}
			return FindKey(TEXT("default"));
		}

		/** 镜头表（shots 借用别的段时取那一段的） */
		const TArray<FMechIntroShot>& ShotsOf(const FMechIntroMap& M) const
		{
			if (!M.ShotsFrom.IsEmpty())
			{
				if (const FMechIntroMap* From = FindKey(M.ShotsFrom))
				{
					return From->Shots;
				}
			}
			return M.Shots;
		}

		float SpeechOf(const FString& Id) const
		{
			const float* S = Speech.Find(Id);
			return S ? *S : 0.f;
		}
	};

	FIntroData LoadData()
	{
		FIntroData D;
		const FString Dir = FPaths::ProjectContentDir() / TEXT("Intro");
		TArray<FString> Rows;
		if (FFileHelper::LoadFileToStringArray(Rows, *(Dir / TEXT("MechIntroVoice.txt"))))
		{
			for (const FString& Row : Rows)
			{
				TArray<FString> T;
				Row.ParseIntoArrayWS(T);
				if (T.Num() < 2 || T[0].StartsWith(TEXT("#")))
				{
					continue;
				}
				D.Speech.Add(T[0], FCString::Atof(*T[1]));
				if (T[0].StartsWith(TEXT("open_")))
				{
					D.Openers.Add(T[0]);
				}
				else if (T[0].StartsWith(TEXT("count_")))
				{
					D.Counts.Add(T[0]);
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[intro] 没有台词时长表 %s（不放解说，只放镜头）"), *(Dir / TEXT("MechIntroVoice.txt")));
		}
		D.Openers.Sort();
		D.Counts.Sort();
		Rows.Reset();
		if (!FFileHelper::LoadFileToStringArray(Rows, *(Dir / TEXT("MechIntro.txt"))))
		{
			UE_LOG(LogTemp, Warning, TEXT("[intro] 没有镜头表 %s"), *(Dir / TEXT("MechIntro.txt")));
			return D;
		}
		FMechIntroMap* M = nullptr;
		FMechIntroShot* S = nullptr;
		int32 LineNo = 0;
		for (FString Row : Rows)
		{
			++LineNo;
			int32 Hash;
			if (Row.FindChar(TEXT('#'), Hash))
			{
				Row.LeftInline(Hash);
			}
			Row.TrimStartAndEndInline();
			if (Row.IsEmpty())
			{
				continue;
			}
			TArray<FString> T;
			Row.ParseIntoArrayWS(T);
			const FString& Cmd = T[0];
			auto Rest = [&Row, &Cmd]() { return Row.Mid(Cmd.Len()).TrimStartAndEnd(); };
			if (Cmd == TEXT("map") && T.Num() >= 2)
			{
				M = &D.Maps.AddDefaulted_GetRef();
				S = nullptr;
				M->Key = T[1];
				M->Match = T.Num() >= 3 ? T[2] : FString();
				M->bCoreOnly = T.Num() >= 4 && T[3] == TEXT("core");
			}
			else if (!M)
			{
				continue;
			}
			else if (Cmd == TEXT("title"))
			{
				M->Title = Rest();
			}
			else if (Cmd == TEXT("sub"))
			{
				M->Sub = Rest();
			}
			else if (Cmd == TEXT("url") && T.Num() >= 2)
			{
				M->Url = T[1];
			}
			else if (Cmd == TEXT("variants"))
			{
				M->Variants.Append(T.GetData() + 1, T.Num() - 1);
			}
			else if (Cmd == TEXT("shots") && T.Num() >= 2)
			{
				M->ShotsFrom = T[1];
			}
			else if (Cmd == TEXT("shot") && T.Num() >= 3)
			{
				S = &M->Shots.AddDefaulted_GetRef();
				S->Nominal = FMath::Max(0.3f, FCString::Atof(*T[1]));
				for (int32 i = 2; i < T.Num(); ++i)
				{
					if (T[i] == TEXT("line"))
					{
						S->bLine = true;
					}
					else if (T[i] == TEXT("team") && i + 1 < T.Num())
					{
						S->Kind = T[i + 1] == TEXT("self") ? 2 : 1;
						++i;
					}
					else if (T[i].StartsWith(TEXT("ease=")))
					{
						S->Ease = FMath::Clamp(FCString::Atof(*T[i].Mid(5)), 0.f, 1.f);
					}
					else if (T[i].StartsWith(TEXT("h=")))
					{
						S->TeamH = FCString::Atof(*T[i].Mid(2));
					}
					else if (T[i].StartsWith(TEXT("d=")))
					{
						S->TeamDist = FMath::Clamp(FCString::Atof(*T[i].Mid(2)), 0.3f, 4.f);
					}
				}
			}
			else if (Cmd == TEXT("k") && S && T.Num() >= 5)
			{
				FIntroKey K;
				K.U = FCString::Atof(*T[1]);
				const bool bOk = ParsePoint(T[2], K.Pos) && ParsePoint(T[3], K.Look);
				K.Fov = FCString::Atof(*T[4]);
				if (bOk)
				{
					S->Keys.Add(K);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[intro] MechIntro.txt 第 %d 行看不懂：%s"), LineNo, *Row);
				}
			}
		}
		for (FMechIntroMap& Map : D.Maps)
		{
			for (FMechIntroShot& Shot : Map.Shots)
			{
				Shot.Keys.Sort([](const FIntroKey& A, const FIntroKey& B) { return A.U < B.U; });
				if (Shot.Kind == 0 && Shot.Keys.Num() == 0)
				{
					Shot.Kind = 1;  // 路径没写关键帧：当作对面一队
				}
			}
		}
		return D;
	}

	/** 镜头时间表：line 标记的镜头依次配台词；一句台词那段（到下一句之前的几镜）不够说完就把这段最后一镜拉长 */
	template <typename RunT>
	float ComputeRuns(const TArray<FMechIntroShot>& Shots, const TArray<FString>& Lines, const FIntroData& D, TArray<RunT>& Out)
	{
		Out.Reset();
		int32 Next = 0;
		for (int32 i = 0; i < Shots.Num(); ++i)
		{
			RunT R;
			R.Shot = i;
			R.Duration = Shots[i].Nominal;
			if (Shots[i].bLine && Next < Lines.Num())
			{
				R.Line = Next++;
			}
			Out.Add(R);
		}
		for (int32 i = 0; i < Out.Num(); ++i)
		{
			if (Out[i].Line < 0)
			{
				continue;
			}
			int32 j = i + 1;
			float Seg = Out[i].Duration;
			while (j < Out.Num() && Out[j].Line < 0)
			{
				Seg += Out[j].Duration;
				++j;
			}
			const float Need = D.SpeechOf(Lines[Out[i].Line]) + (j >= Out.Num() ? LastLineTail : LineGap);
			if (Need > Seg)
			{
				Out[j - 1].Duration += Need - Seg;
			}
		}
		float T = 0.f;
		for (RunT& R : Out)
		{
			R.Start = T;
			T += R.Duration;
		}
		return T;
	}

	float Ease(float U, float Amount)
	{
		U = FMath::Clamp(U, 0.f, 1.f);
		return FMath::Lerp(U, U * U * (3.f - 2.f * U), Amount);
	}

	/** 三次 Hermite（Catmull-Rom 切线，按关键帧时间非均匀） */
	FVector SplineAt(const TArray<FVector>& P, const TArray<float>& U, float u)
	{
		const int32 N = P.Num();
		if (N == 1)
		{
			return P[0];
		}
		int32 i = 0;
		while (i < N - 2 && u > U[i + 1])
		{
			++i;
		}
		const float h = FMath::Max(1e-4f, U[i + 1] - U[i]);
		const float s = FMath::Clamp((u - U[i]) / h, 0.f, 1.f);
		auto Tangent = [&](int32 k) {
			const int32 a = FMath::Max(0, k - 1);
			const int32 b = FMath::Min(N - 1, k + 1);
			return (P[b] - P[a]) / FMath::Max(1e-4f, U[b] - U[a]);
		};
		const FVector M0 = Tangent(i) * h;
		const FVector M1 = Tangent(i + 1) * h;
		const float s2 = s * s, s3 = s2 * s;
		return (2 * s3 - 3 * s2 + 1) * P[i] + (s3 - 2 * s2 + s) * M0 + (-2 * s3 + 3 * s2) * P[i + 1] + (s3 - s2) * M1;
	}

	TWeakObjectPtr<AMechMatchIntro> GActiveLocal;
}

// ================================================================ 转播片头图形（Slate，盖在 HUD 上；不写中文提示，只有地图名大标题）
class SMechIntroOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMechIntroOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
		SetVisibility(EVisibility::HitTestInvisible);
	}

	FString Title;
	FString Sub;
	/** 运镜时间、总长、开场那一镜结束的时间 */
	float T = 0.f;
	float Length = 10.f;
	float TitleEnd = 3.f;
	/** 收尾（0→1：遮幅收起、全部淡出） */
	float Out = 0.f;

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Els, int32 Layer,
		const FWidgetStyle& Style, bool bParentEnabled) const override
	{
		const FVector2f Size = FVector2f(G.GetLocalSize());
		const float W = Size.X, H = Size.Y;
		const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
		auto Box = [&](float X, float Y, float BW, float BH, const FLinearColor& C) {
			FSlateDrawElement::MakeBox(Els, ++Layer, G.ToPaintGeometry(FVector2f(BW, BH), FSlateLayoutTransform(FVector2f(X, Y))), White, ESlateDrawEffect::None, C);
		};
		auto Text = [&](const FString& S, const FSlateFontInfo& F, float X, float Y, const FLinearColor& C) {
			FSlateDrawElement::MakeText(Els, ++Layer, G.ToPaintGeometry(FVector2f(1.f, 1.f), FSlateLayoutTransform(FVector2f(X, Y))), S, F, ESlateDrawEffect::None, C);
		};
		const float In = FMath::SmoothStep(0.f, 0.45f, T);
		const float Vis = In * (1.f - FMath::SmoothStep(0.f, 1.f, Out));
		// 开头 0.7 秒从黑场淡入：切过来第一帧自动曝光还是上一个画面的（白天图会白一下）
		const float Black = 1.f - FMath::SmoothStep(0.15f, 0.7f, T);
		if (Black > 0.f)
		{
			Box(0.f, 0.f, W, H, FLinearColor(0.f, 0.f, 0.f, Black));
		}
		// 遮幅（2.35:1）
		const float Bar = FMath::Max(0.f, (H - W / 2.35f) * 0.5f) * Vis;
		Box(0.f, 0.f, W, Bar, FLinearColor(0.f, 0.f, 0.f, 1.f));
		Box(0.f, H - Bar, W, Bar, FLinearColor(0.f, 0.f, 0.f, 1.f));
		const FLinearColor Accent(0.25f, 0.85f, 1.f, 1.f);
		// 开场那一镜：左下大标题（地图名）+ 一道青色竖线 + 小字
		const float A = FMath::SmoothStep(0.35f, 0.95f, T) * (1.f - FMath::SmoothStep(TitleEnd - 0.55f, TitleEnd - 0.1f, T)) * Vis;
		if (A > 0.01f && !Title.IsEmpty())
		{
			const float Slide = (1.f - FMath::SmoothStep(0.35f, 1.1f, T)) * H * 0.02f;
			const float X = W * 0.075f - Slide;
			const float Y = H * 0.60f;
			const float TitleSize = H * 0.062f;
			Box(X - H * 0.022f, Y + H * 0.008f, FMath::Max(2.f, H * 0.004f), TitleSize * 1.9f, Accent.CopyWithNewOpacity(A));
			Text(Title, MechFonts::Get(TitleSize, true), X, Y, FLinearColor(1.f, 1.f, 1.f, A));
			if (!Sub.IsEmpty())
			{
				FSlateFontInfo F = MechFonts::GetCanvas(H * 0.0185f);
				F.LetterSpacing = 320;
				Text(Sub, F, X + H * 0.004f, Y + TitleSize * 1.42f, FLinearColor(0.78f, 0.86f, 0.92f, A * 0.9f));
			}
		}
		// 之后：遮幅里很淡的地图名 + 右上「LIVE」
		const float Small = FMath::SmoothStep(TitleEnd - 0.2f, TitleEnd + 0.5f, T) * Vis;
		if (Bar > H * 0.03f)
		{
			const float Fs = H * 0.019f;
			const float Y = (Bar - Fs * 1.35f) * 0.5f;
			if (Small > 0.01f && !Title.IsEmpty())
			{
				Text(Title, MechFonts::Get(Fs), W * 0.035f, Y, FLinearColor(1.f, 1.f, 1.f, 0.38f * Small));
			}
			const float Blink = 0.6f + 0.4f * FMath::Abs(FMath::Sin(T * 2.6f));
			const float D = Fs * 0.55f;
			Box(W * 0.93f - D * 1.6f, Y + Fs * 0.42f, D, D, FLinearColor(0.95f, 0.18f, 0.16f, Vis * Blink));
			FSlateFontInfo F = MechFonts::GetCanvas(Fs * 0.95f);
			F.LetterSpacing = 250;
			Text(TEXT("LIVE"), F, W * 0.93f, Y, FLinearColor(1.f, 1.f, 1.f, 0.7f * Vis));
			// 底边一道很细的进度线
			Box(0.f, H - Bar, W * FMath::Clamp(T / FMath::Max(1.f, Length), 0.f, 1.f), FMath::Max(1.f, H * 0.002f), Accent.CopyWithNewOpacity(0.35f * Vis));
		}
		return Layer;
	}
};

// ================================================================ 任意键跳过
class FMechIntroInput : public IInputProcessor
{
public:
	TWeakObjectPtr<AMechMatchIntro> Owner;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& E) override
	{
		const FKey K = E.GetKey();
		// 修饰键、Alt 组合（切窗口）、截图键不算
		if (!E.IsRepeat() && !K.IsModifierKey() && !E.IsAltDown() && !E.IsCommandDown() && K != EKeys::Tab && K != EKeys::F12 && K != EKeys::LeftCommand &&
			K != EKeys::RightCommand)
		{
			Fire();
		}
		return false;  // 不吃掉按键（Esc 照样开暂停菜单）
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& E) override
	{
		Fire();
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("MechIntroInput"); }

private:
	void Fire()
	{
		if (AMechMatchIntro* O = Owner.Get())
		{
			O->LocalSkipRequested();
		}
	}
};

// ================================================================ actor
AMechMatchIntro::AMechMatchIntro()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;  // 机体都摆完以后再摆镜头（「队伍」镜头按机体位置取景）
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	Anchor[0] = Anchor[1] = FVector::ZeroVector;
}

void AMechMatchIntro::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMechMatchIntro, MapKey);
	DOREPLIFETIME(AMechMatchIntro, Lines);
	DOREPLIFETIME(AMechMatchIntro, CountLine);
	DOREPLIFETIME(AMechMatchIntro, StartTime);
	DOREPLIFETIME(AMechMatchIntro, Length);
	DOREPLIFETIME(AMechMatchIntro, CutTime);
	DOREPLIFETIME(AMechMatchIntro, Anchor);
	DOREPLIFETIME(AMechMatchIntro, AnchorYaw);
	DOREPLIFETIME(AMechMatchIntro, Center);
	DOREPLIFETIME(AMechMatchIntro, bSolo);
}

// 11-01 用户：「开场的配音暂时全部删除」→ 解说配音（开场句 + 三句地图台词 + 倒计时那句）默认不播，运镜和地图标题照旧。
// 「暂时」：声音 /Game/Audio/Intro/S_intro_*、台词表 MechIntroVoice.txt 都留着，开关在这里——控制台 mech.Intro.Voice 1 或命令行 -MechIntroVoice 恢复
// （服务器开局时定，台词编号复制给各端，下一局生效）。不配台词时镜头按镜头表自己的标称时长走（以前一句台词说不完会把那段最后一镜拉长），
// 一般 14.2 秒（以前带配音 15–18 秒），倒计时还是原来的 3 秒、不说话。
namespace MechIntroVoicePriv
{
	int32 Voice = 0;
	FAutoConsoleVariableRef CVarVoice(TEXT("mech.Intro.Voice"), Voice, TEXT("开场解说配音：0 不播（默认，11-01 用户要求暂时去掉）、1 播（下一局生效）"));

	bool IsOn()
	{
		return Voice != 0 || FParse::Param(FCommandLine::Get(), TEXT("MechIntroVoice"));
	}
}

float AMechMatchIntro::BeginForMatch(AGameModeBase* Mode, bool bNormalFlow)
{
	UWorld* World = Mode ? Mode->GetWorld() : nullptr;
	AMechGameState* GS = World ? World->GetGameState<AMechGameState>() : nullptr;
	if (!GS)
	{
		return 0.f;
	}
	const TCHAR* Cmd = FCommandLine::Get();
	FString TestKey;
	const bool bTest = FParse::Value(Cmd, TEXT("MechIntroTest="), TestKey);
	if (FParse::Param(Cmd, TEXT("MechNoIntro")) || (!bNormalFlow && !bTest) || FParse::Param(Cmd, TEXT("MechLobbyTest")) || FParse::Param(Cmd, TEXT("MechAutoBattle")))
	{
		return 0.f;
	}
	const FIntroData Data = LoadData();
	const FString MapName = World->GetMapName();
	const FMechIntroMap* M = Data.FindForWorld(MapName, GS->bCoreMode);
	if (bTest)
	{
		// 测试：要测的不是当前这张图就换过去（命令行照旧带着，换过去以后再开一次）
		const FMechIntroMap* Want = Data.FindKey(TestKey);
		if (Want && Want != M && !Want->Url.IsEmpty())
		{
			FString Path = Want->Url, Opts;
			Want->Url.Split(TEXT("?"), &Path, &Opts);
			UE_LOG(LogTemp, Display, TEXT("[intro] 测试 %s：换到 %s"), *TestKey, *Want->Url);
			UGameplayStatics::OpenLevel(World, FName(*Path), true, Opts);
			return 30.f;
		}
	}
	if (!M)
	{
		UE_LOG(LogTemp, Warning, TEXT("[intro] %s 没有开场运镜（MechIntro.txt 里也没有 default 段）"), *MapName);
		return 0.f;
	}
	const TArray<FMechIntroShot>& Shots = Data.ShotsOf(*M);
	if (Shots.Num() == 0)
	{
		return 0.f;
	}
	// 挑台词：开场句随机、这张图的台词套随机（尽量不连着重复），倒计时句随机
	static TMap<FString, int32> LastVariant;
	static int32 LastOpener = -1;
	static int32 LastCount = -1;
	auto PickIndex = [](int32 Num, int32& Last, int32 Forced) {
		if (Num <= 0)
		{
			return -1;
		}
		int32 I = Forced >= 0 ? Forced % Num : FMath::RandRange(0, Num - 1);
		if (Forced < 0 && Num > 1 && I == Last)
		{
			I = (I + 1 + FMath::RandRange(0, Num - 2)) % Num;
		}
		Last = I;
		return I;
	};
	TArray<FString> Variants;
	for (const FString& V : M->Variants)
	{
		if (Data.Speech.Contains(FString::Printf(TEXT("%s_%s_1"), *M->Key, *V)))
		{
			Variants.Add(V);
		}
	}
	FString ForceV;
	FParse::Value(Cmd, TEXT("MechIntroVariant="), ForceV);
	int32 ForceOpener = -1;
	FParse::Value(Cmd, TEXT("MechIntroOpener="), ForceOpener);
	TArray<FString> Lines;
	const int32 Op = PickIndex(Data.Openers.Num(), LastOpener, ForceOpener >= 1 ? ForceOpener - 1 : -1);
	if (Op >= 0)
	{
		Lines.Add(Data.Openers[Op]);
	}
	const int32 Vi = PickIndex(Variants.Num(), LastVariant.FindOrAdd(M->Key, -1), ForceV.IsEmpty() ? -1 : FMath::Max(0, Variants.IndexOfByKey(ForceV)));
	if (Vi >= 0)
	{
		for (int32 n = 1; n <= 3; ++n)
		{
			const FString Id = FString::Printf(TEXT("%s_%s_%d"), *M->Key, *Variants[Vi], n);
			if (Data.Speech.Contains(Id))
			{
				Lines.Add(Id);
			}
		}
	}
	int32 Ci = PickIndex(Data.Counts.Num(), LastCount, -1);
	const bool bVoice = MechIntroVoicePriv::IsOn();
	if (!bVoice)
	{
		// 11-01 配音关着（默认）：台词、倒计时句都不要——各端收到空的台词表就不放声音；镜头时长不再按台词拉长（ComputeRuns 只用标称时长）
		Lines.Reset();
		Ci = -1;
	}

	TArray<FShotRun> Runs;
	const float Len = ComputeRuns(Shots, Lines, Data, Runs);

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMechMatchIntro* I = World->SpawnActor<AMechMatchIntro>(AMechMatchIntro::StaticClass(), FTransform::Identity, P);
	if (!I)
	{
		return 0.f;
	}
	I->MapKey = M->Key;
	I->Lines = Lines;
	I->CountLine = Ci >= 0 ? Data.Counts[Ci] : FString();
	I->StartTime = World->GetTimeSeconds() + 0.3f;  // 留一点给客户端收到这个 actor，大家同一刻开始
	I->Length = Len;
	I->bSolo = GS->IsSoloDM();
	// 锚点：两队刚生成的机体的中心和朝向（个人死斗用出生区）；C = 两队中间
	for (int32 Team = 0; Team < 2; ++Team)
	{
		FVector Sum = FVector::ZeroVector;
		FVector Fwd = FVector::ZeroVector;
		int32 N = 0;
		if (!I->bSolo)
		{
			for (TActorIterator<AMechCharacter> It(World); It; ++It)
			{
				if (It->GetTeam() == Team && IsValid(*It))
				{
					Sum += It->GetActorLocation();
					Fwd += It->GetActorForwardVector();
					++N;
				}
			}
		}
		if (N > 0)
		{
			I->Anchor[Team] = Sum / N;
			I->AnchorYaw[Team] = Fwd.Rotation().Yaw;
		}
		else
		{
			I->Anchor[Team] = GS->MapSpawn[Team] + FVector(0.f, 0.f, 145.f);
			I->AnchorYaw[Team] = (GS->MapSpawn[1 - Team] - GS->MapSpawn[Team]).Rotation().Yaw;
		}
	}
	I->Center = (I->Anchor[0] + I->Anchor[1]) * 0.5f;
	I->SetLifeSpan(Len + 20.f);
	UE_LOG(LogTemp, Display, TEXT("[intro] 开场：%s（%s）%s台词 %s，倒计时句 %s，运镜 %.1f 秒，%d 镜"), *M->Key, *MapName,
		bVoice ? TEXT("") : TEXT("配音关着（mech.Intro.Voice 0）、"), *FString::Join(Lines, TEXT(" ")), *I->CountLine, Len, Runs.Num());
	return Len + 0.3f;  // 加上开始前留给客户端的 0.3 秒，运镜之后完整留出原来的倒计时
}

void AMechMatchIntro::BeginPlay()
{
	Super::BeginPlay();
	bTest = FString(FCommandLine::Get()).Contains(TEXT("-MechIntroTest="));
}

void AMechMatchIntro::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bLocalStarted && !bLocalDone)
	{
		APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
		FinishLocal(IsValid(PC) ? PC : nullptr, false);
	}
	if (Cam)
	{
		Cam->Destroy();
		Cam = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

bool AMechMatchIntro::IsPlayingLocally(const UWorld* World)
{
	const AMechMatchIntro* I = GActiveLocal.Get();
	return I && I->GetWorld() == World && I->bLocalStarted && !I->bLocalDone;
}

float AMechMatchIntro::ServerNow() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? static_cast<float>(GS->GetServerWorldTimeSeconds()) : GetWorld()->GetTimeSeconds();
}

float AMechMatchIntro::EndAt() const
{
	return CutTime >= 0.f ? FMath::Min(Length, CutTime - StartTime) : Length;
}

void AMechMatchIntro::BuildTimeline()
{
	const FIntroData Data = LoadData();
	const FMechIntroMap* M = Data.FindKey(MapKey);
	if (!M)
	{
		M = Data.FindKey(TEXT("default"));
	}
	if (!M)
	{
		return;
	}
	Map = MakeShared<FMechIntroMap>(*M);
	Map->Shots = Data.ShotsOf(*M);
	Title = M->Title;
	Subtitle = M->Sub;
	ComputeRuns(Map->Shots, Lines, Data, Runs);
	CountSpeech = Data.SpeechOf(CountLine);
}

int32 AMechMatchIntro::OwnTeam() const
{
	APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	if (const AMechCharacter* Mine = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr)
	{
		return Mine->GetTeam();
	}
	const AMechPlayerController* MPC = Cast<AMechPlayerController>(PC);
	return MPC ? MPC->GetMenuTeam() : 0;
}

FVector AMechMatchIntro::Resolve(int32 AnchorKind, bool bScaled, const FVector& V) const
{
	if (AnchorKind == World)
	{
		return V;
	}
	const float D = FMath::Max(1000.f, FVector::Dist2D(Anchor[0], Anchor[1]));
	const FVector L = bScaled ? V * D : V;
	FVector O = Center;
	float Yaw = (Anchor[1] - Anchor[0]).Rotation().Yaw;
	if (AnchorKind != Mid)
	{
		int32 Team = AnchorKind == Spawn0 ? 0 : AnchorKind == Spawn1 ? 1 : -1;
		if (Team < 0)
		{
			const int32 Own = FMath::Clamp(bSolo ? 0 : OwnTeam(), 0, 1);
			Team = AnchorKind == Ally ? Own : 1 - Own;
		}
		O = Anchor[Team];
		Yaw = AnchorYaw[Team];
	}
	const FRotator R(0.f, Yaw, 0.f);
	return O + R.RotateVector(L);
}

FVector AMechMatchIntro::ResolvePoint(int32 AnchorKind, bool bScaled, bool bGround, const FVector& V) const
{
	if (!bGround)
	{
		return Resolve(AnchorKind, bScaled, V);
	}
	// 水平位置照常算（z 先当 0），高度 = 那里从上往下第一个挡住的东西 + V.Z
	FVector P = Resolve(AnchorKind, bScaled, FVector(V.X, V.Y, 0.f));
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechIntroGround), false);
	const float Top = Center.Z + 200000.f;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(P.X, P.Y, Top), FVector(P.X, P.Y, Center.Z - 200000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q))
	{
		P.Z = Hit.ImpactPoint.Z + V.Z;
	}
	else
	{
		P.Z = Center.Z + V.Z;
	}
	return P;
}

void AMechMatchIntro::KeepAboveLandscape(FVector& Pos) const
{
	// 只认地形（类名带 Landscape）：镜头在地形下面或贴得太近就抬到地形上 1.2 米；高架桥、树下的低机位不管
	TArray<FHitResult> Hits;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechIntroLandscape), false);
	GetWorld()->LineTraceMultiByObjectType(Hits, Pos + FVector(0.f, 0.f, 50000.f), Pos - FVector(0.f, 0.f, 50.f), FCollisionObjectQueryParams(ECC_WorldStatic), Q);
	for (const FHitResult& H : Hits)
	{
		const AActor* A = H.GetActor();
		if (A && A->GetClass()->GetName().Contains(TEXT("Landscape")) && H.ImpactPoint.Z > Pos.Z - 120.f)
		{
			Pos.Z = H.ImpactPoint.Z + 120.f;
			return;
		}
	}
}

void AMechMatchIntro::TeamShot(const FMechIntroShot& Shot, float U, FVector& OutPos, FVector& OutLook, float& OutFov) const
{
	const int32 Which = Shot.Kind;
	// Which：1 对面一队（正面低机位、慢慢推近）；2 自己这队（后上方摇下来，最后接近游戏里的第三人称镜头）
	APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(GetWorld()) : nullptr;
	AMechCharacter* Mine = PC ? Cast<AMechCharacter>(PC->GetPawn()) : nullptr;
	const int32 Own = Mine ? Mine->GetTeam() : OwnTeam();
	TArray<AMechCharacter*> Group;
	for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
	{
		AMechCharacter* C = *It;
		if (!IsValid(C) || C->GetHealth()->IsDead())
		{
			continue;
		}
		const bool bMine = C->GetTeam() == Own;
		if (Which == 2 ? bMine : !bMine)
		{
			Group.Add(C);
		}
	}
	if (bSolo && Mine)
	{
		// 个人死斗各自散在场上：自己这镜只拍自己，对面那镜拍离自己最近的一台
		if (Which == 2)
		{
			Group = {Mine};
		}
		else if (Group.Num() > 1)
		{
			const FVector At = Mine->GetActorLocation();
			Group.Sort([&At](const AMechCharacter& A, const AMechCharacter& B) { return FVector::DistSquared(A.GetActorLocation(), At) < FVector::DistSquared(B.GetActorLocation(), At); });
			Group.SetNum(1);
		}
	}
	// 同一队高低差太大（出生点落到了天桥 / 楼顶上）就只拍最低的那一层（离最低那台 4 米以内的），免得取景中心悬在半空
	if (Group.Num() > 1)
	{
		float Low = UE_BIG_NUMBER;
		for (const AMechCharacter* C : Group)
		{
			Low = FMath::Min(Low, C->GetActorLocation().Z);
		}
		Group.RemoveAll([Low](const AMechCharacter* C) { return C->GetActorLocation().Z > Low + 400.f; });
	}
	FVector P = FVector::ZeroVector;
	FVector Fwd = FVector::ZeroVector;
	float Width = 0.f;
	if (Group.Num() > 0)
	{
		for (const AMechCharacter* C : Group)
		{
			P += C->GetActorLocation();
			Fwd += C->GetActorForwardVector();
			for (const AMechCharacter* C2 : Group)
			{
				Width = FMath::Max(Width, FVector::Dist(C->GetActorLocation(), C2->GetActorLocation()));
			}
		}
		P /= Group.Num();
	}
	else
	{
		const int32 T = FMath::Clamp(Which == 2 ? Own : 1 - Own, 0, 1);
		P = Anchor[T];
		Fwd = FRotator(0.f, AnchorYaw[T], 0.f).Vector();
	}
	Fwd = Fwd.GetSafeNormal2D();
	if (Fwd.IsNearlyZero())
	{
		Fwd = FVector::ForwardVector;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
	const float u = Ease(U, 0.65f);
	if (Which == 1)
	{
		// 机体站成一排面朝镜头：镜头在它们正前方偏一侧、略低于胸口往上看，慢慢推近
		const float Dist = FMath::Clamp(Width * 0.9f + 900.f, 1000.f, 3200.f) * Shot.TeamDist;
		const float d = FMath::Lerp(Dist * 1.25f, Dist * 0.92f, u);
		OutPos = P + Fwd * d + Right * FMath::Lerp(-0.1f, -0.05f, u) * Dist + FVector(0.f, 0.f, Shot.TeamH + FMath::Lerp(-30.f, 20.f, u));
		OutLook = P + FVector(0.f, 0.f, FMath::Lerp(70.f, 90.f, u)) + Right * FMath::Lerp(0.05f, 0.f, u) * Width;
		OutFov = FMath::Lerp(48.f, 42.f, u);
	}
	else
	{
		// 自己这队：从后上方往下摇、推到自己机体右后方（接游戏里的第三人称镜头：后 5.6 米、右 0.7、上 1.1）
		const FVector Me = Mine ? Mine->GetActorLocation() : P;
		const float k = Shot.TeamDist;
		const FVector From = Me - Fwd * 800.f * k + Right * 320.f * k + FVector(0.f, 0.f, 330.f * k);
		const FVector To = Me - Fwd * 600.f + Right * 85.f + FVector(0.f, 0.f, 185.f);
		OutPos = FMath::Lerp(From, To, u);
		// 前一段一直看着自己的机体（机体在画面中间），后一段才抬头看向前方，接游戏镜头
		const float LookU = FMath::SmoothStep(0.35f, 1.f, u);
		OutLook = FMath::Lerp(Me + Fwd * 120.f + FVector(0.f, 0.f, 30.f), Me + Fwd * 2500.f + FVector(0.f, 0.f, 90.f), LookU);
		OutFov = FMath::Lerp(50.f, 68.f, u);
	}
	// 自动镜头别钻进墙里 / 地里：从拍的机体（一定在地面上、空地里）往镜头位置扫一下，挡住就停在挡住的地方（机体不算挡）
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MechIntroCam), false);
	const FVector From = (Which == 2 && Mine ? Mine->GetActorLocation() : P) + FVector(0.f, 0.f, 60.f);
	if (GetWorld()->SweepSingleByObjectType(Hit, From, OutPos, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(25.f), Q))
	{
		OutPos = Hit.Location;
	}
}

void AMechMatchIntro::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const AMechGameState* GS = World->GetGameState<AMechGameState>();
	APlayerController* PC = GEngine ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
	if (!GS || Length <= 0.f)
	{
		return;
	}
	const float Now = ServerNow();
	const float T = Now - StartTime;
	const bool bCountdown = GS->Phase == EMechRoundPhase::Countdown;
	if (PC && !bLocalStarted && !bLocalDone && bCountdown && T > -0.3f && T < EndAt() - 0.2f)
	{
		StartLocal(PC);
	}
	if (bLocalStarted && !bLocalDone)
	{
		LocalClock += DeltaSeconds;
		if (!bCountdown || T >= EndAt() || !PC)
		{
			FinishLocal(PC, false);
		}
		else
		{
			UpdateLocal(PC, FMath::Max(0.f, T));
		}
	}
	// 按键跳过：输入预处理器里只记一下，这里处理（在预处理器回调里注销它自己不安全）
	if (bSkipRequested)
	{
		bSkipRequested = false;
		if (bLocalStarted && !bLocalDone)
		{
			FinishLocal(PC, true);
			AMechPlayerController* MPC = Cast<AMechPlayerController>(PC);
			if (MPC && !bSkipSent)
			{
				bSkipSent = true;
				MPC->ServerSkipIntro();
			}
		}
	}
	// 倒计时句：说完正好开打（不早于运镜结束）；跳过了也照样说
	if (PC && !bCountPlayed && bCountdown && (bLocalDone || T >= EndAt()) && !CountLine.IsEmpty() && Map.IsValid())
	{
		if (Now >= FMath::Max(GS->PhaseEndTime - CountSpeech - 0.12f, StartTime + EndAt() + 0.25f))
		{
			bCountPlayed = true;
			PlayLine(CountLine, 0.f);
			UE_LOG(LogTemp, Display, TEXT("[intro] %.2f 秒 倒计时句 %s（离开打 %.2f 秒）"), T, *CountLine, GS->PhaseEndTime - Now);
		}
	}
	if (bTest)
	{
		TestTick(T);
	}
}

void AMechMatchIntro::StartLocal(APlayerController* PC)
{
	BuildTimeline();
	if (!Map.IsValid() || Runs.Num() == 0)
	{
		bLocalDone = true;
		return;
	}
	bLocalStarted = true;
	GActiveLocal = this;
	LocalClock = 0.f;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.ObjectFlags |= RF_Transient;
	Cam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	if (Cam)
	{
		Cam->SetReplicates(false);
		Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
	}
	if (AHUD* Hud = PC->GetHUD())
	{
		bPrevShowHUD = Hud->bShowHUD;
		Hud->bShowHUD = false;
	}
	// 声音先全部加载好（开打前同步加载几个小文件，免得播到一半卡）
	for (const FString& Id : Lines)
	{
		LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/Intro/S_intro_%s.S_intro_%s"), *Id, *Id));
	}
	if (!CountLine.IsEmpty())
	{
		LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/Intro/S_intro_%s.S_intro_%s"), *CountLine, *CountLine));
	}
	if (GEngine && GEngine->GameViewport)
	{
		Overlay = SNew(SMechIntroOverlay);
		Overlay->Title = Title;
		Overlay->Sub = Subtitle;
		Overlay->Length = Length;
		Overlay->TitleEnd = Runs.Num() > 0 ? Runs[0].Duration : 3.f;
		GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 12);  // HUD 10、选配 40、菜单 50
	}
	if (FSlateApplication::IsInitialized() && !bTest)
	{
		Input = MakeShared<FMechIntroInput>();
		Input->Owner = this;
		FSlateApplication::Get().RegisterInputPreProcessor(Input);
	}
	CurrentShot = -1;
	UE_LOG(LogTemp, Display, TEXT("[intro] 本机开始播：%s「%s」%d 镜 %.1f 秒；锚点 蓝 %s（朝 %.0f）红 %s（朝 %.0f）中 %s，本机队伍 %d"), *MapKey, *Title, Runs.Num(), Length,
		*Anchor[0].ToCompactString(), AnchorYaw[0], *Anchor[1].ToCompactString(), AnchorYaw[1], *Center.ToCompactString(), OwnTeam());
	if (bTest)
	{
		for (TActorIterator<AMechCharacter> It(GetWorld()); It; ++It)
		{
			UE_LOG(LogTemp, Display, TEXT("[intro]   机体 %s 队 %d 位置 %s"), *It->DisplayName, It->GetTeam(), *It->GetActorLocation().ToCompactString());
		}
	}
	for (const FShotRun& R : Runs)
	{
		const FString Line = R.Line >= 0 && Lines.IsValidIndex(R.Line) ? Lines[R.Line] : FString();
		UE_LOG(LogTemp, Display, TEXT("[intro]   镜 %d：%5.2f – %5.2f 秒%s%s"), R.Shot, R.Start, R.Start + R.Duration, Line.IsEmpty() ? TEXT("") : TEXT("  台词 "), *Line);
	}
}

void AMechMatchIntro::UpdateLocal(APlayerController* PC, float T)
{
	if (!Cam || !Map.IsValid())
	{
		return;
	}
	int32 Ri = Runs.Num() - 1;
	for (int32 i = 0; i < Runs.Num(); ++i)
	{
		if (T < Runs[i].Start + Runs[i].Duration)
		{
			Ri = i;
			break;
		}
	}
	const FShotRun& R = Runs[Ri];
	const FMechIntroShot& S = Map->Shots[R.Shot];
	const float U = FMath::Clamp((T - R.Start) / FMath::Max(0.05f, R.Duration), 0.f, 1.f);
	FVector Pos, Look;
	float Fov = 60.f;
	if (S.Kind == 0)
	{
		if (CachedShot != Ri)
		{
			// 这一镜的关键帧点只算一次（带 G 的要往下打射线找地面）
			CachedShot = Ri;
			CachedPos.Reset();
			CachedLook.Reset();
			for (const FIntroKey& K : S.Keys)
			{
				CachedPos.Add(ResolvePoint(K.Pos.Anchor, K.Pos.bScaled, K.Pos.bGround, K.Pos.V));
				CachedLook.Add(ResolvePoint(K.Look.Anchor, K.Look.bScaled, K.Look.bGround, K.Look.V));
			}
		}
		const TArray<FVector>& Ps = CachedPos;
		const TArray<FVector>& Ls = CachedLook;
		TArray<float> Us;
		TArray<float> Fs;
		for (const FIntroKey& K : S.Keys)
		{
			Us.Add(K.U);
			Fs.Add(K.Fov);
		}
		const float u = Ease(U, S.Ease);
		Pos = SplineAt(Ps, Us, u);
		Look = SplineAt(Ls, Us, u);
		int32 k = 0;
		while (k < Us.Num() - 2 && u > Us[k + 1])
		{
			++k;
		}
		Fov = Us.Num() == 1 ? Fs[0] : FMath::Lerp(Fs[k], Fs[k + 1], FMath::Clamp((u - Us[k]) / FMath::Max(1e-4f, Us[k + 1] - Us[k]), 0.f, 1.f));
	}
	else
	{
		TeamShot(S, U, Pos, Look, Fov);
	}
	KeepAboveLandscape(Pos);
	Cam->SetActorLocationAndRotation(Pos, (Look - Pos).Rotation());
	Cam->GetCameraComponent()->SetFieldOfView(Fov);
	if (PC->GetViewTarget() != Cam)
	{
		PC->SetViewTarget(Cam);  // 客户端收到自己的机体时引擎会把视角切回机体，这里每帧切回来
	}
	if (Ri != CurrentShot)
	{
		CurrentShot = Ri;
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();  // 硬切：不拖动态模糊、不留 TSR 残影
		}
	}
	if (R.Line >= 0 && !PlayedLines.Contains(R.Line) && Lines.IsValidIndex(R.Line))
	{
		PlayedLines.Add(R.Line);
		const float Late = T - R.Start;
		PlayLine(Lines[R.Line], Late > 0.35f ? Late : 0.f);
		UE_LOG(LogTemp, Display, TEXT("[intro] %.2f 秒 镜 %d 台词 %s"), T, R.Shot, *Lines[R.Line]);
	}
	if (Overlay.IsValid())
	{
		Overlay->T = T;
		Overlay->Out = FMath::Clamp((T - (EndAt() - 0.5f)) / 0.5f, 0.f, 1.f);
	}
}

void AMechMatchIntro::PlayLine(const FString& Id, float Offset)
{
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/Intro/S_intro_%s.S_intro_%s"), *Id, *Id));
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("[intro] 没有声音 /Game/Audio/Intro/S_intro_%s"), *Id);
		return;
	}
	if (UAudioComponent* A = UGameplayStatics::SpawnSound2D(this, Sound, 1.f, 1.f, Offset))
	{
		Voices.Add(A);
	}
}

void AMechMatchIntro::FinishLocal(APlayerController* PC, bool bSkipped)
{
	bLocalDone = true;
	if (GActiveLocal.Get() == this)
	{
		GActiveLocal.Reset();
	}
	if (Input.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Input);
	}
	Input.Reset();
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	if (bSkipped)
	{
		for (UAudioComponent* A : Voices)
		{
			if (IsValid(A))
			{
				A->FadeOut(0.25f, 0.f);
			}
		}
	}
	Voices.Reset();
	if (PC)
	{
		if (AHUD* Hud = PC->GetHUD())
		{
			Hud->bShowHUD = bPrevShowHUD;
		}
		if (PC->GetViewTarget() == Cam)
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				PC->SetViewTargetWithBlend(Pawn, bSkipped ? 0.45f : BlendOut, VTBlend_EaseInOut, 2.f);
			}
			else
			{
				PC->SetViewTarget(PC);
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[intro] 本机结束（%s），运镜第 %.2f 秒"), bSkipped ? TEXT("跳过") : TEXT("播完"), ServerNow() - StartTime);
}

void AMechMatchIntro::LocalSkipRequested()
{
	if (bLocalStarted && !bLocalDone && LocalClock >= 0.6f && !bTest)
	{
		bSkipRequested = true;  // 下一帧 Tick 里处理
	}
}

void AMechMatchIntro::PlayerSkipped(APlayerController* PC)
{
	UWorld* World = GetWorld();
	AMechGameState* GS = World->GetGameState<AMechGameState>();
	if (!HasAuthority() || !PC || !PC->PlayerState || !GS || CutTime >= 0.f)
	{
		return;
	}
	SkippedIds.Add(PC->PlayerState->GetPlayerId());
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* P = It->Get();
		if (P && P->PlayerState && !SkippedIds.Contains(P->PlayerState->GetPlayerId()))
		{
			return;  // 还有人在看
		}
	}
	const float Now = World->GetTimeSeconds();
	if (Now >= StartTime + Length || GS->Phase != EMechRoundPhase::Countdown)
	{
		return;
	}
	// 所有真人都跳过了：倒计时提前（运镜之后那 3 秒倒计时照旧留着）
	const float Tail = FMath::Max(0.5f, GS->PhaseEndTime - (StartTime + Length));
	GS->PhaseEndTime = Now + Tail;
	CutTime = Now;
	UE_LOG(LogTemp, Display, TEXT("[intro] 所有真人都跳过了，第 %.2f 秒结束运镜，%.1f 秒后开打"), Now - StartTime, Tail);
}

void AMechMatchIntro::TestTick(float T)
{
	// 逐秒截图（含遮幅和标题）到 Saved/Screenshots/Intro/<地图键>/，结束后再录到开打、退出。
	// 联机测试时客户端的截图文件名前面加 client_；-MechIntroSkipAt=秒 在那一刻模拟按键跳过（测跳过流程）
	const bool bClient = GetNetMode() == NM_Client;
	const FString Prefix = bClient ? TEXT("client_") + MapKey : MapKey;
	float SkipAt = -1.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("MechIntroSkipAt="), SkipAt) && SkipAt > 0.f && T >= SkipAt && bLocalStarted && !bLocalDone && !bSkipRequested && !bSkipSent)
	{
		UE_LOG(LogTemp, Display, TEXT("[intro] 测试：第 %.2f 秒模拟按键跳过"), T);
		bSkipRequested = true;
	}
	const int32 Sec = FMath::FloorToInt(T);
	if (T >= 0.f && T < EndAt() + 0.1f && Sec != TestShot)
	{
		TestShot = Sec;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots/Intro") / MapKey / FString::Printf(TEXT("%s_%02d.png"), *Prefix, Sec);
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogTemp, Display, TEXT("[intro] 截图 %02d 秒 镜 %d：镜头 %s 朝向 %s 视野 %.0f"), Sec, CurrentShot, Cam ? *Cam->GetActorLocation().ToCompactString() : TEXT("-"),
			Cam ? *Cam->GetActorRotation().ToCompactString() : TEXT("-"), Cam ? Cam->GetCameraComponent()->FieldOfView : 0.f);
	}
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	if (TestQuitAt < 0.f && GS && GS->Phase == EMechRoundPhase::Fight)
	{
		TestQuitAt = GetWorld()->GetTimeSeconds() + 1.5f;
		UE_LOG(LogTemp, Display, TEXT("[intro] 开打（运镜开始后 %.2f 秒）"), T);
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/Intro") / MapKey / FString::Printf(TEXT("%s_fight.png"), *Prefix), true, false);
	}
	if (TestQuitAt > 0.f && GetWorld()->GetTimeSeconds() >= TestQuitAt)
	{
		UE_LOG(LogTemp, Display, TEXT("[intro] 测试结束，退出"));
		if (APlayerController* PC = GEngine->GetFirstLocalPlayerController(GetWorld()))
		{
			PC->ConsoleCommand(TEXT("quit"));
		}
		TestQuitAt = 1e9f;
	}
}

// ---------------------------------------------------------------- 控制器的跳过 RPC（声明在 MechGameMode.h）
void AMechPlayerController::ServerSkipIntro_Implementation()
{
	for (TActorIterator<AMechMatchIntro> It(GetWorld()); It; ++It)
	{
		It->PlayerSkipped(this);
	}
}
