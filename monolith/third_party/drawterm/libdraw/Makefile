ROOT=..
include ../Make.config
LIB=libdraw.a

# 9front's libdraw, the one for drawterm and for the Plan 9 programs on
# its kernel (README.9front); not the libthread parts (no libthread here)
NOTHREAD=enter getrect keyboard menuhit mouse newwindow readcolmap
OFILES=$(patsubst %.c,%.$O,$(filter-out $(patsubst %,%.c,$(NOTHREAD)),$(wildcard *.c)))

default: $(LIB)
$(LIB): $(OFILES)
	$(AR) r $(LIB) $(OFILES)
	$(RANLIB) $(LIB)

%.$O: %.c
	$(CC) $(CFLAGS) $*.c
