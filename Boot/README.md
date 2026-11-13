# Boot

Телефонная загрузочная цепочка Byter: ARM64 shim запускает AArch64 ELF из
Android boot image, переданного командой `fastboot boot`.

```text
arch/aarch64/entry.S       ARM64 Image entry и переход к ядру
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
framebuffer. Это пока не полный Limine memory map: paging и HHDM не реализованы.
