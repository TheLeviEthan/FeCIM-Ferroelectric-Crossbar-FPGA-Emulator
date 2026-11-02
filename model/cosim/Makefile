# model/cosim/Makefile -- L0 co-simulation harness (Lane C)
#
#   make -C model/cosim              build ./run_l0 (Verilated mac_lane + C++ model)
#   ./model/cosim/run_l0 [seed] [n]  run; exit 0 = PASS
#   make -C model/cosim run          build and run with defaults
#   make -C model/cosim LANE_ID=63   check a different lane
#
# Verilator's generated make refuses to build in a directory whose path
# contains a space (verilated.mk: "GNU Make cannot build in directories
# containing spaces"). If this checkout is under such a path -- e.g. a Windows
# folder like "C:\Users\<you>\Crossbars Local" mounted in WSL -- the sources are
# staged to $(STAGE) and built there, and run_l0 is copied back here. CI and a
# clone under ~/ build in place.

VERILATOR ?= verilator
LANE_ID   ?= 5
VFLAGS    ?= -Wall
STAGE     ?= $(or $(TMPDIR),/tmp)/fecim-cosim-$(or $(USER),build)

RTL_SRC   := ../../rtl/fecim_pkg.sv ../../rtl/array/mac_lane.sv
MODEL_SRC := $(wildcard ../src/*.cpp)
MODEL_HDR := $(wildcard ../include/fecim/*.h)
DEPS      := run_l0.cpp Makefile $(RTL_SRC) $(MODEL_SRC) $(MODEL_HDR)

.PHONY: all run clean
all: run_l0

ifeq ($(word 2,$(CURDIR)),)
# ---- normal case: build in place ------------------------------------------
# Relative paths throughout; Verilator's make runs inside obj_dir, so -I is
# relative to obj_dir.
run_l0: $(DEPS)
	$(VERILATOR) --cc --exe --build -j 0 $(VFLAGS) \
	    --top-module mac_lane -GLANE_ID=$(LANE_ID) \
	    -CFLAGS "-std=c++17 -O2 -I../../include -DCOSIM_LANE_ID=$(LANE_ID)" \
	    --Mdir obj_dir -o run_l0 \
	    $(RTL_SRC) run_l0.cpp $(MODEL_SRC)
	cp obj_dir/run_l0 run_l0
else
# ---- path contains a space: stage and build elsewhere -----------------------
run_l0: $(DEPS)
	@echo "cosim: checkout path contains a space; building in $(STAGE)"
	rm -rf "$(STAGE)"
	mkdir -p "$(STAGE)/rtl/array" "$(STAGE)/model/cosim"
	cp ../../rtl/fecim_pkg.sv "$(STAGE)/rtl/"
	cp ../../rtl/array/mac_lane.sv "$(STAGE)/rtl/array/"
	cp -r ../src ../include "$(STAGE)/model/"
	cp run_l0.cpp Makefile "$(STAGE)/model/cosim/"
	$(MAKE) -C "$(STAGE)/model/cosim" run_l0 VERILATOR="$(VERILATOR)" LANE_ID=$(LANE_ID) VFLAGS="$(VFLAGS)"
	cp "$(STAGE)/model/cosim/run_l0" run_l0
endif

run: run_l0
	./run_l0

clean:
	rm -rf obj_dir run_l0
