FLAGS = -I src/headers -I src/commands -MMD -MP

SOURCES = src/main.cpp $(wildcard src/commands/*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

mgit: $(SOURCES)
	@g++ $(FLAGS) $(SOURCES) -o mgit
	@echo "Successfully created the executable \"mgit\""