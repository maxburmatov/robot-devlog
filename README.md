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
| [DeskBot_v04](firmware/DeskBot_v04/) | История присутствия: ожидание, скука и возвращение | [Telegram](), [Instagram](), [YouTube]()  |

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
- Для воспроизводимости используйте ссылку на конкретный Git commit, а не
  только на ветку `main`.

## Навигация

- [Все версии прошивки](firmware/)
- Telegram: https://t.me/burmatov_builds
- Instagram: https://www.instagram.com/burmatov.pm
- YouTube: https://www.youtube.com/@burmatov.pmrobo