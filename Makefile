BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
ISO_FILE := $(BUILD_DIR)/neptune-os-build-006.iso

CC := clang
LD := ld.lld
LIMINE_DIR := limine
PYTHON := python3

CFLAGS := --target=x86_64-unknown-none-elf \
          -std=gnu11 -O2 -Wall -Wextra \
          -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone \
          -mno-sse -mno-sse2 -mno-mmx \
          -mcmodel=kernel -I$(LIMINE_DIR) \
          -Isrc/drivers -Isrc/kernel -I$(BUILD_DIR)

LDFLAGS := -nostdlib -static \
           -z max-page-size=0x1000 \
           --build-id=none -T linker.ld

.PHONY: all clean

all: $(ISO_FILE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/audio_assets.c: tools/embed_audio.py | $(BUILD_DIR)
	$(PYTHON) tools/embed_audio.py

$(BUILD_DIR)/audio_assets.h: $(BUILD_DIR)/audio_assets.c
	test -f $@

$(BUILD_DIR)/audio_assets.o: $(BUILD_DIR)/audio_assets.c $(BUILD_DIR)/audio_assets.h
	$(CC) $(CFLAGS) -c $(BUILD_DIR)/audio_assets.c -o $@

$(BUILD_DIR)/audio.o: src/drivers/audio.c src/drivers/audio.h $(BUILD_DIR)/audio_assets.h
	$(CC) $(CFLAGS) -c src/drivers/audio.c -o $@

$(BUILD_DIR)/main.o: src/kernel/main.c src/kernel/graphics.h src/drivers/keyboard.h src/drivers/mouse.h src/drivers/audio.h src/drivers/ata.h src/kernel/filesystem.h src/kernel/exceptions.h src/kernel/desktop.h src/kernel/lockscreen.h src/kernel/power.h $(LIMINE_DIR)/limine.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/main.c -o $@

$(BUILD_DIR)/graphics.o: src/kernel/graphics.c src/kernel/graphics.h $(LIMINE_DIR)/limine.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/graphics.c -o $@

$(BUILD_DIR)/keyboard.o: src/drivers/keyboard.c src/drivers/keyboard.h src/drivers/mouse.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/drivers/keyboard.c -o $@

$(BUILD_DIR)/mouse.o: src/drivers/mouse.c src/drivers/mouse.h src/drivers/keyboard.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/drivers/mouse.c -o $@

$(BUILD_DIR)/exceptions.o: src/kernel/exceptions.c src/kernel/exceptions.h src/kernel/graphics.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/exceptions.c -o $@

$(BUILD_DIR)/exceptions_asm.o: src/kernel/exceptions.S
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/exceptions.S -o $@

$(BUILD_DIR)/desktop.o: src/kernel/desktop.c src/kernel/desktop.h src/kernel/graphics.h src/drivers/keyboard.h src/drivers/mouse.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/desktop.c -o $@

$(BUILD_DIR)/lockscreen.o: src/kernel/lockscreen.c src/kernel/lockscreen.h src/kernel/graphics.h src/drivers/keyboard.h src/drivers/mouse.h src/drivers/audio.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/lockscreen.c -o $@

$(BUILD_DIR)/power.o: src/kernel/power.c src/kernel/power.h src/kernel/graphics.h src/drivers/keyboard.h src/drivers/mouse.h src/drivers/audio.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/power.c -o $@

$(BUILD_DIR)/filesystem.o: src/kernel/filesystem.c src/kernel/filesystem.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/filesystem.c -o $@

$(BUILD_DIR)/ata.o: src/drivers/ata.c src/drivers/ata.h src/kernel/filesystem.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/drivers/ata.c -o $@

$(BUILD_DIR)/neptune.elf: $(BUILD_DIR)/main.o $(BUILD_DIR)/graphics.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/exceptions.o $(BUILD_DIR)/exceptions_asm.o $(BUILD_DIR)/desktop.o $(BUILD_DIR)/lockscreen.o $(BUILD_DIR)/power.o $(BUILD_DIR)/audio.o $(BUILD_DIR)/audio_assets.o $(BUILD_DIR)/filesystem.o $(BUILD_DIR)/ata.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/main.o $(BUILD_DIR)/graphics.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/mouse.o $(BUILD_DIR)/exceptions.o $(BUILD_DIR)/exceptions_asm.o $(BUILD_DIR)/desktop.o $(BUILD_DIR)/lockscreen.o $(BUILD_DIR)/power.o $(BUILD_DIR)/audio.o $(BUILD_DIR)/audio_assets.o $(BUILD_DIR)/filesystem.o $(BUILD_DIR)/ata.o

$(ISO_FILE): $(BUILD_DIR)/neptune.elf boot/limine.conf
	test -x $(LIMINE_DIR)/limine
	test -f $(LIMINE_DIR)/limine-bios.sys
	test -f $(LIMINE_DIR)/limine-bios-cd.bin
	test -f $(LIMINE_DIR)/limine-uefi-cd.bin
	test -f $(LIMINE_DIR)/BOOTX64.EFI

	mkdir -p $(ISO_ROOT)/boot
	mkdir -p $(ISO_ROOT)/EFI/BOOT

	cp $(BUILD_DIR)/neptune.elf $(ISO_ROOT)/boot/neptune.elf
	cp boot/limine.conf $(ISO_ROOT)/boot/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/

	xorriso -as mkisofs \
		-R -r -J \
		-b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image \
		--protective-msdos-label \
		-o $(ISO_FILE) $(ISO_ROOT)

	$(LIMINE_DIR)/limine bios-install $(ISO_FILE)

clean:
	rm -rf $(BUILD_DIR)
