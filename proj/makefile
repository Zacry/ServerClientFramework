MY_SUBMODULES_BUILD = common \
                      server \
                      client

MY_SUBMODULES_CLEAN = common_clean \
                      server_clean \
                      client_clean
.PHONY: all clean

all: $(MY_SUBMODULES_BUILD)
clean: $(MY_SUBMODULES_CLEAN)

$(MY_SUBMODULES_BUILD):
	@echo 
	@echo 
	@echo ******Apps - $@******
	@echo 
	@cd ../$@/proj && make all
	@echo 
	@echo 
	@echo 

$(MY_SUBMODULES_CLEAN):
	@echo 
	@echo 
	@echo 
	@echo ******Apps - $(subst _clean,,$@) clean******
	@echo 
	@cd ../$(subst _clean,,$@)/proj && make clean 
	@echo 
	@echo 
	@echo 

