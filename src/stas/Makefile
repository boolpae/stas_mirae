PRJ_HOME	:= $(shell echo $(PROJECT_ROOT) | sed 's/\ /\\ /g')
KERNEL_VERSION	:= $(shell uname -r | awk -F. '{print $$1}')

-include $(PRJ_HOME)/Makefile
PWD	:= $(shell pwd | sed 's/\ /\\ /g')
ifeq ($(BUILD), )
BUILD	:= $(PWD:$(shell dirname $(PWD))/%=%)
endif

###############################################################################
SOURCE			:= CallExecutor.cpp \
				Scheduler.cpp \
				VFClient.cpp \
				stas.cpp \
				CallReceiver.cpp \
				Notifier.cpp \
				VDCManager.cpp \
				VRCManager.cpp \
				ItfOdbcPool.cpp \
				DBHandler.cpp \
				VDClient.cpp \
				VRClient.cpp \
				VRClientMT.cpp \
				HAManager.cpp \
				FileHandler.cpp \
				VFCManager.cpp \
				WorkTracer.cpp \
				VASDivSpeaker.cpp \
				DivSpkManager.cpp \
				RedisHandler.cpp \
				Utils.cpp
#SOURCE			+= v1/restapi_v1.cc v1/servers.cc v1/waves.cc
#INCLUDE_PATH	:= $(PRJ_HOME)/include/dnn
INCLUDE_PATH	:= /usr/local/include
ifeq ($(ENABLE_REALTIME), "TRUE")
ifeq ($(USE_REDIS), "TRUE")
INCLUDE_PATH	+= /usr/local/include/xredis
endif
endif
#INCLUDE_PATH	+= /usr/local/include/zdb
LIBRARIES		:= ${DIST}/itf_common
#LIBRARIES		+= dnn/libsplproc dnn/libfrontend dnn/libmsearch dnn/libasearch
#LIBRARIES		+= dnn/libbase dnn/liblsearch dnn/liblaserdnn2 dnn/libdnnapi dnn/libdnn.gpu
FLAGS			:= -DFAD_FUNC
ifeq ($(USE_TIBERO), "TRUE")
FLAGS			+= -DUSE_TIBERO
endif

ifeq ($(ENABLE_REALTIME), "TRUE")
FLAGS			+= -DENABLE_REALTIME # 실시간 기능 활성화 Flag
ifeq ($(USE_REDIS), "TRUE")
FLAGS			+= -DUSE_REDIS_POOL # 실시간 기능에서 필요로 하며 redis로 실시간 결과를 전송
endif
ifeq ($(USE_REALTIME_MF), "TRUE")
FLAGS			+= -DUSE_REALTIME_MF # 실시간 Gearman Multi Function Name 사용
endif
ifeq ($(USE_REALTIME_MT), "TRUE")
FLAGS			+= -DUSE_REALTIME_MT # 실시간 tasK에 대해 채널 당 화자 별 전용 thread를 이용
FLAGS			+= -DCHANNEL_SYNC # 두 화자 간의 음성 데이터 처리 속도를 맞추기 위한 정의
#FLAGS			+= -DDIAL_SYNC # 두 화자 간의 대화 내용을 맞추기 위한 정의(CHANNEL_SYNC가 이미 정의되어야 한다.)
endif
ifeq ($(EN_RINGBACK_LEN), "TRUE")
FLAGS			+= -DEN_RINGBACK_LEN # 통화연결음 길이를 이용할 경우 - 추후 Define을 이요하지 않고 기본으로 빌드할 예정
endif
ifeq ($(EN_SAVE_PCM), "TRUE")
FLAGS			+= -DEN_SAVE_PCM # 실제 VR로 보내는 PCM데이터 확인용 코드, 테스트를 위해서만 사용해야 함
endif
ifeq ($(FOR_TEST), "TRUE")
FLAGS			+= -DFOR_TEST # 상담원, 고객 간의 STT결과를 하나의 파일에 저장하기 위한 테스트 코드(실시간 전용)
endif
ifeq ($(USE_FIND_KEYWORD), "TRUE")
FLAGS			+= -DUSE_FIND_KEYWORD
endif
ifeq ($(FOR_ITFACT), "TRUE")
FLAGS			+= -DFOR_ITFACT
endif
ifeq ($(USE_DIFF_CSCODE), "TRUE")
FLAGS			+= -DUSE_DIFF_CSCODE
endif
endif

ifeq ($(USE_RETRY_TABLE), "TRUE")
FLAGS			+= -DUSE_RETRY_TABLE
endif

ifeq ($(USE_CS_TABLE), "TRUE")
FLAGS			+= -DUSE_CS_TABLE
endif

ifeq ($(USE_UNSEG_WITH_TIME), "TRUE")
FLAGS			+= -DUSE_UNSEG_WITH_TIME
endif

FLAGS			+= -DUSE_UPDATE_POOL # Update Task 시 전용 thread를 이용하는 방법을 이용 

SHARED_LIBS		:= -lboost_program_options -lboost_filesystem -lboost_system
SHARED_LIBS		+= -llog4cpp -lgearman -lodbc -lcrypto

ifeq ($(ENABLE_REALTIME), "TRUE")
SHARED_LIBS		+= -lfvad
ifeq ($(USE_REDIS), "TRUE")
SHARED_LIBS		+= -lxredis -lhiredis
endif
endif

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
