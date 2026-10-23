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
#   V=1             echo full commands
#
# Targets:
#   test        (default) the unit tests, built twice: as configured, and with
#               -DNSV_INT_MAX=32767 to reach the checks a 16-bit int needs
#   fuzz        the standalone fuzz driver; any compiler
#   libfuzzer   clang libFuzzer with ASan and UBSan; Apple clang ships no libFuzzer
#   coverage    clang source coverage of nanosemver.c; fails unless 100%
#   clean       everything under build/ except the envy package cache

CFG  ?= Release
ARCH ?= 64
SAN  ?=

comma := ,
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
ifeq ($(filter clean,$(MAKECMDGOALS)),)
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
INT16_DEFS := -DNSV_INT_MAX=32767

# --- sources ----------------------------------------------------------------------
# Globbed, so a new test file needs no registration here; build.bat globs the same.
FUZZ_LIB_SRCS := tests/fuzz/check.cc tests/fuzz/gen.cc
UNIT_SRCS := nanosemver.c $(sort $(wildcard tests/*.c) $(wildcard tests/*.cc)) $(FUZZ_LIB_SRCS)
INT16_SRCS := $(UNIT_SRCS) tests/int16/test_int16.cc
FUZZ_SRCS := nanosemver.c tests/fuzz/driver.cc tests/ref_semver.cc $(FUZZ_LIB_SRCS)

UNIT_OBJS := $(patsubst %,$(BUILD)/%.o,$(UNIT_SRCS))
INT16_OBJS := $(patsubst %,$(BUILD)/int16/%.o,$(INT16_SRCS))
FUZZ_OBJS := $(patsubst %,$(BUILD)/%.o,$(FUZZ_SRCS))
# nanosemver.c also has to build as C11, though nothing links that object.
C11_OBJ := $(BUILD)/c11/nanosemver.c.o
ALL_OBJS := $(sort $(UNIT_OBJS) $(INT16_OBJS) $(FUZZ_OBJS) $(C11_OBJ))

# --- rules ------------------------------------------------------------------------
.PHONY: test fuzz libfuzzer coverage clean
.DEFAULT_GOAL := test

test: $(BUILD)/nsv_unittests.ok $(BUILD)/nsv_unittests_int16.ok $(C11_OBJ)

$(BUILD)/int16/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (int16)"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) $(INT16_DEFS) -c $< -o $@

$(BUILD)/int16/%.cc.o: %.cc Makefile
	@mkdir -p $(dir $@)
	$(MSG) CXX "$< (int16)"
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INT16_DEFS) -c $< -o $@

$(BUILD)/c11/%.c.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(MSG) CC "$< (c11)"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -c $< -o $@

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
LF_FLAGS := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I.
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
# each test binary on its own. Two configurations of one source cannot share a
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
		$(COV_BUILD)/nsv_unittests $(COV_BUILD)/nsv_unittests_int16
	$(call cov_check,nsv_unittests)
	$(call cov_check,nsv_unittests_int16)

# Everything under build/ except the envy cache: refetching packages is not what
# anyone means by `make clean`. `rm -rf build` for that.
clean:
	@if [ -d build ]; then \
		find build -mindepth 1 -maxdepth 1 ! -name envy-cache -exec rm -rf {} +; \
	fi

-include $(ALL_OBJS:.o=.d) $(LF_OBJS:.o=.d)
