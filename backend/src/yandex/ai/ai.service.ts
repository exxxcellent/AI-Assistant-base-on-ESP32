import { Injectable } from '@nestjs/common';
import type { AiTextGeneration } from './types/ai.type';

@Injectable()
export class AiService {
    private apiKey: string;
    private folderId: string;
    private baseUrl: string;

    private readonly gptUrl: string = process.env
        .YANDEX_AI_SEARCHER_URL as string;

    constructor() {
        this.apiKey = process.env.YANDEX_API_KEY as string;
        this.folderId = process.env.YANDEX_FOLDER_ID as string;
        this.baseUrl = 'https://ai.api.cloud.yandex.net/foundationModels/v1';
    }

    async create(input: string): Promise<string> {
        const body: Partial<AiTextGeneration> = {
            modelUri: this.gptUrl,
            completionOptions: {
                stream: false,
                temperature: 0.3,
                reasoningOptions: {
                    mode: 'DISABLED',
                },
                maxTokens: '0',
            },
            messages: [
                {
                    role: 'user',
                    text: input,
                },
            ],
        };

        const response = await fetch(`${this.baseUrl}/completion`, {
            body: JSON.stringify(body),
            method: 'POST',
            headers: {
                Authorization: `Api-Key ${this.apiKey}`,
                'x-folder-id': this.folderId,
            },
        }).then((res) => res.json());

        console.log(response);

        return response.result.alternatives[0].message.text as string;
    }

    async checkHealth(input: string): Promise<string> {
        const response = await this.create(input);
        console.log(response);
        return response;
    }
}
