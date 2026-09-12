UNAME_S := $(shell uname -s)
RAYLIB_PREFIX := $(shell brew --prefix raylib 2>/dev/null)

ifeq ($(RAYLIB_PREFIX),)
  CFLAGS_RL := $(shell pkg-config --cflags raylib 2>/dev/null)
  LIBS_RL   := $(shell pkg-config --libs raylib 2>/dev/null)
else
  CFLAGS_RL := -I$(RAYLIB_PREFIX)/include
  LIBS_RL   := -L$(RAYLIB_PREFIX)/lib -lraylib
endif

ifeq ($(UNAME_S),Linux)
  LIBS_RL += -lm -ldl -lpthread
endif

SRCS := $(wildcard src/*.cpp)
HDRS := $(wildcard src/*.h)

backrooms: $(SRCS) $(HDRS)
	c++ -std=c++17 -O2 -Wall -Wno-missing-field-initializers $(CFLAGS_RL) $(SRCS) -o backrooms $(LIBS_RL)

run: backrooms
	./backrooms

clean:
	rm -f backrooms

.PHONY: run clean

# Uses the same native renderer as the game; run from shots/regression for captures.
regression: tools/regression.cpp $(filter-out src/main.cpp,$(SRCS)) $(HDRS)
	c++ -std=c++17 -O2 -Wall -Wno-missing-field-initializers $(CFLAGS_RL) -Isrc tools/regression.cpp $(filter-out src/main.cpp,$(SRCS)) -o /tmp/backrooms-regression $(LIBS_RL)
	mkdir -p shots/regression
	cd shots/regression && /tmp/backrooms-regression

.PHONY: regression
