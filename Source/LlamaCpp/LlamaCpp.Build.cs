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
			"Engine"
		});

		string ThirdPartyPath = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "llama");
		string IncludePath = Path.Combine(ThirdPartyPath, "include");

		PublicIncludePaths.Add(IncludePath);

		// Let the headers resolve dllimport (Win64) or visibility (other platforms)
		PublicDefinitions.Add("LLAMA_SHARED=1");
		PublicDefinitions.Add("GGML_SHARED=1");

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

			// Android packaging via APL
			string APLPath = Path.Combine(ModuleDirectory, "..", "..", "LlamaCpp_APL.xml");
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", APLPath);
		}
		else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = Path.Combine(ThirdPartyPath, "lib", "Win64");

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
				PublicAdditionalLibraries.Add(Path.Combine(LibPath, Lib));
			}

			foreach (string DLL in DLLs)
			{
				PublicDelayLoadDLLs.Add(DLL);
				RuntimeDependencies.Add(Path.Combine(LibPath, DLL));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string LibPath = Path.Combine(ThirdPartyPath, "lib", "Mac");

			string[] DyLibs = new string[] {
				"libggml-base.dylib",
				"libggml-cpu.dylib",
				"libggml.dylib",
				"libllama.dylib"
			};

			foreach (string DyLib in DyLibs)
			{
				string FullPath = Path.Combine(LibPath, DyLib);
				PublicAdditionalLibraries.Add(FullPath);
				RuntimeDependencies.Add(FullPath);
			}
		}
	}
}
