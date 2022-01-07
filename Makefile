FLAGS=`/media/luali/324d947f-8b18-4216-850f-8fe4a7315517/pull/batocera.linux/output/rg552/host/bin/pkg-config --cflags --libs libdrm`
FLAGS+=-Wall -O0 -g -lrga
FLAGS+=-D_FILE_OFFSET_BITS=64

all:
	/media/luali/324d947f-8b18-4216-850f-8fe4a7315517/pull/batocera.linux/output/rg552/host/bin/aarch64-linux-gcc -o drmfbcopy drmfbcopy.c $(FLAGS)
