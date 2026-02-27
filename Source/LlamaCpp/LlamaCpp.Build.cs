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

		// Suppress dllexport/dllimport macros — we link the .so directly
		PublicDefinitions.Add("LLAMA_API=");
		PublicDefinitions.Add("GGML_API=extern");

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
	}
}
