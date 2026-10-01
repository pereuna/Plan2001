# A Plan 9 program as WebAssembly on drawterm's kernel (appmain.c):
#   make -f wasmapp/wasmapp.mk CONF=emscripten APP=clock
# in tools/build's copy of drawterm, after drawterm's own build: drawterm's
# objects but cpu.o and the program (9apps/$(APP).c) -> $(APP).js,
# $(APP).wasm; libdraw is drawterm's, 9front's (libdraw/README.9front)
include Makefile

APPOBJ=9apps/$(APP).$O wasmapp/appmain.$O wasmapp/p9libc.$O
APPCFLAGS=-include wasmapp/plan9.h -Wno-unused-variable -Wno-unused-function -Wno-format

9apps/$(APP).$O: 9apps/$(APP).c
	$(CC) $(APPCFLAGS) $(CFLAGS) -Dmain=wasmappmain -o $@ $<
wasmapp/p9libc.$O: wasmapp/p9libc.c
	$(CC) -O2 -pthread -c -o $@ $<
wasmapp/appmain.$O: wasmapp/appmain.c
	$(CC) $(CFLAGS) '-DWASMAPP="$(APP)"' -o $@ $<

$(APP).js: $(filter-out cpu.$O,$(OFILES)) $(APPOBJ) $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $(filter-out cpu.$O,$(OFILES)) $(APPOBJ) $(LIBS) $(LDADD)
