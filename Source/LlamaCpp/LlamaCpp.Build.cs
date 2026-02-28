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

		// Let the headers resolve dllimport (Win64) or visibility (other platforms)
		PublicDefinitions.Add("LLAMA_SHARED=1");
		PublicDefinitions.Add("GGML_SHARED=1");
		PublicDefinitions.Add("WHISPER_SHARED=1");

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
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

			// Whisper shared library
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
			string ImportLibPath = Path.Combine(ThirdPartyPath, "lib", "Win64");
			string BinPath = Path.Combine(PluginDir, "Binaries", "Win64");

			string[] DLLs = new string[] {
				"ggml-base.dll",
				"ggml-cpu.dll",
				"ggml.dll",
				"llama.dll"
			};

			string[] ImportLibs = new string[] {
				"ggml-base.lib",
				"ggml-cpu.lib",
				"ggml.lib",
				"llama.lib"
			};

			List<string> RequiredWin64Files = new List<string>();

			foreach (string Lib in ImportLibs)
			{
				string FullImportLibPath = Path.Combine(ImportLibPath, Lib);
				RequiredWin64Files.Add(FullImportLibPath);
				PublicAdditionalLibraries.Add(FullImportLibPath);
			}

			foreach (string DLL in DLLs)
			{
				RequiredWin64Files.Add(Path.Combine(BinPath, DLL));
				PublicDelayLoadDLLs.Add(DLL);
				RuntimeDependencies.Add(Path.Combine(BinPath, DLL));
			}

			// Whisper
			string WhisperImportLibPath = Path.Combine(WhisperThirdPartyPath, "lib", "Win64");
			string WhisperImportLib = Path.Combine(WhisperImportLibPath, "whisper.lib");
			string WhisperDll = Path.Combine(BinPath, "whisper.dll");
			RequiredWin64Files.Add(WhisperImportLib);
			RequiredWin64Files.Add(WhisperDll);
			PublicAdditionalLibraries.Add(WhisperImportLib);
			PublicDelayLoadDLLs.Add("whisper.dll");
			RuntimeDependencies.Add(WhisperDll);

			RequireFiles("Win64", RequiredWin64Files);
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string BinPath = Path.Combine(PluginDir, "Binaries", "Mac");

			string[] DyLibs = new string[] {
				"libggml-base.dylib",
				"libggml-cpu.dylib",
				"libggml.dylib",
				"libllama.dylib"
			};

			List<string> RequiredMacFiles = new List<string>();

			foreach (string DyLib in DyLibs)
			{
				string FullPath = Path.Combine(BinPath, DyLib);
				RequiredMacFiles.Add(FullPath);
				PublicAdditionalLibraries.Add(FullPath);
				RuntimeDependencies.Add(FullPath);
			}

			// Whisper
			string WhisperDyLib = Path.Combine(BinPath, "libwhisper.dylib");
			RequiredMacFiles.Add(WhisperDyLib);
			PublicAdditionalLibraries.Add(WhisperDyLib);
			RuntimeDependencies.Add(WhisperDyLib);

			RequireFiles("Mac", RequiredMacFiles);
		}
	}
}
