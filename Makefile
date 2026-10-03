FLAGS = -I src/headers -I src/commands -I src/crypto -MMD -MP

COMMAND_SOURCES = $(filter-out src/commands/c.cpp,$(wildcard src/commands/*.cpp))
SOURCES = src/main.cpp $(COMMAND_SOURCES) $(wildcard src/crypto/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

mgit: $(SOURCES)
	@g++ $(FLAGS) $(SOURCES) -o mgit -lcrypto
	@echo "Successfully created the executable \"mgit\""