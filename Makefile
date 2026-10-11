BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
ISO_FILE := $(BUILD_DIR)/neptune-os-build-001.iso

CC := clang
LD := ld.lld
LIMINE_DIR := limine

CFLAGS := --target=x86_64-unknown-none-elf \
          -std=gnu11 -O2 -Wall -Wextra \
          -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -mno-red-zone \
          -mcmodel=small -I$(LIMINE_DIR)

LDFLAGS := -nostdlib -static \
           -z max-page-size=0x1000 \
           --build-id=none -T linker.ld

.PHONY: all clean

all: $(ISO_FILE)

$(BUILD_DIR)/main.o: src/kernel/main.c src/kernel/graphics.h $(LIMINE_DIR)/limine.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/main.c -o $@

$(BUILD_DIR)/graphics.o: src/kernel/graphics.c src/kernel/graphics.h $(LIMINE_DIR)/limine.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c src/kernel/graphics.c -o $@

$(BUILD_DIR)/neptune.elf: $(BUILD_DIR)/main.o $(BUILD_DIR)/graphics.o linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD_DIR)/main.o $(BUILD_DIR)/graphics.o

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
