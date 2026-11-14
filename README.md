# Byter

**А что, если бы Limine был на телефонах?** Byter — эксперимент над этим
вопросом: загрузчик в духе Limine для Android-телефонов, сначала для Nothing
Phone (3a), с понятным протоколом и простой сборкой собственного ядра.

Сейчас цепочка запускается из RAM через `fastboot boot`:

```text
Android boot image → ARM64 shim → AArch64 ELF kernel
```

Ничего не прошивается в разделы телефона.

## Быстрый старт

Нужны `clang`, `ld.lld` и `llvm-objcopy`, чтобы собрать shim.

```sh
make -C Boot shim
```

Основной репозиторий содержит загрузчик и его платформенный код. Сборка ядра и
Android boot image выполняется отдельно в [Byter template](https://github.com/noxzion/byter-template).
В нём можно собрать ELF или запустить готовый template через `fastboot boot`.
Исходники прежнего диагностического ядра могут оставаться локально в
игнорируемом `example/`, но в репозиторий не входят.

## Структура

- `Boot/arch/aarch64/` — ARM64 Image entry и linker script shim.
- `Boot/core/` — загрузчик ELF и обработка запросов Limine.
- `Boot/include/byter.h` — Byter-имена для структур и ID Limine-совместимого wire ABI.
- [`Boot/ABI.md`](Boot/ABI.md) — поддерживаемые запросы, ревизии и ограничения ABI.
- `tools/` — утилиты загрузчика, используемые template при упаковке образа.
- [ROADMAP.md](ROADMAP.md) — этапы развития и критерии готовности.

[Минимальный пример ядра на Byter ABI](https://github.com/noxzion/byter-template.git)

Shim включает 4 KiB page tables перед входом в ядро: RAM и boot data остаются
доступны по identity map, а ELF-сегменты отображаются по своим виртуальным
адресам с правами из `PT_LOAD`. Shim отвечает на HHDM-запрос и возвращает
указатели ABI через HHDM. Отображаются первые 4 GiB, RAM и framebuffer из DTB,
а также известные reservations выше 4 GiB. Полная карта физических областей и
совместимость с base revision Limine пока не реализованы.

Загрузчик выводит ход загрузки текстовыми сообщениями в framebuffer без полной
заливки экрана перед первой строкой.
При остановке на экране остаётся последняя строка; сообщения об ошибке настройки
таблиц страниц указывают конкретный этап.
