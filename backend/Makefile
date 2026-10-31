# ============================================================================
#  Makefile — Build BidMatrix C++ Backend
# ============================================================================
#  Usage:
#    make          — build the server executable
#    make clean    — remove build artifacts
#    make run      — build and run
# ============================================================================

CXX      = g++
CXXFLAGS = -std=c++14 -O2 -Wall -Wextra -I../include
LDFLAGS  = -lws2_32
TARGET   = bidmatrix.exe
SRCDIR   = src
SRC      = $(SRCDIR)/main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)
	@echo.
	@echo Build successful! Run with: ./$(TARGET)

clean:
	del /f $(TARGET) 2>nul

run: $(TARGET)
	./$(TARGET)
