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

# web/src is the raylib platform (the Web build); shared/ holds core, sim and
# port, the engine-independent layers both builds compile (docs/migration.md).
# No fused multiply-adds: core's results must not depend on the compiler
# (shared/core/fp_strict.h). Every command here compiles core with the rest.
CXX_FLAGS := -std=c++17 -O2 -Wall -Wno-missing-field-initializers -ffp-contract=off -Ishared
SRCS := $(wildcard web/src/*.cpp shared/core/*.cpp shared/sim/*.cpp shared/port/*.cpp)
HDRS := $(wildcard web/src/*.h shared/core/*.h shared/sim/*.h shared/port/*.h)

web/src/object_materials.generated.h: tools/embed-materials.py $(wildcard assets/materials/*.jpg)
	python3 tools/embed-materials.py objects

web/src/models.generated.h: tools/embed-materials.py $(shell find assets/models -name "*.glb")
	python3 tools/embed-materials.py models

# The directories are listed too: deleting a clip changes its folder's mtime but
# no remaining .ogg, and the header would otherwise keep embedding the dead file.
web/src/sounds.generated.h: tools/embed-materials.py $(shell find assets/sounds -name "*.ogg" -o -type d)
	python3 tools/embed-materials.py sounds

GENERATED := web/src/object_materials.generated.h web/src/models.generated.h web/src/sounds.generated.h

backrooms: $(SRCS) $(HDRS) $(GENERATED)
	c++ $(CXX_FLAGS) $(CFLAGS_RL) $(SRCS) -o backrooms $(LIBS_RL)

run: backrooms
	./backrooms

clean:
	rm -f backrooms

.PHONY: run clean

# Uses the same native renderer as the game; run from shots/regression for captures.
regression: $(GENERATED) tools/regression.cpp $(filter-out web/src/main.cpp,$(SRCS)) $(HDRS)
	c++ $(CXX_FLAGS) $(CFLAGS_RL) -Iweb/src tools/regression.cpp $(filter-out web/src/main.cpp,$(SRCS)) -o /tmp/backrooms-regression $(LIBS_RL)
	mkdir -p shots/regression
	cd shots/regression && BACKROOMS_TEST_ASSET_DIR="$(CURDIR)/tests/fixtures" /tmp/backrooms-regression

.PHONY: regression

benchmark-animation: $(GENERATED) tools/bench-animation.cpp $(filter-out web/src/main.cpp,$(SRCS)) $(HDRS)
	c++ -std=c++17 -O2 -ffp-contract=off -Ishared $(CFLAGS_RL) -Iweb/src tools/bench-animation.cpp $(filter-out web/src/main.cpp,$(SRCS)) -o /tmp/backrooms-animation-after $(LIBS_RL)

.PHONY: benchmark-animation

# Core's golden answers, linked against shared/core alone (docs/migration.md,
# "Contract tests").
CORE_SRCS := $(wildcard shared/core/*.cpp)
contract: tools/contract.cpp tools/contract_lib.cpp tools/contract_lib.h $(CORE_SRCS) $(wildcard shared/core/*.h)
	c++ $(CXX_FLAGS) tools/contract.cpp tools/contract_lib.cpp $(CORE_SRCS) -o contract

contract-check: contract
	./contract --check

.PHONY: contract-check

# A recorded trace (BACKROOMS_RECORD) replayed through the sim alone.
SIM_SRCS := $(wildcard shared/sim/*.cpp)
replay: tools/replay.cpp $(SIM_SRCS) $(CORE_SRCS) $(wildcard shared/sim/*.h shared/core/*.h)
	c++ $(CXX_FLAGS) tools/replay.cpp $(SIM_SRCS) $(CORE_SRCS) -o replay

replay-check: replay
	for t in tests/traces/*.trace; do ./replay "$$t" || exit 1; done

.PHONY: replay-check

# Every texture generator without a window (tools/texdump.cpp); `--unreal DIR`
# writes what the Unreal editor imports.
TEXDUMP_SRCS := tools/texdump.cpp web/src/textures.cpp web/src/surfaces.cpp web/src/levels.cpp web/src/util.cpp shared/port/atlas.cpp $(CORE_SRCS)
texdump: $(TEXDUMP_SRCS) $(HDRS) $(GENERATED)
	c++ $(CXX_FLAGS) $(CFLAGS_RL) $(TEXDUMP_SRCS) -o texdump $(LIBS_RL)

# The Unreal project (unreal/), on a Mac with Unreal Engine 5.8: tools/unreal.sh.
# Build after every pull; opening the .uproject does not recompile.
unreal:
	tools/unreal.sh build
unreal-open:
	tools/unreal.sh open
unreal-play:
	tools/unreal.sh play
unreal-test:
	tools/unreal.sh test
unreal-surfaces:
	tools/unreal.sh surfaces
.PHONY: unreal unreal-open unreal-play unreal-test unreal-surfaces
