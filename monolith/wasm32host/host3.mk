# Plan2001 wasm32 processes on drawterm's kernel (host3.c):
#   make -f wasm32host/host3.mk CONF=emscripten host3.js
# in tools/build's copy of drawterm, after drawterm's own build: drawterm's
# objects but cpu.o, and host3.o -> host3.js, host3.wasm
include Makefile

H3=wasm32host/host3.$O wasm32host/host3js.$O

wasm32host/host3.$O: wasm32host/host3.c
	$(CC) $(CFLAGS) -o $@ $<
wasm32host/host3js.$O: wasm32host/host3js.c
	$(CC) -O2 -pthread -c -o $@ $<

host3.js: $(filter-out cpu.$O,$(OFILES)) $(H3) $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $(filter-out cpu.$O,$(OFILES)) $(H3) $(LIBS) $(LDADD) -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_h3sys1,_h3start,_h3finish,_h3waitdone,_h3newco,_h3coend,_h3mkhelper -sEXPORTED_RUNTIME_METHODS=ENV,UTF8ToString,FS,addRunDependency,removeRunDependency -sFORCE_FILESYSTEM
