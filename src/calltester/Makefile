PRJ_HOME	:= $(shell echo $(PROJECT_ROOT) | sed 's/\ /\\ /g')
KERNEL_VERSION	:= $(shell uname -r | awk -F. '{print $$1}')

-include $(PRJ_HOME)/Makefile
PWD	:= $(shell pwd | sed 's/\ /\\ /g')
ifeq ($(BUILD), )
BUILD	:= $(PWD:$(shell dirname $(PWD))/%=%)
endif

###############################################################################
SOURCE			:= CallTester.cpp
#SOURCE			+= v1/restapi_v1.cc v1/servers.cc v1/waves.cc
#INCLUDE_PATH	:= $(PRJ_HOME)/include/dnn
LIBRARIES		:= ${DIST}/itf_common
#LIBRARIES		+= dnn/libsplproc dnn/libfrontend dnn/libmsearch dnn/libasearch
#LIBRARIES		+= dnn/libbase dnn/liblsearch dnn/liblaserdnn2 dnn/libdnnapi dnn/libdnn.gpu
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


SHARED_LIBS		:= -lboost_program_options -lboost_filesystem -lboost_system
SHARED_LIBS		+= -llog4cpp
SHARED_LIBS		+= -lrt -lm -lpthread
#ifeq ($(KERNEL_VERSION), 2)
#SHARED_LIBS		+= -L/usr/lib64/atlas -llapack -lcblas -latlas -lf77blas
#else ifeq ($(KERNEL_VERSION), 3)
#SHARED_LIBS		+= -L/usr/lib64/atlas -lsatlas -ltatlas
#endif
#SHARED_LIBS		+= -L/usr/lib64/atlas -llapack -lcblas -latlas -lf77blas
#SHARED_LIBS		+= -L/usr/lib64/atlas -lsatlas -ltatlas
#SHARED_LIBS		+= -L/usr/local/cuda/lib64 -lcudart -lcublas -lcuda
###############################################################################

ifeq ($(MAKECMDGOALS), $(BUILD)_all)
-include $(DEPEND_FILE)
endif

OBJ_DIR		:= $(shell echo $(OBJS_PATH)/$(BUILD) | sed 's/\ /\\ /g')
LIB_DIR		:= $(shell echo $(LIBS_PATH) | sed 's/\ /\\ /g')
BUILD_DIR	:= $(shell echo $(BINS_PATH) | sed 's/\ /\\ /g')

$(BUILD)_OBJS	:= $(SOURCE:%.cpp=$(OBJ_DIR)/%.o)
$(BUILD)_LIBS	:= $(LIBRARIES:%=$(LIB_DIR)/%.a)
BUILD_NAME		:= $(BUILD_DIR)/$(PROJECT_NAME)_$(BUILD)

$(BUILD)_all: $($(BUILD)_OBJS)
	$(CPP) -o "$(BUILD_NAME)" $($(BUILD)_OBJS) $($(BUILD)_LIBS) $(SHARED_LIBS)

.SECONDEXPANSION:
$(OBJ_DIR)/%.o: %.cpp
	@`[ -d "$(OBJ_DIR)" ] || $(MKDIR) "$(OBJ_DIR)"`
	@`[ -d "$(OBJ_DIR)/$(shell dirname $<)" ] || $(MKDIR) "$(OBJ_DIR)/$(shell dirname $<)"`
	$(CPP) $(CFLAGS) $(FLAGS) $(INCLUDE) $(INCLUDE_PATH:%=-I"%") -c $< -o "$@"

$(BUILD)_depend:
	@$(ECHO) "# $(OBJ_DIR)" > $(DEPEND_FILE)
	@for FILE in $(SOURCE:%.cpp=%); do \
		$(CPP) -MM -MT "$(OBJ_DIR)/$$FILE.o" $$FILE.cpp $(CFLAGS) $(FLAGS) $(INCLUDE) >> $(DEPEND_FILE); \
	done

$(BUILD)_clean:
	$(RM) -rf "$(OBJ_DIR)"
	$(RM) -f "$(BUILD_NAME)"

$(BUILD)_mrproper:
	@$(RM) -f $(DEPEND_FILE)
