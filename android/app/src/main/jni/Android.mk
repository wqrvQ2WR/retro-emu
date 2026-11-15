LOCAL_PATH := $(call my-dir)
HOST := $(LOCAL_PATH)/../../../../../host
include $(CLEAR_VARS)
LOCAL_MODULE := rhostjni
LOCAL_SRC_FILES := $(HOST)/rhost.c jni.c
LOCAL_C_INCLUDES := $(HOST) $(HOST)/include
LOCAL_LDLIBS := -llog -ldl -ljnigraphics -landroid
LOCAL_CFLAGS := -O3
include $(BUILD_SHARED_LIBRARY)
