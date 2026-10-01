# A Plan 9 program as WebAssembly on drawterm's kernel (appmain.c):
#   make -f wasmapp/wasmapp.mk CONF=emscripten APP=clock
# in tools/build's copy of drawterm, after drawterm's own build: drawterm's
# objects but cpu.o, 9front's libdraw (9libdraw/) and the program
# (9apps/$(APP).c) -> $(APP).js, $(APP).wasm; 9apps/ before drawterm's
# include/: 9front's draw.h and event.h for them
include Makefile

# not the libthread ones (enter getrect keyboard menuhit mouse) nor rio's newwindow, nor readcolmap (libbio)
LIBDRAW9=$(patsubst %.c,%.$O,$(filter-out $(patsubst %,9libdraw/%.c,enter getrect keyboard menuhit mouse newwindow readcolmap),$(wildcard 9libdraw/*.c)))
APPOBJ=9apps/$(APP).$O wasmapp/appmain.$O wasmapp/p9libc.$O
APPCFLAGS=-include wasmapp/plan9.h -I9apps -Wno-unused-variable -Wno-unused-function -Wno-format

9libdraw/%.$O: 9libdraw/%.c
	$(CC) $(APPCFLAGS) $(CFLAGS) -o $@ $<
9apps/$(APP).$O: 9apps/$(APP).c
	$(CC) $(APPCFLAGS) $(CFLAGS) -Dmain=wasmappmain -o $@ $<
wasmapp/p9libc.$O: wasmapp/p9libc.c
	$(CC) -O2 -pthread -c -o $@ $<
wasmapp/appmain.$O: wasmapp/appmain.c
	$(CC) $(CFLAGS) '-DWASMAPP="$(APP)"' -o $@ $<

$(APP).js: $(filter-out cpu.$O,$(OFILES)) $(APPOBJ) $(LIBDRAW9) $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $(filter-out cpu.$O,$(OFILES)) $(APPOBJ) $(LIBDRAW9) $(LIBS) $(LDADD)
