LOCAL_DIR := $(GET_LOCAL_DIR)

ifeq ($(SUBARCH),arm)

ASM_STRING_OPS := bcopy bzero memcpy memmove memset

MODULE_SRCS += \
	$(LOCAL_DIR)/arm/memcpy.S \
	$(LOCAL_DIR)/arm/memset.S

# filter out the C implementation
C_STRING_OPS := $(filter-out $(ASM_STRING_OPS),$(C_STRING_OPS))
endif
