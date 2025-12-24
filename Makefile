CC = gcc
NASM = nasm
CFLAGS = -Wall -Wextra -fPIC -Iinclude -g
LDFLAGS = -shared -ldl
NASMFLAGS = -f elf64

# Targets
LIB_TARGET = build/libmemorypatch.so
CLI_TARGET = build/memorypatch

# Source files
CORE_SRCS = src/core/hooks.c src/core/tracker.c
ASM_SRCS = src/asm/unwind.asm
CLI_SRCS = src/cli/main.c

# Object files
CORE_OBJS = $(CORE_SRCS:.c=.o)
ASM_OBJS = $(ASM_SRCS:.asm=.o)
CLI_OBJS = $(CLI_SRCS:.c=.o)

.PHONY: all clean

all: $(LIB_TARGET) $(CLI_TARGET)

# Build Shared Library
$(LIB_TARGET): $(CORE_OBJS) $(ASM_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build CLI Runner
$(CLI_TARGET): $(CLI_OBJS)
	$(CC) -Wall -Wextra -Iinclude -o $@ $^

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile ASM files
%.o: %.asm
	$(NASM) $(NASMFLAGS) $< -o $@

clean:
	rm -f $(CORE_OBJS) $(ASM_OBJS) $(CLI_OBJS) $(LIB_TARGET) $(CLI_TARGET)
