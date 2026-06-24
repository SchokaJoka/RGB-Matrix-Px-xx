# This toplevel Makefile compiles the library in the lib subdirectory and then
# builds the DI Plaza Infoscreen programs in examples-api-use/.
RGB_LIBDIR=./lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

all : $(RGB_LIBRARY)

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)
	$(MAKE) -C examples-api-use

clean:
	$(MAKE) -C lib clean
	$(MAKE) -C examples-api-use clean

FORCE:
.PHONY: FORCE
