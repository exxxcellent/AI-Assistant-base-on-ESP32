import { Module } from '@nestjs/common';
import { AppController } from './app.controller';
import { AppService } from './app.service';
import { AudioModule } from './audio/audio.module';
import { YandexModule } from './yandex/yandex.module';

@Module({
    imports: [YandexModule, AudioModule],
    controllers: [AppController],
    providers: [AppService],
})
export class AppModule {}
