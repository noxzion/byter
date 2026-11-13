.DEFAULT_GOAL := all

.PHONY: help all image kernel boot doctor clean

help:
	@printf '%s\n' 'make         собрать RAM-образ' \
	               'make kernel  собрать ELF-ядро' \
	               'make boot    собрать и запустить через fastboot boot' \
	               'make boot ELF=/path/kernel.elf  загрузить своё ELF' \
	               'make doctor  проверить инструменты сборки' \
	               'make clean   удалить результаты сборки'

all: image

image:
	$(MAKE) -C Boot image $(if $(ELF),ELF=$(abspath $(ELF)))

kernel:
	$(MAKE) -C Boot kernel

boot:
	$(MAKE) -C Boot boot $(if $(ELF),ELF=$(abspath $(ELF)))

doctor:
	@status=0; for tool in clang ld.lld llvm-objcopy mkbootimg fastboot python3; do \
		if command -v $$tool >/dev/null 2>&1; then printf 'OK      %s\n' "$$tool"; \
		else printf 'MISSING %s\n' "$$tool"; status=1; fi; \
	done; exit $$status

clean:
	$(MAKE) -C Boot clean
