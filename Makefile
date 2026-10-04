FLAGS = -I src/headers -I src/commands -I src/crypto -MMD -MP

COMMAND_SOURCES = $(filter-out src/commands/c.cpp,$(wildcard src/commands/*.cpp))
HELPER_SOURCES = $(wildcard src/helpers/*.cpp)
SOURCES = src/main.cpp $(COMMAND_SOURCES) $(HELPER_SOURCES) $(wildcard src/crypto/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

mgit: $(SOURCES) $(wildcard src/headers/*.hpp) Makefile
	@g++ $(FLAGS) $(SOURCES) -o mgit -lcrypto
	@echo "Successfully created the executable \"mgit\""