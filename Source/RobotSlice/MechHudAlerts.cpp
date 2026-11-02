// 战机式告警（总说明见 MechHudAlerts.h）。各项的触发条件 / 声音 / 图形：
//
//  被跟踪（琥珀）：敌人对自己的导弹锁定在走（UMechCombatComponent::GetLockThreat，只算导弹就绪的）、狙击开镜枪线从自己 8 米内扫过
//     —— S_hud_track 0.55 秒一下；LCK 灯琥珀；方向：那台敌人在屏幕内就在它身上套一个琥珀菱形，在屏幕外就在屏幕边那个方向亮一段琥珀光带
//  被锁定（红）：导弹锁定完成、狙击枪线 2.5 米内、充能步枪对着自己充能、火炮架着对着自己
//     —— S_hud_lock 0.15 秒一下（急促的「嘀嘀嘀」）；LCK 灯红；红色双菱形 / 红光带
//  导弹来袭：追着自己的导弹（或朝自己飞、会从 5 米内擦过的敌方导弹）—— 新的一批先响 S_hud_missile_launch，
//     之后 S_hud_missile 按最近一发的距离 50 米外 0.30 秒一下 → 6 米内 0.075 秒一下（音调也略升）；MSL 灯红；
//     屏幕内导弹上一个红色小菱形，屏幕外红光带跟着哔声一闪一闪
//  低装甲：装甲 < 25%（回到 30% 以上才解除）—— S_hud_caution 1.5 秒一轮（< 12% 1.05 秒一轮），三轮后音量降一半（危急时不降）；
//     AP 灯红；屏幕四边暗红呼吸（跟着告警音一下一下）
//  推进耗尽：能量打空过热 —— S_hud_overheat；EN 灯琥珀；恢复时很轻的 S_hud_thrust_ready
//  失衡将满：失衡槽 ≥ 75%（回到 55% 以下才会再响）—— S_hud_stagger；BAL 灯琥珀，已经失衡时红
//  被网住 —— S_hud_net；SYS 灯琥珀（准星外的网格原来就有）
//  被钩爪拖拽（敌人钩的；队友来救不算）—— S_hud_drag；SYS 灯琥珀；朝拉自己那台的屏幕边一段琥珀光带 + 往外走的双箭头
//  中 EMP —— S_hud_emp，干扰期间 HUD 闪断时偶尔 S_hud_static；SYS 灯红、EN 灯琥珀；整帧横向撕裂、闪断、读数乱跳、偏冷色
//  受重击（受击分档 1、2）—— 不加声音（受击本来就有声）；整个 HUD 抖 3 / 7 像素，最重一档开头再撕一道
//  出生 / 重生（第三人称；第一人称有自己的启动画面，只加声音）—— S_hud_power_up；HUD 从下往上逐块闪亮 + 一道扫描线，告警灯全亮自检
//  被击毁 —— S_hud_power_down；最后一帧 HUD 闪两下、上下压成一条线、缩成一点熄灭（之后是原来的四角红框）
//
// 混音：循环通道同时只响一种：导弹来袭 > 锁定 > 跟踪 > 低装甲；往更急的切马上响，往回落时停掉上一下、隔 0.35 秒再响。
// 短提示音叠加播放，同一种 1 秒内只响一次。音量 = 每种的基础音量 × 设置「告警音量」（默认 0.6）× 主音量。
//
// 语音（10-31 用户追加：「警报加一些机械配音吧」→ 英文短句、三种风格设置里切换；又改「战机告警可以重复啊」）：
//  持续性的状况持续期间重复说（像战机的 "MISSILE, MISSILE"），解除就停；同时只有最要紧的那一条在重复，更要紧的出现时打断。
//  间隔（从上一句开始算，至少等上一句说完再停 0.35 秒）：导弹来袭 1.0–1.5 秒（越近越密）、锁定 / 狙击 2.5、装甲危急 3、推进耗尽 / 失衡将满 2.5、
//  网 / 拖拽 2、装甲低 8（低装甲可能一直持续到打完，太密会烦）；企业客服的间隔再乘 1.3，而且重复时说短句（第一次整句）。
//  一次性的：SYSTEMS ONLINE（上电后 0.8 秒）、SYSTEM FAILURE（EMP 刚中）、UNIT LOST（被击毁后 0.3 秒）；比正在持续的状况不要紧就不说了。
//  说话时提示音压到 55%。语音音量跟「告警音量」走。资源 /Game/Audio/HUD/Voice/S_vo_<female|mech|corp>_<句>（SourceAssets/sfx_hud/voice）。

#include "MechHudAlerts.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundBase.h"
#include "MechCharacter.h"
#include "MechCombatComponent.h"
#include "MechHealthComponent.h"
#include "MechHUD.h"
#include "MechHudOverlay.h"
#include "MechHudTheme.h"
#include "MechMissile.h"
#include "MechMovementComponent.h"
#include "MechSettings.h"
#include "MechShoulderComponent.h"
#include "MechWeapons.h"
#include "MechMapRules.h"
#include "Components/CapsuleComponent.h"

namespace MechAlertsUi  // 不用匿名命名空间：合并编译时会和 MechHUD.cpp 里同名的颜色撞
{
	FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float A)
	{
		FLinearColor C(FColor(R, G, B));
		C.A = A;
		return C;
	}
	// 和 HUD 同一套色：橙（琥珀）= 提醒，红 = 危险。10-31 起是当前 HUD 主题（MechHudTheme.h）成员的引用，跟着本机框架制造商变
	const FLinearColor& Amber = MechHudTheme::Get().Caution;
	const FLinearColor& Red = MechHudTheme::Get().Danger;
	const FLinearColor& Faint = MechHudTheme::Get().Faint;
	const FLinearColor& LampText = MechHudTheme::Get().LampText;
	const FName DragRootMotion(TEXT("MechGrappleDrag"));  // UMechShoulderComponent 里被拖拽的根运动名字
	constexpr float Big = 1.e9f;

	FLinearColor Fade(FLinearColor C, float A)
	{
		C.A *= A;
		return C;
	}

	void Quad(TArray<FCanvasUVTri>& Out, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D,
		const FLinearColor& CA, const FLinearColor& CB, const FLinearColor& CC, const FLinearColor& CD)
	{
		FCanvasUVTri T;
		T.V0_Pos = A, T.V1_Pos = B, T.V2_Pos = C;
		T.V0_Color = CA, T.V1_Color = CB, T.V2_Color = CC;
		Out.Add(T);
		T.V0_Pos = A, T.V1_Pos = C, T.V2_Pos = D;
		T.V0_Color = CA, T.V1_Color = CC, T.V2_Color = CD;
		Out.Add(T);
	}

	/** 一个元素用来判断「在屏幕哪一块」的参考点 */
	FVector2f RefPoint(const FMechHudFrame& F, const FMechHudFrame::FItem& It)
	{
		switch (It.Kind)
		{
		case FMechHudFrame::EKind::Rect:
			return It.Pos + It.Size * 0.5f;
		case FMechHudFrame::EKind::Lines:
			return It.Count > 0 ? F.Points[It.First] : FVector2f::ZeroVector;
		case FMechHudFrame::EKind::Tris:
			return It.Count > 0 ? F.TriPos[It.First] : FVector2f::ZeroVector;
		case FMechHudFrame::EKind::Image:
			return It.Count > 0 ? F.ImgPos[It.First] : FVector2f::ZeroVector;
		default:
			return It.Pos;
		}
	}

	void ScaleAlpha(FMechHudFrame& F, FMechHudFrame::FItem& It, float A)
	{
		switch (It.Kind)
		{
		case FMechHudFrame::EKind::Tris:
			for (int32 i = 0; i < It.Count; ++i)
			{
				F.TriColor[It.First + i].A *= A;
			}
			break;
		case FMechHudFrame::EKind::Image:
			for (int32 i = 0; i < It.Count; ++i)
			{
				F.ImgColor[It.First + i].A *= A;
			}
			break;
		default:
			It.Color.A *= A;
			break;
		}
	}

	void Shift(FMechHudFrame& F, FMechHudFrame::FItem& It, const FVector2f& O)
	{
		switch (It.Kind)
		{
		case FMechHudFrame::EKind::Lines:
			for (int32 i = 0; i < It.Count; ++i)
			{
				F.Points[It.First + i] += O;
			}
			break;
		case FMechHudFrame::EKind::Tris:
			for (int32 i = 0; i < It.Count; ++i)
			{
				F.TriPos[It.First + i] += O;
			}
			break;
		case FMechHudFrame::EKind::Image:
			for (int32 i = 0; i < It.Count; ++i)
			{
				F.ImgPos[It.First + i] += O;
			}
			break;
		default:
			It.Pos += O;
			break;
		}
	}

	void Tint(FMechHudFrame& F, FMechHudFrame::FItem& It, const FLinearColor& K)
	{
		auto Mul = [&K](FLinearColor& C) {
			C.R *= K.R;
			C.G *= K.G;
			C.B *= K.B;
		};
		if (It.Kind == FMechHudFrame::EKind::Tris)
		{
			for (int32 i = 0; i < It.Count; ++i)
			{
				Mul(F.TriColor[It.First + i]);
			}
		}
		else if (It.Kind != FMechHudFrame::EKind::Image)
		{
			Mul(It.Color);
		}
	}
}

namespace MechAlertsVoice
{
	constexpr int32 Num = static_cast<int32>(EMechVoice::Count);
	const TCHAR* Keys[Num] = {TEXT("online"), TEXT("missile"), TEXT("lock"), TEXT("sniper"), TEXT("armor_low"), TEXT("armor_crit"), TEXT("thrust"),
		TEXT("balance"), TEXT("emp"), TEXT("net"), TEXT("drag"), TEXT("lost")};
	/** 越大越要紧（高的打断低的）：被击毁 > 导弹 > EMP > 锁定 > 狙击 > 装甲危急 > 拖拽 > 网 > 失衡 > 装甲低 > 推进 > 上电 */
	const int32 Priority[Num] = {30, 90, 80, 78, 45, 70, 40, 50, 85, 60, 62, 100};
	/** 持续性的重复间隔（秒，0 = 一次性）；导弹按距离另算 */
	const float Repeat[Num] = {0.f, 1.5f, 2.5f, 2.5f, 8.f, 3.f, 2.5f, 2.5f, 0.f, 2.f, 2.f, 0.f};
	const TCHAR* Styles[4] = {TEXT(""), TEXT("female"), TEXT("mech"), TEXT("corp")};
	const TCHAR* StyleNames[4] = {TEXT("关"), TEXT("冷静女声"), TEXT("机械合成"), TEXT("企业客服")};

	USoundBase* Load(int32 Style, const TCHAR* Key, const TCHAR* Suffix = TEXT(""))
	{
		const FString Name = FString::Printf(TEXT("S_vo_%s_%s%s"), Styles[Style], Key, Suffix);
		return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/HUD/Voice/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
}

UMechHudAlerts::UMechHudAlerts()
{
}

void UMechHudAlerts::EnsureSounds()
{
	if (bSoundsLoaded)
	{
		return;
	}
	bSoundsLoaded = true;
	// 按路径加载、不用 ConstructorHelpers：音效还没导入时（新检出、导入脚本没跑）只是没声音，不会在编辑器启动时报「CDO 找不到资源」。
	// /Game/Audio 整个目录在 DefaultGame.ini 的 DirectoriesToAlwaysCook 里，打包会带上
	int32 Found = 0;
	auto Load = [&Found](const TCHAR* Name) {
		USoundBase* S = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/HUD/%s.%s"), Name, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		Found += S ? 1 : 0;
		return S;
	};
	SndTrack = Load(TEXT("S_hud_track"));
	SndLock = Load(TEXT("S_hud_lock"));
	SndMissile = Load(TEXT("S_hud_missile"));
	SndLaunch = Load(TEXT("S_hud_missile_launch"));
	SndCaution = Load(TEXT("S_hud_caution"));
	SndOverheat = Load(TEXT("S_hud_overheat"));
	SndThrustReady = Load(TEXT("S_hud_thrust_ready"));
	SndStagger = Load(TEXT("S_hud_stagger"));
	SndNet = Load(TEXT("S_hud_net"));
	SndDrag = Load(TEXT("S_hud_drag"));
	SndEmp = Load(TEXT("S_hud_emp"));
	SndStatic = Load(TEXT("S_hud_static"));
	SndPowerDown = Load(TEXT("S_hud_power_down"));
	SndPowerUp = Load(TEXT("S_hud_power_up"));
	UE_LOG(LogTemp, Display, TEXT("[alert] 告警音效 %d/14 已加载（/Game/Audio/HUD）"), Found);
}

// ---------------------------------------------------------------- 每帧：判断、声音、告警图形
void UMechHudAlerts::Draw(AMechHUD& Hud, AMechCharacter* Mech)
{
	UWorld* World = Hud.GetWorld();
	if (!World || !Hud.Canvas)
	{
		return;
	}
	EnsureSounds();
	const float Real = World->GetRealTimeSeconds();
	Dt = FMath::Clamp(Real - LastReal, 0.f, 0.1f);
	LastReal = Real;
	static const bool bTest = FParse::Param(FCommandLine::Get(), TEXT("MechAlertTest"));
	static const bool bLogParam = FParse::Param(FCommandLine::Get(), TEXT("MechAlertLog"));
	bLog = bTest || bLogParam;
	// 语音：这一帧的持续性状况从零算起（死了、没机体、观战时都是空的 → 重复停下），不管从哪里返回最后都推进一下语音
	FMemory::Memzero(VoiceActive);
	ON_SCOPE_EXIT
	{
		UpdateVoice(Hud, Real);
	};
	bMineNow = Mech && Mech == Hud.GetOwningPawn() && Mech->IsLocallyControlled();
	if (bTest && bMineNow)
	{
		MechAlertTest::Tick(Hud, *this, Mech, Dt);
	}
	if (!Mech)
	{
		StopLoop();
		Loop = EMechAlertLoop::None;
		LastMech.Reset();
		return;
	}
	UMechHealthComponent* Health = Mech->GetHealth();
	const bool bDead = Health->IsDead();
	if (Mech != LastMech.Get())
	{
		// 换了一台（出生 / 重生 / 新回合 / 观战换人）：状态从这台现在的样子算起，免得一换过来就把「刚进入」当成新告警
		LastMech = Mech;
		StopLoop();
		Loop = EMechAlertLoop::None;
		CautionCycles = 0;
		bLowArmor = false;
		bWasOverheat = Mech->IsOverheated();
		bWasEmp = Mech->IsEmped();
		bWasNetted = Mech->IsNetted();
		bWasDragged = false;
		bStaggerArmed = true;
		FMemory::Memzero(VoiceSaid);
		LastHurtSeen = Health->GetLastHurtTime();
		EmpStart = ShakeStart = -100.f;  // 断电动画不清：被击毁后镜头 / 控制常常换到别的机体上，那一下正好在放断电
		FMemory::Memzero(LampLevel);
		LampPanel = 0.f;
		bWasDead = bDead;
		if (bMineNow && !bDead)
		{
			StartPowerUp(Hud, Real, true);
		}
	}
	if (bDead)
	{
		if (!bWasDead && bMineNow)
		{
			StartPowerDown(Hud, Real);
		}
		bWasDead = true;
		StopLoop();
		Loop = EMechAlertLoop::None;
		FMemory::Memzero(LampLevel);
		return;
	}
	bWasDead = false;

	Gather(Hud, Mech, Real, bMineNow);

	// ---- 系统状态：刚进入时各响一声
	const float APFrac = FMath::Clamp(Health->GetAPFraction(), 0.f, 1.f);
	const bool bEmp = Mech->IsEmped();
	const bool bOver = Mech->IsOverheated();
	const bool bNet = Mech->IsNetted();
	// 被钩爪拖拽：身上有拖拽的根运动，且最近的一台「正钩着人」的是敌人（队友来救也是同一个根运动，不报）
	bool bDrag = false;
	Puller.Reset();
	if (UMechMovementComponent* Move = Mech->GetMechMovement(); Move && Move->GetRootMotionSource(MechAlertsUi::DragRootMotion).IsValid())
	{
		AMechCharacter* Best = nullptr;
		float BestD = MechAlertsUi::Big;
		for (TActorIterator<AMechCharacter> It(World); It; ++It)
		{
			if (*It == Mech || It->GetHealth()->IsDead() || !It->GetShoulder() || It->GetShoulder()->GetGrappleState() != EMechGrapple::Hooked)
			{
				continue;
			}
			const float D = FVector::Dist(It->GetActorLocation(), Mech->GetActorLocation());
			if (D < BestD)
			{
				BestD = D;
				Best = *It;
			}
		}
		bDrag = !(Best && Best->GetTeam() == Mech->GetTeam());
		if (bDrag && Best)
		{
			Puller = Best;
			FThreat& T = Threats.AddDefaulted_GetRef();
			T.Source = Best;
			T.Pos = Best->GetActorLocation();
			T.Level = 4;
			T.Dist = BestD;
		}
	}
	if (bEmp && !bWasEmp)
	{
		EmpStart = Real;
		NextStatic = Real + 0.3f;
		Cue(Hud, SndEmp, TEXT("EMP"), 0.75f, Real);
		if (bMineNow)
		{
			SayOnce(EMechVoice::Emp, Real, 0.f);
		}
	}
	if (bOver && !bWasOverheat && !bEmp)
	{
		Cue(Hud, SndOverheat, TEXT("推进耗尽"), 0.7f, Real);
	}
	if (!bOver && bWasOverheat && !bEmp)
	{
		Cue(Hud, SndThrustReady, TEXT("推进恢复"), 0.6f, Real);
	}
	const float Stag = Health->GetStaggerFraction();
	if (Health->IsStaggered())
	{
		bStaggerArmed = false;  // 已经失衡了：失衡本身有声音，不再报「将满」
	}
	else if (Stag < 0.55f)
	{
		bStaggerArmed = true;
	}
	else if (bStaggerArmed && Stag >= 0.75f)
	{
		bStaggerArmed = false;
		Cue(Hud, SndStagger, TEXT("失衡将满"), 0.9f, Real);
	}
	if (bNet && !bWasNetted)
	{
		Cue(Hud, SndNet, TEXT("被网住"), 1.f, Real);
	}
	if (bDrag && !bWasDragged)
	{
		Cue(Hud, SndDrag, TEXT("被拖拽"), 1.f, Real);
	}
	bWasEmp = bEmp;
	bWasOverheat = bOver;
	bWasNetted = bNet;
	bWasDragged = bDrag;

	// ---- 受重击：HUD 抖一下（分档见 UMechHealthComponent::GetLastHurtTier；子弹那档不抖，不然连射时一直在抖）
	const float Hurt = Health->GetLastHurtTime();
	if (Hurt > LastHurtSeen + 0.001f)
	{
		LastHurtSeen = Hurt;
		const int32 Tier = Health->GetLastHurtTier();
		if (Tier >= 1)
		{
			ShakeStart = Real;
			ShakeAmp = Tier >= 2 ? 7.f : 3.f;
			ShakeLen = Tier >= 2 ? 0.32f : 0.2f;
			bShakeTear = Tier >= 2;
			if (bLog)
			{
				UE_LOG(LogTemp, Display, TEXT("[alert] 受击抖动 分档 %d"), Tier);
			}
		}
	}

	// ---- 告警灯
	using EL = EMechAlertLamp;
	SetLamp(EL::Missile, MissileCount > 0 ? 2 : 0, Real);
	SetLamp(EL::Lock, MaxMechLevel >= 2 ? 2 : MaxMechLevel == 1 ? 1 : 0, Real);
	SetLamp(EL::Armor, APFrac < 0.25f ? 2 : 0, Real);
	SetLamp(EL::Energy, bOver ? 1 : 0, Real);
	SetLamp(EL::Balance, Health->IsStaggered() ? 2 : Stag >= 0.75f ? 1 : 0, Real);
	SetLamp(EL::System, bEmp ? 2 : (bNet || bDrag) ? 1 : 0, Real);

	UpdateLoop(Hud, Real, bMineNow, APFrac);

	// ---- 语音：这一帧哪些持续性状况在（只有自己的机体说话）
	if (bMineNow)
	{
		auto Set = [this](EMechVoice V, bool b) { VoiceActive[static_cast<int32>(V)] = b; };
		Set(EMechVoice::Missile, MissileCount > 0);
		Set(EMechVoice::Lock, bLockByOther);
		Set(EMechVoice::Sniper, bLockBySniper && !bLockByOther);
		Set(EMechVoice::ArmorCrit, APFrac < 0.12f);
		Set(EMechVoice::ArmorLow, bLowArmor && APFrac >= 0.12f);
		Set(EMechVoice::Thrust, bOver && !bEmp);
		Set(EMechVoice::Balance, !Health->IsStaggered() && Stag >= 0.75f);
		Set(EMechVoice::Net, bNet);
		Set(EMechVoice::Drag, bDrag);
	}

	// ---- 画（HUD 启动门控之外，透明度按 1 画）
	const float SavedAlpha = Hud.HudAlpha;
	Hud.HudAlpha = 1.f;
	DrawCaution(Hud, Real, APFrac);
	DrawThreats(Hud, Mech, Real);
	DrawLamps(Hud, Mech, Real);
	DrawWaterWarn(Hud, Mech, Real);
	Hud.HudAlpha = SavedAlpha;
}

// ---------------------------------------------------------------- 威胁：谁在锁自己、哪些导弹在追自己
void UMechHudAlerts::Gather(AMechHUD& Hud, AMechCharacter* Mech, float Real, bool bMine)
{
	UWorld* World = Hud.GetWorld();
	Threats.Reset();
	MaxMechLevel = 0;
	bLockBySniper = false;
	bLockByOther = false;
	const int32 MyTeam = Mech->GetTeam();
	const FVector MyPos = Mech->GetActorLocation();
	if (LosCache.Num() > 48)
	{
		LosCache.Reset();
	}
	for (TActorIterator<AMechCharacter> It(World); It; ++It)
	{
		AMechCharacter* E = *It;
		if (E == Mech || E->GetTeam() == MyTeam || E->GetHealth()->IsDead() || !E->GetCombat())
		{
			continue;
		}
		const UMechCombatComponent* C = E->GetCombat();
		int32 Level = 0;
		const bool bOnMe = C->GetTarget() == Mech;
		// 导弹锁定（服务器算、复制来的；看得见才会涨，所以不用再查视线）
		const float Lock = C->GetLockThreat();
		if (bOnMe && Lock > 0.03f)
		{
			Level = Lock >= 0.99f ? 2 : 1;
			bLockByOther |= Level >= 2;
		}
		// 枪 / 火炮直接对着自己：按对方的枪线（摆姿势的瞄准方向，各端都有）离自己多远判断，再查一次中间有没有墙
		const MechWeapons::FGun& G = C->GetGun();
		const bool bGunStance = C->GetStance() == EMechStance::Gun;
		const bool bSniper = bGunStance && G.EnergyPerShot > 0.f && C->IsAiming();
		const bool bCharge = bGunStance && G.ChargeTime > 0.f && C->GetCharge() > 0.02f;
		const bool bArty = E->GetShoulder() && E->GetShoulder()->IsArtilleryDeployed();
		if ((bSniper || bCharge || bArty) && Level < 2)
		{
			const float Miss = AimMiss(E, Mech);
			if ((Miss < 1000.f || (bArty && bOnMe)) && HasLos(E, Mech, Real))
			{
				if (bSniper)
				{
					Level = FMath::Max(Level, Miss < 250.f ? 2 : Miss < 800.f ? 1 : 0);
					bLockBySniper |= Miss < 250.f;
				}
				if (bCharge && Miss < 350.f)
				{
					Level = 2;
					bLockByOther = true;
				}
				if (bArty && (bOnMe || Miss < 600.f))
				{
					Level = 2;
					bLockByOther = true;
				}
			}
		}
		if (Level > 0)
		{
			FThreat& T = Threats.AddDefaulted_GetRef();
			T.Source = E;
			T.Pos = E->GetActorLocation();
			T.Level = static_cast<uint8>(Level);
			T.Dist = FVector::Dist(E->GetActorLocation(), MyPos);
			MaxMechLevel = FMath::Max(MaxMechLevel, Level);
		}
	}
	// 导弹：目标是自己的；没锁定乱射的，朝自己飞、2 秒内会从 5 米内擦过的也算
	MissileCount = 0;
	ClosestMissile = MechAlertsUi::Big;
	TSet<TWeakObjectPtr<AActor>> Seen;
	bool bNew = false;
	for (TActorIterator<AMechMissile> It(World); It; ++It)
	{
		AMechMissile* M = *It;
		if (M->IsExploded() || M->IsActorBeingDestroyed())
		{
			continue;
		}
		// 队伍按射手算（导弹自己的队伍号不复制，射手是 Instigator，复制的）
		const AMechCharacter* Shooter = Cast<AMechCharacter>(M->GetInstigator());
		if (Shooter == Mech || (Shooter && Shooter->GetTeam() == MyTeam))
		{
			continue;
		}
		const FVector To = MyPos - M->GetActorLocation();
		const float D = To.Size();
		bool bIn = M->GetTarget() == Mech;
		if (!bIn && D < 4000.f)
		{
			const FVector V = M->GetVelocity();
			const float S2 = V.SizeSquared();
			if (S2 > 250000.f)
			{
				const float T = (To | V) / S2;
				bIn = T > 0.f && T < 2.f && (To - V * T).Size() < 500.f;
			}
		}
		if (!bIn)
		{
			continue;
		}
		++MissileCount;
		ClosestMissile = FMath::Min(ClosestMissile, D);
		FThreat& T = Threats.AddDefaulted_GetRef();
		T.Source = M;
		T.Pos = M->GetActorLocation();
		T.Level = 3;
		T.Dist = D;
		const TWeakObjectPtr<AActor> Key(M);
		Seen.Add(Key);
		bNew |= !KnownMissiles.Contains(Key);
	}
	KnownMissiles = MoveTemp(Seen);
	if (bNew)
	{
		Cue(Hud, SndLaunch, TEXT("导弹发射"), 0.45f, Real, 1.2f);  // 一批四发 0.27 秒内陆续出来，限流成一声
	}
	(void)bMine;
}

float UMechHudAlerts::AimMiss(const AMechCharacter* Enemy, const AMechCharacter* Me)
{
	const UMechCombatComponent* C = Enemy->GetCombat();
	const FVector From = C->GetPoseAimPivot();
	const FVector Dir = C->GetPoseAimDirection().GetSafeNormal();
	const FVector To = Me->GetActorLocation() + FVector(0.f, 0.f, 40.f) - From;
	const float Along = To | Dir;
	if (Along < 100.f)
	{
		return MechAlertsUi::Big;
	}
	return (To - Dir * Along).Size();
}

bool UMechHudAlerts::HasLos(AMechCharacter* Enemy, const AMechCharacter* Me, float Real)
{
	FLos& L = LosCache.FindOrAdd(TWeakObjectPtr<AMechCharacter>(Enemy));
	if (Real >= L.Next)
	{
		L.Next = Real + 0.12f;
		FHitResult Hit;
		FCollisionQueryParams P(SCENE_QUERY_STAT(MechAlertLos), false, Enemy);
		P.AddIgnoredActor(Me);
		L.bClear = !Enemy->GetWorld()->LineTraceSingleByObjectType(Hit, Enemy->GetActorLocation() + FVector(0.f, 0.f, 100.f),
			Me->GetActorLocation() + FVector(0.f, 0.f, 60.f), FCollisionObjectQueryParams(ECC_WorldStatic), P);
	}
	return L.bClear;
}

void UMechHudAlerts::SetLamp(EMechAlertLamp L, uint8 Level, float Real)
{
	const int32 i = static_cast<int32>(L);
	if (Level == LampLevel[i])
	{
		return;
	}
	if (Level > LampLevel[i])
	{
		LampSince[i] = Real;  // 亮起 / 升级：开头闪一会儿
	}
	if (bLog)
	{
		static const TCHAR* Names[6] = {TEXT("MSL"), TEXT("LCK"), TEXT("AP"), TEXT("EN"), TEXT("BAL"), TEXT("SYS")};
		UE_LOG(LogTemp, Display, TEXT("[alert] 告警灯 %s %d → %d"), Names[i], LampLevel[i], Level);
	}
	LampLevel[i] = Level;
}

// ---------------------------------------------------------------- 循环通道：同时只响最要紧的一种
void UMechHudAlerts::UpdateLoop(AMechHUD& Hud, float Real, bool bMine, float APFrac)
{
	if (APFrac >= 0.3f)
	{
		bLowArmor = false;
		CautionCycles = 0;
	}
	else if (APFrac < 0.25f)
	{
		bLowArmor = true;
	}
	const EMechAlertLoop Want = MissileCount > 0 ? EMechAlertLoop::Missile
		: MaxMechLevel >= 2 ? EMechAlertLoop::Lock
		: MaxMechLevel == 1 ? EMechAlertLoop::Track
		: bLowArmor ? EMechAlertLoop::Caution : EMechAlertLoop::None;
	if (Want != Loop)
	{
		if (bLog)
		{
			UE_LOG(LogTemp, Display, TEXT("[alert] 循环告警 %s → %s"), MechAlertTest::LoopName(Loop), MechAlertTest::LoopName(Want));
		}
		// 往更急的换：马上响；往回落（导弹炸了回到低装甲之类）：掐掉上一下，隔一小会儿再响，别和刚才那下挤在一起
		if (Want < Loop)
		{
			StopLoop();
		}
		NextBeep = Want > Loop ? Real : Real + 0.35f;
		Loop = Want;
	}
	if (Loop == EMechAlertLoop::None || Real < NextBeep)
	{
		return;
	}
	float Interval = 1.f;
	switch (Loop)
	{
	case EMechAlertLoop::Missile:
	{
		// 最近一发 50 米外 0.30 秒一下，6 米内 0.075 秒一下
		const float F = FMath::Clamp((ClosestMissile - 600.f) / 4400.f, 0.f, 1.f);
		Interval = FMath::Lerp(0.075f, 0.30f, F);
		Beep(Hud, SndMissile, TEXT("导弹来袭"), 0.5f, FMath::Lerp(1.12f, 1.f, F));
		break;
	}
	case EMechAlertLoop::Lock:
		Interval = 0.15f;
		Beep(Hud, SndLock, TEXT("锁定"), 0.42f);
		break;
	case EMechAlertLoop::Track:
		Interval = 0.55f;
		Beep(Hud, SndTrack, TEXT("跟踪"), 0.55f);
		break;
	case EMechAlertLoop::Caution:
	{
		// 主告警：前三轮正常音量，之后降一半（一直低装甲打下去的时候别一直吵）；危急（< 12%）时加急、不降
		const bool bCrit = APFrac < 0.12f;
		Interval = bCrit ? 1.05f : 1.5f;
		Beep(Hud, SndCaution, TEXT("低装甲"), (CautionCycles < 3 || bCrit) ? 0.6f : 0.3f);
		++CautionCycles;
		LastCautionBeep = Real;
		break;
	}
	default:
		break;
	}
	LastBeep = Real;
	NextBeep = Real + Interval;
	(void)bMine;
}

// ---------------------------------------------------------------- 声音
void UMechHudAlerts::Count(const TCHAR* Name)
{
	SoundCounts.FindOrAdd(Name) += 1;
}

void UMechHudAlerts::Cue(AMechHUD& Hud, USoundBase* Sound, const TCHAR* Name, float Volume, float Real, float MinGap, float Pitch)
{
	float& Last = LastCue.FindOrAdd(Name, -100.f);
	if (Real - Last < MinGap)
	{
		return;
	}
	Last = Real;
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[alert] 提示音 %s%s"), Name, !bMineNow ? TEXT("（观战，不出声）") : Sound ? TEXT("") : TEXT("（音效没导入）"));
	}
	if (!bMineNow || !Sound)
	{
		return;
	}
	Count(Name);
	UGameplayStatics::PlaySound2D(&Hud, Sound, Volume * Duck() * FMechSettings::Get().AlertVolume * MechHudTheme::Get().AlertGain, Pitch * MechHudTheme::Get().AlertPitch);  // 主题：音调 / 音量各乘一点（新纪低沉、维克托尖短、相位轻）
}

void UMechHudAlerts::Beep(AMechHUD& Hud, USoundBase* Sound, const TCHAR* Name, float Volume, float Pitch)
{
	if (!bMineNow || !Sound)
	{
		return;
	}
	StopLoop();
	Count(Name);
	LoopBeep = UGameplayStatics::SpawnSound2D(&Hud, Sound, Volume * Duck() * FMechSettings::Get().AlertVolume * MechHudTheme::Get().AlertGain, Pitch * MechHudTheme::Get().AlertPitch);
}

void UMechHudAlerts::StopLoop()
{
	if (UAudioComponent* A = LoopBeep.Get())
	{
		A->Stop();
	}
	LoopBeep.Reset();
}

void UMechHudAlerts::PreviewVolume(const UObject* WorldContext)
{
	static double Last = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (!WorldContext || Now - Last < 0.3)
	{
		return;
	}
	Last = Now;
	static TWeakObjectPtr<USoundBase> Cached;
	USoundBase* S = Cached.Get();
	if (!S)
	{
		S = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/HUD/S_hud_lock.S_hud_lock"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		Cached = S;
	}
	if (S)
	{
		UGameplayStatics::PlaySound2D(WorldContext, S, 0.42f * FMechSettings::Get().AlertVolume);  // UI 音：菜单暂停时也响
	}
}

float UMechHudAlerts::Duck() const
{
	return IsSpeaking() ? 0.55f : 1.f;
}

// ---------------------------------------------------------------- 语音
int32 UMechHudAlerts::VoiceStyle()
{
	static const int32 Override = [] {
		int32 N = -1;
		FParse::Value(FCommandLine::Get(), TEXT("MechAlertVoice="), N);
		return N;
	}();
	return FMath::Clamp(Override >= 0 ? Override : FMechSettings::Get().bAlertVoiceSet ? FMechSettings::Get().AlertVoice : MechHudTheme::Get().DefaultVoice, 0, 3);
}

void UMechHudAlerts::EnsureVoice(int32 Style)
{
	if (Style == LoadedVoiceStyle)
	{
		return;
	}
	LoadedVoiceStyle = Style;
	VoiceFull.SetNum(MechAlertsVoice::Num);
	VoiceRepeat.SetNum(MechAlertsVoice::Num);
	int32 Found = 0;
	for (int32 i = 0; i < MechAlertsVoice::Num; ++i)
	{
		VoiceFull[i] = Style > 0 ? MechAlertsVoice::Load(Style, MechAlertsVoice::Keys[i]) : nullptr;
		VoiceRepeat[i] = Style > 0 ? MechAlertsVoice::Load(Style, MechAlertsVoice::Keys[i], TEXT("_r")) : nullptr;  // 没有重复句的就是空，重复时还说整句
		Found += VoiceFull[i] ? 1 : 0;
	}
	UE_LOG(LogTemp, Display, TEXT("[alert] 告警语音 %s：%d/%d 句已加载（/Game/Audio/HUD/Voice）"), MechAlertsVoice::StyleNames[Style], Found, MechAlertsVoice::Num);
}

void UMechHudAlerts::SayOnce(EMechVoice V, float Real, float Delay)
{
	FPendingVoice& P = PendingVoice.AddDefaulted_GetRef();
	P.V = static_cast<int32>(V);
	P.Due = Real + Delay;
}

bool UMechHudAlerts::IsSpeaking() const
{
	const UAudioComponent* C = VoiceComp.Get();
	return C && C->IsPlaying();
}

void UMechHudAlerts::UpdateVoice(AMechHUD& Hud, float Real)
{
	using namespace MechAlertsVoice;
	const int32 Style = VoiceStyle();
	if (Style <= 0)
	{
		PendingVoice.Reset();
		return;
	}
	EnsureVoice(Style);
	const bool bSpeaking = IsSpeaking();
	if (!bSpeaking)
	{
		VoiceNow = -1;
		VoicePriority = 0;
	}
	// 状况解除的：下一次再出现时从整句说起
	for (int32 i = 0; i < Num; ++i)
	{
		VoiceSaid[i] = VoiceSaid[i] && VoiceActive[i];
	}
	// 持续性状况里最要紧的那一条（同时只有它重复；它在等间隔时别的也不插嘴）
	int32 Top = -1;
	for (int32 i = 0; i < Num; ++i)
	{
		if (VoiceActive[i] && (Top < 0 || Priority[i] > Priority[Top]))
		{
			Top = i;
		}
	}
	const int32 TopPri = Top >= 0 ? Priority[Top] : -1;
	// 正在说的那条持续性状况已经解除了：别的可以打断它（10-31 测试：客服的「装甲危急」整句 5.4 秒，装甲回满了还在念，把后面的「推进耗尽」整个挡掉了）
	const int32 SpeakingPri = (bSpeaking && VoiceNow >= 0 && Repeat[VoiceNow] > 0.f && !VoiceActive[VoiceNow]) ? 0 : VoicePriority;
	// 一次性的：到点的里挑最要紧的；要比持续性的要紧、也比正在说的要紧才说，不然扔掉（上电时正被锁定，就不说「上电」了）
	int32 Best = -1;
	for (int32 k = PendingVoice.Num() - 1; k >= 0; --k)
	{
		if (Real < PendingVoice[k].Due)
		{
			continue;
		}
		if (Real - PendingVoice[k].Due > 2.5f)
		{
			PendingVoice.RemoveAt(k);
			Best = -1;  // 下标变了，重新挑（极少发生，下一帧再说）
			continue;
		}
		if (Best < 0 || Priority[PendingVoice[k].V] > Priority[PendingVoice[Best].V])
		{
			Best = k;
		}
	}
	if (Best >= 0)
	{
		const int32 V = PendingVoice[Best].V;
		if (Priority[V] <= TopPri)
		{
			PendingVoice.RemoveAt(Best);
		}
		else if (!bSpeaking || Priority[V] > SpeakingPri)
		{
			PendingVoice.RemoveAt(Best);
			PlayVoice(Hud, V, true, Real);
			return;
		}
	}
	if (Top < 0 || Real < VoiceNext[Top])
	{
		return;
	}
	if (bSpeaking && (VoiceNow == Top || SpeakingPri >= TopPri))
	{
		return;  // 这一句还没说完，或者正在说更要紧的（一次性的被击毁 / EMP）
	}
	PlayVoice(Hud, Top, !VoiceSaid[Top], Real);
	VoiceSaid[Top] = true;
	float Gap = Repeat[Top];
	if (Top == static_cast<int32>(EMechVoice::Missile))
	{
		Gap = FMath::Lerp(1.f, 1.5f, FMath::Clamp((ClosestMissile - 600.f) / 4400.f, 0.f, 1.f));  // 和哔声一起越近越密
	}
	if (Style == 3)
	{
		Gap *= 1.3f;  // 客服句子长
	}
	VoiceNext[Top] = Real + FMath::Max(Gap, VoiceEnd - Real + 0.35f);
}

void UMechHudAlerts::PlayVoice(AMechHUD& Hud, int32 V, bool bFull, float Real)
{
	using namespace MechAlertsVoice;
	USoundBase* S = nullptr;
	if (!bFull && VoiceRepeat.IsValidIndex(V))
	{
		S = VoiceRepeat[V];
	}
	if (!S && VoiceFull.IsValidIndex(V))
	{
		S = VoiceFull[V];
	}
	if (!S)
	{
		return;
	}
	if (UAudioComponent* C = VoiceComp.Get())
	{
		C->Stop();  // 更要紧的打断正在说的
	}
	VoiceComp = UGameplayStatics::SpawnSound2D(&Hud, S, 0.95f * FMechSettings::Get().AlertVolume);
	VoiceNow = V;
	VoicePriority = Priority[V];
	VoiceEnd = Real + S->GetDuration();
	Count(*FString::Printf(TEXT("语音 %s"), Keys[V]));
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[alert] 语音 %s %s%s（%.2f 秒，真实时间 %.2f）"), StyleNames[VoiceStyle()], Keys[V],
			(bFull || !VoiceRepeat.IsValidIndex(V) || !VoiceRepeat[V]) ? TEXT("") : TEXT(" 重复句"), S->GetDuration(), Real);
	}
}

void UMechHudAlerts::PreviewVoice(const UObject* WorldContext, int32 Style)
{
	static double Last = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (!WorldContext || Style <= 0 || Style > 3 || Now - Last < 0.3)
	{
		return;
	}
	Last = Now;
	if (USoundBase* S = MechAlertsVoice::Load(Style, TEXT("online")))
	{
		UGameplayStatics::PlaySound2D(WorldContext, S, 0.95f * FMechSettings::Get().AlertVolume);  // UI 音：菜单暂停时也响
	}
}

// ---------------------------------------------------------------- 画：低装甲四边暗红呼吸
void UMechHudAlerts::DrawWaterWarn(AMechHUD& Hud, AMechCharacter* Mech, float Real)
{
	UMechWater* Water = Hud.GetWorld()->GetSubsystem<UMechWater>();
	if (!bMineNow || !Water || !Water->HasWater())
	{
		return;
	}
	const FVector Feet = Mech->GetActorLocation() - FVector(0.f, 0.f, Mech->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	float Clear = 0.f;
	if (!Water->OverWater(Feet, Mech, Clear))
	{
		return;
	}
	const float Vz = Mech->GetVelocity().Z;
	// 按这个速度多久撞水：快撞了（2.5 秒内）或者已经贴着水面才提示；飞得高的时候不吵
	const float Impact = Vz < -100.f ? Clear / -Vz : 1.e6f;
	const bool bRed = Impact < 1.2f || Clear < 300.f;
	if (!bRed && Impact > 2.5f && Clear > 800.f)
	{
		return;
	}
	const float Blink = 0.55f + 0.45f * FMath::Abs(FMath::Sin(Real * (bRed ? 11.f : 6.f)));
	const FLinearColor C = MechAlertsUi::Fade(bRed ? MechAlertsUi::Red : MechAlertsUi::Amber, Blink);
	const float UI = Hud.UI;
	const float X = Hud.Canvas->SizeX * 0.5f;
	const float Y = Hud.Canvas->SizeY * 0.71f;
	for (int32 i = 0; i < 3; ++i)
	{
		const float Sx = (i - 1) * 46.f * UI;
		const float Dy = -FMath::Fmod(Real * 22.f, 8.f) * UI;
		Hud.SoftLine(X + Sx - 16.f * UI, Y + Dy, X + Sx, Y + Dy - 12.f * UI, C, 2.5f);
		Hud.SoftLine(X + Sx, Y + Dy - 12.f * UI, X + Sx + 16.f * UI, Y + Dy, C, 2.5f);
	}
	Hud.Text(*FString::Printf(TEXT("PULL UP  %.0fm"), FMath::Max(Clear, 0.f) / 100.f), X, Y + 8.f * UI, 15.f, C, 0.5f);
	Cue(Hud, SndCaution, TEXT("贴近水面"), bRed ? 0.6f : 0.4f, Real, bRed ? 0.45f : 0.9f, bRed ? 1.2f : 1.f);
}

void UMechHudAlerts::DrawCaution(AMechHUD& Hud, float Real, float APFrac)
{
	if (!bLowArmor)
	{
		return;
	}
	const bool bCrit = APFrac < 0.12f;
	// 跟着告警音一下一下（循环通道被更急的告警占着时停在底亮度）；响过三轮后暗一些
	// 白天亮背景上 0.15 几乎看不见（10-31 测试截图），按峰值 0.3 / 危急 0.4 给
	float A = (bCrit ? 0.4f : 0.3f) * (0.35f + 0.65f * FMath::Exp(-(Real - LastCautionBeep) * 2.6f));
	if (CautionCycles > 3 && !bCrit)
	{
		A *= 0.65f;
	}
	const float W = Hud.Canvas->SizeX;
	const float H = Hud.Canvas->SizeY;
	const float D = 110.f * Hud.UI;
	const FLinearColor Out(0.75f, 0.04f, 0.03f, A);
	const FLinearColor In(0.75f, 0.04f, 0.03f, 0.f);
	TArray<FCanvasUVTri> Tris;
	// 四条边各一道往里淡的渐变（角上两道叠在一起更深，像暗角）
	MechAlertsUi::Quad(Tris, FVector2D(0, 0), FVector2D(W, 0), FVector2D(W, D), FVector2D(0, D), Out, Out, In, In);
	MechAlertsUi::Quad(Tris, FVector2D(0, H), FVector2D(W, H), FVector2D(W, H - D), FVector2D(0, H - D), Out, Out, In, In);
	MechAlertsUi::Quad(Tris, FVector2D(0, 0), FVector2D(0, H), FVector2D(D, H), FVector2D(D, 0), Out, Out, In, In);
	MechAlertsUi::Quad(Tris, FVector2D(W, 0), FVector2D(W, H), FVector2D(W - D, H), FVector2D(W - D, 0), Out, Out, In, In);
	Hud.AddTris(Tris);
}

// ---------------------------------------------------------------- 画：威胁方向（屏幕内套在来源上，屏幕外在屏幕边）
void UMechHudAlerts::DrawThreats(AMechHUD& Hud, AMechCharacter* Mech, float Real)
{
	APlayerController* PC = Hud.PlayerOwner;
	if (!PC || !PC->PlayerCameraManager || Threats.IsEmpty())
	{
		return;
	}
	const float W = Hud.Canvas->SizeX;
	const float H = Hud.Canvas->SizeY;
	const float UI = Hud.UI;
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
	const FRotator CamYaw(0.f, CamRot.Yaw, 0.f);
	Threats.Sort([](const FThreat& A, const FThreat& B) { return A.Level != B.Level ? A.Level > B.Level : A.Dist < B.Dist; });
	// 跟着循环通道那一下闪（导弹来袭时就是越近闪得越快）
	const float Flash = FMath::Exp(-(Real - LastBeep) * 9.f);
	int32 Shown = 0;
	int32 MissilesShown = 0;
	for (const FThreat& T : Threats)
	{
		AActor* Src = T.Source.Get();
		if (!Src || Shown >= 5)
		{
			continue;
		}
		const bool bMissile = T.Level == 3;
		if (bMissile && ++MissilesShown > 2)
		{
			continue;  // 一批导弹从差不多的方向来，最多画最近的两发，免得一团叠在一起
		}
		++Shown;
		const FVector At = bMissile ? Src->GetActorLocation() : Src->GetActorLocation() + FVector(0.f, 0.f, 40.f);
		const FVector Local = CamRot.UnrotateVector(At - CamLoc);
		FVector2D P;
		const float Pad = 50.f * UI;
		const bool bOn = Local.X > 0.f && UGameplayStatics::ProjectWorldToScreen(PC, At, P) && P.X > Pad && P.X < W - Pad && P.Y > Pad && P.Y < H - Pad;
		const FLinearColor Base = (T.Level == 1 || T.Level == 4) ? MechAlertsUi::Amber : MechAlertsUi::Red;
		if (bOn)
		{
			auto Diamond = [&](float R, const FLinearColor& C, float Thick) {
				Hud.SoftLine(P.X, P.Y - R, P.X + R, P.Y, C, Thick);
				Hud.SoftLine(P.X + R, P.Y, P.X, P.Y + R, C, Thick);
				Hud.SoftLine(P.X, P.Y + R, P.X - R, P.Y, C, Thick);
				Hud.SoftLine(P.X - R, P.Y, P.X, P.Y - R, C, Thick);
			};
			if (bMissile)
			{
				// 导弹本身：红色小菱形，跟着哔声一跳
				Diamond(7.f * UI * (1.f + 0.45f * Flash), MechAlertsUi::Fade(Base, 0.55f + 0.45f * Flash), 2.f);
			}
			else
			{
				// 照着自己的那台：菱形（和自己锁定别人的括号框区分开）；锁定时外面再套一圈、轻轻一跳一跳
				const float R = FMath::Clamp(9000.f / FMath::Max(T.Dist, 100.f), 20.f, 60.f) * UI * 1.3f;
				const float Pulse = T.Level == 2 ? 0.7f + 0.3f * Flash : 0.8f;
				Diamond(R, MechAlertsUi::Fade(Base, Pulse), 1.5f);
				if (T.Level == 2)
				{
					Diamond(R + 6.f * UI, MechAlertsUi::Fade(Base, 0.5f * Pulse), 1.f);
				}
			}
		}
		else
		{
			// 屏幕外：按俯视方位（正前方 = 屏幕上方，和受击方向弧一个约定）在屏幕边上亮一段。正后方左右两边都亮
			const FVector Flat = CamYaw.UnrotateVector(At - CamLoc);
			const float Ang = FMath::Atan2(Flat.Y, Flat.X);
			const float Alpha = bMissile ? 0.45f + 0.55f * Flash : T.Level == 2 ? 0.7f + 0.25f * Flash : T.Level == 4 ? 0.8f : 0.6f;
			const float Thick = (bMissile ? 20.f : T.Level == 2 ? 16.f : 12.f) * UI;
			EdgeBand(Hud, Ang, MechAlertsUi::Fade(Base, Alpha), Thick, T.Level == 4 ? 2 : 1, Real);
			if (FMath::Abs(Ang) > PI * 0.92f)
			{
				EdgeBand(Hud, -Ang, MechAlertsUi::Fade(Base, Alpha), Thick, T.Level == 4 ? 2 : 1, Real);
			}
		}
	}
	(void)Mech;
}

void UMechHudAlerts::EdgeBand(AMechHUD& Hud, float Ang, const FLinearColor& C, float Thick, int32 Chevrons, float Real)
{
	const float W = Hud.Canvas->SizeX;
	const float H = Hud.Canvas->SizeY;
	const float UI = Hud.UI;
	// 方位沿「上边 + 左右两边」这个 U 形排：前方 ±45° 在上边（从左到右），往后沿左右两边往下走，正后方在两边最下面。
	// 不用下边：下边正中是仪表盘，光带和箭头会压在读数上（10-31 第一轮截图）
	const float A = FMath::Abs(Ang);
	const bool bRight = Ang >= 0.f;
	const bool bSide = A > PI * 0.25f;
	const float EdgeLen = bSide ? H : W;
	const float Len = (bSide ? 0.24f : 0.16f) * EdgeLen;
	FVector2D Mid;
	FVector2D Along;
	FVector2D Inward;
	if (!bSide)
	{
		Mid = FVector2D(W * 0.5f + (Ang / (PI * 0.25f)) * W * 0.5f, 0.f);
		Along = FVector2D(1.f, 0.f);
		Inward = FVector2D(0.f, 1.f);
	}
	else
	{
		Mid = FVector2D(bRight ? W : 0.f, (A - PI * 0.25f) / (PI * 0.75f) * H);
		Along = FVector2D(0.f, 1.f);
		Inward = FVector2D(bRight ? -1.f : 1.f, 0.f);
	}
	// 靠近角落时整段沿边挪回来，不折过角
	if (bSide)
	{
		Mid.Y = FMath::Clamp<double>(Mid.Y, Len * 0.5f, EdgeLen - Len * 0.5f);
	}
	else
	{
		Mid.X = FMath::Clamp<double>(Mid.X, Len * 0.5f, EdgeLen - Len * 0.5f);
	}
	// 光带：沿边分段，两头淡中间亮；外沿亮、往里淡到 0
	TArray<FCanvasUVTri> Tris;
	const int32 N = 12;
	for (int32 i = 0; i < N; ++i)
	{
		const float U0 = float(i) / N;
		const float U1 = float(i + 1) / N;
		const FVector2D A0 = Mid + Along * (U0 - 0.5f) * Len;
		const FVector2D A1 = Mid + Along * (U1 - 0.5f) * Len;
		const FLinearColor O0 = MechAlertsUi::Fade(C, FMath::Sin(PI * U0));
		const FLinearColor O1 = MechAlertsUi::Fade(C, FMath::Sin(PI * U1));
		const FLinearColor I0 = MechAlertsUi::Fade(C, 0.f);
		MechAlertsUi::Quad(Tris, A0, A1, A1 + Inward * Thick, A0 + Inward * Thick, O0, O1, I0, I0);
	}
	Hud.AddTris(Tris);
	// 外沿中间一截细亮线（像灯管）
	const FVector2D L0 = Mid - Along * Len * 0.22f + Inward * 1.5f * UI;
	const FVector2D L1 = Mid + Along * Len * 0.22f + Inward * 1.5f * UI;
	Hud.DrawLine(L0.X, L0.Y, L1.X, L1.Y, MechAlertsUi::Fade(C, 0.95f), 3.f);
	// 光带里侧的小箭头，尖朝外（= 威胁在那边）；拖拽画两个、一直往外走（被往那边拉）
	const float S2 = 7.f * UI;
	for (int32 k = 0; k < Chevrons; ++k)
	{
		const float Off = Chevrons > 1 ? FMath::Fmod(Real * 1.6f + k * 0.5f, 1.f) : 0.f;
		const float ChevA = Chevrons > 1 ? FMath::Sin(PI * Off) : 1.f;
		const FVector2D Tip = Mid + Inward * (Thick + (10.f + 16.f * (1.f - Off)) * UI);
		const FVector2D Tail = Tip + Inward * S2;
		const FLinearColor CC = MechAlertsUi::Fade(C, ChevA);
		Hud.SoftLine(Tail.X - Along.X * S2, Tail.Y - Along.Y * S2, Tip.X, Tip.Y, CC, 2.f);
		Hud.SoftLine(Tip.X, Tip.Y, Tail.X + Along.X * S2, Tail.Y + Along.Y * S2, CC, 2.f);
	}
}

// ---------------------------------------------------------------- 画：仪表盘上沿正中一排告警灯（都灭了一会儿就整排淡出）
void UMechHudAlerts::DrawLamps(AMechHUD& Hud, AMechCharacter* Mech, float Real)
{
	bool bAny = false;
	for (const uint8 L : LampLevel)
	{
		bAny |= L > 0;
	}
	// 上电自检：告警灯全亮半秒（第三人称上电时）
	const float TU = Real - PowerUpStart;
	const bool bLampTest = TU > 0.3f && TU < 0.85f && !Mech->IsFirstPerson();
	if (bAny || bLampTest)
	{
		LampLastLit = Real;
	}
	const float Want = Real - LampLastLit < 1.5f ? 1.f : 0.f;
	LampPanel = FMath::FInterpConstantTo(LampPanel, Want, Dt, Want > 0.f ? 8.f : 1.2f);
	if (LampPanel <= 0.01f || Mech->GetBootTime() >= 0.f)
	{
		return;
	}
	static const TCHAR* Names[6] = {TEXT("MSL"), TEXT("LCK"), TEXT("AP"), TEXT("EN"), TEXT("BAL"), TEXT("SYS")};
	static const uint8 TestLevel[6] = {2, 2, 2, 1, 1, 1};
	const float UI = Hud.UI;
	const float LW = 36.f * UI;
	const float LH = 14.f * UI;
	const float Gap = 5.f * UI;
	const float X0 = Hud.Canvas->SizeX * 0.5f - (6.f * LW + 5.f * Gap) * 0.5f;
	const float Y = Hud.Canvas->SizeY - 108.f * UI - 26.f * UI;  // 仪表盘上沿（H - 108）上面一点，速度表正上方
	const float TextSize = 10.5f;
	const float TextH = Hud.Measure(TEXT("MSL"), TextSize).Y;
	for (int32 i = 0; i < 6; ++i)
	{
		const float X = X0 + i * (LW + Gap);
		const uint8 L = bLampTest ? TestLevel[i] : LampLevel[i];
		// 新亮起的灯先闪 1.2 秒（4 Hz）再常亮
		const bool bOffPhase = !bLampTest && L > 0 && Real - LampSince[i] < 1.2f && FMath::Fmod(Real - LampSince[i], 0.25f) > 0.15f;
		if (L > 0 && !bOffPhase)
		{
			const FLinearColor C = MechAlertsUi::Fade(L >= 2 ? MechAlertsUi::Red : MechAlertsUi::Amber, 0.88f * LampPanel);
			Hud.DrawRect(C, X, Y, LW, LH);
			Hud.Text(Names[i], X + LW * 0.5f, Y + (LH - TextH) * 0.5f, TextSize, MechAlertsUi::Fade(MechAlertsUi::LampText, LampPanel), 0.5f);
		}
		else
		{
			Hud.Box(X, Y, LW, LH, MechAlertsUi::Fade(MechAlertsUi::Faint, LampPanel), 1.f);
			Hud.Text(Names[i], X + LW * 0.5f, Y + (LH - TextH) * 0.5f, TextSize, MechAlertsUi::Fade(MechAlertsUi::Faint, LampPanel), 0.5f);
		}
	}
}

// ---------------------------------------------------------------- 上电 / 断电
void UMechHudAlerts::StartPowerUp(AMechHUD& Hud, float Real, bool bSound)
{
	PowerUpStart = Real;
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[alert] 上电"));
	}
	if (bSound)
	{
		Cue(Hud, SndPowerUp, TEXT("上电"), 0.7f, Real, 0.5f);
		SayOnce(EMechVoice::Online, Real, 0.8f);  // 等上电那串声音的「就绪」响完
	}
}

void UMechHudAlerts::DebugPowerUp(AMechHUD& Hud)
{
	if (UWorld* World = Hud.GetWorld())
	{
		StartPowerUp(Hud, World->GetRealTimeSeconds(), true);
	}
}

void UMechHudAlerts::StartPowerDown(AMechHUD& Hud, float Real)
{
	if (bLog)
	{
		UE_LOG(LogTemp, Display, TEXT("[alert] 断电（最后一帧 %s）"), Snap.IsValid() ? TEXT("有") : TEXT("没有"));
	}
	PowerDownStart = Snap.IsValid() ? Real : -100.f;
	Cue(Hud, SndPowerDown, TEXT("断电"), 0.8f, Real, 0.5f);
	SayOnce(EMechVoice::Lost, Real, 0.3f);
}

void UMechHudAlerts::AppendCollapsed(AMechHUD& Hud, FMechHudFrame& F, float T)
{
	// 被击毁：最后存下的那一帧 HUD 闪两下（0–0.14 秒）→ 上下压成一条线、略收窄、发白（0.14–0.42）→ 亮线缩成一点熄灭（0.42–0.62）
	const FMechHudFrame& S = *Snap;
	const FVector2f C(F.CanvasSize.X * 0.5f, F.CanvasSize.Y * 0.5f);
	float Alpha = 1.f, SX = 1.f, SY = 1.f, TextA = 1.f, Glow = 0.f;
	if (T < 0.14f)
	{
		Alpha = FMath::Fmod(T * 22.f, 1.f) < 0.5f ? 1.f : 0.15f;
	}
	else if (T < 0.42f)
	{
		const float K = FMath::SmoothStep(0.f, 1.f, (T - 0.14f) / 0.28f);
		SY = FMath::Max(0.008f, 1.f - K);
		SX = 1.f - 0.2f * K;
		TextA = FMath::Max(0.f, 1.f - K * 2.5f);
		Glow = 0.6f * K;
	}
	if (T < 0.42f)
	{
		auto Xf = [&](const FVector2f& P) { return C + (P - C) * FVector2f(SX, SY); };
		auto Col = [&](FLinearColor Cl, float A) {
			Cl.R = FMath::Lerp(Cl.R, 0.85f, Glow);
			Cl.G = FMath::Lerp(Cl.G, 1.f, Glow);
			Cl.B = FMath::Lerp(Cl.B, 1.f, Glow);
			Cl.A *= A;
			return Cl;
		};
		for (const FMechHudFrame::FItem& It : S.Items)
		{
			if (It.Kind == FMechHudFrame::EKind::Image)
			{
				continue;  // 小地图底图不跟着压（三角形 UV 压扁了也看不出是什么）
			}
			if (It.Kind == FMechHudFrame::EKind::Text && TextA <= 0.f)
			{
				continue;
			}
			FMechHudFrame::FItem N = It;
			switch (It.Kind)
			{
			case FMechHudFrame::EKind::Rect:
				N.Pos = Xf(It.Pos);
				N.Size = FVector2f(It.Size.X * SX, FMath::Max(It.Size.Y * SY, 1.f));
				N.Color = Col(It.Color, Alpha);
				break;
			case FMechHudFrame::EKind::Text:
				N.Pos = Xf(It.Pos);
				N.Color = Col(It.Color, Alpha * TextA);
				break;
			case FMechHudFrame::EKind::Lines:
				N.First = F.Points.Num();
				for (int32 i = 0; i < It.Count; ++i)
				{
					F.Points.Add(Xf(S.Points[It.First + i]));
				}
				N.Color = Col(It.Color, Alpha);
				break;
			case FMechHudFrame::EKind::Tris:
				N.First = F.TriPos.Num();
				for (int32 i = 0; i < It.Count; ++i)
				{
					F.TriPos.Add(Xf(S.TriPos[It.First + i]));
					F.TriColor.Add(Col(S.TriColor[It.First + i], Alpha));
				}
				break;
			default:
				break;
			}
			F.Items.Add(MoveTemp(N));
		}
	}
	if (T >= 0.3f)
	{
		const float U = FMath::Clamp((T - 0.42f) / 0.2f, 0.f, 1.f);
		const float HalfW = F.CanvasSize.X * 0.4f * (1.f - U) + 2.f * Hud.UI;
		const float A = T < 0.42f ? (T - 0.3f) / 0.12f : 1.f - U;
		Hud.DrawRect(MechAlertsUi::Fade(MechHudTheme::Get().Glow, 0.85f * A), C.X - HalfW, C.Y - 1.f * Hud.UI, HalfW * 2.f, 2.f * Hud.UI);
	}
}

// ---------------------------------------------------------------- 整帧：上电逐块亮起、EMP 干扰、受击抖动、断电
void UMechHudAlerts::PostProcess(AMechHUD& Hud, FMechHudFrame& F, AMechCharacter* Mech)
{
	UWorld* World = Hud.GetWorld();
	if (!World)
	{
		return;
	}
	const float Real = World->GetRealTimeSeconds();
	const float UI = Hud.UI;
	const float W = F.CanvasSize.X;
	const float H = F.CanvasSize.Y;
	const bool bMine = Mech && Mech == Hud.GetOwningPawn();
	const bool bAlive = Mech && !Mech->GetHealth()->IsDead();

	// 活着时每 0.1 秒存一份原样的整帧（被击毁的那一帧细条、仪表盘已经不画了，要拿之前的来做「熄灭」）
	if (bMine && bAlive && Real - SnapTime > 0.1f)
	{
		if (!Snap.IsValid())
		{
			Snap = MakeShared<FMechHudFrame>();
		}
		*Snap = F;
		SnapTime = Real;
	}

	// ---- 上电（第三人称）：从下往上逐块闪两下亮起——仪表盘 → 细条 → 准星一带 → 上方（小地图、比分），同一块里从中间往两边；一道扫描线从下扫到上
	const float TU = Real - PowerUpStart;
	if (bAlive && TU >= 0.f && TU < 1.1f && !Mech->IsFirstPerson())
	{
		for (FMechHudFrame::FItem& It : F.Items)
		{
			const FVector2f P = MechAlertsUi::RefPoint(F, It);
			const float Y = P.Y / H;
			const float At = (Y > 0.86f ? 0.06f : Y > 0.62f ? 0.28f : Y > 0.3f ? 0.48f : 0.68f) + FMath::Abs(P.X / W - 0.5f) * 0.12f;
			const float A = TU < At ? 0.f : TU < At + 0.16f ? (FMath::Fmod((TU - At) * 26.f, 1.f) < 0.5f ? 1.f : 0.2f) : 1.f;
			if (A < 1.f)
			{
				MechAlertsUi::ScaleAlpha(F, It, A);
			}
		}
		if (TU < 0.9f)
		{
			const float K = TU / 0.9f;
			Hud.DrawRect(MechAlertsUi::Fade(MechHudTheme::Get().Glow, 0.22f * (1.f - K)), 0.f, H * (1.f - K), W, 2.f * UI);
		}
	}

	// ---- EMP：横向撕裂、整体抖、闪断、读数乱跳、偏冷色。开头 0.15 秒最狠，0.9 秒内减到三成，之后每 0.3 秒有三成机会猛一下
	if (bAlive && Mech->IsEmped())
	{
		const float TE = Real - EmpStart;
		float I = TE < 0.15f ? 1.f : TE < 0.9f ? FMath::Lerp(1.f, 0.3f, (TE - 0.15f) / 0.75f) : 0.3f;
		FRandomStream Win(FMath::FloorToInt(TE / 0.3f) * 31 + 7);
		if (TE > 0.9f && Win.FRand() < 0.3f && FMath::Fmod(TE, 0.3f) < 0.09f)
		{
			I = 0.85f;
		}
		FRandomStream R(static_cast<int32>(GFrameCounter * 7919u));
		const bool bBlack = R.FRand() < 0.1f * I;
		struct FBand
		{
			float Y0, Y1, DX;
		};
		TArray<FBand, TInlineAllocator<3>> Bands;
		if (R.FRand() < I)
		{
			const int32 NB = 1 + R.RandHelper(3);
			for (int32 b = 0; b < NB; ++b)
			{
				const float Y0 = static_cast<float>(R.FRandRange(0.f, H));
				const float BandH = static_cast<float>(R.FRandRange(16.f, 90.f)) * UI;
				const float BandDX = (R.FRand() < 0.5f ? -1.f : 1.f) * static_cast<float>(R.FRandRange(8.f, 36.f)) * UI * I;
				Bands.Add({Y0, Y0 + BandH, BandDX});
			}
		}
		const float Jx = R.FRandRange(-3.f, 3.f) * UI * I;
		const float Mix = 0.6f * I;
		const FLinearColor Cool(FMath::Lerp(1.f, 0.78f, Mix), FMath::Lerp(1.f, 0.97f, Mix), FMath::Lerp(1.f, 1.18f, Mix), 1.f);
		for (FMechHudFrame::FItem& It : F.Items)
		{
			const float Y = MechAlertsUi::RefPoint(F, It).Y;
			float DX = Jx;
			for (const FBand& B : Bands)
			{
				DX += (Y >= B.Y0 && Y < B.Y1) ? B.DX : 0.f;
			}
			if (DX != 0.f)
			{
				MechAlertsUi::Shift(F, It, FVector2f(DX, 0.f));
			}
			MechAlertsUi::Tint(F, It, Cool);
			if (It.Kind == FMechHudFrame::EKind::Text)
			{
				// 数字乱跳：按文字内容 + 帧号取随机（字和它的影子是两个元素，乱成一样的）
				FRandomStream TR(static_cast<int32>(GetTypeHash(It.Text) ^ static_cast<uint32>(GFrameCounter)));
				if (TR.FRand() < 0.35f * I)
				{
					for (TCHAR& Ch : It.Text.GetCharArray())
					{
						if (Ch >= TEXT('0') && Ch <= TEXT('9'))
						{
							Ch = TEXT('0') + static_cast<TCHAR>(TR.RandHelper(10));
						}
					}
				}
			}
			if (bBlack)
			{
				MechAlertsUi::ScaleAlpha(F, It, 0.12f);
			}
		}
		// 两三道很淡的全宽扫描线
		for (int32 k = 0; k < 3; ++k)
		{
			Hud.DrawRect(FLinearColor(0.7f, 0.85f, 1.f, 0.07f * I), 0.f, R.FRandRange(0.f, H), W, R.FRandRange(1.f, 3.f) * UI);
		}
		if (bBlack && Real >= NextStatic && bMine)
		{
			NextStatic = Real + 0.25f;
			Cue(Hud, SndStatic, TEXT("干扰"), 0.5f, Real, 0.25f, R.FRandRange(0.85f, 1.2f));
		}
	}

	// ---- 受重击：整个 HUD 跟着抖（越重越大、衰减），最重一档开头 0.1 秒再横着撕一道
	const float TS = Real - ShakeStart;
	if (bAlive && TS >= 0.f && TS < ShakeLen)
	{
		const float K = FMath::Square(1.f - TS / ShakeLen);
		const FVector2f O(FMath::Sin(Real * 97.f) * ShakeAmp * UI * K, FMath::Cos(Real * 73.f) * ShakeAmp * 0.6f * UI * K);
		float TearY0 = -1.f, TearY1 = -1.f, TearDX = 0.f;
		if (bShakeTear && TS < 0.1f)
		{
			FRandomStream R(static_cast<int32>(ShakeStart * 1000.f));
			TearY0 = H * R.FRandRange(0.3f, 0.75f);
			TearY1 = TearY0 + 40.f * UI;
			TearDX = (R.FRand() < 0.5f ? -10.f : 10.f) * UI;
		}
		for (FMechHudFrame::FItem& It : F.Items)
		{
			FVector2f Off = O;
			if (TearY1 > 0.f)
			{
				const float Y = MechAlertsUi::RefPoint(F, It).Y;
				Off.X += (Y >= TearY0 && Y < TearY1) ? TearDX : 0.f;
			}
			MechAlertsUi::Shift(F, It, Off);
		}
	}

	// ---- 被击毁：把最后一帧收成一条线熄灭（在最上面画，不跟着抖）
	const float TD = Real - PowerDownStart;
	if (TD >= 0.f && TD < 0.62f && Snap.IsValid())
	{
		AppendCollapsed(Hud, F, TD);
	}
}
