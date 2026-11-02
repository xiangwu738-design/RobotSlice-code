#include "MechWeapons.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	using MechWeapons::FBlade;
	using MechWeapons::FGun;

	// 顺序和 EMechShooter 一致。模型都按 lookdev/weapon_normalize.py 摆过：原点在握把、枪管沿 +X
	// 伤害 10-30 整体提高约 15%（用户：「枪械数值略微提高一点」）；同日晚上按玩家反馈改了冲锋枪、霰弹枪、精准步枪（→ 充能步枪）、狙击枪、双持手枪，
	// 每把改了什么写在各自那一行上面。DPS / TTK 对照表在 README「战斗」
	FGun Guns[] = {
		// 名字, 说明, 模型, 枪口, 弹匣, 间隔, 伤害, 冲击, 弹速, 飞行时间, 换弹, 散布, 瞄准散布倍数, 弹丸, 曳光, 后坐,
		// 热量, 充能时间, 充能激光每秒伤害, 激光每秒冲击, 耗能, 吸血比例, 连中加成, 双持, 左手握点
		{TEXT("步枪"), TEXT("连射稳定，连续打中同一目标伤害越打越高"),
			TEXT("/Game/Weapons/Meshes/SM_Gun3"), FVector(77.f, 0.f, 17.f),
			36, 0.11f, 86.f, 22.f, 33000.f, 1.1f, 2.2f, 0.55f, 0.35f, 1, 1.f, 16.f,
			0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.03f, false, FVector(30.f, 0.f, 5.f)},
		// 冲锋枪（10-30 玩家：强度过低、没特色、不能持续输出）→ 持续输出、中近距离最强：伤害 46→82、热量每发 0.022→0.0093
		// （连续开火约 7 秒才过热，以前 3 秒）、散热更快、过热锁 2→1 秒；18 米内满伤，38 米外只剩四成（距离衰减见下面 GunTable）。
		// 10-30 晚试玩后（玩家：稍微削弱一点点）伤害 82→78（−5%）：18 米内 60 秒平均（含过热）约 1080/秒，步枪约 680、充能步枪约 720，
		// 和狙击枪「每发都开镜命中」的理论值（约 1100）基本持平，仍是不开镜的枪里最高
		// 11-01 用户：「冲锋枪过热快一点」→ 每发热量 0.0093→0.013：连续开火约 7 秒 → 约 5 秒过热（开火时不散热）
		{TEXT("冲锋枪"), TEXT("近距离输出最高，不用换弹；连续开火会过热，距离远伤害掉得快"),
			TEXT("/Game/Weapons/Meshes/SM_Gun6"), FVector(62.f, 0.f, 8.f),
			0, 0.065f, 78.f, 16.f, 28000.f, 0.6f, 0.f, 1.2f, 0.6f, 1, 0.8f, 10.f,
			0.013f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false, FVector(22.f, 0.f, -1.f)},
		// 霰弹枪（10-30 玩家：设计为近战优势，但面对刀剑优势不高）→ 击毁回 3000 改成按伤害 30% 吸血；15 米内冲击 ×1.5、
		// 打中正在出刀（还没砍下）的人直接打断这一刀。
		// 11-01 用户：「霰弹枪伤害x1.5倍」→ 每颗 69→104（一发满中 621→936；吸血按伤害算，跟着涨）
		{TEXT("霰弹枪"), TEXT("近距离爆发，打中回装甲，能打断对方出刀"),
			TEXT("/Game/Weapons/Meshes/SM_Gun11"), FVector(77.f, 0.f, 7.f),
			6, 0.75f, 104.f, 32.f, 24000.f, 0.45f, 2.6f, 4.5f, 0.75f, 9, 0.7f, 40.f,
			0.f, 0.f, 0.f, 0.f, 0.f, 0.3f, 0.f, false, FVector(34.f, 0.f, -3.f)},
		// 精准步枪 → 充能步枪（10-30 玩家：改成类似 Apex 旧版充能步枪）：按住充能 1 秒，期间枪口一道激光（命中扫描、所有人看得见，
		// 照到的敌人每秒 120 伤害），充满自动打出一道光束 1150（以前充满一发 960、要自己松开）；弹匣 8→6。
		// 10-30 晚试玩后（玩家：松手就发射，不会撤回）：提前松开按当时充能打出，230（不到 0.15 充能）到 1150 线性，光束粗细亮度跟着变
		{TEXT("充能步枪"), TEXT("按住充能，充满或松手就打出光束；充能时的激光也伤人，所有人都看得见"),
			TEXT("/Game/Weapons/Meshes/SM_Gun2"), FVector(87.f, 0.f, 11.f),
			6, 0.45f, 1150.f, 320.f, 60000.f, 1.f, 2.4f, 0.15f, 0.2f, 1, 1.6f, 34.f,
			0.f, 1.f, 120.f, 40.f, 0.f, 0.f, 0.f, false, FVector(40.f, 0.f, -6.f)},
		// 狙击枪（10-30 玩家：伤害、手感、追踪都要提升）：伤害 1260→1400、间隔 1.4→1.3 秒、每发耗能 35→28、弹速 950→1200 米/秒（少提前量），
		// 开镜时小范围吸附（bScopeMagnet，以前开镜完全不帮）
		{TEXT("狙击枪"), TEXT("单发重击，不用弹药、耗推进能量；开镜时子弹吸附到准星附近的敌人"),
			TEXT("/Game/Weapons/Meshes/SM_Gun9"), FVector(102.f, 0.f, 9.f),
			0, 1.3f, 1100.f, 760.f, 120000.f, 0.6f, 0.f, 2.f, 0.01f, 1, 2.2f, 50.f,
			0.f, 0.f, 0.f, 0.f, 28.f, 0.f, 0.f, false, FVector(44.f, 0.f, -4.f)},
		// 双持手枪（10-30 玩家：闪避自动装满太强，基本等于无限弹药）→ 先改成闪避装满 6 秒一次；
		// 10-30 晚试玩后（玩家：自己换弹时间变长，闪避装满改成「打出重击补满子弹」）→ 闪避装满去掉，换弹 1.6→2.4 秒，
		// 近战重斩砍中敌人时弹匣立即补满（bHeavyRefill，鼓励刀枪切换）。
		// 10-31 单看开枪偏弱（打中型约 22 秒、步枪约 14 秒）→ 伤害 104→128、弹匣 24→28、间隔 0.14→0.13：
		// 10 秒平均 499→约 717/秒（步枪 791），打穿中型约 15 秒；弹匣内瞬时约 985/秒，靠重斩补满省掉换弹时更快。
		// 11-01 用户：「手枪近战精准度提升」→ 腰射散布 0.8°→0.5°（贴身来回闪的时候基本不用开镜），开镜 0.4°→0.25°；
		// 「手枪还是冲刺回弹，没有回弹cd」→ 快速闪避立即装满、没有冷却（bDodgeRefill），重斩补满照旧
		{TEXT("双持手枪"), TEXT("连射；快速闪避立刻装满弹匣，重斩砍中也补满"),
			TEXT("/Game/Weapons/Meshes/SM_Pistol7"), FVector(32.f, 0.f, 14.f),
			28, 0.13f, 128.f, 28.f, 30000.f, 0.9f, 2.4f, 0.5f, 0.5f, 1, 0.9f, 12.f,
			0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, true},
	};

	/** 各把枪表里位置参数之外的特色（距离衰减、重斩补满、霰弹近距离、狙击吸附），第一次取表时补上 */
	const FGun* GunTable()
	{
		static const bool bInit = [] {
			FGun& Smg = Guns[static_cast<int32>(EMechShooter::SMG)];
			Smg.FalloffStart = 1800.f;
			Smg.FalloffEnd = 3800.f;
			Smg.FalloffMin = 0.4f;
			FGun& Shotgun = Guns[static_cast<int32>(EMechShooter::Shotgun)];
			Shotgun.CloseRange = 1500.f;
			Shotgun.CloseImpact = 1.5f;
			Shotgun.bInterruptMelee = true;
			Guns[static_cast<int32>(EMechShooter::Sniper)].bScopeMagnet = true;
			Guns[static_cast<int32>(EMechShooter::DualPistol)].bHeavyRefill = true;
			Guns[static_cast<int32>(EMechShooter::DualPistol)].bDodgeRefill = true;  // 11-01 用户要回闪避装满（不要冷却）
			// 10-31 握持：右手握把（截面中心、往上方向、前后 / 左右半宽和圆角、沿轴范围）和左手托的护木（截面中心、左右 / 上下半宽）。
			// 用 Blender 从模型里量的（侧视剪影 + 从截面中心往外打射线），量法和图在 README「握持」
			auto SetGrip = [](EMechShooter G, const FVector& C, const FVector& Up, const FVector& Size, const FVector2D& Along,
				const FVector& Fore, const FVector2D& ForeHalf)
			{
				FGun& Gun = Guns[static_cast<int32>(G)];
				Gun.GripCenter = C;
				Gun.GripUp = Up.GetSafeNormal();
				Gun.GripSize = Size;
				Gun.GripAlong = Along;
				Gun.ForeCenter = Fore;
				Gun.ForeHalf = ForeHalf;
			};
			SetGrip(EMechShooter::Rifle, FVector(-1.8f, 0.f, 0.7f), FVector(0.2f, 0.f, 0.98f), FVector(4.4f, 2.2f, 1.6f), FVector2D(-6.2f, 7.f),
				FVector(30.f, 0.f, 11.5f), FVector2D(1.6f, 2.5f));
			SetGrip(EMechShooter::SMG, FVector(0.64f, 0.f, 2.3f), FVector(0.5f, 0.f, 0.86f), FVector(1.9f, 1.6f, 1.2f), FVector2D(-3.5f, 4.5f),
				FVector(22.f, 0.f, 4.2f), FVector2D(2.2f, 1.9f));
			// 杠杆霰弹枪：握的是枪托的握颈（顺着枪托斜着），手指伸进杠杆环
			SetGrip(EMechShooter::Shotgun, FVector(-6.45f, 0.f, 2.f), FVector(0.91f, 0.f, 0.41f), FVector(3.2f, 3.4f, 2.2f), FVector2D(-12.f, 6.f),
				FVector(34.f, 0.f, 7.5f), FVector2D(3.3f, 2.5f));
			// 充能步枪没有普通握把：握扳机环后面那根往后斜的杆（手指伸进环里扣扳机）
			SetGrip(EMechShooter::Marksman, FVector(-8.f, 0.f, 2.6f), FVector(-0.5f, 0.f, 0.87f), FVector(1.6f, 2.1f, 1.2f), FVector2D(-3.5f, 3.5f),
				FVector(40.f, 0.f, -0.3f), FVector2D(2.7f, 3.f));
			SetGrip(EMechShooter::Sniper, FVector(1.1f, 0.f, 1.45f), FVector(0.2f, 0.f, 0.98f), FVector(2.1f, 1.5f, 1.f), FVector2D(-3.f, 3.5f),
				FVector(44.f, 0.f, 4.1f), FVector2D(2.3f, 3.f));
			SetGrip(EMechShooter::DualPistol, FVector(-0.6f, 0.f, -2.5f), FVector(0.2f, 0.f, 0.98f), FVector(3.f, 2.1f, 1.5f), FVector2D(-4.f, 5.5f),
				FVector::ZeroVector, FVector2D(2.f, 2.f));
			// 10-31 握持第二版：充能步枪的斜杆顶往后倒 30 度（一般握把是往前 10 度左右），照一般握把那样贴着指根线握（0.225），
			// 手腕比动作里多折 56 度（步枪 25 度，截图里手往上翘着）。改成斜杆反着斜过手心（-0.2：拇指那头靠手腕、小指那头靠指尖），
			// 多折降到 36 度；再斜（-0.25 起）杆就压到拇指根、小指包不过去了（原型 tools/grip_lab，四个斜度的对比见 README「握持」）。
			// 测试：-MechChargeGripTilt=Z 临时改（握把轴在手里 = (1, 0, Z)）
			{
				float Tilt = -0.2f;
				FParse::Value(FCommandLine::Get(), TEXT("MechChargeGripTilt="), Tilt);
				Guns[static_cast<int32>(EMechShooter::Marksman)].GripHandDir = FVector(1.f, 0.f, Tilt);
			}
			return true;
		}();
		return Guns;
	}

	// 顺序和 EMechMeleeWeapon 一致。打法按用户的表：攻击快慢 / 伤害 / 耐性削减；10-30 每把加了一个特殊机制（数值在 MechWeapons.h，
	// 打法在 UMechCombatComponent），大剑、钝器的削失衡明显下调（玩家：1v1 几乎无法抗衡）
	FBlade Blades[] = {
		// 名字, 说明, 模型, 快慢, 伤害, 冲击, 削失衡, 够得着, 双刀, 匕首数, 回复, 匕首伤害, 匕首冲击, 匕首速度
		{TEXT("化境"), TEXT("攻击范围最大；第一下伤害最高，越打越低，停手恢复；打中失衡的敌人追加伤害；没有击退"),
			TEXT("/Game/Weapons/Meshes/SM_Sword07"), 1.f, 1.f, 1.f, 0.55f, 520.f, false, 0, 0.f, 0.f, 0.f, 0.f},
		{TEXT("快刀"), TEXT("出刀快，连续砍中越砍越快，移速也更快"),
			TEXT("/Game/Weapons/Meshes/SM_Sword11"), 0.8f, 0.85f, 0.8f, 0.4f, 370.f, false, 0, 0.f, 0.f, 0.f, 0.f,
			true, FVector(-6.45f, 3.95f, -1.10f), FQuat(0.00629f, -0.28633f, -0.19775f, 0.93748f), FVector(-9.f, 0.f, 0.f)},  // 长剑包，双手
		{TEXT("单手刀"), TEXT("闪避后马上轻击，能远距离突进斩"),
			TEXT("/Game/Weapons/Meshes/SM_Sword13"), 0.88f, 0.9f, 0.85f, 0.4f, 350.f, false, 0, 0.f, 0.f, 0.f, 0.f,
			true, FVector(-6.11f, 4.07f, -2.30f), FQuat(0.45294f, -0.01837f, 0.89037f, 0.04194f)},  // 单手剑包
		// 削失衡 0.6→0.42（中型框架：四连 + 重斩 1747→1223，以前光这一套就失衡，现在还要再补几枪）
		{TEXT("大剑"), TEXT("慢但伤害高；重斩时不被打断、受伤减少"),
			TEXT("/Game/Weapons/Meshes/SM_Sword14"), 1.3f, 1.45f, 1.25f, 0.42f, 440.f, false, 0, 0.f, 0.f, 0.f, 0.f,
			true, FVector(-10.45f, 5.39f, -0.70f), FQuat(0.64021f, -0.05469f, 0.76401f, 0.05856f), FVector(-17.f, 0.f, 0.f)},  // 大剑包，双手
		{TEXT("双刀"), TEXT("出刀很快、伤害低；砍中会让对方受到的伤害增加"),
			TEXT("/Game/Weapons/Meshes/SM_Sword16"), 0.65f, 0.6f, 0.6f, 0.25f, 340.f, true, 0, 0.f, 0.f, 0.f, 0.f,
			true, FVector(-9.15f, 6.06f, -0.31f), FQuat(0.59658f, -0.20384f, 0.77470f, 0.04879f)},  // 双刀包
		// 削失衡 0.8→0.45（中型框架：以前光四连就 1656 直接失衡，现在四连 932、四连 + 重斩 1573 才失衡）
		// 11-01 用户：「钝器减少伤害，增加破韧效果」（破韧 = 削韧性条，也就是失衡）→ 伤害倍数 1.35→1.1（四连 + 重斩循环约 1119→912/秒，近战里最低）；
		// 削失衡 0.45→0.65（失衡积累按冲击算、不看伤害；中型框架上限 1400：四连 932→1346，差一点，再中任何一下就失衡；没回到 0.8，那时光四连就失衡，玩家说 1v1 无法抗衡）
		{TEXT("钝器"), TEXT("很慢，削失衡很快；重斩砍在格挡上直接破防"),
			TEXT("/Game/Weapons/Meshes/SM_Sword20"), 1.5f, 1.1f, 1.5f, 0.65f, 420.f, false, 0, 0.f, 0.f, 0.f, 0.f,
			true, FVector(-7.08f, 4.92f, -13.85f), FQuat(-0.01428f, 0.76663f, -0.06417f, -0.63872f), FVector(-30.f, 0.f, 0.f)},  // 锤子包（右手握在靠锤头那一端）；握点往刀体挪 12 厘米，下面多留一段柄给左手
		// 冲击 130→160（10-30 玩家：匕首命中加轻微击退；冲击过 150 才有击退和顿帧），削失衡 0.4→0.33，一把积累的失衡不变（约 53）
		{TEXT("投掷匕首"), TEXT("按住左键投掷，会追踪目标，每第 5 把威力 3 倍；右键近身挥刀，不能格挡"),
			TEXT("/Game/Weapons/Meshes/SM_Sword03"), 1.f, 1.f, 1.f, 0.33f, 300.f, false, 4, 1.6f, 240.f, 160.f, 7500.f,
			true, FVector(-8.87f, 4.85f, 1.55f), FQuat(0.04369f, 0.49782f, -0.06716f, 0.86357f)},  // 双匕首包（反手握）
	};
	/** 10-31 握持：各把刀的柄（右手空腔对准的点、半径、柄的 x 范围）、刀身在右手里的方向（手坐标 k 往拇指、n 手心、d 往指尖）、
	 *  刃口朝向、双手武器左手对准的 x。方向照动作包：双手动作里左手相对右手的位置（tools/grip_lab 里的 left_rel.py，README「握持」）——
	 *  快刀（长剑包）和双手刀（武士刀包）左手在右手的小指侧、往手腕方向，刀身约 (0.85, 0, 0.53)，取它和指根线 (0.98, 0, 0.22) 的中间；
	 *  其余照 10-30 从动作包算的挂点的刀身方向（大剑、双刀、钝器基本横过手心，单手刀斜一点，匕首反手） */
	const FBlade* BladeTable()
	{
		static const bool bInit = [] {
			auto SetGrip = [](EMechMeleeWeapon W, const FVector& Grip, float R, const FVector2D& Range, const FVector& Dir, float Edge, float LeftAt)
			{
				FBlade& B = Blades[static_cast<int32>(W)];
				B.Grip = Grip;
				B.GripRadius = R;
				B.GripRange = Range;
				B.HandDir = Dir;
				B.EdgeZ = Edge;
				B.LeftAt = LeftAt;
			};
			SetGrip(EMechMeleeWeapon::Katana, FVector(4.5f, 0.f, 0.f), 1.5f, FVector2D(-9.f, 9.5f), FVector(0.93f, -0.08f, 0.37f), -1.f, -4.5f);
			SetGrip(EMechMeleeWeapon::QuickBlade, FVector(1.5f, 0.f, -1.84f), 1.5f, FVector2D(-12.f, 7.f), FVector(0.93f, -0.02f, 0.37f), -1.f, -7.f);
			SetGrip(EMechMeleeWeapon::OneHand, FVector(-3.f, 0.f, 0.4f), 1.6f, FVector2D(-15.f, 3.f), FVector(0.78f, 0.2f, 0.59f), -1.f, 0.f);
			SetGrip(EMechMeleeWeapon::GreatSword, FVector(3.5f, 0.f, 0.9f), 2.6f, FVector2D(-22.5f, 10.f), FVector(0.97f, 0.14f, 0.19f), -1.f, -12.f);
			SetGrip(EMechMeleeWeapon::TwinBlades, FVector(2.f, 0.f, 0.42f), 2.f, FVector2D(-17.f, 15.f), FVector(0.95f, -0.04f, 0.32f), -1.f, 0.f);
			SetGrip(EMechMeleeWeapon::Blunt, FVector(1.f, 0.1f, 0.93f), 2.f, FVector2D(-34.f, 5.5f), FVector(0.96f, 0.18f, 0.19f), 1.f, -20.f);
			SetGrip(EMechMeleeWeapon::Dagger, FVector(-1.5f, 0.f, 0.4f), 0.9f, FVector2D(-7.5f, 3.f), FVector(-0.84f, -0.21f, -0.5f), -1.f, 0.f);
			return true;
		}();
		return Blades;
	}
	static_assert(UE_ARRAY_COUNT(Guns) == static_cast<int32>(EMechShooter::DualPistol) + 1, "射击武器表要和枚举一致");
	static_assert(UE_ARRAY_COUNT(Blades) == static_cast<int32>(EMechMeleeWeapon::Dagger) + 1, "近战武器表要和枚举一致");
}

int32 MechWeapons::NumGuns()
{
	return UE_ARRAY_COUNT(Guns);
}

int32 MechWeapons::NumBlades()
{
	return UE_ARRAY_COUNT(Blades);
}

const MechWeapons::FGun& MechWeapons::Gun(EMechShooter Shooter)
{
	return GunTable()[FMath::Clamp(static_cast<int32>(Shooter), 0, NumGuns() - 1)];
}

const MechWeapons::FBlade& MechWeapons::Blade(EMechMeleeWeapon Melee)
{
	return BladeTable()[FMath::Clamp(static_cast<int32>(Melee), 0, NumBlades() - 1)];
}
