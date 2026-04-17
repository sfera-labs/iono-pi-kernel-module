MODULE_MAIN_OBJ := module.o
COMMON_MODULES := utils gpio wiegand atecc
UDEV_RULES := 99-ionopi.rules

SOURCE_DIR := $(if $(src),$(src),$(CURDIR))
include $(SOURCE_DIR)/commons/scripts/kmod-common.mk
