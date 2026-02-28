using UnrealBuildTool;
using System.IO;

public class LlamaCpp : ModuleRules
{
	public LlamaCpp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"AudioCapture",
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

			foreach (string Lib in Libs)
			{
				string FullPath = Path.Combine(LibPath, Lib);
				PublicAdditionalLibraries.Add(FullPath);
				RuntimeDependencies.Add(FullPath);
			}

			// Whisper shared library
			string WhisperLibPath = Path.Combine(WhisperThirdPartyPath, "lib", "Android", "arm64-v8a");
			string WhisperSo = Path.Combine(WhisperLibPath, "libwhisper.so");
			PublicAdditionalLibraries.Add(WhisperSo);
			RuntimeDependencies.Add(WhisperSo);

			// Android packaging via APL
			string APLPath = Path.Combine(ModuleDirectory, "..", "..", "LlamaCpp_APL.xml");
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

			foreach (string Lib in ImportLibs)
			{
				PublicAdditionalLibraries.Add(Path.Combine(ImportLibPath, Lib));
			}

			foreach (string DLL in DLLs)
			{
				PublicDelayLoadDLLs.Add(DLL);
				RuntimeDependencies.Add(Path.Combine(BinPath, DLL));
			}

			// Whisper
			string WhisperImportLibPath = Path.Combine(WhisperThirdPartyPath, "lib", "Win64");
			PublicAdditionalLibraries.Add(Path.Combine(WhisperImportLibPath, "whisper.lib"));
			PublicDelayLoadDLLs.Add("whisper.dll");
			RuntimeDependencies.Add(Path.Combine(BinPath, "whisper.dll"));
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

			foreach (string DyLib in DyLibs)
			{
				string FullPath = Path.Combine(BinPath, DyLib);
				PublicAdditionalLibraries.Add(FullPath);
				RuntimeDependencies.Add(FullPath);
			}

			// Whisper
			string WhisperDyLib = Path.Combine(BinPath, "libwhisper.dylib");
			PublicAdditionalLibraries.Add(WhisperDyLib);
			RuntimeDependencies.Add(WhisperDyLib);
		}
	}
}
