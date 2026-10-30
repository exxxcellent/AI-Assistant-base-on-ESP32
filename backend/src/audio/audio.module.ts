import { Module } from '@nestjs/common';
import { AiService } from 'src/yandex/ai/ai.service';
import { S3Service } from 'src/yandex/s3/s3.service';
import { SpeechService } from 'src/yandex/speech/speech.service';
import { AudioController } from './audio.controller';
import { AudioService } from './audio.service';

@Module({
    controllers: [AudioController],
    providers: [AudioService, SpeechService, AiService, S3Service],
})
export class AudioModule {}
