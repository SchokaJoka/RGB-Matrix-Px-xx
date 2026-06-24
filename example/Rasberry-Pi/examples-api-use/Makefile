include ../config.mk

CFLAGS=-Wall -O3 $(CPU_ARCH_FLAGS) $(LTO_FLAGS) -g -Wextra -Wno-unused-parameter
CXXFLAGS=$(CFLAGS)
OBJECTS=demo-main.o weather-demo.o train-demo.o plaza-info.o
BINARIES=demo plaza-info

# Where our library resides. You mostly only need to change the
# RGB_LIB_DISTRIBUTION, this is where the library is checked out.
RGB_LIB_DISTRIBUTION=..
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
LDFLAGS+=$(CPU_ARCH_FLAGS) $(LTO_FLAGS) -L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

all : $(BINARIES)

$(RGB_LIBRARY): FORCE
	$(MAKE) -C $(RGB_LIBDIR)

# The weather (-D 13) and train (-D 14) displays.
demo : demo-main.o weather-demo.o train-demo.o $(RGB_LIBRARY)
	$(CXX) demo-main.o weather-demo.o train-demo.o -o $@ $(LDFLAGS)

# The combined autonomous infoscreen.
plaza-info : plaza-info.o $(RGB_LIBRARY)
	$(CXX) plaza-info.o -o $@ $(LDFLAGS)

%.o : %.cc
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(BINARIES)

FORCE:
.PHONY: FORCE
