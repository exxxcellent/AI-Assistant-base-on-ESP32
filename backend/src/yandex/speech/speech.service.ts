import { Injectable } from '@nestjs/common';
import console from 'console';

interface SttEvent {
    result: {
        sessionUuid?: { uuid: string; userRequestId?: string };
        audioCursors?: any;
        final?: {
            alternatives: { text: string }[];
        };
        finalRefinement?: {
            normalizedText: { alternatives: { text: string }[] };
        };
        eouUpdate?: { timeMs: number };
        channelTag?: string;
    };
}

@Injectable()
export class SpeechService {
    private apiKey: string;
    private folderId: string;

    constructor() {
        this.apiKey = process.env.YANDEX_SPEECH_API_KEY as string;
        this.folderId = process.env.YANDEX_FOLDER_ID as string;
    }

    public async recognize(uri: string): Promise<string | null> {
        const body = {
            uri,
            recognitionModel: {
                model: 'general',
                audioFormat: {
                    containerAudio: {
                        containerAudioType: 'MP3',
                    },
                },
                languageRestriction: {
                    restrictionType: 'WHITELIST',
                    languageCode: ['ru-RU', 'en-EN'],
                },
                textNormalization: {
                    textNormalization: 'TEXT_NORMALIZATION_ENABLED',
                    phoneFormattingMode: 'PHONE_FORMATTING_MODE_DISABLED',
                    profanityFilter: false,
                    literatureText: false,
                },
            },
            speakerLabeling: {
                speakerLabeling: 'SPEAKER_LABELING_DISABLED',
            },
            unsafeMode: false,
        };

        try {
            const response = await fetch(
                'https://stt.api.cloud.yandex.net/stt/v3/recognizeFileAsync',
                {
                    method: 'POST',
                    body: JSON.stringify(body),
                    headers: {
                        Authorization: `Bearer ${this.apiKey}`,
                        'x-folder-id': this.folderId,
                        'Content-Type': 'application/json',
                    },
                },
            ).then((res) => res.json());

            const operationId = response.id;
            const isDone = await this.getOperation(operationId);
            if (!isDone) return null;

            const events = await this.getRecognition(operationId);
            return this.extractTexts(events);
        } catch (error) {
            console.error(error);
            return null;
        }
    }

    private async getOperation(operationId: string) {
        const start = Date.now();
        const timeoutMs = 180000;
        const pollIntervalMs = 2000;

        while (true) {
            const url = `https://operation.api.cloud.yandex.net/operations/${operationId}`;

            try {
                const response = await fetch(url, {
                    headers: {
                        Authorization: `Bearer ${this.apiKey}`,
                        'x-folder-id': this.folderId,
                    },
                });

                if (!response.ok) {
                    const text = await response.text();
                    throw new Error(
                        `Operation status error: ${response.status} ${text}`,
                    );
                }

                const data = await response.json();

                if (data.done) {
                    return true;
                }

                if (Date.now() - start > timeoutMs) {
                    throw new Error(
                        'Timeout waiting for operation to complete',
                    );
                }

                await new Promise((resolve) =>
                    setTimeout(resolve, pollIntervalMs),
                );
            } catch (error) {
                console.error(error);
            }
        }
    }

    private async getRecognition(operationId: string): Promise<any[]> {
        const url = `https://stt.api.cloud.yandex.net/stt/v3/getRecognition?operationId=${operationId}`;

        try {
            const res = await fetch(url, {
                headers: {
                    Authorization: `Api-Key ${this.apiKey}`,
                    'x-folder-id': this.folderId,
                },
            });

            if (!res.ok) {
                throw new Error(`HTTP error! status: ${res.status}`);
            }

            const text = await res.text();
            return text
                .trim()
                .split(/\r?\n/)
                .filter((line) => line.trim() !== '')
                .map((line) => JSON.parse(line));
        } catch (error) {
            console.error(error);
            throw error;
        }
    }

    private extractTexts(events: SttEvent[]): string | null {
        const parsedEvents = events
            .map((ev) => {
                const r = ev.result;
                let text: string | null = null;
                if (
                    r.finalRefinement?.normalizedText?.alternatives?.[0]?.text
                ) {
                    text =
                        r.finalRefinement.normalizedText.alternatives[0].text;
                } else if (r.final?.alternatives?.[0]?.text) {
                    text = r.final.alternatives[0].text;
                }

                return text ? { channelTag: r.channelTag, text } : null;
            })
            .filter(
                (x): x is { channelTag: string; text: string } => x !== null,
            );
        const lastFinalRefinement = parsedEvents.at(-1);
        if (!lastFinalRefinement) return null;
        return lastFinalRefinement.text;
    }

    public async synthesize(text: string): Promise<Uint8Array> {
        const url =
            'https://tts.api.cloud.yandex.net/tts/v3/utteranceSynthesis';

        const body = {
            hints: [
                {
                    speed: 1,
                },
            ],
            outputAudioSpec: {
                audioFormat: 'CONTAINER_AUDIO',
                containerAudio: { containerAudioType: 'MP3' },
            },
            loudnessNormalizationType: 'LUFS',
            text,
        };

        const res = await fetch(url, {
            method: 'POST',
            body: JSON.stringify(body),
            headers: {
                Authorization: `Bearer ${this.apiKey}`,
                'x-folder-id': this.folderId,
                'Content-Type': 'application/json',
            },
        });

        if (!res.ok) {
            const errorBody = await res.text().catch(() => 'Unknown error');
            throw new Error(`TTS HTTP error: ${res.status} ${errorBody}`);
        }

        const data = await res.json().catch((e) => {
            throw new Error(`Failed to parse TTS response: ${e.message}`);
        });

        const rawChunks = data.result?.audioChunks ?? [];
        const audioChunks =
            rawChunks.length > 0
                ? rawChunks
                : data.result?.audioChunk
                  ? [data.result.audioChunk]
                  : [];

        if (audioChunks.length === 0) {
            console.error(
                'Full response (no audio):',
                JSON.stringify(data, null, 2),
            );
            throw new Error('No audio chunks returned from TTS');
        }

        const totalLength = audioChunks.reduce(
            (sum, c) => sum + (c.data?.length ?? 0),
            0,
        );
        const merged = new Uint8Array(totalLength);
        let offset = 0;

        for (const chunk of audioChunks) {
            let chunkUint8: Uint8Array;

            if (typeof chunk.data === 'string') {
                // Это base64 — декодируем
                const buf = Buffer.from(chunk.data, 'base64');
                chunkUint8 = new Uint8Array(buf);
            } else if (Array.isArray(chunk.data)) {
                // Это массив чисел — просто оборачиваем
                chunkUint8 = new Uint8Array(chunk.data);
            } else {
                throw new Error(
                    `Unexpected chunk.data type: ${typeof chunk.data}`,
                );
            }

            merged.set(chunkUint8, offset);
            offset += chunkUint8.length;
        }

        return merged;
    }
}
