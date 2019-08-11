.PHONY : install uninstall clean

CXX = g++
CXXFLAGS = -I. -pthread -DOS_LINUX -DLITTLE_ENDIAN=1 -std=c++11 -g2
LDFLAGS = -pthread -L/home/jackw/Documents/lambda/titan

src = $(wildcard titan/*.cpp)
obj = $(src:%.cpp=%.o)
examples_src = $(wildcard examples/*.cpp)
examples = $(examples_src:.cpp=)

library = libtitan.a
targets = $(library) $(examples) 

default: $(targets)

$(library): $(obj)
	rm -f $@
	ar -rs $@ $(obj)

install: libtitan.a
	mkdir -p /usr/local/include/titan
	cp -f titan/*.h /usr/local/include/titan
	cp -f libtitan.a /usr/local/lib

uninstall:
	rm -rf /usr/local/include/titan /usr/local/lib/libtitan.a

clean:
	-rm -f $(targets)
	-rm -f */*.o

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

.cpp:
	$(CXX) -o $@ $< $(CXXFLAGS) $(library)