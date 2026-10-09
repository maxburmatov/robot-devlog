# Дневник разработки DeskBot

Публичная история разработки настольного робота DeskBot: рабочие примеры
прошивки, схемы подключения и короткие пояснения к этапам проекта.

Каждая версия в `firmware/` — самостоятельный Arduino-скетч, связанный с
конкретным этапом разработки или готовящимся материалом. Старые версии не
переписываются под текущее состояние робота: следующий заметный этап получает
новую папку.

## Версии прошивки

| Версия | Что добавлено | Связанный материал |
|---|---|---|
| [DeskBot_v01](firmware/DeskBot_v01/) | OLED-лицо и реакция на приближение по ToF | [Telegram](https://t.me/burmatov_builds/28), [Instagram](https://www.instagram.com/reel/DdrJl56gOWI/), [YouTube](https://www.youtube.com/shorts/_fq64NIzsw0) |
| [DeskBot_v02](firmware/DeskBot_v02/) | Сон и пробуждение по данным BH1750 | [Telegram](https://t.me/burmatov_builds/36), [Instagram](https://www.instagram.com/reel/DdwUL0hAVOD/), [YouTube](https://www.youtube.com/shorts/4MXHmpsthuA) | 
| [DeskBot_v03](firmware/DeskBot_v03/) | LSM6DS3: движение, поднятие и встряхивание | [Telegram](https://t.me/burmatov_builds/41), [Instagram](https://www.instagram.com/reel/Dd4CDNrBK1f/), [YouTube](https://youtube.com/shorts/9AerNHL3oIc?si=KGGBzXKbvFo9xdsB)  | 
| [DeskBot_v04](firmware/DeskBot_v04/) | История присутствия: ожидание, скука и возвращение | [Telegram](https://t.me/burmatov_builds/46), [Instagram](https://www.instagram.com/p/Dd_cWb4hO-q/), [YouTube](https://www.youtube.com/shorts/DK7hzfpQc8Y)  |
| [DeskBot_v05](firmware/DeskBot_v05/) | Синхронные реплики и звуковые реакции | [Telegram](https://t.me/burmatov_builds/53), [Instagram](https://www.instagram.com/p/DeE9d7hhq9S/), [YouTube](https://www.youtube.com/shorts/kA2ZADaTd4c)  |
| [DeskBot_v06](firmware/DeskBot_v06/) | APDS-9960, жесты, часы, погода и runtime mute | [Telegram](https://t.me/burmatov_builds/56), [Instagram](https://www.instagram.com/p/DeJvD2Zh4gx/), [YouTube](https://www.youtube.com/shorts/TML_74t38BA)  |
| [DeskBot_v07](firmware/DeskBot_v07/) | MAX9814, SOUND/CLAP, FFT-анализ музыки и анимированный режим Dancing | — |

## Как пользоваться

1. Откройте каталог нужной версии.
2. Прочитайте локальный `README.md`: там перечислены компоненты, библиотеки,
   подключение и ограничения именно этой версии.
3. Откройте одноимённый `.ino` в Arduino IDE.
4. Установите указанные библиотеки и выберите ESP32-S3.
5. Соберите и загрузите скетч.

## Важно

- Это дневник разработки раннего физического прототипа, а не стабильный SDK.
- Распиновка и состав оборудования могут отличаться между версиями.
- Пороги датчиков требуют настройки под конкретный корпус и условия.

## Навигация

- [Все версии прошивки](firmware/)
- Telegram: https://t.me/burmatov_builds
- Instagram: https://www.instagram.com/burmatov.pm
- YouTube: https://www.youtube.com/@burmatov.pmrobo

## Музыкальный режим v07

`DeskBot_v07` анализирует MAX9814 с частотой 8 кГц, распознаёт регулярные
удары и показывает состояние `Dancing`: пара глаз плавно покачивается,
зрачки и вертикальный импульс реагируют на биты, периодически появляется
довольный прищур и всплывают музыкальные ноты. Нижняя часть OLED продолжает
отображать восьмиполосный FFT-эквалайзер. Подробности — в README версии.
