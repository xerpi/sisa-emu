TARGET = sisa-emu
OBJS = main.o sisa.o

CC = gcc
CFLAGS = -O2 -Wno-unused-result

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $^ -o $@

.c.o:
	$(CC) $(CFLAGS) -c $^ -o $@
clean:
	@rm -f $(TARGET) $(OBJS)
