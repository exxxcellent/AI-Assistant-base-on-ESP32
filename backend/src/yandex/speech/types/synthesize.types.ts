// AudioFormatOptions
export enum AudioFormat {
    RAW_AUDIO = 'RAW_AUDIO',
    CONTAINER_AUDIO = 'CONTAINER_AUDIO',
}

export interface AudioFormatOptions {
    audioFormat?: AudioFormat;
    rawAudio?: RawAudio;
    containerAudio?: ContainerAudio;
}

// RawAudio
export enum AudioEncoding {
    AUDIO_ENCODING_UNSPECIFIED = 0,
    LINEAR16_PCM = 1,
}

export interface RawAudio {
    audioEncoding: AudioEncoding;
    sampleRateHertz: number; // int64 в TS обычно маппят на number
}

// ContainerAudio
export enum ContainerAudioType {
    CONTAINER_AUDIO_TYPE_UNSPECIFIED = 0,
    WAV = 1,
    OGG_OPUS = 2,
    MP3 = 3,
}

export interface ContainerAudio {
    containerAudioType: ContainerAudioType;
}

// AudioContent
export interface AudioContent {
    content?: Uint8Array; // bytes в TS удобно представлять как Uint8Array
    audioSpec: AudioFormatOptions;
}

// TextVariable
export interface TextVariable {
    variableName: string;
    variableValue: string;
}

// AudioVariable
export interface AudioVariable {
    variableName: string;
    variableStartMs: number;
    variableLengthMs: number;
}

// WordTiming
export interface WordTiming {
    word: string;
    startMs: number;
    lengthMs: number;
}

// UtteranceSynthesisResponse
export enum ChunkType {
    CHUNK_TYPE_UNSPECIFIED = 0,
    AUDIO_ONLY = 1,
    TEXT_ONLY = 2,
    AUDIO_TEXT_COMBINED = 3,
    NO_SPEAKABLE_TEXT = 4,
}

export interface AudioChunk {
    data: Uint8Array;
}

export interface TextChunk {
    text: string;
}

export interface UtteranceSynthesisResponse {
    audioChunk?: AudioChunk;
    textChunk?: TextChunk;
    startMs: number;
    lengthMs: number;
    wordTimings?: WordTiming[];
    chunkType: ChunkType;
}

// TextTemplate
export interface TextTemplate {
    textTemplate: string;
    variables?: TextVariable[];
}

// AudioTemplate
export interface AudioTemplate {
    audio: AudioContent;
    textTemplate?: TextTemplate;
    variables?: AudioVariable[];
}

// DurationHint
export enum DurationHintPolicy {
    DURATION_HINT_POLICY_UNSPECIFIED = 0,
    EXACT_DURATION = 1,
    MIN_DURATION = 2,
    MAX_DURATION = 3,
}

export interface DurationHint {
    policy: DurationHintPolicy;
    durationMs: number;
}

// Hints (oneof Hint)
export enum HintType {
    VOICE = 'VOICE',
    AUDIO_TEMPLATE = 'AUDIO_TEMPLATE',
    SPEED = 'SPEED',
    VOLUME = 'VOLUME',
    ROLE = 'ROLE',
    PITCH_SHIFT = 'PITCH_SHIFT',
    DURATION = 'DURATION',
}

export interface Hints {
    hintType?: HintType;
    voice?: string;
    audioTemplate?: AudioTemplate;
    speed?: number;
    volume?: number;
    role?: string;
    pitchShift?: number;
    duration?: DurationHint;
}

// UtteranceSynthesisRequest
export enum LoudnessNormalizationType {
    LOUDNESS_NORMALIZATION_TYPE_UNSPECIFIED = 0,
    MAX_PEAK = 1,
    LUFS = 2,
}

export interface UtteranceSynthesisRequest {
    model?: string;
    utteranceType?: 'TEXT' | 'TEXT_TEMPLATE';
    text?: string;
    textTemplate?: TextTemplate;
    hints?: Hints[];
    outputAudioSpec?: AudioFormatOptions;
    loudnessNormalizationType?: LoudnessNormalizationType;
    unsafeMode?: boolean;
}

// SynthesisOptions
export interface SynthesisOptions {
    model?: string;
    voice: string;
    role?: string;
    speed?: number;
    volume?: number;
    pitchShift?: number;
    outputAudioSpec?: AudioFormatOptions;
    loudnessNormalizationType?: LoudnessNormalizationType;
}

// SynthesisInput
export interface SynthesisInput {
    text: string;
}

// ForceSynthesisEvent
export interface ForceSynthesisEvent {}

// StreamSynthesisRequest (oneof Event)
export enum StreamSynthesisEventType {
    OPTIONS = 'OPTIONS',
    SYNTHESIS_INPUT = 'SYNTHESIS_INPUT',
    FORCE_SYNTHESIS = 'FORCE_SYNTHESIS',
}

export interface StreamSynthesisRequest {
    eventType: StreamSynthesisEventType;
    options?: SynthesisOptions;
    synthesisInput?: SynthesisInput;
    forceSynthesis?: ForceSynthesisEvent;
}

// StreamSynthesisResponse
export interface StreamSynthesisResponse {
    audioChunk?: AudioChunk;
    textChunk?: TextChunk;
    startMs: number;
    lengthMs: number;
    wordTimings?: WordTiming[];
    chunkType: ChunkType;
}
