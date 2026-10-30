import { Module } from '@nestjs/common';
import { AiModule } from './ai/ai.module';
import { SpeechModule } from './speech/speech.module';
import { S3Module } from './s3/s3.module';

@Module({
    imports: [AiModule, SpeechModule, S3Module],
})
export class YandexModule {}
