#include ../../build.cfg
include ./build.cfg

TAR_FILE=configtool.exe
RESUME_DIR_FILES += -path ./onvif -prune -o

SRC_FILES=$(shell find ./  $(RESUME_DIR_FILES) -name "*.c" -print)
OBJ_FILES=$(patsubst %.c,%.o,$(SRC_FILES))


LIB_PATH=./
LIB_FILES= -lonvif -ldebugout -lssl -lcrypto -lpthread -ldl


all:$(RM) $(TAR_FILE)
$(RM):
	rm -rf $(TAR_FILE)
$(TAR_FILE):$(OBJ_FILES)
	$(CC) -o $(TAR_FILE) $(OBJ_FILES) -L$(LIB_PATH) $(LIB_FILES)
	
install:
	cp $(TAR_FILE) $(IMAGE_DIR)/userfs/bin
clean:
	rm -rf $(OBJ_FILES)
	rm -rf $(TAR_FILE)
