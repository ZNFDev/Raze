CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter -Wno-unused-variable \
           -Wno-misleading-indentation -Wno-switch
TARGET   = raze
SRC      = main.cpp
HEADERS  = $(wildcard include/*.hpp)

.PHONY: all clean test test-import test-all repl

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) && echo "Built ./$(TARGET)"

test: $(TARGET)
	@echo "================================================"
	./$(TARGET) tests/test.rz
	@echo "================================================"

test-import: $(TARGET)
	./$(TARGET) tests/import_test.rz

test-all: $(TARGET) test test-import
	@echo "All test suites passed!"

repl: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

eval: $(TARGET)
	./$(TARGET) -e 'println("Raze v3.0 " + RAZE_VERSION); println(hex(255));'
