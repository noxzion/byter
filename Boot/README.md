# Boot

Телефонная загрузочная цепочка Byter: ARM64 shim запускает AArch64 ELF из
Android boot image, переданного командой `fastboot boot`.

```text
arch/aarch64/entry.S       ARM64 Image entry и переход к ядру на EL1
arch/aarch64/page_tables.c таблицы страниц и права PT_LOAD
arch/aarch64/image.ld      linker script shim
core/elf_loader.c          проверка и загрузка ELF, ответы Byter ABI
include/byter.h           публичные структуры и ID Byter ABI
../example/                 пример ядра
../tools/                  упаковщик Android boot image
```

Собрать образ можно из корня проекта командой `make`; результат появится в
`Boot/build/byter-boot.img`. `make boot` дополнительно запускает его через
Fastboot. Передаётся только временный RAM-образ, разделы не прошиваются.

Пример ядра в `example/main.c` проверяет ответы framebuffer, memory map и
executable address, затем рисует цветной квадрат.
Framebuffer shim получает из DTB-узла `simple-framebuffer`. Если ABL его не
передаёт, используется изолированный проверенный профиль A059 в
`platform/a059/framebuffer.c`. Shim отвечает на bootloader-info, DTB,
framebuffer и memory-map запросы. Карта памяти строится по RAM и known
reservations из DTB, с отдельными типами для областей загрузчика, ELF-ядра и
framebuffer. Shim включает MMU и передаёт ядру identity map для RAM и boot data,
а сегменты ядра отображает по ELF VMA с правами `PT_LOAD`. HHDM отображает RAM
первые 4 GiB, RAM из DTB и framebuffer. Пример запрашивает Byter handoff revision 0
и меняет цвет центрального квадрата после прохождения каждого этапа. Если
загрузка останавливается, квадрат сохраняет цвет последнего достигнутого этапа.
Memory map пока ограничена RAM и reservations из DTB, поэтому полной совместимости
с Limine Boot Protocol ещё нет.
