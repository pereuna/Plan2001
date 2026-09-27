ROOT=..
include ../Make.config
LIB=libgui.a

OFILES=\
	web.$O\
	wsock.$O\
	webjs.$O\

default: $(LIB)
$(LIB): $(OFILES)
	$(AR) r $(LIB) $(OFILES)
	$(RANLIB) $(LIB)

