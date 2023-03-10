print-%:; @echo $($*)

CC = gcc
LD = gcc

# library paths
PATH_LIB = lib
PATH_SDL = $(PATH_LIB)/SDL

INCFLAGS = -iquotesrc

CCFLAGS  = -std=c2x
CCFLAGS += -O2
CCFLAGS += -g
CCFLAGS += -fbracket-depth=1024
CCFLAGS += -fmacro-backtrace-limit=0
CCFLAGS += -Wall
CCFLAGS += -Wextra
CCFLAGS += -Wpedantic
CCFLAGS += -Wfloat-equal
CCFLAGS += -Wstrict-aliasing
CCFLAGS += -Wswitch-default
CCFLAGS += -Wformat=2
CCFLAGS += -Wno-newline-eof
CCFLAGS += -Wno-unused-parameter
CCFLAGS += -Wno-strict-prototypes
CCFLAGS += -Wno-fixed-enum-extension
CCFLAGS += -Wno-int-to-void-pointer-cast
CCFLAGS += -Wno-gnu-statement-expression
CCFLAGS += -Wno-gnu-compound-literal-initializer
CCFLAGS += -Wno-gnu-zero-variadic-macro-arguments
CCFLAGS += -Wno-gnu-empty-struct
CCFLAGS += -Wno-gnu-auto-type
CCFLAGS += -Wno-gnu-empty-initializer
CCFLAGS += -Wno-gnu-pointer-arith
CCFLAGS += -Wno-c99-extensions
CCFLAGS += -Wno-c11-extensions

LDFLAGS = -lm

BIN = bin
SRC = $(shell find src -name "*.c")
OBJ = $(SRC:%.c=$(BIN)/%.o)
DEP = $(SRC:%.c=$(BIN)/%.d)
OUT = $(BIN)/game

-include $(DEP)

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
	CC = $(shell brew --prefix llvm)/bin/clang
	LD = $(shell brew --prefix llvm)/bin/clang

	INCFLAGS += -I$(PATH_SDL)/include
	LDFLAGS += $(shell $(BIN)/sdl/sdl2-config --prefix=$(BIN) --static-libs)
else ifeq ($(UNAME),Linux)
	LDFLAGS += -lSDL2
endif

$(BIN):
	mkdir -p $@

dirs: $(BIN)
	rsync -a --include '*/' --exclude '*' "src" "bin"

lib-sdl:
	mkdir -p $(BIN)/sdl
	cmake -S $(PATH_SDL) -B $(BIN)/sdl
	cd $(BIN)/sdl && make -j 10
	chmod +x $(BIN)/sdl/sdl2-config
	mkdir -p $(BIN)/lib
	cp $(BIN)/sdl/libSDL2.a $(BIN)/lib

libs: lib-sdl

$(OBJ): $(BIN)/%.o: %.c
	$(CC) -o $@ -MMD -c $(CCFLAGS) $(INCFLAGS) $<

doom: dirs $(BIN)/src/main_doom.o
	$(LD) -o bin/doom $(BIN)/src/main_doom.o $(LDFLAGS)

wolf: dirs $(BIN)/src/main_wolf.o
	$(LD) -o bin/wolf $(BIN)/src/main_wolf.o $(LDFLAGS)

all: dirs doom wolf

clean:
	rm -rf bin
