LOCAL_LDFLAGS=-L/opt/X11/lib -lX11 -lxcb -lxcb-image
LOCAL_CFLAGS=-I/opt/X11/include
LOCAL_LIBADD=
LOCAL_OBJS=

BASE=../../../..
include $(BASE)/build/modmake.rules
