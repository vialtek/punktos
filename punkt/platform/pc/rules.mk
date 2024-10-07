LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
    lib/acpi_lite \
    lib/bio \
    lib/cbuf \
    dev/bus/pci/drivers

MODULE_SRCS += \
    $(LOCAL_DIR)/cmos.c \
    $(LOCAL_DIR)/console.c \
    $(LOCAL_DIR)/debug.c \
    $(LOCAL_DIR)/ide.c \
    $(LOCAL_DIR)/interrupts.c \
    $(LOCAL_DIR)/keyboard.c \
    $(LOCAL_DIR)/lapic.c \
    $(LOCAL_DIR)/pic.c \
    $(LOCAL_DIR)/platform.c \
    $(LOCAL_DIR)/timer.c \
    $(LOCAL_DIR)/uart.c \

include make/module.mk

