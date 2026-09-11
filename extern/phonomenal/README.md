# Phonomenal native engine

Vendored from aridlin/phonomenal commit `73b7f53af25349ef8d4c1b7d4ffe4421eca9b6a4`.
The engine sources are unchanged and retain their MIT license. Only the native
splicer and its public header are included; voice recordings and packs are
user-provided. No Python service or synthesis server is required at runtime.

The engine reads `.vcpack` and `.phbank` voices, plans words/phonemes locally,
and generates PCM WAV with pitch handling and adaptive word gaps. An optional
`espeak-ng.dll` supplies pronunciations; it does not synthesize the voice audio.
