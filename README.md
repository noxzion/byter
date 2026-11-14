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

`example/` — демонстрационное ядро загрузчика, а не основная часть Byter. Сейчас
оно проверяет ответы framebuffer, memory map и executable address по Byter ABI,
затем рисует один цветной квадрат. Чтобы изменить пример, открой
`example/main.c` и выполни `make boot`.

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
указатели ABI через HHDM. Сам HHDM покрывает RAM из DTB и framebuffer. Полная
карта физических областей и совместимость с base revision Limine пока не
реализованы.
