.DEFAULT_GOAL := backrooms
UNAME_S := $(shell uname -s)
RAYLIB_PREFIX := $(shell brew --prefix raylib 2>/dev/null)

ifeq ($(RAYLIB_PREFIX),)
  CFLAGS_RL := $(shell pkg-config --cflags raylib 2>/dev/null)
  LIBS_RL   := $(shell pkg-config --libs raylib 2>/dev/null)
else
  CFLAGS_RL := -I$(RAYLIB_PREFIX)/include
  LIBS_RL   := -L$(RAYLIB_PREFIX)/lib -lraylib
endif

ifeq ($(UNAME_S),Darwin)
  LIBS_RL += -framework CoreGraphics
endif

ifeq ($(UNAME_S),Linux)
  LIBS_RL += -lm -ldl -lpthread
endif

SRCS := $(wildcard src/*.cpp)
HDRS := $(wildcard src/*.h)

src/object_materials.generated.h: tools/embed-materials.py $(wildcard assets/materials/*.jpg)
	python3 tools/embed-materials.py objects

src/models.generated.h: tools/embed-materials.py $(shell find assets/models -name "*.glb")
	python3 tools/embed-materials.py models

# The directories are listed too: deleting a clip changes its folder's mtime but
# no remaining .ogg, and the header would otherwise keep embedding the dead file.
src/sounds.generated.h: tools/embed-materials.py $(shell find assets/sounds -name "*.ogg" -o -type d)
	python3 tools/embed-materials.py sounds

GENERATED := src/object_materials.generated.h src/models.generated.h src/sounds.generated.h

backrooms: $(SRCS) $(HDRS) $(GENERATED)
	c++ -std=c++17 -O2 -Wall -Wno-missing-field-initializers $(CFLAGS_RL) $(SRCS) -o backrooms $(LIBS_RL)

run: backrooms
	./backrooms

clean:
	rm -f backrooms

.PHONY: run clean

# Uses the same native renderer as the game; run from shots/regression for captures.
regression: $(GENERATED) tools/regression.cpp $(filter-out src/main.cpp,$(SRCS)) $(HDRS)
	c++ -std=c++17 -O2 -Wall -Wno-missing-field-initializers $(CFLAGS_RL) -Isrc tools/regression.cpp $(filter-out src/main.cpp,$(SRCS)) -o /tmp/backrooms-regression $(LIBS_RL)
	mkdir -p shots/regression
	cd shots/regression && BACKROOMS_TEST_ASSET_DIR="$(CURDIR)/tests/fixtures" /tmp/backrooms-regression

.PHONY: regression

benchmark-animation: $(GENERATED) tools/bench-animation.cpp $(filter-out src/main.cpp,$(SRCS)) $(HDRS)
	c++ -std=c++17 -O2 $(CFLAGS_RL) -Isrc tools/bench-animation.cpp $(filter-out src/main.cpp,$(SRCS)) -o /tmp/backrooms-animation-after $(LIBS_RL)

.PHONY: benchmark-animation
