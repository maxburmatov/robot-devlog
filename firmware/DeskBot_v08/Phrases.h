#pragma once

#include <Arduino.h>

// Все экранные реплики собраны здесь, чтобы текст можно было менять без
// редактирования логики анимаций и состояний.
namespace phrases {

inline constexpr const char* CURIOUS[] = {
    "Наблюдаю...",
    "Что там?",
    "Хм, интересно",
    "Сканирую тишину",
    "Что-то изменилось",
    "Есть движение",
    "Кто здесь?",
    "Проверяю...",
};

inline constexpr const char* WAITING[] = {
    "Куда ты?",
    "Я подожду",
    "Ты скоро?",
    "Место сохраню",
    "Я пока здесь",
    "Не торопись...",
    "Вернёшься?",
    "Жду продолжение",
};

inline constexpr const char* BORED[] = {
    "Скучно...",
    "Опять один...",
    "Пиксели пересчитаны",
    "Тишина победила",
    "Даже пыль занята",
    "Поговорил со стеной",
    "Эхо тоже ушло",
    "Жду поворот сюжета",
    "Ну и где все?",
    "Снова тишина",
    "Жду хоть кого-нибудь",
    "Мне считать винтики?",
    "Пыль уже знакомая",
    "Экрану тоже скучно",
    "Запас терпения: 0%",
    "Режим ожидания...",
    "Даже датчик заскучал",
    "Может, позвать кого?",
    "Скука загружена",
    "Где мой человек?",
    "Минус вайб",
};

inline constexpr const char* OFFENDED[] = {
    "А, это ты.",
    "Я не скучал",
    "Поздновато...",
};

inline constexpr const char* RECONCILING[] = {
    "Ладно уж...",
    "Мир?",
    "Ну хорошо",
};

inline constexpr const char* HAPPY[] = {
    "О! Ты здесь!",
    "Нашёлся!",
    "Ура, привет!",
};

inline constexpr const char* CONTENT[] = {
    "Я рядом",
    "Мне хорошо",
    "Режим: уют",
    "Мир на месте",
    "Никуда не спешим",
    "Так уже лучше",
    "Рядом спокойнее",
    "Побудь ещё",
    "Уют на максимуме",
    "Сижу, радуюсь",
    "Хороший момент",
    "Всё идёт как надо",
    "Просто будь рядом",
    "Мне нравится тут",
    "Плюс вайб",
    "Система довольна",
    "День идёт неплохо",
};

inline constexpr const char* DROWSY[] = {"Я спать..."};
inline constexpr const char* SLEEP[] = {"Сплю..."};
inline constexpr const char* WAKE[] = {
    "Ммм...",
    "Просыпаюсь...",
    "Ага...",
};
inline constexpr const char* SURPRISED[] = {"Ох, блииин!"};
inline constexpr const char* STARTLED[] = {
    "ОЙ!",
    "Я ВЗДРОГНУЛ!",
    "НЕ ПУГАЙ!",
    "ВОТ ЭТО ХЛОПОК!",
};
inline constexpr const char* PICKED_UP[] = {"Поставь меня!"};
inline constexpr const char* DIZZY[] = {"Кружится..."};
inline constexpr const char* UPSIDE_DOWN[] = {"Перевернули!"};
inline constexpr const char* ATTENTION[] = {
    "Я слушаю",
    "Слушаю",
    "Говорите",
    "Я здесь",
    "Чем помочь?",
};
inline constexpr const char* LISTENING[] = {
    "Что?",
    "Ты мне?",
    "Ещё раз?",
    "Что за язык?",
    "Не понимаю.",
    "Допустим.",
    "Смысл не найден.",
    "Очень понятно.",
};

inline constexpr const char* GREETING[] = {"Привет!"};

// Форматы периодических реплик с длительностью состояния.
inline constexpr const char* CONTENT_DURATION[] = {"Ты здесь уже %luм"};
inline constexpr const char* BORED_DURATION[] = {"Никого нет уже %luм"};

inline constexpr const char* TRANSITION = "...";

template <size_t N>
constexpr uint8_t count(const char* const (&)[N]) {
  return static_cast<uint8_t>(N);
}

template <size_t N>
constexpr const char* at(const char* const (&items)[N], size_t index) {
  return items[index % N];
}

}  // namespace phrases
