#include <stdio.h>
#include <stdint.h>
#include <termios.h>
#include <unistd.h>
#include "sisa.h"

#define USER_START_ADDR 0x1000
#define MAX_CODE_SIZE   (SISA_MEMORY_SIZE - SISA_CODE_LOAD_ADDR)
#define MAX_DATA_SIZE   (SISA_VGA_START_ADDR - SISA_DATA_LOAD_ADDR)
#define MAX_USER_SIZE   (SISA_DATA_LOAD_ADDR - USER_START_ADDR)

static struct termios told;

static void usage(char *argv[])
{
	printf("Usage:\n\t%s code.bin <data.bin> <user.bin>\n", argv[0]);
}

static size_t fp_get_size(FILE *fp)
{
	size_t size;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);

	return size;
}

static int load_file_err(const char *file, void *dst, size_t max_size, const char *err_size)
{
	FILE *fp;
	int read_size;

	if ((fp = fopen(file, "rb")) == NULL) {
		printf("Error opening: %s\n", file);
		return -1;
	}

	if (fp_get_size(fp) > max_size) {
		printf("%s\n", err_size);
		return -1;
	}

	read_size = fread(dst, 1, max_size, fp);
	fclose(fp);

	return read_size;
}

static void stdin_setup()
{
	static struct termios t;
	tcgetattr(STDIN_FILENO, &t);
	told = t;
	t.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void stdin_restore()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &told);
}

enum run_mode {
	RUN_MODE_STEP,
	RUN_MODE_RUN
};

int main(int argc, char *argv[])
{
	uint8_t code[MAX_CODE_SIZE];
	uint8_t data[MAX_DATA_SIZE];
	uint8_t user[MAX_USER_SIZE];
	int code_size;
	int data_size;
	int user_size;
	struct sisa_context sisa;
	enum run_mode run_mode = RUN_MODE_STEP;
	int step;
	char c;

	printf("sisa-emu by xerpi\n");

	if (argc < 2) {
		usage(argv);
		return -1;
	}

	sisa_init(&sisa);

	if ((code_size = load_file_err(argv[1], code, MAX_CODE_SIZE, "code size limit exceeded.")) < 0)
		return -1;

	printf("loaded 0x%04X bytes of code\n", code_size);
	sisa_load_binary(&sisa, SISA_CODE_LOAD_ADDR, code, code_size);

	if (argc > 2) {
		if ((data_size = load_file_err(argv[2], data, MAX_DATA_SIZE, "data size limit exceeded.")) < 0)
			return -1;
		printf("loaded 0x%04X bytes of data\n", data_size);
		sisa_load_binary(&sisa, SISA_DATA_LOAD_ADDR, data, data_size);
	}

	if (argc > 3) {
		if ((user_size = load_file_err(argv[3], user, MAX_USER_SIZE, "user size limit exceeded.")) < 0)
			return -1;
		printf("loaded 0x%04X bytes of user\n", user_size);
		sisa_load_binary(&sisa, USER_START_ADDR, user, user_size);
	}

	stdin_setup();

	while (1) {
		step = 0;
		if (run_mode == RUN_MODE_STEP || sisa_cpu_is_halted(&sisa)) {
			c = getchar();
			if (c == 's') {
				step = 1;
			} else if (c == 'c') {
				run_mode = RUN_MODE_RUN;
			} else if (c == 'r') {
				sisa_init(&sisa);
				run_mode = RUN_MODE_STEP;
				printf("CPU reseted\n");
			} else if (c == 'q') {
				break;
			}
		}

		if (run_mode == RUN_MODE_STEP && step) {
			if (!sisa_cpu_is_halted(&sisa)) {
				sisa_print_dump(&sisa);
				sisa_step_cycle(&sisa);
				printf("\n");
			}
		} else if (run_mode == RUN_MODE_RUN) {
			if (!sisa_cpu_is_halted(&sisa)) {
				sisa_print_dump(&sisa);
				sisa_step_cycle(&sisa);
				printf("\n");
			}
		}
	}

	stdin_restore();

	return 0;
}
