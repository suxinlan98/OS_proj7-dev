K=kernel
U=user
ARCH ?= riscv
BUILD ?= ../build/$(ARCH)
HOST_BUILD ?= ../build/host

COMMON_OBJS = \
  $K/console.o \
  $K/printf.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/block.o \
  $K/fs.o \
  $K/ext2.o \
  $K/fat32.o \
  $K/vfs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o

ifeq ($(ARCH),riscv)
ARCH_OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/kernelvec.o \
  $K/hal.o \
  $K/plic.o \
  $K/virtio_disk.o
KERNEL_LD = $K/kernel.ld
USER_LD = $U/user.ld
ARCH_CFLAGS = -DARCH_RISCV -mcmodel=medany
ARCH_ASFLAGS =
QEMU = qemu-system-riscv64
else ifeq ($(ARCH),loongarch)
ARCH_OBJS = \
  $K/arch/loongarch/entry.o \
  $K/arch/loongarch/start.o \
  $K/arch/loongarch/vm.o \
  $K/proc.o \
  $K/arch/loongarch/swtch.o \
  $K/arch/loongarch/uservec.o \
  $K/arch/loongarch/kernelvec.o \
  $K/arch/loongarch/tlbrefill.o \
  $K/arch/loongarch/merror.o \
  $K/arch/loongarch/trap.o \
  $K/arch/loongarch/hal.o \
  $K/arch/loongarch/apic.o \
  $K/arch/loongarch/extioi.o \
  $K/arch/loongarch/virtio_pci.o \
  $K/virtio_disk.o
KERNEL_LD = $K/arch/loongarch/kernel.ld
USER_LD = $K/arch/loongarch/user.ld
ARCH_CFLAGS = -DARCH_LOONGARCH -I$K -I$K/arch/loongarch -march=loongarch64 -mabi=lp64s
ARCH_ASFLAGS = -DARCH_LOONGARCH -I$K -I$K/arch/loongarch -march=loongarch64 -mabi=lp64s
QEMU = qemu-system-loongarch64
else
$(error unsupported ARCH=$(ARCH))
endif

OBJS = $(addprefix $(BUILD)/, $(ARCH_OBJS) $(COMMON_OBJS))
KERNEL_OUT = $(BUILD)/$K/kernel
FSIMG = $(BUILD)/fs.img
EXT2IMG = $(BUILD)/ext2.img
FAT32IMG = $(BUILD)/fat32.img
MKFS = $(HOST_BUILD)/mkfs
MKFAT32 = $(HOST_BUILD)/mkfat32

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
ifeq ($(ARCH),riscv)
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
else ifeq ($(ARCH),loongarch)
TOOLPREFIX := $(shell if loongarch64-linux-gnu-objdump -i >/dev/null 2>&1; \
	then echo 'loongarch64-linux-gnu-'; \
	elif loongarch64-unknown-linux-gnu-objdump -i >/dev/null 2>&1; \
	then echo 'loongarch64-unknown-linux-gnu-'; \
	else echo ''; fi)
endif
endif

ifeq ($(ARCH),loongarch)
ifeq ($(TOOLPREFIX),)
$(error Couldn't find a loongarch64 GCC/binutils; set TOOLPREFIX=... ARCH=loongarch)
endif
endif

MIN_QEMU_VERSION = 7.2

ifeq ($(ARCH),loongarch)
CC := $(shell if command -v $(TOOLPREFIX)gcc >/dev/null 2>&1; \
	then echo $(TOOLPREFIX)gcc; \
	elif command -v $(TOOLPREFIX)gcc-14 >/dev/null 2>&1; \
	then echo $(TOOLPREFIX)gcc-14; \
	else echo $(TOOLPREFIX)gcc; fi)
else
CC = $(TOOLPREFIX)gcc
endif
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -MD
CFLAGS += $(ARCH_CFLAGS)
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

.PHONY: kernel/kernel fs.img qemu qemu-gdb clean clean-all print-gdbport check-qemu-version

kernel/kernel: $(KERNEL_OUT)

fs.img: $(FSIMG)

$(KERNEL_OUT): $(OBJS) $(KERNEL_LD)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T $(KERNEL_LD) -o $@ $(OBJS)
	$(OBJDUMP) -S $@ > $(BUILD)/$K/kernel.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD)/$K/kernel.sym

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/$K/%.o: $K/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_ASFLAGS) -g -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c

ULIB = $(BUILD)/$U/ulib.o $(BUILD)/$U/usys.o $(BUILD)/$U/printf.o $(BUILD)/$U/umalloc.o

$(BUILD)/$U/_%: $(BUILD)/$U/%.o $(ULIB) $(USER_LD)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -T $(USER_LD) -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $(BUILD)/$U/$*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(BUILD)/$U/$*.sym

$(BUILD)/$U/usys.S: $U/usys.pl
	@mkdir -p $(dir $@)
	perl $U/usys.pl > $@

$(BUILD)/$U/usys.o: $(BUILD)/$U/usys.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/$U/_forktest: $(BUILD)/$U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $(BUILD)/$U/forktest.o $(BUILD)/$U/ulib.o $(BUILD)/$U/usys.o
	$(OBJDUMP) -S $@ > $(BUILD)/$U/forktest.asm

$(MKFS): mkfs/mkfs.c $K/fs.h $K/param.h
	@mkdir -p $(dir $@)
	gcc -I. -o $@ mkfs/mkfs.c

$(MKFAT32): mkfs/mkfat32.c
	@mkdir -p $(dir $@)
	gcc -Wall -Werror -O -o $@ mkfs/mkfat32.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: $(BUILD)/%.o

UPROGS=\
	$(BUILD)/$U/_cat\
	$(BUILD)/$U/_echo\
	$(BUILD)/$U/_forktest\
	$(BUILD)/$U/_grep\
	$(BUILD)/$U/_init\
	$(BUILD)/$U/_kill\
	$(BUILD)/$U/_ln\
	$(BUILD)/$U/_ls\
	$(BUILD)/$U/_mkdir\
	$(BUILD)/$U/_mount\
	$(BUILD)/$U/_umount\
	$(BUILD)/$U/_umountstress\
	$(BUILD)/$U/_rm\
	$(BUILD)/$U/_sh\
	$(BUILD)/$U/_stressfs\
	$(BUILD)/$U/_hello\
	$(BUILD)/$U/_test1\
	$(BUILD)/$U/_test2\
	$(BUILD)/$U/_unlinkopen\
	$(BUILD)/$U/_bigfile\
	$(BUILD)/$U/_vfsbench\
	$(BUILD)/$U/_usertests\
	$(BUILD)/$U/_grind\
	$(BUILD)/$U/_wc\
	$(BUILD)/$U/_zombie\
	$(BUILD)/$U/_logstress\
	$(BUILD)/$U/_forphan\
	$(BUILD)/$U/_dorphan\

EXT2_BLOCKS := $(shell awk '/^#define[[:space:]]+EXT2SIZE/{print $$3}' $K/param.h)

$(EXT2IMG): hello.c $K/param.h
	@command -v mkfs.ext2 >/dev/null || { echo "missing mkfs.ext2 (install e2fsprogs)"; exit 1; }
	@command -v debugfs >/dev/null || { echo "missing debugfs (install e2fsprogs)"; exit 1; }
	@mkdir -p $(dir $@)
	rm -f $(EXT2IMG)
	dd if=/dev/zero of=$(EXT2IMG) bs=1024 count=$(EXT2_BLOCKS)
	mkfs.ext2 -F -q -b 1024 -I 128 -m 0 $(EXT2IMG)
	debugfs -w -R "write hello.c hello.c" $(EXT2IMG)
	debugfs -w -R "write hello.c verylongfilename.txt" $(EXT2IMG)
	dd if=/dev/zero bs=1024 count=1024 of=$(BUILD)/hostbig.bin 2>/dev/null
	debugfs -w -R "write $(BUILD)/hostbig.bin hostbig.bin" $(EXT2IMG)
	rm -f $(BUILD)/hostbig.bin

$(FAT32IMG): $(MKFAT32) $K/param.h
	@mkdir -p $(dir $@)
	rm -f $(FAT32IMG)
	$(MKFAT32) $(FAT32IMG)

$(BUILD)/tests/test1: $(BUILD)/$U/_test1
	@mkdir -p $(dir $@)
	ln -sf ../user/_test1 $@

$(BUILD)/tests/test2: $(BUILD)/$U/_test2
	@mkdir -p $(dir $@)
	ln -sf ../user/_test2 $@

$(BUILD)/tests/hello: $(BUILD)/$U/_hello
	@mkdir -p $(dir $@)
	ln -sf ../user/_hello $@

$(FSIMG): $(MKFS) README hello.c tests/cmds.sh tests/hello.c $(BUILD)/tests/test1 $(BUILD)/tests/test2 $(BUILD)/tests/hello $(UPROGS) $(EXT2IMG) $(FAT32IMG)
	@mkdir -p $(dir $@)
	$(MKFS) $@ README hello.c tests/cmds.sh tests/hello.c $(BUILD)/tests/test1 $(BUILD)/tests/test2 $(BUILD)/tests/hello $(UPROGS)
	cat $(EXT2IMG) >> $@
	cat $(FAT32IMG) >> $@

-include $(BUILD)/kernel/*.d $(BUILD)/kernel/arch/loongarch/*.d $(BUILD)/user/*.d

clean:
	rm -rf $(BUILD)
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*/*.o */*/*/*.o */*.d */*/*.d */*/*/*.d \
	*/*.asm */*/*.asm */*/*/*.asm */*.sym */*/*.sym */*/*/*.sym \
	$K/kernel fs.img ext2.img fat32.img .arch-* .gdbinit \
	mkfs/mkfs mkfs/mkfat32 \
	tests/test1 tests/test2 tests/hello \
	$U/usys.S $U/_*

clean-all: clean
	rm -rf ../build

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 3
endif

ifeq ($(ARCH),loongarch)
QEMUOPTS = -machine virt -kernel $(KERNEL_OUT) -m 1G -smp $(CPUS) -nographic -no-reboot -net none
QEMUOPTS += -drive file=$(FSIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-pci,drive=x0,disable-legacy=on,vectors=0
else
QEMUOPTS = -machine virt -bios none -kernel $(KERNEL_OUT) -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(FSIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
endif

qemu: check-qemu-version $(KERNEL_OUT) $(FSIMG)
	$(QEMU) $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $(KERNEL_OUT) .gdbinit $(FSIMG)
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+).*/\1/')
check-qemu-version:
	@python3 -c 'import sys; cur=tuple(int(x) for x in sys.argv[1].split(".")); req=tuple(int(x) for x in sys.argv[2].split(".")); sys.exit(0 if cur >= req else 1)' "$(QEMU_VERSION)" "$(MIN_QEMU_VERSION)" || { \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION) (got $(QEMU_VERSION))"; \
		exit 1; \
	}
