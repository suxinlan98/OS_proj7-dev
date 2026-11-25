SHELL := /bin/bash

XV6_DIR := src
STATE_FILE := .last-test
ARCH_STATE_FILE := .last-test-arch
TEST_ARCH ?= riscv

.PHONY: test_baseline public_vfs ext2 fat32 error semantics persist smp \
	mount_lifecycle remount_io mount_storm \
	loongarch_baseline loongarch_public_vfs loongarch_ext2 loongarch_fat32 loongarch_error \
	loongarch_semantics loongarch_persist loongarch_smp loongarch_mount_lifecycle \
	loongarch_remount_io loongarch_mount_storm \
	test_public_vfs test_ext2 test_fat32 test_error_path run verify-all

define prepare_case
	@set -eu; \
	TEST_NAME="$(1)"; \
	TARGET_DESC="$(2)"; \
	TEST_DIR="tests/$$TEST_NAME"; \
	LOG_DIR="$$TEST_DIR/logs"; \
	mkdir -p "$$LOG_DIR"; \
	echo "[$$TEST_NAME] record environment"; \
	{ \
		echo "Test Name: $$TEST_NAME"; \
		echo "Date: $$(date -Iseconds)"; \
		echo "Target: $$TARGET_DESC"; \
		echo "Platform: $(TEST_ARCH)"; \
		echo; \
		echo "== Host =="; \
		uname -a; \
		echo; \
		echo "== OS =="; \
		cat /etc/os-release 2>/dev/null || true; \
		echo; \
		echo "== Tools =="; \
		python3 --version 2>&1 || true; \
		make --version 2>&1 | head -n 1 || true; \
		if [ "$(TEST_ARCH)" = "loongarch" ]; then \
			qemu-system-loongarch64 --version 2>&1 | head -n 1 || true; \
			loongarch64-linux-gnu-gcc --version 2>&1 | head -n 1 || true; \
		else \
			qemu-system-riscv64 --version 2>&1 | head -n 1 || true; \
			(riscv64-unknown-elf-gcc --version 2>/dev/null || riscv64-linux-gnu-gcc --version 2>&1) | head -n 1 || true; \
		fi; \
		mkfs.ext2 -V 2>&1 | head -n 1 || true; \
		echo; \
		echo "== Git =="; \
		git rev-parse --short HEAD 2>/dev/null || echo "unknown"; \
		git status --short 2>/dev/null || true; \
	} > "$$LOG_DIR/env.log" 2>&1; \
	echo "[$$TEST_NAME] build $(TEST_ARCH) kernel and fs image"; \
	( \
		cd "$(XV6_DIR)" && \
		make ARCH="$(TEST_ARCH)" clean && \
		make ARCH="$(TEST_ARCH)" kernel/kernel && \
		rm -f "../build/$(TEST_ARCH)/fs.img" "../build/$(TEST_ARCH)/ext2.img" "../build/$(TEST_ARCH)/fat32.img" && \
		make ARCH="$(TEST_ARCH)" fs.img \
	) > "$$LOG_DIR/build.log" 2>&1; \
	printf "%s\n" "$$TEST_NAME" > "$(STATE_FILE)"; \
	printf "%s\n" "$(TEST_ARCH)" > "$(ARCH_STATE_FILE)"; \
	echo "[$$TEST_NAME] prepare done"; \
	echo "Next command: make run"
endef

test_baseline:
	$(call prepare_case,test_baseline,xv6-riscv rev5 baseline)

public_vfs:
	$(call prepare_case,public_vfs,public VFS ext2 cross-filesystem tests)

ext2:
	$(call prepare_case,ext2,ext2 mount/read/write/unlink smoke test)

fat32:
	$(call prepare_case,fat32,FAT32 8.3 create/overwrite/unlink smoke test)

error:
	$(call prepare_case,error,VFS invalid-operation stability test)

semantics:
	$(call prepare_case,semantics,mount parent and link semantics test)

persist:
	$(call prepare_case,persist,ext2/FAT32 image persistence test)

loongarch_baseline:
	@$(MAKE) TEST_ARCH=loongarch test_baseline

loongarch_public_vfs:
	@$(MAKE) TEST_ARCH=loongarch public_vfs

loongarch_ext2:
	@$(MAKE) TEST_ARCH=loongarch ext2

loongarch_fat32:
	@$(MAKE) TEST_ARCH=loongarch fat32

loongarch_error:
	@$(MAKE) TEST_ARCH=loongarch error

loongarch_semantics:
	@$(MAKE) TEST_ARCH=loongarch semantics

loongarch_persist:
	@$(MAKE) TEST_ARCH=loongarch persist

smp:
	@set -eu; \
	TEST_NAME="smp"; \
	TEST_DIR="tests/$$TEST_NAME"; \
	LOG_DIR="$$TEST_DIR/logs"; \
	mkdir -p "$$LOG_DIR"; \
	echo "[smp] build $(TEST_ARCH) kernel and fs image"; \
	( \
		cd "$(XV6_DIR)" && \
		make ARCH="$(TEST_ARCH)" clean && \
		make ARCH="$(TEST_ARCH)" kernel/kernel && \
		rm -f "../build/$(TEST_ARCH)/fs.img" "../build/$(TEST_ARCH)/ext2.img" "../build/$(TEST_ARCH)/fat32.img" && \
		make ARCH="$(TEST_ARCH)" fs.img \
	) > "$$LOG_DIR/build.log" 2>&1; \
	printf "%s\n" "$$TEST_NAME" > "$(STATE_FILE)"; \
	printf "%s\n" "$(TEST_ARCH)" > "$(ARCH_STATE_FILE)"; \
	echo "[smp] prepare done"; \
	echo "Next command: make run"

loongarch_smp:
	@$(MAKE) TEST_ARCH=loongarch smp

define prepare_extended_case
	@set -eu; \
	TEST_NAME="$(1)"; \
	TEST_DIR="tests/$$TEST_NAME"; \
	LOG_DIR="$$TEST_DIR/logs"; \
	mkdir -p "$$LOG_DIR"; \
	{ \
		echo "Test Name: $$TEST_NAME"; \
		echo "Date: $$(date -Iseconds)"; \
		echo "Platform: $(TEST_ARCH)"; \
		echo "Host: $$(uname -a)"; \
		echo "Git: $$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"; \
	} > "$$LOG_DIR/env.log" 2>&1; \
	echo "[$$TEST_NAME] build $(TEST_ARCH) kernel and fs image"; \
	( \
		cd "$(XV6_DIR)" && \
		make ARCH="$(TEST_ARCH)" clean && \
		make ARCH="$(TEST_ARCH)" kernel/kernel && \
		rm -f "../build/$(TEST_ARCH)/fs.img" "../build/$(TEST_ARCH)/ext2.img" "../build/$(TEST_ARCH)/fat32.img" && \
		make ARCH="$(TEST_ARCH)" fs.img \
	) > "$$LOG_DIR/build.log" 2>&1; \
	printf "%s\n" "$$TEST_NAME" > "$(STATE_FILE)"; \
	printf "%s\n" "$(TEST_ARCH)" > "$(ARCH_STATE_FILE)"; \
	echo "[$$TEST_NAME] prepare done"; \
	echo "Next command: make run"
endef

mount_lifecycle:
	$(call prepare_extended_case,mount_lifecycle)

remount_io:
	$(call prepare_extended_case,remount_io)

mount_storm:
	$(call prepare_extended_case,mount_storm)

loongarch_mount_lifecycle:
	@$(MAKE) TEST_ARCH=loongarch mount_lifecycle

loongarch_remount_io:
	@$(MAKE) TEST_ARCH=loongarch remount_io

loongarch_mount_storm:
	@$(MAKE) TEST_ARCH=loongarch mount_storm

test_public_vfs: public_vfs
test_ext2: ext2
test_fat32: fat32
test_error_path: error

run:
	@set -eu; \
	if [ ! -f "$(STATE_FILE)" ]; then \
		echo "No prepared test."; \
		echo "Use: make <test>; see tests/README.md for available tests"; \
		exit 1; \
	fi; \
	TEST_NAME="$$(cat "$(STATE_FILE)")"; \
	TEST_ARCH="$$(cat "$(ARCH_STATE_FILE)" 2>/dev/null || echo riscv)"; \
	case "$$TEST_NAME" in \
		test_baseline) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/test_baseline/run.sh" ;; \
		public_vfs) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/public_vfs/run.sh" ;; \
		ext2) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/ext2/run.sh" ;; \
		fat32) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/fat32/run.sh" ;; \
		error) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/error/run.sh" ;; \
		semantics) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/semantics/run.sh" ;; \
		persist) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/persist/run.sh" ;; \
		smp) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/smp/run.sh" ;; \
		mount_lifecycle) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/mount_lifecycle/run.sh" ;; \
		remount_io) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/remount_io/run.sh" ;; \
		mount_storm) ARCH="$$TEST_ARCH" TEST_ARCH="$$TEST_ARCH" bash "tests/mount_storm/run.sh" ;; \
		*) echo "Unknown prepared test: $$TEST_NAME"; exit 1 ;; \
	esac

verify-all:
	@bash tests/check_env.sh
	@bash tests/run_all.sh
	@bash tests/full_usertests.sh riscv
	@bash tests/full_usertests.sh loongarch
	@echo "verify-all: ALL GREEN"
