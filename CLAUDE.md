# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LlamaCpp is an Unreal Engine 5 plugin integrating **llama.cpp** (LLM inference) and **whisper.cpp** (speech-to-text) with full Blueprint support. Targets Windows x64, macOS Universal, and Android ARM64 (Quest 3).

## Architecture

### Module Loading (`LlamaCppModule`)
- Explicitly loads shared libraries via `FPlatformProcess::GetDllHandle` in dependency order at startup
- Windows/Mac: ggml-base → ggml-cpu → ggml → llama → whisper
- Android: handled via APL `System.loadLibrary` in `LlamaCpp_APL.xml`
- Calls `llama_backend_init()` after loading

### Key Classes
- **`ULlamaCppInference`** — async text generation with token streaming. Uses `GenerateTextAsync()`, fires `OnTokenGenerated` per token and `OnGenerationComplete` at end.
- **`UWhisperCppTranscription`** — three transcription modes: WAV file, mic capture (record then transcribe), and real-time streaming (sliding 30s window). Resamples all audio to 16kHz mono.
- **`ULlamaCppBlueprintLibrary`** — factory functions `CreateLlamaCppInference()` and `CreateWhisperTranscription()` for Blueprint use.

### Critical Design Decision: Separate GGML
whisper.dll/dylib/so has **ggml statically linked inside** to avoid ABI conflicts with llama's separate ggml shared libraries. Both projects use different ggml versions. The whisper build workflows patch `src/CMakeLists.txt` to force whisper as SHARED while building ggml as static.

### Threading Pattern
All heavy operations (model loading, inference, transcription) run on background threads via `Async(EAsyncExecution::Thread)`. Results are marshaled back to the game thread via `AsyncTask(ENamedThreads::GameThread)`. Thread safety uses `TAtomic<bool>` for cancellation and `FCriticalSection` for audio buffers. `TWeakObjectPtr` prevents use-after-free on UObjects.

## Build System

### Prerequisites
Prebuilt native binaries must be present before building. Download from GitHub Releases or build via CI workflows. `Build.cs` will fail with `RequireFiles()` if binaries are missing.

### Binary Locations
| Platform | llama libs | whisper libs | Runtime DLLs |
|----------|-----------|-------------|-------------|
| Win64 | `ThirdParty/llama/lib/Win64/*.lib` | `ThirdParty/whisper/lib/Win64/whisper.lib` | `Binaries/Win64/*.dll` |
| Mac | `Binaries/Mac/lib*.dylib` | `Binaries/Mac/libwhisper.dylib` | same |
| Android | `ThirdParty/llama/lib/Android/arm64-v8a/*.so` | `ThirdParty/whisper/lib/Android/arm64-v8a/libwhisper.so` | packaged via APL |

### Module Dependencies
Core, CoreUObject, Engine, Projects, AudioCapture, AudioCaptureCore, AudioMixer

## CI/CD Workflows

Six build workflows in `.github/workflows/`:
- `build-windows.yml`, `build-macos.yml`, `build-android.yml` — build llama.cpp from `ggml-org/llama.cpp` tag `b8169`
- `build-whisper-windows.yml`, `build-whisper-macos.yml`, `build-whisper-android.yml` — build whisper.cpp from `ggml-org/whisper.cpp` tag `v1.8.3`
- `package-all-platforms.yml` — orchestrates all builds and assembles plugin zip

Trigger individually via `workflow_dispatch` or on tag push (`v*`).

## Release Process

1. Push changes to `main`
2. Trigger platform build workflows (or push a `v*` tag)
3. Download artifacts from workflow runs
4. Assemble plugin directory with binaries in correct locations
5. Create zip and upload to GitHub Release via `gh release create`

## Platform Notes

- **Windows**: DLLs are preloaded in `StartupModule`. Import libs link at compile time, DLLs loaded at runtime.
- **macOS**: Universal binaries (arm64+x86_64). Dylibs loaded in `StartupModule`.
- **Android/Quest 3**: ARM64 only. Libraries loaded via APL manifest. Requires `RECORD_AUDIO` permission for mic capture. GPU disabled (`use_gpu=false`) for compatibility.

## Debug Logging

Diagnostic output (whisper system info, model info, inference params) is wrapped in `#if !UE_BUILD_SHIPPING` — active in Editor and Development builds, stripped in Shipping.
