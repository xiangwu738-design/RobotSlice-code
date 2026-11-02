// 机体的动作：不用动画蓝图，C++ 直接混合。
// 底层是移动：Game Animation Sample 的走、跑各 8 个方向循环（已重定向到模板人形骨架），按左脚落地的同步标记对齐步伐相位，
// 步频按「步幅 × 步频 = 实际速度」算，脚不打滑；地面冲刺换半蹲滑行姿势，空中换下落循环，落地蹲一下；整个身体按加速度倾斜。
// 空中：普通下落；推进器喷射 / 滑翔时换飞行姿势（往前冲是超人式平飞，其余是悬停），高处重重落地时单膝落地。地面加起步、急停。
// 上面几层来自商城动作包（UE4 人形重定向过来，见 tools/anim_pick.py）：手上的姿势（刀在手上是这把刀的持刀待机，
// 端长枪是步枪包的瞄准姿势并按俯仰混瞄上 / 瞄下，双持手枪是双枪待机）；上身动作（拔刀 / 收刀 / 格挡 / 换弹 / 投掷）；
// 全身动作（各把刀的轻击连招、重斩，八方向闪避、四方向失衡）。攻击片段按片段里的 Hit 标记和玩法上的命中时刻对齐拉伸。
// 没有动作的枪姿（缺片段时）手臂由 UMechMeshComponent 在骨骼定稿后程序覆盖。

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "MechAnimInstance.generated.h"

class UAnimSequence;

/** 动作片段编号 */
namespace MechClip
{
	// 移动（Game Animation Sample）：走 / 跑按 F FR RR BR B BL LL FL
	constexpr int32 Idle = 0;
	constexpr int32 Walk = 1;
	constexpr int32 Run = 9;
	constexpr int32 Fall = 17;
	constexpr int32 Crouch = 18;
	// 武士刀动作包
	constexpr int32 KIdle = 19;
	constexpr int32 KLight = 20;     // 轻击四连 20–23
	constexpr int32 KHeavy = 24;
	constexpr int32 KDodge = 25;     // 闪避 25–32，方向 F FR R BR B BL L FL
	constexpr int32 KStagger = 33;   // 失衡 33–36，受击来自 前 右 后 左
	constexpr int32 KDraw = 37;
	constexpr int32 KSheathe = 38;
	constexpr int32 KBlockStart = 39;
	constexpr int32 KBlockLoop = 40;
	constexpr int32 KBlockHit = 41;
	constexpr int32 KBlockEnd = 42;
	// 其他刀（MechWeapons 的 1–6 号；0 号双手刀用上面武士刀那组）：每把一组，缺的片段用武士刀的
	constexpr int32 BladeBase = 43;
	constexpr int32 BladeStride = 10;
	constexpr int32 BladeSets = 6;
	enum EBladeSlot : int32 { BIdle, BLight1, BLight2, BLight3, BLight4, BHeavy, BBlockStart, BBlockLoop, BBlockHit, BBlockEnd };
	// 长枪（步枪包）：瞄准正中 / 瞄上 / 瞄下、换弹
	constexpr int32 RifleAim = 103;
	constexpr int32 RifleAimUp = 104;
	constexpr int32 RifleAimDown = 105;
	constexpr int32 RifleReload = 106;
	// 双持手枪
	constexpr int32 DualAim = 107;
	constexpr int32 DualReload = 108;
	// 投掷匕首
	constexpr int32 Throw = 109;
	// 飞行：悬停、平飞、单膝落地
	constexpr int32 FlyHover = 110;
	constexpr int32 FlyFast = 111;
	constexpr int32 FlyLand = 112;
	// 起步、急停
	constexpr int32 MoveStart = 113;
	constexpr int32 MoveStop = 114;
	// 蹬墙跳：飞行包「原地起飞」开头收腿再蹬直的那一段
	constexpr int32 FlyKick = 115;
	constexpr int32 Num = 116;
}

/** 角色每帧交给动画的动作层（游戏线程算好，代理里混） */
struct FMechActionAnim
{
	/** 全身动作（攻击 / 闪避 / 失衡）：片段、按玩法计的已播时间（秒）、权重；
	 *  StrikeAt > 0 时片段里的 Hit 标记对齐到这个时刻，Duration > 0 时整段拉伸到这么长 */
	int32 FullClip = INDEX_NONE;
	float FullTime = 0.f;
	float FullWeight = 0.f;
	float FullStrikeAt = 0.f;
	float FullDuration = 0.f;
	/** 这个全身动作的腿是地面步法（闪避、失衡）：空中时腿让给空中姿势（下落 / 悬停），只留上身 */
	bool bFullGroundLegs = false;
	/** 这个全身动作是出刀（10-30 刀剑手感）：腿按突进速度 / 阶段混（前冲时提前摆好砍下那一刻的弓步滑过去、砍下踩实、
	 *  可取消之后走起来就交回移动循环、空中换空中姿势），整机按突进速度前倾。FullCancel = 可取消时刻（秒，玩法时间） */
	bool bFullMelee = false;
	float FullCancel = 0.f;
	/** 动作层换片段 / 重新开始 / 权重突变时的过渡时长（秒）：从上一帧画面上的姿势（带着当时的速度往前推）缓入新姿势，
	 *  各段之间不跳。刀按快慢 0.08–0.15，闪避、失衡 0.08 */
	float BlendTime = 0.1f;
	/** 离上一次蹬墙跳过了多久（秒）：刚蹬时腿播收腿再蹬直 */
	float KickTime = 100.f;
	/** 上身动作（拔刀 / 收刀 / 格挡）：只盖 spine_01 以上 */
	int32 UpperClip = INDEX_NONE;
	float UpperTime = 0.f;
	float UpperWeight = 0.f;
	float UpperDuration = 0.f;
	bool bUpperLoop = false;
	/** 刀在手上：上身换成这把刀的持刀待机（0–1） */
	int32 StanceClip = MechClip::KIdle;
	float StanceWeight = 0.f;
	/** 枪在手上：上身换成这把枪的瞄准姿势（缺片段为 INDEX_NONE）；长枪再按俯仰（度，向上为正）混瞄上 / 瞄下 */
	int32 GunClip = INDEX_NONE;
	float GunWeight = 0.f;
	float AimPitch = 0.f;
	/** 端枪交战（0–1）：上身按端枪动作在模型空间的朝向立住（不跟着下落、后退、平飞时的骨盆歪）；
	 *  全身动作层（起步急停、单膝落地、闪避）在上身让给端枪；空中冲刺不用平飞姿势改悬停（平飞时上身是躺平的，枪口拧不过来） */
	float GunAim = 0.f;
	/** 重枪开火时整机后仰（世界水平方向 × 角度，度；以脚为支点，和加速度倾斜叠在一起） */
	FVector RecoilLean = FVector::ZeroVector;
};

struct FMechAnimProxy : public FAnimInstanceProxy
{
	FMechAnimProxy() = default;
	explicit FMechAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	struct FClip
	{
		const UAnimSequence* Seq = nullptr;
		float Length = 1.f;
		/** 根骨平均水平速度（动画空间，厘米/秒） */
		float Speed = 0.f;
		/** 平均一个步伐周期（左右各一步）的时长；各次左脚落地的时间（循环里通常有好几步） */
		float Cycle = 1.f;
		TArray<float, TInlineAllocator<8>> Plants;
		/** 攻击片段里挥砍最快的时刻（Hit 标记），没有为 0 */
		float HitAt = 0.f;		/** 这一帧移动层的权重、采样时间 */
		float Weight = 0.f;
		float Time = 0.f;
	};
	FClip Clips[MechClip::Num];

	/** 动作层的采样时间（片段时间） */
	float MapTime(int32 Clip, float T, float StrikeAt, float Duration, bool bLoop) const;
	/** 出刀片段的采样时间（10-31 连招段间跳变，-MechComboOld 换回 MapTime）：按阶段分段映射，每段播片段不超过 mech.MeleeClipRate 倍速——
	 *  命中前从 MeleeClipStart 播到 Hit 标记（起手太长就裁掉开头）、命中到可取消播一小段收刀、之后才往收招走（不播到片段结尾，权重淡出交给待机）。
	 *  命中时刻（StrikeAt ↔ Hit 标记）和以前一样 */
	float MeleeClipTime(int32 Clip, float T, float StrikeAt, float Cancel, float Duration) const;
	/** 这一招从片段的哪一刻开始播（命中前按倍速上限往回推，片段起手比这长的部分不播） */
	float MeleeClipStart(int32 Clip, float StrikeAt) const;

	// ---- 出刀的全身混合、动作层之间的过渡（10-30 刀剑手感，-MechMeleeOld 关掉对比）
	/** 出刀：上身按片段时间，骨盆和腿按「腿的时间」（突进时提前走到砍下那一刻的弓步）、按阶段 / 速度 / 空中让给移动层 */
	void MeleeLayer(FPoseContext& Output, const TArray<bool, TInlineAllocator<160>>& Upper, FCompactPoseBoneIndex Pelvis, FCompactPoseBoneIndex Spine);
	/** 动作层出现跳变（换片段、重新开始、权重一帧变很多）时：从上一帧画面上的姿势出发、带着当时各骨头的速度往前推（速度衰减），
	 *  按平滑曲线混到这一帧算出来的新姿势（「死混合」，比两段片段硬切 / 从静止姿势淡入都顺） */
	void TransitionBlend(FCompactPose& Pose);
	/** 突进程度（0–1，按速度、出刀命中前只升不降）、腿交给移动循环的程度（可取消之后走起来）、出刀的前倾 */
	float MeleeLungeW = 0.f;
	float MeleeMoveLegs = 0.f;
	/** 10-31：这一段出刀是从待机 / 移动起手的（不是接在上一段出刀后面）——起手照旧从片段第 0 帧播，不裁；上一帧是不是在出刀 */
	bool bMeleeFromIdle = true;
	bool bPrevMelee = false;	float MeleeLegTime = 0.f;
	/** 上一帧的动作层（判断跳变用） */
	int32 PrevFullClip = INDEX_NONE;
	float PrevFullTime = 0.f;
	float PrevFullWeight = 0.f;
	int32 PrevUpperClip = INDEX_NONE;
	float PrevUpperTime = 0.f;
	float PrevUpperWeight = 0.f;
	/** 过渡：这一帧要开始（Update 里判断、Evaluate 里拍下起点）、已过去的时间、总时长 */
	bool bBlendStart = false;
	float BlendElapsed = 10.f;
	float BlendDuration = 0.1f;
	float FrameDt = 1.f / 60.f;
	float LastFrameDt = 1.f / 60.f;
	/** 最近两帧画面上的局部姿势（骨头按紧凑下标）和过渡起点姿势、各骨头的角速度（弧度/秒）、平移速度 */
	TArray<FTransform> LastPose;
	TArray<FTransform> LastPose2;
	TArray<FTransform> BlendFrom;
	TArray<FVector> BlendAngVel;
	TArray<FVector> BlendLinVel;

	/** 腿部关节限位（Evaluate 最后一步，地面 / 空中 / 出刀 / 闪避 / 失衡 / 落地都生效，见 MechAnimInstance.cpp 里的限位表）：
	 *  髋、膝、踝按机械关节的范围软限位（膝只能单向弯，侧弯、扭转只留几度），空中再收紧膝盖和抬腿（原来的空中收腿限幅）；
	 *  然后每根腿骨每帧最多转多少度（混合权重突变、换片段时一帧大角度跳）。-MechNoLegLimit 整个关掉，-MechAnimOldFall 只关空中收紧。
	 *  RawKnee / RawHip 记这一帧限位前的膝弯曲、髋前屈（左、右，度），-MechAnimLog 打出来 */
	void LimitLegs(FCompactPose& Pose);
	float RawKnee[2] = {0.f, 0.f};
	float RawHip[2] = {0.f, 0.f};
	/** 这一帧的时长（Update 开头记，暂停时为 0）、上一帧输出的腿骨局部旋转（左右 × 髋膝踝）、上一次算的真实时间（隔太久不限速） */
	float LegDt = 0.f;
	FQuat PrevLeg[2][3];
	bool bPrevLeg = false;
	/** 上一帧限位前的弯曲角、扭转角（左右 × 髋膝踝 × 两个量）：过 ±180 度时按连续性展开（膝盖折过 180 度不当成反折） */
	float PrevRawAng[2][3][2] = {};
	double PrevLegClock = 0.0;
	/** -MechAnimLog 统计（上一行日志以来）：各量限位前 / 后的极值（度）、每帧最大转角（限速前 / 后）、限速起作用的帧数 */
	enum ELegStat { HipFlexMax, HipFlexMin, HipSideMax, HipSideMin, HipTwist, KneeFlexMax, KneeFlexMin, KneeSide, KneeTwist,
		AnkPitchMax, AnkPitchMin, AnkSide, AnkRoll, StepMax, NumLegStats };
	float LegStatRaw[NumLegStats] = {};
	float LegStatOut[NumLegStats] = {};
	int32 LegCapped = 0;
	void ResetLegStats();
	/** 10-31 阵营机体：这台网格的头 / 踝限位（PreUpdate 按网格查 MechCatalog::FrameBodyForMesh，摄影棚、结算展示的网格也认得）。
	 *  HeadYawMax / HeadPitchMax（度，0 = 不限）：维克托中 / 重下沉炮塔头相对胸的软限位；AnkleUpMax：脚尖上翘上限（相位中 / 重 15，其余 35） */
	const UObject* LimitsFor = nullptr;
	float HeadYawMax = 0.f;
	float HeadPitchMax = 0.f;
	float AnkleUpMax = 35.f;
	/** 头部转动限位（Evaluate 里腿部限位之后）：头相对胸（neck_01 → neck_02 → head 合起来）拆成偏航 / 俯仰 / 侧歪各自软限位，
	 *  收回的转动加在 neck_01 上。-MechNoHeadLimit 关掉对比 */
	void LimitHead(FCompactPose& Pose);
	/** -MechAnimLog：上一行日志以来头相对胸的偏航 / 俯仰 / 侧歪绝对值最大（限位前、后，度） */
	float HeadStatRaw[3] = {0.f, 0.f, 0.f};
	float HeadStatOut[3] = {0.f, 0.f, 0.f};

	// 游戏线程上抄过来的状态
	FVector LocalVelocity = FVector::ZeroVector;  // 机体朝向坐标系，已除以网格缩放
	/** 石林飞行器飞行中：身子顺着航迹压下去 */
	bool bGlideLean = false;
	FVector WorldVelocity = FVector::ZeroVector;
	FTransform MeshToWorld = FTransform::Identity;
	bool bAir = false;
	bool bBoost = false;
	bool bThrust = false;
	bool bValid = false;
	FMechActionAnim Action;
	/** -MechAnimLog：本机玩家那台每 0.1 秒打一行各层权重（查空中动作乱用） */
	bool bLogThis = false;
	float LogClock = 0.f;
	float LogTime = 0.f;

	// 混合状态
	float AirW = 0.f;
	/** 空中：平飞、悬停的比重（在空中权重里分） */
	float FlyW = 0.f;
	float HoverW = 0.f;
	float FlyTime = 0.f;
	float HoverTime = 0.f;
	/** 水平减速度（厘米/秒²，抹平过；正 = 在减速）：空中减速时不平飞 */
	float AirDecel = 0.f;
	/** 重重落地后单膝落地播了多久（没在播为负） */
	float HeroTime = -1.f;
	/** 起步、急停播了多久（没在播为负）；上一帧水平速度、静止了多久 */
	float StartTime = -1.f;
	float StopTime = -1.f;
	float PrevSpeed = 0.f;
	float StillTime = 0.f;
	float GlideW = 0.f;
	float LandW = 0.f;
	float MoveW = 0.f;
	float Gait = 0.f;
	float Angle = 0.f;
	/** 步伐相位（单位：步伐周期，整数部分 = 第几个周期） */
	float Phase = 0.f;
	float IdleTime = 0.f;
	float FallTime = 0.f;
	/** 10-31：下落片段是 UE4 跳跃循环（首尾接不上）→ 在中间一段按余弦来回播，不首尾循环 */
	bool bFallSwing = false;
	float CrouchTime = 0.f;
	float StanceTime = 0.f;
	float LastVZ = 0.f;
	bool bWasAir = false;
	FVector PrevVelocity = FVector::ZeroVector;
	FVector LeanVec = FVector::ZeroVector;  // 世界水平方向 × 角度（度）
	FQuat Lean = FQuat::Identity;           // 网格组件空间
	/** 走 / 跑的平均速度（动画空间），用来分档和算步频上下限 */
	float WalkSpeed = 150.f;
	float RunSpeed = 400.f;
};

UCLASS(Transient, NotBlueprintable)
class UMechAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UMechAnimInstance();

	/** 片段（顺序见 MechClip），缺了的位置是空的（其他刀缺的用武士刀的补上） */
	UPROPERTY()
	TArray<TObjectPtr<UAnimSequence>> Clips;

	bool HasClip(int32 Clip) const { return Clips.IsValidIndex(Clip) && Clips[Clip] != nullptr; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FMechAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }
};
