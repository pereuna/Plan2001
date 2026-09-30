ROOT=..
include ../Make.config
LIB=libgui.a

OFILES=\
	none.$O\
	tty.$O\

default: $(LIB)
$(LIB): $(OFILES)
	$(AR) r $(LIB) $(OFILES)
	$(RANLIB) $(LIB)
