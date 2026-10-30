import { Body, Controller, Post, UploadedFile } from '@nestjs/common';
import { readFileSync } from 'fs';
import { AiService } from 'src/yandex/ai/ai.service';
import { S3Service } from 'src/yandex/s3/s3.service';
import { AudioService } from './audio.service';

@Controller('audio')
export class AudioController {
    constructor(
        private readonly audioService: AudioService,
        private readonly aiService: AiService,
        private readonly s3Service: S3Service,
    ) {}

    @Post()
    // @UseInterceptors(
    //     FileInterceptor('audio', {
    //         storage: diskStorage({
    //             destination: path.join(process.cwd(), 'uploads'),
    //             filename: (_req, file, cb) => {
    //                 const timestamp = Date.now();
    //                 cb(null, `recording_${timestamp}.wav`);
    //             },
    //         }),
    //         limits: {
    //             fileSize: 30 * 1024 * 1024, // 5 МБ
    //         },
    //         fileFilter: (_req, file, cb) => {
    //             if (
    //                 file.mimetype === 'audio/wav' ||
    //                 file.mimetype === 'audio/x-wav' ||
    //                 file.mimetype === 'audio/wave' ||
    //                 file.mimetype === 'audio/mpeg'
    //             ) {
    //                 cb(null, true);
    //             } else {
    //                 cb(new BadRequestException('Ожидается WAV-файл'), false);
    //             }
    //         },
    //     }),
    // )
    async uploadAudio(@UploadedFile() file: Express.Multer.File) {
        // if (!file) {
        //     throw new BadRequestException('Файл не получен');
        // }

        // console.log(
        //     `Получен файл: ${file.originalname}, размер: ${file.size} байт`,
        // );

        const result = await this.audioService.processAudio(
            'C:\\Users\\Алексей\\Downloads\\Record (online-voice-recorder.com).mp3',
        );

        return {
            status: 'ok',
            filename: file.filename,
            size: file.size,
            result,
        };
    }

    @Post('ai-test')
    async testAi(@Body() body: { input: string }) {
        return this.aiService.checkHealth(body.input);
    }

    @Post('speech-test')
    async speechTest() {
        return this.audioService.processAudio('');
    }

    @Post('synth-test')
    async synthTest(@Body() body: { input: string }) {
        return this.audioService.synthesizeTest(body.input);
    }

    @Post('s3-test')
    async s3Test() {
        const file = readFileSync(
            `D:\\projects\\ai_assistant\\backend\\uploads\\e490602s-960.jpg`,
        );

        return this.s3Service.putObject(file, 'request');
    }
}
