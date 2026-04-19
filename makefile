obj-m := qcom_rndis_driver.o synaptics_rmi4.o msm_fb_driver.o

KERNEL_DIR := /home/runner/work/Android-Kernel-Builder/Android-Kernel-Builder/kernel_source
CC := aarch64-linux-android33-clang
LD := ld.lld
AR := llvm-ar
NM := llvm-nm
STRIP := llvm-strip

CFLAGS := -O2 -fno-strict-aliasing -fno-common -fno-stack-protector

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=arm64 \
		CC=$(CC) LD=$(LD) AR=$(AR) NM=$(NM) STRIP=$(STRIP) \
		CFLAGS_MODULE="$(CFLAGS)" modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean