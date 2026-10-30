import { Test, TestingModule } from '@nestjs/testing';
import { SpeechService } from './speech.service';

describe('SpeechService', () => {
    let service: SpeechService;

    beforeEach(async () => {
        const module: TestingModule = await Test.createTestingModule({
            providers: [SpeechService],
        }).compile();

        service = module.get<SpeechService>(SpeechService);
    });

    it('should be defined', () => {
        expect(service).toBeDefined();
    });
});
