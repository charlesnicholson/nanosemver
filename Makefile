# nanosemver: `make` builds and runs every test. Linux and macOS; Windows uses build.bat.
#
# Settings, all optional:
#   CC, CXX         compilers (cc / c++)
#   CFG             Release (-Os, the default) or Debug (-O0 -g3)
#   ARCH            64 (default) or 32 (-m32; Linux multilib)
#   SAN             a -fsanitize= list, e.g. address,undefined or memory
#   DOCTEST_H       path to doctest.h; set it and envy is never invoked
#   FUZZ_ARGS       standalone fuzz driver arguments for `make fuzz` (-t 30)
#   LIBFUZZER_ARGS  libFuzzer arguments for `make libfuzzer` (-max_total_time=60)
#   BENCH_BASE      the git revision `make bench` and `make bench-cm` measure against
#                   (origin/main)
#   BENCH_OPT       optimization for `make bench` (-O2)
#   BENCH_ARGS      benchmark arguments for `make bench`, e.g. --quick or --filter cmp
#   ARM_CC          arm-none-eabi-gcc for the Cortex-M targets; envy's by default
#   NODE, NODE_SEMVER  Node.js and npm's semver package directory for `make npm-check`;
#                   envy's by default
#   NPM_ARGS        generated ranges for `make npm-check`, and a seed (200000 1)
#   MUTATE_ARGS     tests/mutate.py arguments for `make mutate`, e.g. --jobs 4
#   V=1             echo full commands
#
# Targets:
#   test        (default) the unit tests, built three times: as configured; as for a
#               small MCU like an AVR, whose 16-bit int needs the length checks that
#               -DNSV_INT_MAX=32767 compiles in, division by shifts, and nsv_cmp's
#               32-bit compare; and with -DNSV_DIAG=1, for its messages. And
#               nanosemver.hpp compiled on its own as C++11 to C++23, with and without
#               exceptions and RTTI, and without the C++ standard headers
#   fuzz        the standalone fuzz driver, with -DNSV_DIAG=1; any compiler
#   libfuzzer   clang libFuzzer with ASan and UBSan, with -DNSV_DIAG=1; Apple clang
#               ships no libFuzzer
#   coverage    clang source coverage of nanosemver.c; fails unless 100%
#   bench       this tree against BENCH_BASE, both in one binary, timed interleaved
#   bench-cm    instructions and estimated cycles per call on emulated Cortex-M0+ and
#               Cortex-M4, against BENCH_BASE
#   size        Cortex-M0+ and Cortex-M4 code size at -Os, function by function, and
#               in all with -DNSV_DIAG=1
#   size-check  fails if the code, with or without NSV_DIAG, outgrows size-budget.mk or
#               calls into libc or libgcc for anything but memcpy
#   ratchet     size-check, and fails if any Cortex-M workload takes more cycles than
#               bench/cm/ratchet.txt allows
#   ratchet-update  lowers size-budget.mk and bench/cm/ratchet.txt to what the code now
#               takes; never raises them
#   npm-check   nsv_range_match against npm's own semver package, under Node.js
#   mutate      mutation testing: fails if the unit tests miss any change to
#               nanosemver.c that tests/mutate-equivalent.txt does not explain
#   clean       everything under build/ except the envy and uv package caches

CFG  ?= Release
ARCH ?= 64
SAN  ?=

comma := ,
space := $(subst ,, )
COMPILER_ID := $(shell $(CC) --version 2>&1)
IS_CLANG := $(findstring clang,$(COMPILER_ID))

ifeq ($(V),1)
  Q :=
  MSG := @true
else
  Q := @
  MSG := @printf '  %-5s %s\n'
endif

# One build directory per configuration, so switching never mixes objects.
CONFIG := $(notdir $(firstword $(CC)))-$(CFG)-$(ARCH)$(if $(SAN),-$(subst $(comma),+,$(SAN)))
BUILD ?= build/$(CONFIG)

# --- doctest, through envy --------------------------------------------------------
# Only the test binaries need it.
NO_DOCTEST_GOALS := clean bench bench-cm size size-check ratchet ratchet-update npm-check
ifneq ($(if $(MAKECMDGOALS),$(filter-out $(NO_DOCTEST_GOALS),$(MAKECMDGOALS)),test),)
  ifeq ($(origin DOCTEST_H),undefined)
    DOCTEST_H := $(shell ./bin/envy -q product doctest_cpp_h)
    ifeq ($(DOCTEST_H),)
      $(error envy could not resolve doctest_cpp_h)
    endif
  else ifeq ($(wildcard $(DOCTEST_H)),)
    $(error DOCTEST_H=$(DOCTEST_H) does not exist)
  endif
endif
# -isystem, not -I: doctest.h is not ours to hold to -Weverything -Werror.
DOCTEST_INC := -isystem $(dir $(DOCTEST_H))

# --- flags ------------------------------------------------------------------------
ifeq ($(CFG),Release)
  OPT := -Os -g
else ifeq ($(CFG),Debug)
  OPT := -O0 -g3
else
  $(error CFG must be Release or Debug, not '$(CFG)')
endif

ARCH_FLAGS := $(if $(filter 32,$(ARCH)),-m32)
SAN_FLAGS := $(if $(SAN),-fsanitize=$(SAN) -fno-sanitize-recover=all -fno-omit-frame-pointer)
ifneq ($(IS_CLANG),)
  SAN_FLAGS += $(if $(SAN),-fsanitize-ignorelist=sanitize-ignorelist.txt)
endif

WARN := -Wall -Wextra -Werror -pedantic
ifneq ($(IS_CLANG),)
  WARN += -Weverything \
          -Wno-unknown-warning-option \
          -Wno-padded \
          -Wno-unsafe-buffer-usage \
          -Wno-poison-system-directories \
          -Wno-switch-default \
          -Wno-declaration-after-statement
  WARN_CXX := -Wno-c++98-compat \
              -Wno-c++98-compat-pedantic \
              -Wno-c2y-extensions \
              -Wno-exit-time-destructors \
              -Wno-global-constructors \
              -Wno-unsafe-buffer-usage-in-libc-call
else
  WARN += -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wcast-align -Wundef \
          -Wswitch-enum -Wdouble-promotion -Wformat=2 -Wmissing-declarations
  WARN_C := -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
endif

CPPFLAGS := -MMD -MP -I.
CFLAGS   := -std=c99 $(WARN) $(WARN_C) $(OPT) $(ARCH_FLAGS) $(SAN_FLAGS) $(EXTRA_FLAGS)
CXXFLAGS := -std=c++20 $(WARN) $(WARN_CXX) $(OPT) $(ARCH_FLAGS) $(SAN_FLAGS) $(EXTRA_FLAGS) \
            $(DOCTEST_INC)
LDFLAGS  := $(ARCH_FLAGS) $(SAN_FLAGS) $(EXTRA_FLAGS)
INT16_DEFS := -DNSV_INT_MAX=32767 -DNSV_SOFT_DIV=1 -DNSV_WIDE_CMP=0
DIAG_DEFS := -DNSV_DIAG=1

# --- sources ----------------------------------------------------------------------
# Globbed, so a new test file needs no registration here; build.bat globs the same.
FUZZ_LIB_SRCS := tests/fuzz/check.cc tests/fuzz/gen.cc
UNIT_SRCS := nanosemver.c $(sort $(wildcard tests/*.c) $(wildcard tests/*.cc)) $(FUZZ_LIB_SRCS)
INT16_SRCS := $(UNIT_SRCS) tests/int16/test_int16.cc
DIAG_SRCS := $(UNIT_SRCS) tests/diag/test_diag.cc
FUZZ_SRCS := nanosemver.c tests/fuzz/driver.cc tests/ref_semver.cc $(FUZZ_LIB_SRCS)

UNIT_OBJS := $(patsubst %,$(BUILD)/%.o,$(UNIT_SRCS))
INT16_OBJS := $(patsubst %,$(BUILD)/int16/%.o,$(INT16_SRCS))
DIAG_OBJS := $(patsubst %,$(BUILD)/diag/%.o,$(DIAG_SRCS))
# The fuzz driver checks the messages too, so it builds as the diag tests do.
FUZZ_OBJS := $(patsubst %,$(BUILD)/diag/%.o,$(FUZZ_SRCS))
# nanosemver.c also has to build as C11, and nanosemver.hpp on its own under every C++
# standard from C++11, with and without exceptions and RTTI, though nothing links those
# objects. Outside $(BUILD)/tests, where tests/mutate.py would link them.
C11_OBJ := $(BUILD)/c11/nanosemver.c.o
HPP_SRC := tests/hpp/standalone.cc
HPP_STDS := c++11 c++14 c++17 c++20 c++23
HPP_OBJS := $(patsubst %,$(BUILD)/hpp/%.o,$(HPP_STDS))
HPP_NOEXC_OBJS := $(patsubst %,$(BUILD)/hpp/%-noexcept.o,$(HPP_STDS))
ALL_OBJS := $(sort $(UNIT_OBJS) $(INT16_OBJS) $(DIAG_OBJS) $(FUZZ_OBJS) $(C11_OBJ) $(HPP_OBJS) \
                   $(HPP_NOEXC_OBJS))

# --- rules ------------------------------------------------------------------------
.PHONY: test fuzz libfuzzer coverage bench bench-cm size size-check ratchet ratchet-update \
        npm-check mutate clean FORCE
.DEFAULT_GOAL := test

test: $(BUILD)/nsv_unittests.ok $(BUILD)/nsv_unittests_int16.ok $(BUILD)/nsv_unittests_diag.ok \
      $(C11_OBJ) $(HPP_OBJS) $(HPP_NOEXC_OBJS)

$(BUILD)/int16/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (int16)"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) $(INT16_DEFS) -c $< -o $@

$(BUILD)/int16/%.cc.o: %.cc Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX "$< (int16)"
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INT16_DEFS) -c $< -o $@

$(BUILD)/diag/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (diag)"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) $(DIAG_DEFS) -c $< -o $@

$(BUILD)/diag/%.cc.o: %.cc Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX "$< (diag)"
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DIAG_DEFS) -c $< -o $@

$(BUILD)/c11/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (c11)"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -c $< -o $@

# No sanitizers: nothing here runs, and UBSan's vptr check needs RTTI. -nostdinc++: the
# header must build without C++ standard headers, as avr-g++ has none.
HPP_FLAGS := $(WARN) $(WARN_CXX) $(OPT) $(ARCH_FLAGS) $(EXTRA_FLAGS) -nostdinc++

$(HPP_OBJS): $(BUILD)/hpp/%.o: $(HPP_SRC) Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX "$< ($*)"
	$(Q)$(CXX) $(CPPFLAGS) -std=$* $(HPP_FLAGS) -c $< -o $@

$(HPP_NOEXC_OBJS): $(BUILD)/hpp/%-noexcept.o: $(HPP_SRC) Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX "$< ($*, no exceptions or RTTI)"
	$(Q)$(CXX) $(CPPFLAGS) -std=$* $(HPP_FLAGS) -fno-exceptions -fno-rtti -c $< -o $@

$(BUILD)/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC $<
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.cc.o: %.cc Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX $<
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/nsv_unittests: $(UNIT_OBJS)
	$(MSG) LINK $@
	$(Q)$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD)/nsv_unittests_int16: $(INT16_OBJS)
	$(MSG) LINK $@
	$(Q)$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD)/nsv_unittests_diag: $(DIAG_OBJS)
	$(MSG) LINK $@
	$(Q)$(CXX) $(LDFLAGS) $^ -o $@

$(BUILD)/nsv_fuzz: $(FUZZ_OBJS)
	$(MSG) LINK $@
	$(Q)$(CXX) $(LDFLAGS) $^ -o $@

# -m: doctest's minimal reporter, silent unless something fails.
DOCTEST_ARGS ?= -m

$(BUILD)/%.ok: $(BUILD)/%
	$(MSG) RUN $<
	$(Q)$< $(DOCTEST_ARGS)
	@touch $@

FUZZ_ARGS ?= -t 30
fuzz: $(BUILD)/nsv_fuzz
	$(MSG) RUN "$< $(FUZZ_ARGS)"
	$(Q)$< $(FUZZ_ARGS)

# --- libFuzzer --------------------------------------------------------------------
# Its own directory and flags: every object instrumented for coverage feedback, the
# harness linked against libFuzzer's main. The standalone driver seeds the corpus.
LF_BUILD := build/libfuzzer
LF_CC ?= clang
LF_CXX ?= clang++
LF_FLAGS := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I. $(DIAG_DEFS)
LF_SRCS := nanosemver.c tests/fuzz/libfuzzer.cc tests/fuzz/check.cc tests/ref_semver.cc
LF_OBJS := $(patsubst %,$(LF_BUILD)/%.o,$(LF_SRCS))
LIBFUZZER_ARGS ?= -max_total_time=60

$(LF_BUILD)/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (libfuzzer)"
	$(Q)$(LF_CC) -std=c99 -MMD -MP $(LF_FLAGS) -fsanitize=fuzzer-no-link -c $< -o $@

$(LF_BUILD)/%.cc.o: %.cc Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX "$< (libfuzzer)"
	$(Q)$(LF_CXX) -std=c++20 -MMD -MP $(LF_FLAGS) -fsanitize=fuzzer-no-link -c $< -o $@

$(LF_BUILD)/nsv_libfuzzer: $(LF_OBJS)
	$(MSG) LINK $@
	$(Q)$(LF_CXX) $(LF_FLAGS) -fsanitize=fuzzer $^ -o $@

$(LF_BUILD)/corpus/.seeded: $(BUILD)/nsv_fuzz
	@mkdir -p $(dir $@)
	$(Q)$< --emit-corpus $(dir $@)
	@touch $@

libfuzzer: $(LF_BUILD)/nsv_libfuzzer $(LF_BUILD)/corpus/.seeded
	$(Q)$< $(LF_BUILD)/corpus -dict=tests/fuzz/semver.dict $(LIBFUZZER_ARGS)

# --- coverage ---------------------------------------------------------------------
# Line, region, function and branch coverage of nanosemver.c, required to be 100% in
# each test binary on its own, the diag one's of the code NSV_DIAG adds too. Two configurations of one source cannot share a
# report: llvm-cov drops one side's counts as "mismatched data".
# The tools that match the compiler, since the profile format is version-specific.
LLVM_PROFDATA ?= $(shell $(CC) -print-prog-name=llvm-profdata)
LLVM_COV ?= $(shell $(CC) -print-prog-name=llvm-cov)
COV_BUILD := build/coverage

define cov_check
	$(Q)rm -f $(COV_BUILD)/$(1).profraw
	$(Q)LLVM_PROFILE_FILE=$(COV_BUILD)/$(1).profraw $(COV_BUILD)/$(1) > /dev/null
	$(Q)$(LLVM_PROFDATA) merge -sparse $(COV_BUILD)/$(1).profraw -o $(COV_BUILD)/$(1).profdata
	$(Q)$(LLVM_COV) show -instr-profile=$(COV_BUILD)/$(1).profdata $(COV_BUILD)/$(1) \
		-show-branches=count -show-line-counts-or-regions nanosemver.c \
		> $(COV_BUILD)/$(1).nanosemver.c.txt
	$(Q)$(LLVM_COV) report -instr-profile=$(COV_BUILD)/$(1).profdata $(COV_BUILD)/$(1) \
		-show-branch-summary nanosemver.c > $(COV_BUILD)/$(1).report.txt
	$(Q)awk '$$1 == "TOTAL" { found = 1; \
		  if ($$3 != 0 || $$6 != 0 || $$9 != 0 || $$12 != 0) { \
		    print "  COV   $(1): FAILED, see $(COV_BUILD)/$(1).nanosemver.c.txt"; \
		    print; exit 1 } \
		  printf "  COV   $(1): 100%% of %d regions, %d functions, %d lines, %d branches\n", \
		    $$2, $$5, $$8, $$11 } \
		END { if (!found) { print "  COV   $(1): no TOTAL line"; exit 1 } }' \
		$(COV_BUILD)/$(1).report.txt
endef

coverage:
	$(Q)$(MAKE) --no-print-directory BUILD=$(COV_BUILD) CFG=Debug SAN= \
		EXTRA_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
		$(COV_BUILD)/nsv_unittests $(COV_BUILD)/nsv_unittests_int16 $(COV_BUILD)/nsv_unittests_diag
	$(call cov_check,nsv_unittests)
	$(call cov_check,nsv_unittests_int16)
	$(call cov_check,nsv_unittests_diag)

# --- benchmarks -------------------------------------------------------------------
BENCH_BASE ?= origin/main

# BENCH_BASE's nanosemver.c and .h, rewritten only when they differ so the objects
# built from them stay up to date. Both benchmarks link it beside this tree's.
BENCH_SRC := build/bench-base/$(subst /,_,$(BENCH_BASE))
NSV_API := parse parse_prefix cmp range_match copy format get_pre get_build
BENCH_RENAME := $(foreach f,$(NSV_API),-Dnsv_$(f)=nsvbase_$(f))

FORCE:

$(BENCH_SRC)/nanosemver.c: FORCE
	@mkdir -p $(dir $@)
	@for f in nanosemver.c nanosemver.h; do \
		git show "$(BENCH_BASE):$$f" > $(dir $@)$$f.tmp || exit 1; \
		if cmp -s $(dir $@)$$f.tmp $(dir $@)$$f; then rm $(dir $@)$$f.tmp; \
		else mv $(dir $@)$$f.tmp $(dir $@)$$f; fi; \
	done

# Host: no sanitizers, the warnings the tests get, BENCH_OPT. The baseline is held to
# none of them: it is whatever BENCH_BASE was.
BENCH_OPT ?= -O2
BENCH_ARGS ?=
BENCH_BUILD := build/bench/$(notdir $(firstword $(CC)))$(subst $(space),,$(BENCH_OPT))-$(ARCH)
BENCH_FLAGS := $(BENCH_OPT) $(ARCH_FLAGS) -I.

# Named for BENCH_BASE, so switching it never links another revision's objects.
BENCH_BASE_OBJ := $(BENCH_BUILD)/base-$(notdir $(BENCH_SRC)).o
BENCH_STUB_OBJ := $(BENCH_BUILD)/base_stub-$(notdir $(BENCH_SRC)).o
BENCH_BIN := $(BENCH_BUILD)/nsv_bench-$(notdir $(BENCH_SRC))

$(BENCH_BASE_OBJ): $(BENCH_SRC)/nanosemver.c
	@mkdir -p $(dir $@)
	$(MSG) CC "$< ($(BENCH_BASE))"
	$(Q)$(CC) -std=c99 $(BENCH_FLAGS) $(BENCH_RENAME) -c $< -o $@

$(BENCH_BUILD)/nanosemver.o: nanosemver.c nanosemver.h Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (bench)"
	$(Q)$(CC) -std=c99 $(WARN) $(WARN_C) $(BENCH_FLAGS) -c $< -o $@

$(BENCH_BUILD)/bench.o: bench/bench.cc nanosemver.h Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX $<
	$(Q)$(CXX) -std=c++20 $(WARN) $(WARN_CXX) $(BENCH_FLAGS) \
		-DNSV_BENCH_BASE='"$(BENCH_BASE)"' -c $< -o $@

# Whether BENCH_BASE has nsv_range_match and nsv_parse_prefix decides which stand-ins
# bench/base_stub.c compiles.
$(BENCH_STUB_OBJ): bench/base_stub.c $(BENCH_SRC)/nanosemver.c
	@mkdir -p $(dir $@)
	$(MSG) CC "$<"
	$(Q)$(CC) -std=c99 $(WARN) $(WARN_C) $(BENCH_FLAGS) \
		-DNSV_BASE_HAS_RANGE=$$(grep -q nsv_range_match $(BENCH_SRC)/nanosemver.h && echo 1 || echo 0) \
		-c $< -o $@

$(BENCH_BIN): $(BENCH_BUILD)/bench.o $(BENCH_BUILD)/nanosemver.o $(BENCH_BASE_OBJ) \
		$(BENCH_STUB_OBJ)
	$(MSG) LINK $@
	$(Q)$(CXX) $(ARCH_FLAGS) $^ -o $@

bench: $(BENCH_BIN)
ifneq ($(SAN),)
	$(error a benchmark built with SAN=$(SAN) measures the sanitizer)
endif
	$(Q)$< $(BENCH_ARGS)

# --- Cortex-M ---------------------------------------------------------------------
# The cross compiler and uv come through envy, resolved once and only by the goals
# below, so nothing else ever fetches them. The cross binutils are the compiler's own.
envy_product = $(or $(shell ./bin/envy -q product $(1)),$(error envy could not resolve $(1)))
ARM_CC ?= $(eval ARM_CC := $$(call envy_product,arm-none-eabi-gcc))$(ARM_CC)
UV ?= $(eval UV := $$(call envy_product,uv))$(UV)
arm_tool = $(or $(wildcard $(shell $(ARM_CC) -print-prog-name=$(1))), \
	$(error $(ARM_CC) has no $(1) beside it))
ARM_NM = $(eval ARM_NM := $$(call arm_tool,nm))$(ARM_NM)
ARM_OBJDUMP = $(eval ARM_OBJDUMP := $$(call arm_tool,objdump))$(ARM_OBJDUMP)
export UV_CACHE_DIR := $(CURDIR)/build/uv-cache
export UV_PYTHON_INSTALL_DIR := $(CURDIR)/build/uv-cache/python

CM_BUILD := build/cm
CM_FLAGS := -mthumb -Os -std=c99 -Wall -Wextra -Werror -Wconversion -Wsign-conversion
cm_cpu = $(if $(filter m0,$(1)),cortex-m0plus,cortex-m4)

include size-budget.mk

$(CM_BUILD)/m0.o $(CM_BUILD)/m4.o: $(CM_BUILD)/%.o: nanosemver.c nanosemver.h Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< ($(call cm_cpu,$*))"
	$(Q)$(ARM_CC) -mcpu=$(call cm_cpu,$*) $(CM_FLAGS) -I. -c $< -o $@

$(CM_BUILD)/m0-diag.o $(CM_BUILD)/m4-diag.o: $(CM_BUILD)/%-diag.o: nanosemver.c nanosemver.h \
		Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< ($(call cm_cpu,$*), diag)"
	$(Q)$(ARM_CC) -mcpu=$(call cm_cpu,$*) $(CM_FLAGS) $(DIAG_DEFS) -I. -c $< -o $@

# Bare images for bench/cm/cmcount.py, which calls the API directly. newlib-nano and
# libgcc supply whatever the code pulls in, and count with it.
CM_LINK := -specs=nano.specs -nostartfiles -T bench/cm/link.ld -Wl,--gc-sections \
	$(foreach f,$(NSV_API),-Wl,-u,nsv_$(f)) -Wl,-e,nsv_parse

$(CM_BUILD)/new-m0.elf $(CM_BUILD)/new-m4.elf: $(CM_BUILD)/new-%.elf: nanosemver.c \
		nanosemver.h bench/cm/link.ld Makefile
	@mkdir -p $(dir $@)
	$(MSG) LINK "$@"
	$(Q)$(ARM_CC) -mcpu=$(call cm_cpu,$*) $(CM_FLAGS) -I. $(CM_LINK) $< -o $@

$(CM_BUILD)/base-m0.elf $(CM_BUILD)/base-m4.elf: $(CM_BUILD)/base-%.elf: \
		$(BENCH_SRC)/nanosemver.c bench/cm/link.ld Makefile
	@mkdir -p $(dir $@)
	$(MSG) LINK "$@ ($(BENCH_BASE))"
	$(Q)$(ARM_CC) -mcpu=$(call cm_cpu,$*) -mthumb -Os -std=c99 $(CM_LINK) $< -o $@

bench-cm: $(foreach c,m0 m4,$(CM_BUILD)/base-$(c).elf $(CM_BUILD)/new-$(c).elf)
	@for c in m0 m4; do \
		echo; echo "$$( [ $$c = m0 ] && echo Cortex-M0+ || echo Cortex-M4 ), -Os:" \
			"$(BENCH_BASE) (base) against this tree (new)"; \
		$(UV) run --quiet bench/cm/cmcount.py --cpu $$c \
			--objdump "$(ARM_OBJDUMP)" \
			$(CM_BUILD)/base-$$c.elf $(CM_BUILD)/new-$$c.elf || exit 1; \
	done

# Total bytes of code, data and strings in object $(1), or a failure. Sections, not
# symbols, so the strings count too: with NSV_DIAG, its messages.
cm_bytes = "$(ARM_OBJDUMP)" -h $(1) | awk '$$2 ~ /^\.(text|rodata|data|bss)/ { \
		for (i = 1; i <= length($$3); i++) n = n * 16 + index("0123456789abcdef", substr($$3, i, 1)) - 1; \
		s += n; n = 0 } END { if (!s) exit 1; print s }'

CM_OBJS := $(foreach c,m0 m4 m0-diag m4-diag,$(CM_BUILD)/$(c).o)

size: $(CM_OBJS)
	@for c in m0 m4; do \
		echo; echo "> arm-none-eabi-gcc -mcpu=$$( [ $$c = m0 ] && echo cortex-m0plus || echo cortex-m4 ) $(CM_FLAGS) -c nanosemver.c"; \
		t=$$($(call cm_bytes,$(CM_BUILD)/$$c.o)) || exit 1; \
		"$(ARM_NM)" --print-size --size-sort --radix=d $(CM_BUILD)/$$c.o | awk -v t=$$t \
			'NF == 4 { printf "  %5d  %s\n", $$2, $$4; s += $$2 } \
			 END { printf "  %5d  strings and padding\n  %5d  total\n", t - s, t }'; \
		echo "  calls: $$("$(ARM_NM)" --undefined-only $(CM_BUILD)/$$c.o | awk '{ printf "%s ", $$2 }')"; \
		echo "  $$($(call cm_bytes,$(CM_BUILD)/$$c-diag.o)) total with $(DIAG_DEFS)"; \
	done

size-check: $(CM_OBJS)
	@for c in m0:$(NSV_SIZE_M0_MAX) m4:$(NSV_SIZE_M4_MAX) m0-diag:$(NSV_SIZE_M0_DIAG_MAX) \
			m4-diag:$(NSV_SIZE_M4_DIAG_MAX); do \
		o=$(CM_BUILD)/$${c%%:*}.o; max=$${c#*:}; \
		t=$$($(call cm_bytes,$$o)) || { echo "$$o: no size"; exit 1; }; \
		echo "$$o: $$t bytes (budget $$max)"; \
		if [ $$t -gt $$max ]; then echo "SIZE BUDGET EXCEEDED"; exit 1; fi; \
		if [ $$t -lt $$max ]; then echo "  under by $$((max - t)): make ratchet-update"; fi; \
		u=$$("$(ARM_NM)" --undefined-only $$o | awk '{ print $$2 }' | grep -vx memcpy); \
		if [ -n "$$u" ]; then echo "$$o calls more than memcpy:"; echo "$$u"; exit 1; fi; \
	done

# The ratchet: code size (size-budget.mk) and Cortex-M cycles per call, workload by
# workload (bench/cm/ratchet.txt), neither ever to grow. ratchet-update lowers both to
# what they now are; raising either is a deliberate edit.
RATCHET := bench/cm/ratchet.txt
cm_name = $$( [ $$c = m0 ] && echo Cortex-M0+ || echo Cortex-M4 )

ratchet: size-check $(CM_BUILD)/new-m0.elf $(CM_BUILD)/new-m4.elf
	@for c in m0 m4; do \
		echo; echo "$(cm_name), -Os: cycles per call against $(RATCHET)"; \
		$(UV) run --quiet bench/cm/cmcount.py --cpu $$c --objdump "$(ARM_OBJDUMP)" \
			--ratchet $(RATCHET) $(CM_BUILD)/new-$$c.elf || exit 1; \
	done

ratchet-update: $(CM_OBJS) $(CM_BUILD)/new-m0.elf $(CM_BUILD)/new-m4.elf
	@for c in m0:M0 m4:M4 m0-diag:M0_DIAG m4-diag:M4_DIAG; do \
		v=NSV_SIZE_$${c#*:}_MAX; t=$$($(call cm_bytes,$(CM_BUILD)/$${c%%:*}.o)) || exit 1; \
		old=$$(sed -n "s/^$$v := //p" size-budget.mk); \
		if [ $$t -gt $$old ]; then echo "$$v: $$t is over $$old"; exit 1; fi; \
		if [ $$t -lt $$old ]; then \
			sed -i.bak "s/^$$v := .*/$$v := $$t/" size-budget.mk && rm -f size-budget.mk.bak; \
			echo "$$v: $$old -> $$t"; \
		fi; \
	done
	@for c in m0 m4; do \
		echo; echo "$(cm_name), -Os: cycles per call, lowering $(RATCHET)"; \
		$(UV) run --quiet bench/cm/cmcount.py --cpu $$c --objdump "$(ARM_OBJDUMP)" \
			--ratchet $(RATCHET) --update $(CM_BUILD)/new-$$c.elf || exit 1; \
	done

# --- npm --------------------------------------------------------------------------
# There is no range standard, but npm's grammar is the nearest thing, and node-semver its
# reference. Node.js and the package come through envy, resolved only for this goal.
NODE ?= $(eval NODE := $$(call envy_product,node))$(NODE)
NODE_SEMVER ?= $(eval NODE_SEMVER := $$(call envy_product,node_semver))$(NODE_SEMVER)
NPM_BUILD := build/npm
NPM_ARGS ?= 200000 1

$(NPM_BUILD)/nanosemver.o: nanosemver.c nanosemver.h Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (npm)"
	$(Q)$(CC) -std=c99 $(WARN) $(WARN_C) -O2 -I. -c $< -o $@

$(NPM_BUILD)/nsv_npm_emit: tests/npm/emit.cc tests/fuzz/gen.cc tests/fuzz/gen.h tests/corpus.h \
		tests/range_cases.h $(NPM_BUILD)/nanosemver.o Makefile
	$(MSG) LINK $@
	$(Q)$(CXX) -std=c++20 $(WARN) $(WARN_CXX) -O2 -I. tests/npm/emit.cc tests/fuzz/gen.cc \
		$(NPM_BUILD)/nanosemver.o -o $@

# Through a file, not a pipe: sh has no pipefail everywhere.
npm-check: $(NPM_BUILD)/nsv_npm_emit
	$(Q)$< $(NPM_ARGS) > $(NPM_BUILD)/cases.jsonl
	$(Q)SEMVER="$(NODE_SEMVER)" "$(NODE)" tests/npm/judge.js < $(NPM_BUILD)/cases.jsonl

# --- mutation testing ---------------------------------------------------------------
# One change at a time to nanosemver.c, each built against this configuration's test
# objects, both int ways or, under #if NSV_DIAG, the diag way, and for its ARCH. uv
# provides the Python.
MUTATE_ARGS ?=
mutate: $(BUILD)/nsv_unittests $(BUILD)/nsv_unittests_int16 $(BUILD)/nsv_unittests_diag
ifneq ($(SAN),)
	$(error mutants link without the sanitizer runtime; build without SAN)
endif
	$(Q)CC="$(CC)" CXX="$(CXX)" ARCH_FLAGS="$(ARCH_FLAGS)" $(UV) run --quiet tests/mutate.py \
		$(BUILD) $(MUTATE_ARGS)

# Everything under build/ except the package caches: refetching them is not what
# anyone means by `make clean`. `rm -rf build` for that.
clean:
	@if [ -d build ]; then \
		find build -mindepth 1 -maxdepth 1 ! -name envy-cache ! -name uv-cache \
			-exec rm -rf {} +; \
	fi

-include $(ALL_OBJS:.o=.d) $(LF_OBJS:.o=.d)
