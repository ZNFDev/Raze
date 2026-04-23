CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter -Wno-unused-variable \
           -Wno-misleading-indentation
TARGET   = raze
SRC      = main.cpp
HEADERS  = $(wildcard include/*.hpp)

.PHONY: all clean test repl eval

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)
	@echo "✓ Built ./$(TARGET)"

test: $(TARGET)
	@echo "────────────────────────────────────────"
	./$(TARGET) tests/test.rz
	@echo "────────────────────────────────────────"

repl: $(TARGET)
	./$(TARGET)

eval: $(TARGET)
	./$(TARGET) -e 'println("Raze v2.0: " + str(6*7)); println("hex(255)=" + hex(255));'

clean:
	rm -f $(TARGET) *.o
