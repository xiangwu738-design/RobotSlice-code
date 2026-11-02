// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RobotSlice : ModuleRules
{
	public RobotSlice(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"AdvancedLightingRuntime",
			"Niagara",
			"RenderCore",
			"RHI",
			"ApplicationCore",
			"NetCore",
			"AnimationCore",
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"RobotSlice",
			"RobotSlice/Variant_Platforming",
			"RobotSlice/Variant_Platforming/Animation",
			"RobotSlice/Variant_Combat",
			"RobotSlice/Variant_Combat/AI",
			"RobotSlice/Variant_Combat/Animation",
			"RobotSlice/Variant_Combat/Gameplay",
			"RobotSlice/Variant_Combat/Interfaces",
			"RobotSlice/Variant_Combat/UI",
			"RobotSlice/Variant_SideScrolling",
			"RobotSlice/Variant_SideScrolling/AI",
			"RobotSlice/Variant_SideScrolling/Gameplay",
			"RobotSlice/Variant_SideScrolling/Interfaces",
			"RobotSlice/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
