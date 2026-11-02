// 标点的 HUD（11-01，AMechHUD::DrawPings / DrawPingWheel / PingGlyph / GatherPingIcons）：照现有 HUD 的风格——细线几何图标、等宽小字，颜色全从 HUD 主题取。
//  屏幕上：地点类画成「图钉」（图标浮在地面点上方 34 像素，一根细杆连到地面上的小菱形）；敌人是胸口一个红菱形 + 四角括号（慢慢呼吸）；
//          物品是插件舱 / 核心外面一圈金色括号；需要支援浮在那台机体胸口上方。图标右边一列写距离，队友标的再小字写名字；
//          刚放下的 2.5 秒和准星压在附近时，那一列最上面多一个词（进攻 / 危险……），其余时间不写——减少文字。
//          屏幕外：贴着屏幕边（和插件舱一样的贴边算法），图标外侧一个小三角指向它。刚放下时一圈扩散，最后一秒淡出。
//  轮盘：正中一圈暗底，8 格图标（选中的放大、外圈亮一段），正中写选中项的名字；打开那一刻准星指着的地方画一个小圈（标点会落在那）。
//  地图：小地图 / 大地图上同一个图标（MechMinimap.cpp 的 DrawMapIcon）。

#include "MechHUD.h"

#include "Camera/PlayerCameraManager.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "MechCharacter.h"
#include "MechComms.h"
#include "MechCoreDirector.h"
#include "MechHudTheme.h"
#include "MechMinimap.h"
#include "MechPing.h"
#include "MechPluginPod.h"

// 带名字的命名空间：合并编译时不和 MechHUD.cpp 里的同名小工具打架（调用一律带前缀）
namespace MechHudPingLocal
{
	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}
	/** 每个标点在这一端第一次画出来的真实时间（扩散圈、刚放下时写词用；联机时自己的标点要等一个来回才到，按收到算） */
	TMap<TWeakObjectPtr<AMechPing>, double>& FirstSeen()
	{
		static TMap<TWeakObjectPtr<AMechPing>, double> Map;
		return Map;
	}
}

void AMechHUD::PingGlyph(uint8 Kind, float X, float Y, float S, const FLinearColor& C, float Thick)
{
	auto Diamond = [&](float R, bool bFill) {
		if (bFill)
		{
			Triangle(FVector2D(X, Y - R), FVector2D(X + R, Y), FVector2D(X - R, Y), C);
			Triangle(FVector2D(X, Y + R), FVector2D(X + R, Y), FVector2D(X - R, Y), C);
		}
		else
		{
			Polyline({FVector2f(X, Y - R), FVector2f(X + R, Y), FVector2f(X, Y + R), FVector2f(X - R, Y), FVector2f(X, Y - R)}, C, Thick);
		}
	};
	switch (static_cast<EMechPing>(Kind))
	{
	case EMechPing::Go:  // 倒三角（图钉）+ 中间一点
		Polyline({FVector2f(X - S * 0.85f, Y - S * 0.6f), FVector2f(X + S * 0.85f, Y - S * 0.6f), FVector2f(X, Y + S), FVector2f(X - S * 0.85f, Y - S * 0.6f)}, C, Thick);
		DrawRect(C, X - S * 0.17f, Y - S * 0.3f, S * 0.34f, S * 0.34f);
		break;
	case EMechPing::Enemy:  // 实心菱形 + 四角括号
		Diamond(S * 0.55f, true);
		Brackets(X, Y, S, S * 0.42f, C, Thick);
		break;
	case EMechPing::Item:  // 六边形 + 中间小菱形
	{
		TArray<FVector2f> Hex;
		for (int32 k = 0; k <= 6; ++k)
		{
			const float A = PI / 6.f + k * PI / 3.f;
			Hex.Add(FVector2f(X + S * FMath::Cos(A), Y + S * FMath::Sin(A)));
		}
		Polyline(Hex, C, Thick);
		Diamond(S * 0.36f, true);
		break;
	}
	case EMechPing::Danger:  // 三角 + 「!」
		Polyline({FVector2f(X, Y - S), FVector2f(X + S * 0.98f, Y + S * 0.75f), FVector2f(X - S * 0.98f, Y + S * 0.75f), FVector2f(X, Y - S)}, C, Thick);
		DrawRect(C, X - S * 0.09f, Y - S * 0.38f, S * 0.18f, S * 0.6f);
		DrawRect(C, X - S * 0.09f, Y + S * 0.36f, S * 0.18f, S * 0.18f);
		break;
	case EMechPing::Attack:  // 两道向上的 V
		for (const float Off : {-0.42f, 0.32f})
		{
			SoftLine(X - S * 0.8f, Y + S * (Off + 0.45f), X, Y + S * (Off - 0.35f), C, Thick + 0.3f);
			SoftLine(X, Y + S * (Off - 0.35f), X + S * 0.8f, Y + S * (Off + 0.45f), C, Thick + 0.3f);
		}
		break;
	case EMechPing::Defend:  // 盾：平顶、尖底
		Polyline({FVector2f(X - S * 0.78f, Y - S * 0.82f), FVector2f(X + S * 0.78f, Y - S * 0.82f), FVector2f(X + S * 0.78f, Y + S * 0.05f),
			FVector2f(X, Y + S), FVector2f(X - S * 0.78f, Y + S * 0.05f), FVector2f(X - S * 0.78f, Y - S * 0.82f)}, C, Thick);
		DrawRect(C, X - S * 0.09f, Y - S * 0.5f, S * 0.18f, S * 0.95f);
		break;
	case EMechPing::Regroup:  // 一个圈 + 四个朝里的小三角
		Arc(X, Y, S * 0.38f, 0.f, 2.f * PI, C, Thick);
		for (int32 k = 0; k < 4; ++k)
		{
			const float A = k * HALF_PI;
			const FVector2D D(FMath::Sin(A), -FMath::Cos(A));
			const FVector2D Pp(-D.Y, D.X);
			const FVector2D Tip = FVector2D(X, Y) + D * S * 0.58f;
			const FVector2D Base = FVector2D(X, Y) + D * S * 1.02f;
			Triangle(Tip, Base + Pp * S * 0.26f, Base - Pp * S * 0.26f, C);
		}
		break;
	case EMechPing::Watch:  // 眼睛：上下两道弧 + 瞳孔
		Arc(X, Y + S * 0.75f, S * 1.25f, -0.9f, 0.9f, C, Thick);
		Arc(X, Y - S * 0.75f, S * 1.25f, PI - 0.9f, PI + 0.9f, C, Thick);
		Diamond(S * 0.26f, true);
		break;
	case EMechPing::EnemyWasHere:  // 空心菱形 + 下面两道短横（脚印 / 残影）
		Diamond(S * 0.7f, false);
		DrawRect(C, X - S * 0.12f, Y - S * 0.12f, S * 0.24f, S * 0.24f);
		DrawRect(MechHudPingLocal::Fade(C, 0.6f), X - S * 0.85f, Y + S * 0.95f, S * 0.6f, FMath::Max(1.f, S * 0.12f));
		DrawRect(MechHudPingLocal::Fade(C, 0.6f), X + S * 0.25f, Y + S * 0.95f, S * 0.6f, FMath::Max(1.f, S * 0.12f));
		break;
	case EMechPing::Help:  // 圈里一个十字
		Arc(X, Y, S * 0.95f, 0.f, 2.f * PI, C, Thick);
		DrawRect(C, X - S * 0.1f, Y - S * 0.55f, S * 0.2f, S * 1.1f);
		DrawRect(C, X - S * 0.55f, Y - S * 0.1f, S * 1.1f, S * 0.2f);
		break;
	default:
		DrawRect(C, X - S * 0.25f, Y - S * 0.25f, S * 0.5f, S * 0.5f);
		break;
	}
}

void AMechHUD::DrawPings(AMechCharacter* Mech)
{
	if (!PlayerOwner || !PlayerOwner->PlayerCameraManager || !Mech)
	{
		return;
	}
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	const FVector2D Center(W * 0.5f, H * 0.5f);
	const FVector Cam = PlayerOwner->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PlayerOwner->PlayerCameraManager->GetCameraRotation();
	const int32 MyTeam = Mech->GetTeam();
	const double Real = FPlatformTime::Seconds();
	TMap<TWeakObjectPtr<AMechPing>, double>& Seen = MechHudPingLocal::FirstSeen();
	for (auto It = Seen.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (TActorIterator<AMechPing> It(GetWorld()); It; ++It)
	{
		AMechPing* Ping = *It;
		if (Ping->IsActorBeingDestroyed() || !Ping->IsVisibleTo(PlayerOwner, MyTeam))
		{
			continue;
		}
		const EMechPing Kind = Ping->Kind;
		const bool bMine = Ping->IsMine(PlayerOwner);
		const float Local = float(Real - Seen.FindOrAdd(Ping, Real));
		const float Left = Ping->Life - Ping->GetAge();
		const float A = FMath::Clamp(Left / 1.f, 0.f, 1.f) * FMath::Clamp(Local / 0.08f, 0.f, 1.f);
		if (A <= 0.f)
		{
			continue;
		}
		// 需要支援标在自己身上：自己屏幕上不画（第三人称会盖在自己机体上），地图上照样有
		if (Kind == EMechPing::Help && Ping->Target == Mech)
		{
			continue;
		}
		const FLinearColor Base = MechPing::Color(Kind);
		const bool bPlace = MechPing::IsPlace(Kind) || Kind == EMechPing::Help;
		const bool bPod = Cast<AMechPluginPod>(Ping->Target) != nullptr;
		FVector At = Ping->GetPoint();
		if (bPod)
		{
			At += FVector(0.f, 0.f, 120.f);  // 和插件舱自己的六边形标记同一个位置（DrawPods），括号套在它外面
		}
		const float Lift = bPlace ? 34.f * UI : 0.f;
		const float S = 13.f * UI;
		const float Pad = 48.f * UI;
		const FVector LocalDir = CamRot.UnrotateVector(At - Cam);
		FVector2D P = FVector2D::ZeroVector;
		const bool bOn = LocalDir.X > 0.f && UGameplayStatics::ProjectWorldToScreen(PlayerOwner, At, P) && P.X > Pad && P.X < W - Pad && P.Y > Pad + Lift && P.Y < H - Pad;
		const float Meters = FVector::Dist(At, Mech->GetActorLocation()) / 100.f;
		const FString Dist = FString::Printf(TEXT("%.0fm"), Meters);
		if (!bOn)
		{
			// 屏幕外：贴边（和插件舱同一个算法），图标外侧一个小三角指向它
			FVector2D D(LocalDir.Y, -LocalDir.Z);
			if (!D.Normalize())
			{
				D = FVector2D(0.f, 1.f);
			}
			const float SX = (W * 0.5f - Pad) / FMath::Max(FMath::Abs(D.X), 0.001f);
			const float SY = (H * 0.5f - Pad) / FMath::Max(FMath::Abs(D.Y), 0.001f);
			P = Center + D * FMath::Min(SX, SY);
			const FLinearColor C = MechHudPingLocal::Fade(Base, 0.9f * A);
			PingGlyph(static_cast<uint8>(Kind), P.X, P.Y, S * 0.85f, C, 1.5f);
			const FVector2D Tip = P + D * (S + 10.f * UI);
			const FVector2D Bk = P + D * (S + 4.f * UI);
			const FVector2D Pp(-D.Y, D.X);
			Triangle(Tip, Bk + Pp * 4.f * UI, Bk - Pp * 4.f * UI, C);
			Text(Dist, P.X, P.Y + S + 3.f * UI, 12.f, MechHudPingLocal::Fade(Base, 0.8f * A), 0.5f);
			continue;
		}
		const FVector2D Icon(P.X, P.Y - Lift);
		const bool bNear = FVector2D::Distance(Icon, Center) < 90.f * UI;
		const float Pop = 1.f + 0.5f * FMath::Clamp(1.f - Local / 0.15f, 0.f, 1.f);
		// 刚放下：一圈扩散
		if (Local < 0.6f)
		{
			const float T = Local / 0.6f;
			Arc(Icon.X, Icon.Y, S * (1.3f + 2.6f * T), 0.f, 2.f * PI, MechHudPingLocal::Fade(Base, (1.f - T) * 0.8f * A), 1.5f);
		}
		if (bPlace && Kind != EMechPing::Help)
		{
			// 图钉：地面上一个小菱形，细杆连到图标
			const float G = 3.f * UI;
			Triangle(FVector2D(P.X, P.Y - G), FVector2D(P.X + G, P.Y), FVector2D(P.X - G, P.Y), MechHudPingLocal::Fade(Base, 0.8f * A));
			Triangle(FVector2D(P.X, P.Y + G), FVector2D(P.X + G, P.Y), FVector2D(P.X - G, P.Y), MechHudPingLocal::Fade(Base, 0.8f * A));
			DrawLine(P.X, P.Y - G, P.X, Icon.Y + S, MechHudPingLocal::Fade(Base, 0.45f * A), 1.f);
		}
		float GlyphS = S * Pop;
		FLinearColor GC = MechHudPingLocal::Fade(Base, A);
		if (Kind == EMechPing::Enemy)
		{
			GC = MechHudPingLocal::Fade(Base, A * (0.75f + 0.25f * FMath::Sin(float(Real) * 6.f)));  // 敌人：慢慢呼吸（和锁定框分开）
		}
		if (Kind == EMechPing::Item && (bPod || Ping->IsCore()))
		{
			// 物品：套在插件舱标记 / 核心外面的一圈金色括号
			Brackets(Icon.X, Icon.Y, 22.f * UI * Pop, 7.f * UI, GC, 1.5f);
			GlyphS = 0.f;
		}
		if (GlyphS > 0.f)
		{
			PingGlyph(static_cast<uint8>(Kind), Icon.X, Icon.Y, GlyphS, GC, 1.5f);
		}
		// 图标右边一列：词（刚放下的 2.5 秒、或者准星压在附近时才有）、距离、队友标的再写名字（放图标下面会和图钉的细杆叠在一起）
		FString Word;
		if (Local < 2.5f || bNear)
		{
			Word = MechPing::Word(Kind);
			if (Kind == EMechPing::Item)
			{
				Word = Ping->IsCore() ? TEXT("核心") : TEXT("插件");
			}
		}
		const float WordA = bNear ? 1.f : FMath::Clamp((2.5f - Local) / 0.4f, 0.f, 1.f);
		const float ColX = Icon.X + (GlyphS > 0.f ? S : 22.f * UI) + 6.f * UI;
		const bool bName = !bMine && !Ping->ByName.IsEmpty();
		// 整列按行数竖直居中在图标上
		const float RowH = 14.f * UI;
		const int32 Rows = (Word.IsEmpty() ? 0 : 1) + (bPod ? 0 : 1) + (bName ? 1 : 0);  // 插件舱自己已经写了距离
		float TY = Icon.Y - Rows * RowH * 0.5f;
		if (!Word.IsEmpty())
		{
			Text(Word, ColX, TY, 13.f, MechHudPingLocal::Fade(Base, A * WordA));
			TY += RowH;
		}
		if (!bPod)
		{
			Text(Dist, ColX, TY, 12.f, MechHudPingLocal::Fade(Base, 0.85f * A));
			TY += RowH;
		}
		if (bName)
		{
			Text(Ping->ByName, ColX, TY, 11.f, MechHudPingLocal::Fade(Th.Dim, 0.9f * A));
		}
	}
}

void AMechHUD::DrawPingWheel()
{
	const UMechCommsComponent* Comms = MechComms::Get(PlayerOwner);
	if (!Comms || !Comms->IsWheelOpen() || !Canvas)
	{
		return;
	}
	const FMechHudTheme& Th = MechHudTheme::Get();
	const float CX = Canvas->SizeX * 0.5f;
	const float CY = Canvas->SizeY * 0.5f;
	const float Open = FMath::Clamp(Comms->GetWheelAge() / 0.09f, 0.f, 1.f);
	const float R = 124.f * UI * (0.85f + 0.15f * Open);
	const float A = Open;
	const int32 Sel = Comms->GetWheelSlot();

	// 标点会落在哪：打开那一刻准星指着的地方，一个小圈 + 点
	FVector2D WP;
	if (PlayerOwner && UGameplayStatics::ProjectWorldToScreen(PlayerOwner, Comms->GetWheelPoint(), WP))
	{
		Arc(WP.X, WP.Y, 7.f * UI, 0.f, 2.f * PI, MechHudPingLocal::Fade(Th.Accent, 0.8f * A), 1.5f);
		DrawRect(MechHudPingLocal::Fade(Th.Accent, 0.9f * A), WP.X - 1.5f * UI, WP.Y - 1.5f * UI, 3.f * UI, 3.f * UI);
	}

	// 暗底圆盘（扇形三角形拼）+ 外圈 / 内圈细线
	{
		TArray<FCanvasUVTri> Tris;
		constexpr int32 Seg = 48;
		const float RO = R * 1.3f;
		for (int32 i = 0; i < Seg; ++i)
		{
			const float A0 = 2.f * PI * i / Seg, A1 = 2.f * PI * (i + 1) / Seg;
			FCanvasUVTri T;
			T.V0_Pos = FVector2D(CX, CY);
			T.V1_Pos = FVector2D(CX + FMath::Sin(A0) * RO, CY - FMath::Cos(A0) * RO);
			T.V2_Pos = FVector2D(CX + FMath::Sin(A1) * RO, CY - FMath::Cos(A1) * RO);
			T.V0_Color = MechHudPingLocal::Fade(Th.Shade, 0.6f * A);
			T.V1_Color = T.V2_Color = MechHudPingLocal::Fade(Th.Shade, 0.45f * A);
			Tris.Add(T);
		}
		AddTris(Tris);
	}
	Arc(CX, CY, R * 1.3f, 0.f, 2.f * PI, MechHudPingLocal::Fade(Th.Faint, 0.9f * A), 1.f);
	Arc(CX, CY, R * 0.4f, 0.f, 2.f * PI, MechHudPingLocal::Fade(Th.Faint, 0.9f * A), 1.f);
	const float Step = 2.f * PI / MechPing::WheelSlots;
	for (int32 i = 0; i < MechPing::WheelSlots; ++i)
	{
		// 格与格之间的短刻度
		const float B = (i + 0.5f) * Step;
		DrawLine(CX + FMath::Sin(B) * R * 1.12f, CY - FMath::Cos(B) * R * 1.12f, CX + FMath::Sin(B) * R * 1.3f, CY - FMath::Cos(B) * R * 1.3f, MechHudPingLocal::Fade(Th.Faint, A), 1.f);
	}
	for (int32 i = 0; i < MechPing::WheelSlots; ++i)
	{
		const EMechPing Kind = MechPing::WheelItem(i);
		const float Ang = i * Step;
		const bool bSel = i == Sel;
		const FLinearColor C = MechPing::Color(Kind);
		const float GR = R * (bSel ? 0.9f : 0.84f);
		const float GX = CX + FMath::Sin(Ang) * GR;
		const float GY = CY - FMath::Cos(Ang) * GR;
		if (bSel)
		{
			Arc(CX, CY, R * 1.3f, Ang - Step * 0.5f + 0.05f, Ang + Step * 0.5f - 0.05f, MechHudPingLocal::Fade(C, A), 3.f);
		}
		PingGlyph(static_cast<uint8>(Kind), GX, GY, (bSel ? 17.f : 13.f) * UI, MechHudPingLocal::Fade(C, (bSel ? 1.f : 0.7f) * A), bSel ? 2.f : 1.5f);
	}
	// 正中：选中项的名字；没选（在正中松开 = 取消）只有一个点
	const FVector2D Cur = Comms->GetWheelCursor();
	const float CurLen = FMath::Clamp(Cur.Size() / UMechCommsComponent::WheelRange, 0.f, 1.f);
	const FVector2D CurDir = Cur.IsNearlyZero() ? FVector2D::ZeroVector : Cur.GetSafeNormal();
	const FVector2D Dot(CX + CurDir.X * CurLen * R * 0.36f, CY - CurDir.Y * CurLen * R * 0.36f);
	DrawRect(MechHudPingLocal::Fade(Th.Ink, 0.9f * A), Dot.X - 2.f * UI, Dot.Y - 2.f * UI, 4.f * UI, 4.f * UI);
	if (Sel >= 0)
	{
		const EMechPing Kind = MechPing::WheelItem(Sel);
		const FString L = MechPing::Label(Kind);
		Text(L, CX, CY + R * 0.4f + 6.f * UI, 18.f, MechHudPingLocal::Fade(Th.Ink, A), 0.5f);
	}
}

void AMechHUD::GatherPingIcons(AMechCharacter* Mech, TArray<FMechMapIcon>& Out)
{
	if (!PlayerOwner)
	{
		return;
	}
	const int32 MyTeam = Mech ? Mech->GetTeam() : MechComms::TeamOf(PlayerOwner);
	for (TActorIterator<AMechPing> It(GetWorld()); It; ++It)
	{
		if (It->IsActorBeingDestroyed() || !It->IsVisibleTo(PlayerOwner, MyTeam))
		{
			continue;
		}
		FMechMapIcon& I = Out.AddDefaulted_GetRef();
		I.Type = FMechMapIcon::Ping;
		I.At = FVector2D(It->GetPoint());
		I.Color = MechPing::Color(It->Kind);
		I.Alpha = FMath::Clamp(It->Life - It->GetAge(), 0.f, 1.f);
		I.PingKind = static_cast<uint8>(It->Kind);
		I.bEdge = true;
	}
}
