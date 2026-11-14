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

Нужны `clang`, `ld.lld`, `llvm-objcopy`, `mkbootimg`, `fastboot` и `python3`.

```sh
make doctor   # проверить инструменты
make          # собрать Boot/build/byter-boot.img
make boot     # собрать и запустить в RAM
```

По умолчанию сборка читает `Boot/byter.cfg`: `kernel=` выбирает ELF, а
`cmdline=` задаёт строку, которую ядро получает через executable-cmdline request.
Путь `kernel=` считается относительно файла конфига. Можно выбрать другой конфиг
командой `make boot CONFIG=Boot/dev.cfg`; параметр
`ELF=/путь/ядро.elf` переопределяет kernel из конфига.

`example/` — демонстрационное ядро загрузчика, а не основная часть Byter. Сейчас
оно проверяет ответы framebuffer, memory map, executable address и наличие
переведённого DTB `reg`-ресурса в memory map, выполняет EL1 SVC с возвратом через
`eret`, затем рисует цветной квадрат.
Верхний левый голубой маркер означает, что `reg` найден как `RESERVED`, но
внешнего ресурса выше 4 GiB в DTB не обнаружено. Пурпурный маркер означает, что
такой ресурс найден и проверен через identity map и HHDM; красный — что проверка
не прошла. Запустить отдельный
диагностический RAM-образ можно командой `make diagnostic`. Чтобы изменить
пример, открой `example/main.c` и выполни `make boot`.

Для своего ELF64 AArch64 `ET_EXEC`:

```sh
make boot ELF=/путь/к/моему-ядру.elf
```

## Структура

- `Boot/arch/aarch64/` — ARM64 Image entry и linker script shim.
- `Boot/core/` — загрузчик ELF и обработка запросов Limine.
- `Boot/include/byter.h` — Byter-имена для структур и ID Limine-совместимого wire ABI.
- [`Boot/ABI.md`](Boot/ABI.md) — поддерживаемые запросы, ревизии и ограничения ABI.
- `example/` — минимальный пример ядра на Byter ABI.
- `tools/` — упаковка Android boot image и инструменты разработки.
- [ROADMAP.md](ROADMAP.md) — этапы развития и критерии готовности.

Shim включает 4 KiB page tables перед входом в ядро: RAM и boot data остаются
доступны по identity map, а ELF-сегменты отображаются по своим виртуальным
адресам с правами из `PT_LOAD`. Shim отвечает на HHDM-запрос и возвращает
указатели ABI через HHDM. Отображаются первые 4 GiB, RAM и framebuffer из DTB,
а также известные reservations выше 4 GiB. Полная карта физических областей и
совместимость с base revision Limine пока не реализованы.

Цвет центрального квадрата показывает последний пройденный этап загрузки:
синий — старт shim, голубой — DTB, жёлтый — проверка ELF, оранжевый — загрузка
ELF, фиолетовый — base revision, бирюзовый — подготовленные таблицы страниц,
белый — handoff, зелёный — demo работает. Квадрат остаётся на последнем цвете,
если выполнение останавливается.
