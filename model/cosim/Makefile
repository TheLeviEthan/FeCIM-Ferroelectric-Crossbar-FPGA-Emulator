# model/cosim/Makefile -- L0 co-simulation harnesses (Lane C)
#
#   make -C model/cosim                       build both harnesses
#   ./model/cosim/run_l0 [seed] [n]           single mac_lane; exit 0 = PASS
#   ./model/cosim/run_l0_array [seed] [n] [rows] [cols]
#                                             full mac_array vs ModelExact::step()
#   make -C model/cosim run                   build and run both with defaults
#   make -C model/cosim LANE_ID=63            single-lane harness on another lane
#
# Verilator's generated make refuses to build in a directory whose path
# contains a space (verilated.mk: "GNU Make cannot build in directories
# containing spaces"). If this checkout is under such a path -- e.g. a Windows
# folder like "C:\Users\<you>\Crossbars Local" mounted in WSL -- the sources are
# staged to $(STAGE) and built there, and the binaries are copied back here.
# CI and a clone under ~/ build in place.

VERILATOR ?= verilator
LANE_ID   ?= 5
VFLAGS    ?= -Wall
STAGE     ?= $(or $(TMPDIR),/tmp)/fecim-cosim-$(or $(USER),build)

RTL_LANE  := ../../rtl/fecim_pkg.sv ../../rtl/array/mac_lane.sv
RTL_ARRAY := ../../rtl/fecim_pkg.sv ../../rtl/array/mac_array.sv ../../rtl/array/mac_lane.sv \
             ../../rtl/array/read_noise.sv
MODEL_SRC := $(wildcard ../src/*.cpp)
MODEL_HDR := $(wildcard ../include/fecim/*.h)
COMMON    := Makefile $(MODEL_SRC) $(MODEL_HDR)

.PHONY: all run clean
all: run_l0 run_l0_array

ifeq ($(word 2,$(CURDIR)),)
# ---- normal case: build in place ------------------------------------------
# Relative paths throughout; Verilator's make runs inside the --Mdir, so -I is
# relative to it. Each harness has its own --Mdir so the two never collide.
run_l0: run_l0.cpp $(RTL_LANE) $(COMMON)
	$(VERILATOR) --cc --exe --build -j 0 $(VFLAGS) \
	    --top-module mac_lane -GLANE_ID=$(LANE_ID) \
	    -CFLAGS "-std=c++17 -O2 -I../../include -DCOSIM_LANE_ID=$(LANE_ID)" \
	    --Mdir obj_dir -o run_l0 \
	    $(RTL_LANE) run_l0.cpp $(MODEL_SRC)
	cp obj_dir/run_l0 run_l0

# --public-flat-rw: the harness reads every lane's w_n / prod / acc through
# Verilator's scope table, so the RTL needs no probe ports.
run_l0_array: run_l0_array.cpp $(RTL_ARRAY) $(COMMON)
	$(VERILATOR) --cc --exe --build -j 0 $(VFLAGS) --public-flat-rw \
	    --top-module mac_array \
	    -CFLAGS "-std=c++17 -O2 -I../../include" \
	    --Mdir obj_dir_array -o run_l0_array \
	    $(RTL_ARRAY) run_l0_array.cpp $(MODEL_SRC)
	cp obj_dir_array/run_l0_array run_l0_array
else
# ---- path contains a space: stage and build elsewhere -----------------------
# One rule for both harnesses: copy everything either needs, then re-run this
# Makefile in the staged copy (a path without a space) for the same target.
run_l0 run_l0_array: %: %.cpp $(RTL_ARRAY) $(COMMON)
	@echo "cosim: checkout path contains a space; building $@ in $(STAGE)"
	rm -rf "$(STAGE)/$@"
	mkdir -p "$(STAGE)/$@/rtl/array" "$(STAGE)/$@/model/cosim"
	cp ../../rtl/fecim_pkg.sv "$(STAGE)/$@/rtl/"
	cp ../../rtl/array/mac_array.sv ../../rtl/array/mac_lane.sv ../../rtl/array/read_noise.sv \
	    "$(STAGE)/$@/rtl/array/"
	cp -r ../src ../include "$(STAGE)/$@/model/"
	cp $@.cpp Makefile "$(STAGE)/$@/model/cosim/"
	$(MAKE) -C "$(STAGE)/$@/model/cosim" $@ VERILATOR="$(VERILATOR)" LANE_ID=$(LANE_ID) VFLAGS="$(VFLAGS)"
	cp "$(STAGE)/$@/model/cosim/$@" $@
endif

run: run_l0 run_l0_array
	./run_l0
	./run_l0_array

clean:
	rm -rf obj_dir obj_dir_array run_l0 run_l0_array
