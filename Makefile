CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -flto -pthread -Wall -Wextra -Wno-unused-parameter
LDFLAGS  := -flto -pthread
TARGET   := neko-chess
SRCDIR   := src
SOURCES  := $(SRCDIR)/main.cpp $(SRCDIR)/chess.cpp
OBJECTS  := $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(SRCDIR)/chess.o: $(SRCDIR)/chess.hpp
$(SRCDIR)/main.o:  $(SRCDIR)/chess.hpp

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
