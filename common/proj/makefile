define print-info
    @echo ""
    @echo "[MAKEFILE] Start building $(1)"
endef

include ../../proj/makefile.rule

PATH_SRC = ../src
PATH_INC = ../inc
PATH_BIN = ../bin
PATH_OBJ = ../obj

SRC = $(wildcard $(PATH_SRC)/*.c)
OBJ = $(patsubst %.c, $(PATH_OBJ)/%.o, $(notdir $(SRC)))

CFLAGS += $(addprefix -I, $(PATH_INC))

$(PATH_OBJ)/%.o : $(PATH_SRC)/%.c
	$(call print-info,$@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean 

all: $(OBJ)
	@echo -------------------------------------------------------------
	@echo "[MAKEFILE] Build Completed"

clean:
	@echo -------------------------------------------------------------
	@echo "[MAKEFILE] Remove obj"
	-rm -f $(PATH_OBJ)/*
	@echo -------------------------------------------------------------

