An Unreal plugin based on...

* Llama.cpp (https://github.com/ggml-org/llama.cpp)
* whisper.cpp (https://github.com/ggml-org/whisper.cpp)
* sherpa-onnx (https://github.com/k2-fsa/sherpa-onnx)

whisper/llama models can be downloaded from Huggin Face https://huggingface.co/ggerganov/whisper.cpp

sherpa-onnx models

Android/Quest 3 Recommendations
https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-en-20M-2023-02-17.tar.bz2

Runner-up: sherpa-onnx-streaming-zipformer-en-2023-06-26
Better accuracy, still fast — ~73 MB int8 on disk.

https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-en-2023-06-26.tar.bz2

Comparison
Model       Int8 Size   Est. RTF             Accuracy 
20M (recommended) │ 44 MB     │ ~0.04    │ Good (LibriSpeech)              
2023-06-26        │ 73 MB     │ ~0.06    │ Better (LibriSpeech)            
2023-06-21        │ 180 MB    │ ~0.09    │ Best (LibriSpeech + GigaSpeech)
