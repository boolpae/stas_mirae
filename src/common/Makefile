PRJ_HOME	:= $(shell echo $(PROJECT_ROOT) | sed 's/\ /\\ /g')
-include $(PRJ_HOME)/Makefile
PWD	:= $(shell pwd | sed 's/\ /\\ /g')
ifeq ($(BUILD), )
BUILD	:= $(PWD:$(shell dirname $(PWD))/%=%)
endif

###############################################################################
VERSION			:= 0.1.0
SOURCE			:= configuration.cpp CallSignal.cpp
INCLUDE_PATH	:= include
LIBRARIES		:= 
FLAGS			:= 
ifeq ($(ENABLE_REALTIME), "TRUE")
FLAGS			+= -DENABLE_REALTIME # 실시간 기능 활성화 Flag
ifeq ($(EN_RINGBACK_LEN), "TRUE")
FLAGS			+= -DEN_RINGBACK_LEN # 통화연결음 길이를 이용할 경우 - 추후 Define을 이요하지 않고 기본으로 빌드할 예정
endif
ifeq ($(FOR_TEST), "TRUE")
FLAGS			+= -DFOR_TEST # 상담원, 고객 간의 STT결과를 하나의 파일에 저장하기 위한 테스트 코드(실시간 전용)
endif
ifeq ($(FOR_ITFACT), "TRUE")
FLAGS			+= -DFOR_ITFACT
endif
endif

SHARED_LIBS		:= -lboost_program_options -lboost_filesystem -lboost_system -llog4cpp
###############################################################################

ifeq ($(MAKECMDGOALS), $(BUILD)_all)
-include $(DEPEND_FILE)
endif

OBJ_DIR		:= $(shell echo $(OBJS_PATH)/$(BUILD) | sed 's/\ /\\ /g')
LIB_DIR		:= $(shell echo $(LIBS_PATH) | sed 's/\ /\\ /g')
BUILD_DIR	:= $(shell echo $(LIBS_PATH)/$(DIST) | sed 's/\ /\\ /g')

$(BUILD)_OBJS	:= $(SOURCE:%.cpp=$(OBJ_DIR)/%.o)
$(BUILD)_LIBS	:= $(LIBRARIES:%=$(LIB_DIR)/%.a)
BUILD_NAME		:= $(BUILD_DIR)/$(PROJECT_NAME)_$(BUILD)-$(VERSION).a

$(BUILD)_all: $($(BUILD)_OBJS)
	@`[ -d "$(BUILD_DIR)" ] || $(MKDIR) "$(BUILD_DIR)"`
	$(AR) rcv "$(BUILD_NAME)" $($(BUILD)_OBJS) $($(BUILD)_LIBS)
	$(RANLIB) "$(BUILD_NAME)"
	$(LINK) "$(BUILD_NAME:$(BUILD_DIR)/%=%)" "$(BUILD_NAME:%-$(VERSION).a=%.a)"

.SECONDEXPANSION:
$(OBJ_DIR)/%.o: %.cpp
	@`[ -d "$(OBJ_DIR)" ] || $(MKDIR) "$(OBJ_DIR)"`
	$(CPP) $(CFLAGS) $(FLAGS) $(INCLUDE) -c $< -o "$@"

$(BUILD)_depend:
	@$(ECHO) "# $(OBJ_DIR)" > $(DEPEND_FILE)
	@for FILE in $(SOURCE:%/%.cc=%); do \
		$(CPP) -MM -MT "$(OBJ_DIR)/$$FILE.o" $$FILE.c $(CFLAGS) $(FLAGS) $(INCLUDE) >> $(DEPEND_FILE); \
	done

$(BUILD)_clean:
	$(RM) -rf "$(OBJ_DIR)"
	$(RM) -f "$(BUILD_NAME)"
	$(RM) -f "$(BUILD_NAME:%-$(VERSION).a=%.a)"

$(BUILD)_mrproper:
	@$(RM) -f $(DEPEND_FILE)
