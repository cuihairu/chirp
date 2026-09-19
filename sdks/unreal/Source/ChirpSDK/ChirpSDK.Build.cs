// ChirpSDK Unreal module: links the native chirp::sdk protocol core built
// from the chirp repo (sdks/core). This file is compiled by UnrealBuildTool
// inside an Unreal project — the chirp repo's own CI cannot compile it; what
// CI does compile and unit-test is the native core itself (sdk_core_tests).
using System;
using System.IO;
using UnrealBuildTool;

public class ChirpSDK : ModuleRules
{
    public ChirpSDK(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });

        // CHIRP_SDK_NATIVE_DIR points at the chirp repo checkout. The native
        // core must be built first, e.g.:
        //   cd <chirp> && cmake -B build <toolchain flags> && \
        //   cmake --build build --target chirp_core_sdk_static
        // The resulting static lib (libchirp_core_sdk.a / chirp_core_sdk.lib)
        // and the sdks/core/include headers are all this module needs.
        string ChirpRoot = Environment.GetEnvironmentVariable("CHIRP_SDK_NATIVE_DIR");
        if (string.IsNullOrEmpty(ChirpRoot))
        {
            throw new BuildException(
                "CHIRP_SDK_NATIVE_DIR is not set: point it at the chirp repo checkout " +
                "containing sdks/core (built) before compiling the ChirpSDK module.");
        }

        string Includes = Path.Combine(ChirpRoot, "sdks", "core", "include");
        string Libs = Path.Combine(ChirpRoot, "build", "sdks", "core");
        if (!Directory.Exists(Includes) || !File.Exists(Path.Combine(Libs, "libchirp_core_sdk.a")))
        {
            throw new BuildException(
                "Native chirp SDK not found under " + ChirpRoot +
                " (expected sdks/core/include and build/sdks/core/libchirp_core_sdk.a). " +
                "Build chirp first: cmake -B build && cmake --build build --target chirp_core_sdk_static");
        }

        PublicIncludePaths.Add(Includes);
        PublicAdditionalLibraries.Add(Path.Combine(Libs, "libchirp_core_sdk.a"));

        // The static core depends on asio/protobuf transitively; those symbols
        // are already inside libchirp_core_sdk.a except protobuf/abseil, which
        // the chirp build links statically into it on Linux/macOS. Windows
        // builds need chirp_core_sdk.lib plus its protobuf import libs — see
        // sdks/unreal/README.md for the per-platform link checklist.
    }
}
