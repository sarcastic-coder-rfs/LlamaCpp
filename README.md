# LlamaCpp

An Unreal Engine 5 plugin integrating [llama.cpp](https://github.com/ggml-org/llama.cpp) (LLM inference) and [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (speech-to-text) with full Blueprint support.

> **Beta** — v1.2. APIs may change between releases.

## Supported Platforms

| Platform | Architecture | Notes |
|----------|-------------|-------|
| Windows | x64 | DLLs loaded at runtime |
| macOS | Universal (arm64 + x86_64) | Dylibs loaded at runtime |
| Android | ARM64 (arm64-v8a) | Quest 3 target; GPU disabled for compatibility |

---

## Features

- **LLM text generation** with per-token streaming callbacks
- **Speech-to-text** via three modes: WAV file, microphone capture, and real-time sliding-window transcription
- Full **Blueprint** and C++ API
- Non-blocking — all heavy operations run on background threads
- Graceful cancellation for both inference and transcription

---

## Installation

Prebuilt native binaries must be present before building. They are **not included** in the repository.

**Option A — Download from GitHub Releases**

Download the latest plugin zip from the [Releases](../../releases) page and extract it into your project's `Plugins/` directory.

**Option B — Build from source via CI**

Trigger the platform build workflows manually via `workflow_dispatch`, or push a `v*` tag to build all platforms automatically. After the workflows complete, download the artifacts and place them in the correct locations (see [Binary Locations](#binary-locations) below).

---

## Binary Locations

| Platform | llama libs | whisper libs | Runtime |
|----------|-----------|-------------|---------|
| Win64 | `ThirdParty/llama/lib/Win64/*.lib` | `ThirdParty/whisper/lib/Win64/whisper.lib` | `Binaries/Win64/*.dll` |
| Mac | — | — | `Binaries/Mac/lib*.dylib` |
| Android | `ThirdParty/llama/lib/Android/arm64-v8a/*.so` | `ThirdParty/whisper/lib/Android/arm64-v8a/libwhisper.so` | packaged via APL |

---

## Quick Start (Blueprint)

### LLM Inference

```
CreateLlamaCppInference → LoadModel (path to .gguf) → GenerateTextAsync
  ├─ OnTokenGenerated   — fires per token, use to stream text to UI
  └─ OnGenerationComplete — fires once with full output
```

Call `StopGeneration()` at any time to cancel mid-stream.

### Speech-to-Text

```
CreateWhisperTranscription → LoadModel (path to .bin)
```

Then use one of three transcription modes:

| Mode | Call | Callback |
|------|------|----------|
| WAV file | `TranscribeWavFileAsync(path, language)` | `OnTranscriptionComplete` |
| Microphone | `StartMicrophoneCapture()` → `StopMicrophoneCaptureAndTranscribe()` | `OnTranscriptionComplete` |
| Real-time | `StartRealtimeTranscription()` → `StopRealtimeTranscription()` | `OnPartialTranscription` |

---

## C++ API

### LLM Inference

```cpp
ULlamaCppInference* Llama = ULlamaCppBlueprintLibrary::CreateLlamaCppInference(this);

Llama->OnTokenGenerated.AddDynamic(this, &AMyActor::HandleToken);
Llama->OnGenerationComplete.AddDynamic(this, &AMyActor::HandleComplete);

Llama->LoadModel(TEXT("/path/to/model.gguf"), /*ContextSize=*/2048);
Llama->GenerateTextAsync(TEXT("Hello, world!"), /*MaxTokens=*/256);

// To cancel:
Llama->StopGeneration();
```

**Sampling parameters** (`FLlamaSamplingParams`):

| Field | Default | Description |
|-------|---------|-------------|
| `Temperature` | `0.8` | Randomness (0 = greedy) |
| `TopK` | `40` | Top-K sampling |
| `TopP` | `0.95` | Top-P (nucleus) sampling |
| `MinP` | `0.05` | Min-P sampling |
| `RepeatPenalty` | `1.1` | Penalty for repeated tokens |

### Speech-to-Text

```cpp
UWhisperCppTranscription* Whisper = ULlamaCppBlueprintLibrary::CreateWhisperTranscription(this);

Whisper->OnTranscriptionComplete.AddDynamic(this, &AMyActor::HandleTranscription);
Whisper->LoadModel(TEXT("/path/to/ggml-base.bin"));

// From WAV file:
Whisper->TranscribeWavFileAsync(TEXT("/path/to/audio.wav"), TEXT("en"));

// From microphone:
Whisper->StartMicrophoneCapture();
// ... later ...
Whisper->StopMicrophoneCaptureAndTranscribe(TEXT("en"));
```

**Transcription result** (`FWhisperTranscriptionResult`):

```cpp
void AMyActor::HandleTranscription(const FWhisperTranscriptionResult& Result)
{
    if (Result.bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("%s"), *Result.FullText);
        for (const FWhisperTranscriptionSegment& Seg : Result.Segments)
            UE_LOG(LogTemp, Log, TEXT("[%.1fs] %s"), Seg.StartTimeSeconds, *Seg.Text);
    }
}
```

---

## Android / Quest 3

- Libraries are loaded automatically via `LlamaCpp_APL.xml`.
- Add `RECORD_AUDIO` permission to your project's `AndroidManifest.xml` if using microphone capture.
- GPU acceleration is disabled (`use_gpu=false`) for broad device compatibility.

---

## Architecture Notes

- **Separate GGML builds**: whisper.dll/dylib/so has ggml statically linked inside to avoid ABI conflicts with llama's separately shipped ggml shared libraries. Both projects use different ggml versions.
- **Library load order** (Windows/Mac): `ggml-base → ggml-cpu → ggml → llama → whisper`
- **Threading**: all inference and transcription runs on `EAsyncExecution::Thread`; results marshal back via `AsyncTask(ENamedThreads::GameThread)`.
- **Debug logging** (model info, inference params, whisper system info) is stripped in Shipping builds via `#if !UE_BUILD_SHIPPING`.

---

## Building from Source

### Dependencies

| Library | Version |
|---------|---------|
| llama.cpp | tag `b8169` |
| whisper.cpp | tag `v1.8.3` |

### CI Workflows

| Workflow | Trigger |
|----------|---------|
| `build-windows.yml` | `workflow_dispatch` or `v*` tag |
| `build-macos.yml` | `workflow_dispatch` or `v*` tag |
| `build-android.yml` | `workflow_dispatch` or `v*` tag |
| `build-whisper-windows.yml` | `workflow_dispatch` or `v*` tag |
| `build-whisper-macos.yml` | `workflow_dispatch` or `v*` tag |
| `build-whisper-android.yml` | `workflow_dispatch` or `v*` tag |
| `package-all-platforms.yml` | Orchestrates all of the above |

### Release Process

```bash
# 1. Push changes to main, then tag a release
git tag v1.3.0 && git push origin v1.3.0

# 2. Workflows build all platforms automatically
# 3. Download artifacts from workflow runs

# 4. Create a GitHub Release with the assembled plugin zip
gh release create v1.3.0 LlamaCpp-v1.3.0.zip --title "v1.3.0"
```

---

## Module Dependencies

`Core`, `CoreUObject`, `Engine`, `Projects`, `AudioCapture`, `AudioCaptureCore`, `AudioMixer`

---

## License

See [LICENSE](LICENSE) for details. llama.cpp and whisper.cpp are subject to their own respective licenses.
