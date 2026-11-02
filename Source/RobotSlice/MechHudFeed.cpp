// 信息区 HUD（11-01，AMechHUD::DrawFeed / FeedIcon，接管原来右上角的击毁通报）：照 CS2 的击杀栏，但按这套 HUD 的规矩——
//  右上角，右对齐，一条一行，新的在最上面（旧行平滑往下挪），最多 6 行；击毁留 7 秒（和自己有关的 10 秒），别的 4.5 秒，最后 0.6 秒淡出。
//  图标为主、字为辅：名字用敌我色（个人死斗：自己主色、别人红），中间一个小图标（武器 / 技能 = 真实模型的剪影，插件六边形、标点图形、核心是几何图形），
//  后面只跟一个很短的灰色词（EMP、地雷、进攻……），击毁不写武器名。
//  自己击毁的那行：一层淡底 + 主色细框；自己被击毁的那行：淡底 + 红色细框（CS2 的做法，一眼找到自己）。
//  不画底板、不写句子；设置里关掉「信息区」（FMechSettings::bShowFeed）整块不画（告警不受影响）。

#include "MechHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "MechCharacter.h"
#include "MechComms.h"
#include "MechFeed.h"
#include "MechGameMode.h"
#include "MechGameState.h"
#include "MechHudTheme.h"
#include "MechPing.h"
#include "MechSettings.h"
#include "Styling/SlateBrush.h"

// 带名字的命名空间：合并编译时不和 MechHUD.cpp 里的同名小工具打架（调用一律带前缀）
namespace MechHudFeedLocal
{
	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	/** 一行能留多久 */
	float LifeOf(const FMechFeedEvent& E, bool bSelf)
	{
		switch (E.Kind)
		{
		case EMechFeed::Kill: return bSelf ? 10.f : 7.f;
		case EMechFeed::CorePick:
		case EMechFeed::CoreScore: return 6.f;
		case EMechFeed::Wave: return 5.f;
		default: return 4.5f;
		}
	}

	/** 剪影显示的高度（按 UI 缩放前的像素）：1080p 下 18 像素，4K 下 36 像素（贴图高 64，都是缩小，够清楚） */
	constexpr float SilhouetteHeight = 18.f;

	/** 武器剪影（11-01，用户：「关于击杀播报，请你在武器显示部分使用武器真正的黑白底图」）：每件武器真实模型的纯白剪影，透明底，枪口 / 刀尖朝右，
	 *  HUD 按主题色染色。贴图 /Game/UI/WeaponIcons/T_WI_<名字>，出图流程 tools/make_weapon_icons.sh（模型换了重跑，这里不用改）。
	 *  按路径加载：这件还没有模型（Brush 为空）就退回下面 FeedIcon 的几何小图标。进程里只加载一次、钉住不让回收（只有二十来张很小的图） */
	struct FSilhouette
	{
		TSharedPtr<FSlateBrush> Brush;
		float Aspect = 1.f;
	};
	const FSilhouette& Silhouette(uint8 Icon)
	{
		static const TCHAR* Names[MechFeedIcon::Blast] = {
			TEXT("Rifle"), TEXT("SMG"), TEXT("Shotgun"), TEXT("Marksman"), TEXT("Sniper"), TEXT("DualPistol"),                                  // EMechShooter
			TEXT("Katana"), TEXT("QuickBlade"), TEXT("OneHand"), TEXT("GreatSword"), TEXT("TwinBlades"), TEXT("Blunt"), TEXT("Dagger"),         // EMechMeleeWeapon
			TEXT("Missiles"), TEXT("Artillery"), TEXT("Shield"), TEXT("Grapple"), TEXT("Net"), TEXT("Smoke"), TEXT("Emp"),                      // EMechShoulder
			TEXT("Burst"), TEXT("Launch"), TEXT("Shockwave"), TEXT("Mine")};
		static const FSilhouette None;
		// 不析构（退出时 UObject 系统已经关了）
		static FSilhouette* Cache = new FSilhouette[MechFeedIcon::Blast];
		static bool bTried[MechFeedIcon::Blast] = {};
		if (Icon >= MechFeedIcon::Blast)
		{
			return None;
		}
		if (!bTried[Icon])
		{
			bTried[Icon] = true;
			const FString Path = FString::Printf(TEXT("/Game/UI/WeaponIcons/T_WI_%s.T_WI_%s"), Names[Icon], Names[Icon]);
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Tex->AddToRoot();
				const TSharedRef<FSlateBrush> Brush = MakeShared<FSlateBrush>();
				Brush->SetResourceObject(Tex);
				// 按导入尺寸（不用 GetSizeX：编辑器里贴图还在异步编译时那是占位图的尺寸，比例会错、剪影被压扁）
				const FIntPoint Size = Tex->GetImportedSize();
				Brush->ImageSize = FVector2D(Size);
				Brush->DrawAs = ESlateBrushDrawType::Image;
				Cache[Icon].Brush = Brush;
				Cache[Icon].Aspect = float(Size.X) / FMath::Max(Size.Y, 1);
			}
		}
		return Cache[Icon];
	}

	/** 武器 / 技能小图标的宽度（按 UI 缩放前的像素）：有剪影按贴图比例（手枪窄、长枪宽）；几何小图标是枪和刀横着的长条，别的方的 */
	float IconWidth(uint8 Icon)
	{
		const FSilhouette& Sil = Silhouette(Icon);
		return Sil.Brush.IsValid() ? SilhouetteHeight * Sil.Aspect : Icon < MechFeedIcon::Shoulder0 ? 34.f : 19.f;
	}

	/** 一行里的一段：字或者图标 */
	struct FPart
	{
		enum EType : uint8 { Word, Weapon, Plugin, Ping, Core, Wave, Post };
		EType Type = Word;
		FString Text;
		float Size = 14.f;
		FLinearColor Color = FLinearColor::White;
		uint8 Icon = 0;
		FName PluginId;
		float W = 0.f;
	};
}

void AMechHUD::FeedIcon(uint8 Icon, float X, float Y, float S, const FLinearColor& C)
{
	// 有真实模型的剪影就画剪影（白色贴图 × 颜色）；爆炸、场地、杂兵、还没有模型的肩部装备走下面的几何小图标
	const MechHudFeedLocal::FSilhouette& Sil = MechHudFeedLocal::Silhouette(Icon);
	if (Sil.Brush.IsValid())
	{
		const float H = MechHudFeedLocal::SilhouetteHeight * UI;
		const float W = H * Sil.Aspect;
		// 左上角对齐整像素：缩小的剪影边缘才利落
		const float X0 = FMath::RoundToFloat(X - W * 0.5f);
		const float Y0 = FMath::RoundToFloat(Y - H * 0.5f);
		const FVector2f A(X0, Y0), B(X0 + W, Y0), D(X0 + W, Y0 + H), E(X0, Y0 + H);
		const TArray<FVector2f> UVs = {FVector2f(0.f, 0.f), FVector2f(1.f, 0.f), FVector2f(1.f, 1.f), FVector2f(0.f, 0.f), FVector2f(1.f, 1.f), FVector2f(0.f, 1.f)};
		// 先压一层淡影子（和 Text 一样：亮天空、爆炸上也看得清），再画剪影
		const FVector2f O(FMath::Max(1.f, UI), FMath::Max(1.f, UI));
		const FLinearColor Shadow(0.f, 0.f, 0.f, C.A * 0.6f);
		AddImageTris(Sil.Brush, {A + O, B + O, D + O, A + O, D + O, E + O}, UVs, {Shadow, Shadow, Shadow, Shadow, Shadow, Shadow});
		AddImageTris(Sil.Brush, {A, B, D, A, D, E}, UVs, {C, C, C, C, C, C});
		return;
	}
	// 坐标按 u（= S / 6）写：横条图标约 28u 宽、12u 高，方图标约 14u；枪口 / 刀尖朝右（击毁者在左、被击毁的在右）
	const float u = S / 6.f;
	auto R = [&](float x, float y, float w, float h) { DrawRect(C, X + x * u, Y + y * u, w * u, h * u); };
	auto L = [&](float x0, float y0, float x1, float y1, float T = 1.5f) { SoftLine(X + x0 * u, Y + y0 * u, X + x1 * u, Y + y1 * u, C, T); };
	auto Tri = [&](float ax, float ay, float bx, float by, float cx, float cy) {
		Triangle(FVector2D(X + ax * u, Y + ay * u), FVector2D(X + bx * u, Y + by * u), FVector2D(X + cx * u, Y + cy * u), C);
	};
	if (Icon < MechFeedIcon::Melee0)
	{
		switch (static_cast<EMechShooter>(Icon - MechFeedIcon::Shooter0))
		{
		case EMechShooter::Rifle:  // 连射步枪：枪托、机匣、枪管、弹匣、瞄具
			R(-13.f, -2.f, 4.f, 4.5f); R(-9.f, -3.f, 12.f, 5.f); R(3.f, -2.f, 10.f, 2.f); R(-3.f, 2.f, 3.f, 5.f); R(-6.f, -5.f, 4.f, 1.5f);
			break;
		case EMechShooter::SMG:  // 冲锋枪：短、长弹匣、细枪托
			R(-12.f, -2.f, 4.f, 1.5f); R(-8.f, -3.f, 10.f, 5.f); R(2.f, -2.f, 6.f, 2.f); R(-2.f, 2.f, 3.f, 7.f); R(-7.f, 2.f, 2.5f, 4.f);
			break;
		case EMechShooter::Shotgun:  // 霰弹枪：粗枪管 + 泵动护木
			R(-13.f, -1.f, 4.f, 5.f); R(-9.f, -3.f, 10.f, 5.f); R(1.f, -3.f, 12.f, 3.f); R(2.f, 0.5f, 7.f, 2.5f);
			break;
		case EMechShooter::Marksman:  // 充能步枪：枪管上两道线圈
			R(-13.f, -2.f, 4.f, 4.f); R(-9.f, -3.f, 10.f, 5.f); R(1.f, -1.5f, 12.f, 2.f); R(4.f, -4.f, 1.3f, 5.f); R(7.5f, -4.f, 1.3f, 5.f);
			break;
		case EMechShooter::Sniper:  // 狙击枪：细长枪管 + 瞄准镜
			R(-14.f, -1.f, 5.f, 4.f); R(-9.f, -2.f, 9.f, 4.f); R(0.f, -1.f, 14.f, 1.5f); R(-7.f, -6.f, 7.f, 2.5f); R(-5.f, -3.5f, 1.f, 1.5f); R(-2.f, -3.5f, 1.f, 1.5f);
			break;
		case EMechShooter::DualPistol:  // 双持手枪：一前一后两把
			R(-10.f, -5.f, 8.f, 2.5f); R(-10.f, -2.5f, 2.5f, 4.f); R(-1.f, -1.f, 8.f, 2.5f); R(-1.f, 1.5f, 2.5f, 4.f);
			break;
		}
		return;
	}
	if (Icon < MechFeedIcon::Shoulder0)
	{
		switch (static_cast<EMechMeleeWeapon>(Icon - MechFeedIcon::Melee0))
		{
		case EMechMeleeWeapon::Katana:  // 双手刀：长柄、护手、略弯的长刃
			R(-14.f, 1.5f, 7.f, 2.f); R(-7.f, -1.f, 1.5f, 6.f); L(-5.5f, 2.5f, 13.f, 0.f, 2.f);
			break;
		case EMechMeleeWeapon::QuickBlade:  // 快刀：短刃
			R(-11.f, 1.5f, 5.f, 2.f); R(-6.f, -0.5f, 1.5f, 5.f); L(-4.5f, 2.5f, 9.f, 1.f, 2.f);
			break;
		case EMechMeleeWeapon::OneHand:  // 单手刀：短柄、中刃
			R(-10.f, 1.5f, 4.f, 2.f); R(-6.f, -1.f, 1.5f, 6.f); L(-4.5f, 2.5f, 11.f, 0.5f, 2.f);
			break;
		case EMechMeleeWeapon::GreatSword:  // 大剑：宽刃 + 大护手
			R(-14.f, 1.f, 5.f, 2.5f); R(-9.f, -3.f, 2.f, 9.f);
			Tri(-7.f, -1.5f, 11.f, -0.5f, 11.f, 3.f); Tri(-7.f, -1.5f, 11.f, 3.f, -7.f, 4.f); Tri(11.f, -0.5f, 14.f, 1.25f, 11.f, 3.f);
			break;
		case EMechMeleeWeapon::TwinBlades:  // 双刀：交叉的两把
			L(-11.f, 4.f, 10.f, -4.f, 1.8f); L(-11.f, -4.f, 10.f, 4.f, 1.8f); R(-8.f, -0.5f, 1.2f, 4.f);
			break;
		case EMechMeleeWeapon::Blunt:  // 钝器：长柄 + 锤头
			L(-12.f, 4.f, 6.f, -0.5f, 2.f); R(4.f, -5.f, 6.f, 8.f);
			break;
		case EMechMeleeWeapon::Dagger:  // 投掷匕首：小匕首 + 后面三道速度线
			R(-4.f, 1.f, 3.f, 2.f); R(-1.f, -1.f, 1.2f, 5.f); L(0.5f, 2.f, 8.f, 1.5f, 1.8f);
			R(-13.f, -2.f, 5.f, 0.8f); R(-12.f, 1.5f, 6.f, 0.8f); R(-13.f, 4.5f, 5.f, 0.8f);
			break;
		}
		return;
	}
	if (Icon < MechFeedIcon::Blast)
	{
		// 肩部装备：和仪表盘上的图标同一套形状（MechHUD.cpp DrawDash），挪到以中心为原点
		switch (static_cast<EMechShoulder>(Icon - MechFeedIcon::Shoulder0))
		{
		case EMechShoulder::Missiles:  // 导弹：弹体 + 尖头 + 尾翼
			R(-6.f, -1.5f, 9.f, 3.f); Tri(3.f, -1.5f, 7.f, 0.f, 3.f, 1.5f); Tri(-6.f, -1.5f, -3.f, -1.5f, -8.f, -4.5f); Tri(-6.f, 1.5f, -3.f, 1.5f, -8.f, 4.5f);
			break;
		case EMechShoulder::Artillery:  // 火炮：方炮尾 + 长炮管
			R(-7.f, -1.f, 6.f, 7.f); L(-1.f, 2.f, 8.f, -3.5f, 1.8f);
			break;
		case EMechShoulder::Shield:  // 护盾：朝前的弧 + 一点
			Arc(X - 3.f * u, Y, 8.f * u, -0.95f, 0.95f, C, 1.8f); R(-4.f, -1.f, 2.f, 2.f);
			break;
		case EMechShoulder::Grapple:  // 钩爪：杆 + 张开的两根爪指
			L(0.f, 6.5f, 0.f, -1.f); L(0.f, -1.f, -5.f, -6.f); L(0.f, -1.f, 5.f, -6.f);
			break;
		case EMechShoulder::Net:  // 捕获网：菱形 + 交叉
			L(0.f, -6.5f, 6.5f, 0.f, 1.2f); L(6.5f, 0.f, 0.f, 6.5f, 1.2f); L(0.f, 6.5f, -6.5f, 0.f, 1.2f); L(-6.5f, 0.f, 0.f, -6.5f, 1.2f);
			L(-3.2f, -3.2f, 3.2f, 3.2f, 1.f); L(-3.2f, 3.2f, 3.2f, -3.2f, 1.f);
			break;
		case EMechShoulder::Smoke:  // 烟雾弹：三个小圈
			Arc(X - 3.f * u, Y + 2.5f * u, 3.5f * u, 0.f, 2.f * PI, C, 1.2f); Arc(X + 3.f * u, Y + 2.5f * u, 3.5f * u, 0.f, 2.f * PI, C, 1.2f);
			Arc(X, Y - 2.5f * u, 3.5f * u, 0.f, 2.f * PI, C, 1.2f);
			break;
		case EMechShoulder::Emp:  // EMP：闪电
			L(2.f, -6.5f, -4.f, 0.5f, 1.6f); L(-4.f, 0.5f, 3.f, -0.5f, 1.6f); L(3.f, -0.5f, -3.f, 6.5f, 1.6f);
			break;
		case EMechShoulder::Burst:  // 紧急脱离：两道 »（和仪表盘上的图标同形）
			L(-6.f, -5.f, -1.f, 0.f); L(-1.f, 0.f, -6.f, 5.f); L(-0.5f, -5.f, 4.5f, 0.f); L(4.5f, 0.f, -0.5f, 5.f);
			break;
		case EMechShoulder::Launch:  // 弹射升空：朝上的三角 + 地面线
			Tri(0.f, -6.f, 4.5f, 1.f, -4.5f, 1.f); R(-6.f, 4.f, 12.f, 1.5f);
			break;
		case EMechShoulder::Shockwave:  // 震荡波：一点 + 一圈 + 两段外弧
			R(-1.f, -1.f, 2.f, 2.f); Arc(X, Y, 3.3f * u, 0.f, 2.f * PI, C, 1.2f); Arc(X, Y, 6.f * u, -0.9f, 0.9f, C, 1.2f); Arc(X, Y, 6.f * u, PI - 0.9f, PI + 0.9f, C, 1.2f);
			break;
		case EMechShoulder::Mine:  // 地雷：扁梯形 + 顶上小灯
			L(-6.f, 3.f, 6.f, 3.f); L(-6.f, 3.f, -3.5f, 0.f); L(6.f, 3.f, 3.5f, 0.f); L(-3.5f, 0.f, 3.5f, 0.f); R(-1.f, -3.5f, 2.f, 2.f);
			break;
		}
		return;
	}
	switch (Icon)
	{
	case MechFeedIcon::Blast:  // 爆炸：一点 + 八道放射线
	case MechFeedIcon::GruntBoom:
		R(-1.2f, -1.2f, 2.4f, 2.4f);
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = k * PI / 4.f;
			const float r0 = 3.f, r1 = k % 2 == 0 ? 7.f : 5.5f;
			L(FMath::Sin(A) * r0, -FMath::Cos(A) * r0, FMath::Sin(A) * r1, -FMath::Cos(A) * r1, 1.3f);
		}
		break;
	case MechFeedIcon::Field:  // 场地：往下的箭头落到地面线上
		L(0.f, -6.f, 0.f, 1.5f); Tri(0.f, 4.f, 3.f, 0.5f, -3.f, 0.5f); R(-6.f, 5.f, 12.f, 1.5f);
		break;
	case MechFeedIcon::GruntGun:  // 机枪无人机：机身 + 两个旋翼 + 一串子弹
		R(-4.f, -1.f, 6.f, 3.f); L(-7.f, -3.f, -2.f, -3.f, 1.2f); L(0.f, -3.f, 5.f, -3.f, 1.2f); R(4.f, 0.f, 1.5f, 1.f); R(6.5f, 0.f, 1.5f, 1.f);
		break;
	case MechFeedIcon::GruntShell:  // 炮车 / 迫击炮：炮弹
		R(-5.f, -2.f, 7.f, 4.f); Tri(2.f, -2.f, 6.f, 0.f, 2.f, 2.f); R(-3.f, -2.5f, 1.f, 5.f);
		break;
	default:
		R(-2.f, -2.f, 4.f, 4.f);
		break;
	}
}

void AMechHUD::DrawFeed()
{
	using MechHudFeedLocal::FPart;
	if (!FMechSettings::Get().bShowFeed || !Canvas)
	{
		return;
	}
	UMechCommsComponent* Comms = MechComms::Get(PlayerOwner);
	if (!Comms)
	{
		return;
	}
	TArray<FMechFeedLine>& Lines = Comms->GetLines();
	const FMechHudTheme& Th = MechHudTheme::Get();
	const AMechGameState* GS = GetWorld()->GetGameState<AMechGameState>();
	const float Now = GetWorld()->GetTimeSeconds();
	const float Dt = GetWorld()->GetDeltaSeconds();
	const AMechCharacter* Focus = GetFocusMech();
	const AMechPlayerController* MPC = Cast<AMechPlayerController>(PlayerOwner);
	const FString Me = Focus ? Focus->DisplayName : MPC ? MPC->GetMenuName() : FString();
	const int32 MyTeam = Focus ? Focus->GetTeam() : MechComms::TeamOf(PlayerOwner);
	const bool bSolo = GS && GS->IsSoloDM();
	// 名字的颜色：自己 = 主色；个人死斗里别人都是敌人；团队按敌我（不按蓝红：自己那队永远是队友色）；没有队伍（场地、波次）灰
	auto NameColor = [&](const FString& N, int32 Team) {
		if (!Me.IsEmpty() && N == Me)
		{
			return Th.Accent;
		}
		if (Team < 0)
		{
			return Th.Dim;
		}
		return bSolo ? Th.Enemy : Team == MyTeam ? Th.Ally : Th.Enemy;
	};
	auto IsSelf = [&](const FMechFeedEvent& E) { return E.Kind == EMechFeed::Kill && !Me.IsEmpty() && (E.A == Me || E.B == Me); };

	// 还没过期的，新的在上，最多 6 行
	TArray<FMechFeedLine*> Shown;
	for (int32 i = Lines.Num() - 1; i >= 0 && Shown.Num() < 6; --i)
	{
		if (Now - Lines[i].Time < MechHudFeedLocal::LifeOf(Lines[i].E, IsSelf(Lines[i].E)))
		{
			Shown.Add(&Lines[i]);
		}
	}
	const float Right = Canvas->SizeX - 36.f * UI;
	const float Top = 30.f * UI;
	const float RowH = 29.f * UI;
	const float Gap = 6.f * UI;
	for (int32 Row = 0; Row < Shown.Num(); ++Row)
	{
		FMechFeedLine& Line = *Shown[Row];
		const FMechFeedEvent& E = Line.E;
		// 行位置平滑：新行进来时旧行往下挪（不是一下跳下去）
		Line.Slot = Line.Slot < 0.f ? float(Row) : FMath::FInterpTo(Line.Slot, float(Row), Dt, 14.f);
		const float Age = Now - Line.Time;
		const bool bSelf = IsSelf(E);
		const float Life = MechHudFeedLocal::LifeOf(E, bSelf);
		const float A = FMath::Clamp((Life - Age) / 0.6f, 0.f, 1.f) * FMath::Clamp(Age / 0.12f, 0.f, 1.f);
		const float In = 1.f - FMath::Clamp(Age / 0.18f, 0.f, 1.f);
		const float RX = Right + In * In * 30.f * UI;  // 从右边滑进来
		const float Y = Top + Line.Slot * RowH;
		const float CY = Y + 11.f * UI;  // 这一行的中线（16 号字的中间）

		// 组一行：名字 [图标] 名字 / 短词
		TArray<FPart> Parts;
		auto AddWord = [&](const FString& S, float Size, const FLinearColor& C) {
			if (S.IsEmpty())
			{
				return;
			}
			FPart& P = Parts.AddDefaulted_GetRef();
			P.Type = FPart::Word;
			P.Text = S;
			P.Size = Size;
			P.Color = C;
		};
		auto AddIcon = [&](FPart::EType Type, uint8 Icon, const FLinearColor& C, float W) {
			FPart& P = Parts.AddDefaulted_GetRef();
			P.Type = Type;
			P.Icon = Icon;
			P.Color = C;
			P.W = W * UI;
		};
		const FLinearColor IconC = Th.Ink;
		switch (E.Kind)
		{
		case EMechFeed::Kill:
			AddWord(E.A, 16.f, NameColor(E.A, E.TeamA));
			AddIcon(FPart::Weapon, E.Icon, IconC, MechHudFeedLocal::IconWidth(E.Icon));
			AddWord(E.B, 16.f, NameColor(E.B, E.TeamB));
			break;
		case EMechFeed::Skill:
			AddWord(E.A, 16.f, NameColor(E.A, E.TeamA));
			AddIcon(FPart::Weapon, E.Icon, IconC, MechHudFeedLocal::IconWidth(E.Icon));
			AddWord(MechFeedIcon::Word(E.Icon), 13.f, Th.Dim);
			break;
		case EMechFeed::Plugin:
		{
			AddWord(E.A, 16.f, NameColor(E.A, E.TeamA));
			FPart& P = Parts.AddDefaulted_GetRef();
			P.Type = FPart::Plugin;
			P.PluginId = E.Plugin;
			P.W = 21.f * UI;
			break;
		}
		case EMechFeed::CorePick:
		case EMechFeed::CoreScore:
			AddWord(E.A, 16.f, NameColor(E.A, E.TeamA));
			AddIcon(FPart::Core, 0, Th.Gold, 19.f);
			AddWord(E.Kind == EMechFeed::CorePick ? TEXT("拿起") : TEXT("交芯"), 13.f, E.Kind == EMechFeed::CoreScore ? Th.Gold : Th.Dim);
			break;
		case EMechFeed::Ping:
		{
			const EMechPing K = static_cast<EMechPing>(E.Icon);
			AddWord(E.A, 16.f, NameColor(E.A, E.TeamA));
			AddIcon(FPart::Ping, E.Icon, MechPing::Color(K), 21.f);
			AddWord(K == EMechPing::Item ? (E.Num > 0 ? TEXT("核心") : TEXT("插件")) : MechPing::Word(K), 13.f, Th.Dim);
			break;
		}
		case EMechFeed::Wave:
			AddIcon(FPart::Wave, 0, Th.Warn, 22.f);
			AddWord(FString::Printf(TEXT("第 %d 波"), E.Num), 14.f, Th.Warn);
			break;
		case EMechFeed::PostHit:
			AddIcon(FPart::Post, 0, Th.Enemy, 16.f);
			AddWord(FString::Printf(TEXT("%d%%"), E.Num), 13.f, Th.Enemy);
			break;
		}
		float Total = 0.f;
		for (FPart& P : Parts)
		{
			if (P.Type == FPart::Word)
			{
				P.W = Measure(P.Text, P.Size).X;
			}
			Total += P.W;
		}
		Total += Gap * FMath::Max(0, Parts.Num() - 1);
		float X = RX - Total;
		// 自己击毁 / 被击毁的那行：淡底 + 细框（自己击毁主色、被击毁红）
		if (bSelf)
		{
			const FLinearColor Frame = E.B == Me ? Th.Enemy : Th.Accent;
			DrawRect(MechHudFeedLocal::Fade(Th.Plate, 0.9f * A), X - 8.f * UI, Y - 3.f * UI, Total + 16.f * UI, RowH - 3.f * UI);
			Box(X - 8.f * UI, Y - 3.f * UI, Total + 16.f * UI, RowH - 3.f * UI, MechHudFeedLocal::Fade(Frame, 0.85f * A), 1.f);
		}
		for (const FPart& P : Parts)
		{
			const float MX = X + P.W * 0.5f;
			switch (P.Type)
			{
			case FPart::Word:
				Text(P.Text, X, CY - Measure(P.Text, P.Size).Y * 0.5f, P.Size, MechHudFeedLocal::Fade(P.Color, A));
				break;
			case FPart::Weapon:
				FeedIcon(P.Icon, MX, CY, 7.f * UI, MechHudFeedLocal::Fade(P.Color, 0.95f * A));
				break;
			case FPart::Plugin:
				PluginIcon(MX, CY, 10.f * UI, P.PluginId, A, 1.3f);
				break;
			case FPart::Ping:
				PingGlyph(P.Icon, MX, CY, 9.f * UI, MechHudFeedLocal::Fade(P.Color, A), 1.3f);
				break;
			case FPart::Core:  // 核心：实心金菱形 + 外面一圈空心菱形
			{
				const FLinearColor C = MechHudFeedLocal::Fade(P.Color, A);
				const float r = 5.f * UI, R2 = 9.f * UI;
				Triangle(FVector2D(MX, CY - r), FVector2D(MX + r, CY), FVector2D(MX - r, CY), C);
				Triangle(FVector2D(MX, CY + r), FVector2D(MX + r, CY), FVector2D(MX - r, CY), C);
				Polyline({FVector2f(MX, CY - R2), FVector2f(MX + R2, CY), FVector2f(MX, CY + R2), FVector2f(MX - R2, CY), FVector2f(MX, CY - R2)}, MechHudFeedLocal::Fade(P.Color, 0.6f * A), 1.f);
				break;
			}
			case FPart::Wave:  // 波次：三个一排的小菱形（一队来敌）
			{
				const FLinearColor C = MechHudFeedLocal::Fade(P.Color, A);
				for (int32 k = -1; k <= 1; ++k)
				{
					const float DX = MX + k * 7.f * UI, r = 3.f * UI;
					Triangle(FVector2D(DX, CY - r), FVector2D(DX + r, CY), FVector2D(DX - r, CY), C);
					Triangle(FVector2D(DX, CY + r), FVector2D(DX + r, CY), FVector2D(DX - r, CY), C);
				}
				break;
			}
			case FPart::Post:  // 指挥所：方块 + 天线 + 一道裂纹
			{
				const FLinearColor C = MechHudFeedLocal::Fade(P.Color, A);
				Box(MX - 5.f * UI, CY - 2.f * UI, 10.f * UI, 8.f * UI, C, 1.f);
				DrawLine(MX, CY - 2.f * UI, MX, CY - 8.f * UI, C, 1.f);
				SoftLine(MX - 2.f * UI, CY - 1.f * UI, MX + 1.f * UI, CY + 3.f * UI, C, 1.f);
				break;
			}
			}
			X += P.W + Gap;
		}
	}
}
