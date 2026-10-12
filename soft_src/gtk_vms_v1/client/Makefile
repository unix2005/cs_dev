# GTK4 车辆管理系统客户端
#
# 构建前置依赖（macOS）：
#   brew install gtk4 pkg-config
# Linux (Debian/Ubuntu)：
#   sudo apt install libgtk-4-dev pkg-config
#
# UI 文件目录通过 -DUI_DIR 注入，运行时按此绝对路径加载 .ui / .css

CC      ?= cc
PKG     ?= gtk4
UI_DIR  := $(abspath ../ui)

CFLAGS  += $(shell pkg-config --cflags $(PKG)) -DUI_DIR=\"$(UI_DIR)\" -Wall -O2 -Wno-deprecated-declarations
LIBS    := $(shell pkg-config --libs $(PKG))

TARGET   = gtk_vms_client
SRC      = main.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
