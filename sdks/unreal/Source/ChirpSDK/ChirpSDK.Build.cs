using UnrealBuildTool;

public class ChirpSDK : ModuleRules
{
	public ChirpSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Enable C++17
		CppStandard = CppStandardVersion.Cpp17;

		// Include directories
		string ChirpRootPath = System.IO.Path.GetFullPath(
			System.IO.Path.Combine(ModuleDirectory, "..", "..", "..", "..", "..")
		);

		PrivateIncludePaths.Add(System.IO.Path.Combine(ChirpRootPath, "sdks", "core", "include"));
		PrivateIncludePaths.Add(System.IO.Path.Combine(ChirpRootPath, "libs", "common", "include"));
		PrivateIncludePaths.Add(System.IO.Path.Combine(ChirpRootPath, "libs", "network", "include"));

		// Add source files
		PublicIncludePaths.Add(ModuleDirectory);

		// Public dependency modules
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities"
		});

		// Private dependency modules
		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore",
			"InputCore",
			"UMG"
		});

		// Add Chirp library sources
		string ChirpCoreSrc = System.IO.Path.Combine(ChirpRootPath, "sdks", "core", "src");
		string CommonSrc = System.IO.Path.Combine(ChirpRootPath, "libs", "common", "src");
		string NetworkSrc = System.IO.Path.Combine(ChirpRootPath, "libs", "network", "src");

		// Chirp Core SDK sources
		if (System.IO.Directory.Exists(ChirpCoreSrc))
		{
			foreach (string file in System.IO.Directory.GetFiles(ChirpCoreSrc, "*.cpp", System.IO.SearchOption.AllDirectories))
			{
				if (!file.Contains("/tests/") && !file.Contains("/test/"))
				{
					PrivateSourceFiles.Add(file);
				}
			}
		}

		// Common library sources
		if (System.IO.Directory.Exists(CommonSrc))
		{
			foreach (string file in System.IO.Directory.GetFiles(CommonSrc, "*.cc", System.IO.SearchOption.AllDirectories))
			{
				if (!file.Contains("/test/") && !file.Contains("/tests/"))
				{
					PrivateSourceFiles.Add(file);
				}
			}
		}

		// Network library sources
		if (System.IO.Directory.Exists(NetworkSrc))
		{
			foreach (string file in System.IO.Directory.GetFiles(NetworkSrc, "*.cc", System.IO.SearchOption.AllDirectories))
			{
				if (!file.Contains("/test/") && !file.Contains("/tests/"))
				{
					PrivateSourceFiles.Add(file);
				}
			}
		}

		// Platform-specific settings
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Windows
			PublicDefinitions.Add("CHIRP_PLATFORM_WINDOWS=1");
			PublicDefinitions.Add("WIN32_LEAN_AND_MEAN");
			PublicDefinitions.Add("NOMINMAX");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// macOS
			PublicDefinitions.Add("CHIRP_PLATFORM_MAC=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			// Linux
			PublicDefinitions.Add("CHIRP_PLATFORM_LINUX=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// Android
			PublicDefinitions.Add("CHIRP_PLATFORM_ANDROID=1");
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			// iOS
			PublicDefinitions.Add("CHIRP_PLATFORM_IOS=1");
		}

		// Disable warnings for third-party code
		bEnableExceptions = true;
		bEnableUndefinedIdentifierWarnings = false;
	}
}
