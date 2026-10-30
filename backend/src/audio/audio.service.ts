import { BadRequestException, Injectable } from '@nestjs/common';
import fs, { readFileSync, writeFileSync } from 'fs';
import path from 'path';
import { AiService } from 'src/yandex/ai/ai.service';
import { S3Service } from 'src/yandex/s3/s3.service';
import { SpeechService } from 'src/yandex/speech/speech.service';

@Injectable()
export class AudioService {
    private readonly uploadDir = path.join(process.cwd(), 'uploads');
    private readonly responsesDir = path.join(process.cwd(), 'responses');

    constructor(
        private readonly speechService: SpeechService,
        private readonly aiService: AiService,
        private readonly s3Service: S3Service,
    ) {
        if (!fs.existsSync(this.uploadDir)) {
            fs.mkdirSync(this.uploadDir, { recursive: true });
        }
        if (!fs.existsSync(this.responsesDir)) {
            fs.mkdirSync(this.responsesDir, { recursive: true });
        }
    }

    // saveFile(file: Express.Multer.File): string {
    //     const timestamp = Date.now();
    //     const filename = `recording_${timestamp}.wav`;
    //     const filepath = path.join(this.uploadDir, filename);

    //     fs.writeFileSync(filepath, file.buffer);

    //     return filename;
    // }

    async processAudio(filepath: string): Promise<string> {
        const s3FileUri = await this.s3Service.putObject(
            readFileSync(filepath),
            'request',
        );
        console.log(s3FileUri);
        if (!s3FileUri) throw new BadRequestException(); // IF NULL recognize
        const recognized = await this.speechService.recognize(s3FileUri);
        if (!recognized) throw new BadRequestException(); // IF NULL recognize
        console.log(recognized);
        const responseAi = await this.aiService.create(recognized);
        if (!responseAi) throw new BadRequestException(); // IF NULL responseAi
        console.log(responseAi);
        const synthesized = await this.speechService.synthesize(responseAi);
        if (!synthesized) throw new BadRequestException(); // IF NULL synthesized
        console.log(synthesized);
        await this.s3Service.putObject(Buffer.from(synthesized), 'response');
        return responseAi;
    }

    private saveSynthesizedFile(audioUint8: Uint8Array<ArrayBufferLike>) {
        const audioBuffer = Buffer.from(audioUint8);
        writeFileSync(`/responses/${new Date().toString()}`, audioBuffer);
    }

    async synthesizeTest(input: string) {
        try {
            const audioUint8 = await this.speechService.synthesize(input);
            this.saveSynthesizedFile(audioUint8);
        } catch (e) {
            console.error(e);
        }
    }
}
