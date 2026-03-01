using UnrealBuildTool;
using System.IO;
using System.Collections.Generic;

public class LlamaCpp : ModuleRules
{
	private static void RequireFiles(string platformName, IEnumerable<string> requiredPaths)
	{
		List<string> missingFiles = new List<string>();

		foreach (string requiredPath in requiredPaths)
		{
			if (!File.Exists(requiredPath))
			{
				missingFiles.Add(requiredPath);
			}
		}

		if (missingFiles.Count > 0)
		{
			throw new BuildException(
				$"LlamaCpp preflight failed for {platformName}. Missing required native binaries:\n - " +
				string.Join("\n - ", missingFiles) +
				"\nAdd the missing files under ThirdParty/llama or ThirdParty/whisper before building.");
		}
	}

	public LlamaCpp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"Projects",
			"AudioCapture",
			"AudioCaptureCore",
			"AudioMixer"
		});

		string PluginDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		string ThirdPartyPath = Path.Combine(PluginDir, "ThirdParty", "llama");
		string IncludePath = Path.Combine(ThirdPartyPath, "include");

		string WhisperThirdPartyPath = Path.Combine(PluginDir, "ThirdParty", "whisper");
		string WhisperIncludePath = Path.Combine(WhisperThirdPartyPath, "include");

		PublicIncludePaths.Add(IncludePath);
		PublicIncludePaths.Add(WhisperIncludePath);

		// llama/ggml use shared libs on all platforms; whisper uses shared on all platforms
		// (whisper.dll/dylib/so has ggml statically linked inside - no separate ggml libs for whisper)
		PublicDefinitions.Add("LLAMA_SHARED=1");
		PublicDefinitions.Add("GGML_SHARED=1");
		PublicDefinitions.Add("WHISPER_SHARED=1");

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// llama.cpp shared libraries (with separate ggml .so files)
			string LibPath = Path.Combine(ThirdPartyPath, "lib", "Android", "arm64-v8a");
			string[] Libs = new string[] {
				"libggml-base.so",
				"libggml-cpu.so",
				"libggml.so",
				"libllama.so"
			};

			List<string> RequiredAndroidFiles = new List<string>();

			foreach (string Lib in Libs)
			{
				string FullPath = Path.Combine(LibPath, Lib);
				RequiredAndroidFiles.Add(FullPath);
				PublicAdditionalLibraries.Add(FullPath);
				RuntimeDependencies.Add(FullPath);
			}

			// whisper.so is self-contained (ggml statically linked inside)
			string WhisperLibPath = Path.Combine(WhisperThirdPartyPath, "lib", "Android", "arm64-v8a");
			string WhisperSo = Path.Combine(WhisperLibPath, "libwhisper.so");
			RequiredAndroidFiles.Add(WhisperSo);
			PublicAdditionalLibraries.Add(WhisperSo);
			RuntimeDependencies.Add(WhisperSo);

			// Android packaging via APL
			string APLPath = Path.Combine(ModuleDirectory, "..", "..", "LlamaCpp_APL.xml");
			RequiredAndroidFiles.Add(APLPath);
			RequireFiles("Android", RequiredAndroidFiles);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", APLPath);
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// llama.cpp import libraries (these reference ggml*.dll and llama.dll)
			string LlamaLibPath = Path.Combine(ThirdPartyPath, "lib", "Win64");
			string[] LlamaImportLibs = new string[] {
				"ggml-base.lib",
				"ggml-cpu.lib",
				"ggml.lib",
				"llama.lib"
			};

			List<string> RequiredWin64Files = new List<string>();

			foreach (string Lib in LlamaImportLibs)
			{
				string FullPath = Path.Combine(LlamaLibPath, Lib);
				RequiredWin64Files.Add(FullPath);
				PublicAdditionalLibraries.Add(FullPath);
			}

			// whisper import library (whisper.dll has ggml statically linked inside)
			string WhisperLibPath = Path.Combine(WhisperThirdPartyPath, "lib", "Win64");
			string WhisperImportLib = Path.Combine(WhisperLibPath, "whisper.lib");
			RequiredWin64Files.Add(WhisperImportLib);
			PublicAdditionalLibraries.Add(WhisperImportLib);

			// Register DLLs as runtime dependencies
			string BinPath = Path.Combine(PluginDir, "Binaries", "Win64");
			string[] RuntimeDlls = new string[] {
				"ggml-base.dll", "ggml-cpu.dll", "ggml.dll", "llama.dll",
				"whisper.dll"
			};
			foreach (string Dll in RuntimeDlls)
			{
				RuntimeDependencies.Add(Path.Combine(BinPath, Dll));
			}

			RequireFiles("Win64", RequiredWin64Files);
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string BinPath = Path.Combine(PluginDir, "Binaries", "Mac");

			// llama dylibs (with separate ggml dylibs)
			string[] LlamaDyLibs = new string[] {
				"libggml-base.dylib",
				"libggml-cpu.dylib",
				"libggml.dylib",
				"libllama.dylib"
			};

			List<string> RequiredMacFiles = new List<string>();

			foreach (string DyLib in LlamaDyLibs)
			{
				string FullPath = Path.Combine(BinPath, DyLib);
				RequiredMacFiles.Add(FullPath);
				PublicAdditionalLibraries.Add(FullPath);
				RuntimeDependencies.Add(FullPath);
			}

			// whisper dylib is self-contained (ggml statically linked inside)
			string WhisperDyLib = Path.Combine(BinPath, "libwhisper.dylib");
			RequiredMacFiles.Add(WhisperDyLib);
			PublicAdditionalLibraries.Add(WhisperDyLib);
			RuntimeDependencies.Add(WhisperDyLib);

			RequireFiles("Mac", RequiredMacFiles);
		}
	}
}
