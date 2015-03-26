define print-info
    @echo ""
    @echo "[MAKEFILE] Start building $(1)"
endef

include ../../proj/makefile.rule

PATH_COMMON = ../../common
PATH_SRC = ../src
PATH_INC = ../inc \
           $(PATH_COMMON)/inc
PATH_BIN = ../bin
PATH_OBJ = ../obj

SRC = $(wildcard $(PATH_SRC)/*.c)
OBJ = $(patsubst %.c, $(PATH_OBJ)/%.o, $(notdir $(SRC)))
TARGET = $(PATH_BIN)/client.out
COMMON_OBJ = $(wildcard $(PATH_COMMON)/obj/*)

CFLAGS += $(addprefix -I, $(PATH_INC))

$(TARGET) : $(OBJ)
	$(call print-info,$@)
	$(CC) $(CFLAGS) $(LIB) -o $@ $^ $(COMMON_OBJ)

$(PATH_OBJ)/%.o : $(PATH_SRC)/%.c
	$(call print-info,$@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean swversion 

all: swversion $(TARGET)
	@echo -------------------------------------------------------------
	@echo "[MAKEFILE] Build Completed"

swversion:
	@perl swversion.pl

clean:
	@echo -------------------------------------------------------------
	@echo "[MAKEFILE] Remove bin and obj"
	-rm -f $(PATH_BIN)/* $(PATH_OBJ)/* 
	@echo -------------------------------------------------------------

