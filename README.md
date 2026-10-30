# AI-Assistant-base-on-ESP32

Working plan for v0.1:

- ESP32 records sound and post to backend | Done
- Backend post sound to Yandex S3 | Priority
- Backend post sound with Yandex S3 uri to Yandex SpeechKit | Done
- Yandex SpeechKit post response to Yandex Alice AI | Done
- Yandex Alice AI response to Yandex Sintezator | Done
- Yandex Sintezator reponse to backend | Done
- Backend wav file to esp32
- Esp32 wav file to audio play

Troubles:

- Quality recording
- Recording cuts out or fails to start
- Too long text for synthesize

Additional features:

- Enums for responses and etc
- Logger
- ConfigModule
- Types

Working plan for v0.2:

- Add record button. When button pressed microphone records sound as you much as you hold back
- OLED display. Oled display shows emotions depending on app state:
  - Animation blinking and running eyes on waiting as if in search of something
  - Animation recording
  - Animation seaching answer on your query
  - Animation response
