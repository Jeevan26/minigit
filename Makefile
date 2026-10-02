FLAGS = -I src/headers -I src/commands -I src/crypto -MMD -MP

SOURCES = src/main.cpp $(wildcard src/commands/*.cpp) $(wildcard src/crypto/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

mgit: $(SOURCES)
	@g++ $(FLAGS) $(SOURCES) -o mgit -lcrypto
	@echo "Successfully created the executable \"mgit\""