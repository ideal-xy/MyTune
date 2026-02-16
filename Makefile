CXX = clang++
FLAG = -std=c++17 -O3

TARGET = MyTune
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(FLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

run: all
	./$(TARGET)
	
clean:
	rm -f $(TARGET)
