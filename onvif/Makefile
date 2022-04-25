include ../../../build.cfg

TAR_FILE=libonvif.a


RESUME_DIR_FILES += -path ./demo -prune -o
#RESUME_DIR_FILES += -path ./libcurl  -prune -o

#SRC_FILES=$(shell find ./ -name "*.c" -print)
SRC_FILES = $(shell find ./  $(RESUME_DIR_FILES) -name "*.c" -print)
OBJ_FILES=$(patsubst %.c,%.o,$(SRC_FILES))


INC_PATH=./
INC_PATH+=-I../include/
ifeq ($(ARCH_NAME), arm)
INC_PATH+=-I./libcurl/arm_53/include/
else
INC_PATH+=-I./libcurl/x86/include/
endif
INC_PATH+=-I../../../P1/include/
CFLAGS+=-I$(INC_PATH) -Wall -DWITH_DOM -DWITH_OPENSSL
#$(CXXFLAGS) +=-g -Wall -D__linux

all:$(TAR_FILE)
$(TAR_FILE):$(OBJ_FILES)
	echo $(OBJ_FILES)
	$(AR) -rc $(TAR_FILE) $(OBJ_FILES)
	cp $(TAR_FILE) ../

install:
	cp $(TAR_FILE) ../
clean:
	rm -rf $(OBJ_FILES)
	rm -rf $(TAR_FILE)
