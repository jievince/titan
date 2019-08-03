.PHONY : install uninstall clean

src = $(wildcard titan/*.cpp)
obj = $(src:%.cpp=%.o)
target = libtitan.a
CXX=g++
CXXFLAGS= -pthread -DOS_LINUX -DLITTLE_ENDIAN=1 -std=c++11 -g2

$(target): $(obj)
	rm -f $@
	ar -rs $@ $(obj)

$(obj): $(src)
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: libtitan.a
	mkdir -p /usr/local/include/titan
	cp -f titan/*.h /usr/local/include/titan
	cp -f libtitan.a /usr/local/lib

uninstall:
	rm -rf /usr/local/include/titan /usr/local/lib/libtitan.a

clean:
	-rm -f $(target)
	-rm -f $(obj)
