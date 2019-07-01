all: test run_test

SRC= $(wildcard *.cpp)

test: $(SRC)
	c++ -ggdb -std=c++1z -o test $(SRC)

run_test: test
	./test

clean:
	rm -f test
