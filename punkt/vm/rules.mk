LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_DEPS += \
	lib/sbl \
  lib/user_copy

MODULE_SRCS += \
	$(LOCAL_DIR)/bootalloc.c \
	$(LOCAL_DIR)/pmm.c \
	$(LOCAL_DIR)/vm.c \
	$(LOCAL_DIR)/vmm.c \

MODULE_OPTIONS := extra_warnings

include make/module.mk
