CXXFLAGS=-O2
LIBS=-lmt32emu -lm -lasound -lpthread
XLIBS=-L/usr/X11R6/lib -lX11 -lXt -lXpm

INCLUDES=
OBJS=wav.o alsadrv.o
XOBJ=keypad.o lcd.o pixmaps.o

default: mt32d xmt32

$(XOBJ) $(OBJS): %.o: src/%.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(INCLUDES)

mt32d: $(OBJS) src/console.cpp
	$(CXX) src/console.cpp -o mt32d $(CXXFLAGS) $(INCLUDES) $(OBJS) $(LIBS)

xmt32: $(OBJS) $(XOBJ) src/xmt32.cpp
	$(CXX) src/xmt32.cpp -o xmt32 $(CXXFLAGS) $(INCLUDES) $(OBJS) $(XOBJ) $(LIBS) $(XLIBS)

install:
	install mt32d /usr/local/bin
	install xmt32 /usr/local/bin
	install -d roms /usr/share/mt32-rom-data
	cd roms; install * /usr/share/mt32-rom-data
	cd /usr/share/mt32-rom-data; chmod 644 *

clean:
	rm -f *.o *~

realclean:
	rm mt32d xmt32 -f
