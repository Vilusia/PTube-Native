TARGET = PTube
OBJS = main.o net.o player.o
CFLAGS = -O2 -G0 -Wall -Wextra
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
# Resolve libc/SDK wrappers before importing firmware stubs; otherwise socket
# wrappers split the sceNetInet import table and fixup-imports cannot sort it.
LIBS = -Wl,--start-group -ljpeg -lpspdebug -lpspsdk -lc -lcglue -Wl,--end-group -lpspaudio -lpsppower -lpsputility -lpspnet_inet -lpspnet_resolver
BUILD_PRX = 1
PSP_FW_VERSION = 660
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PTube Alpha
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
