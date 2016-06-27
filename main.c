#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include "sisa.h"

#define xstr(a) str(a)
#define str(a) #a

enum run_mode {
	RUN_MODE_STEP,
	RUN_MODE_RUN
};

static struct termios told;

static void usage(char *argv[])
{
	printf("Source code: https://github.com/xerpi/sisa-emu\n\n"
		"Usage: %s [OPTIONS] <code.bin> <data.bin>\n\n"
		"  -t, --enable-tlb      enables the TLB\n"
		"                          (defaults to disabled)\n"
		"  -v, --show-vga        prints the VGA when in continue mode\n"
		"                          (defaults to disabled)\n"
		"  -c, --code-addr=ADDR  address where to load the code at\n"
		"                          (defaults to " xstr(SISA_CODE_LOAD_ADDR) ")\n"
		"  -d, --data-addr=ADDR  address where to load the data at\n"
		"                          (defaults to " xstr(SISA_DATA_LOAD_ADDR) ")\n"
		"  -p, --pc-addr=ADDR    initial address of the PC\n"
		"                          (defaults to " xstr(SISA_CODE_LOAD_ADDR) ")\n"
		"  -l, --load addr=ADDR,file=FILE loads FILE to ADDR\n"
		"  -h, --help            displays this help and exit\n"
		"\nExample:\n"
		"\t./sisa-emu -t -l addr=0x1000,file=user.bin syscode.bin sysdata.bin\n\n"
		"\t Will enable the TLB and load 'user.bin' to 0x1000, 'syscode.bin' to " xstr(SISA_CODE_LOAD_ADDR)
		" and 'sysdata.bin' to " xstr(SISA_DATA_LOAD_ADDR) "\n"
		"\nNote: when loading a file, if the filename ends with .bin it will be loaded as\n"
		"raw binary, and as text (ASCII) otherwise (for example if it ends with .hex).\n"
		, argv[0]);
}

static void print_help()
{
	printf(
		"Help:\n"
		"s - do step\n"
		"c - pause/continue\n"
		"r - reset\n"
		"i - info registers\n"
		"t - info TLB\n"
		"v - dump VGA\n"
		"h - show this help\n"
		"q - quit\n\n"
	);
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

static int kbhit()
{
	struct timeval tv = { 0L, 0L };
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(0, &fds);
	return select(1, &fds, NULL, NULL, &tv);
}

static size_t fp_get_size(FILE *fp)
{
	size_t size;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	rewind(fp);

	return size;
}

static int load_file_bin(struct sisa_context *sisa, const char *file, uint16_t addr)
{
	FILE *fp;
	size_t size;
	size_t read_size;
	void *buffer;

	if (!(fp = fopen(file, "rb"))) {
		printf("Error opening '%s': %s\n", file, strerror(errno));
		return 0;
	}

	size = fp_get_size(fp);

	if (SISA_MEMORY_SIZE - addr < size) {
		printf("Error loading '%s': size limit exceeded\n", file);
		fclose(fp);
		return 0;
	}

	buffer = malloc(size);
	read_size = fread(buffer, 1, size, fp);

	sisa_load_binary(sisa, addr, buffer, read_size);

	free(buffer);
	fclose(fp);

	printf("Loaded '%s' at address 0x%04X\n", file, addr);

	return 1;
}

static int load_file_hex(struct sisa_context *sisa, const char *file, uint16_t addr)
{
	FILE *fp;
	int ret;
	uint16_t *buffer;
	uint16_t word;
	uint16_t offset = 0;
	const uint16_t max_size = SISA_MEMORY_SIZE - addr;

	if (!(fp = fopen(file, "r"))) {
		printf("Error opening '%s': %s\n", file, strerror(errno));
		return 0;
	}

	buffer = malloc(max_size);

	while (2 * offset < max_size) {
		ret = fscanf(fp, "%4hx", &word);
		if (ret == -1) {
			if (errno != 0) {
				printf("Error loading '%s': %s\n", file,
					strerror(errno));
				free(buffer);
				fclose(fp);
				return 0;
			} else {
				break;
			}
		}

		if (ret == EOF || ret == 0)
			break;

		buffer[offset] = word;
		offset++;
	}

	sisa_load_binary(sisa, addr, buffer, 2 * offset);

	free(buffer);
	fclose(fp);

	printf("Loaded '%s' at address 0x%04X\n", file, addr);

	return 1;
}


static int load_file(struct sisa_context *sisa, const char *file, uint16_t addr)
{
	const char *ext = strrchr(file, '.');

	if (ext != NULL && strcmp(ext + 1, "bin") == 0) {
		return load_file_bin(sisa, file, addr);
	} else {
		return load_file_hex(sisa, file, addr);
	}
}

enum load_subopt {
	ADDR_OPT = 0,
	FILE_OPT
};

static char *const load_subopt_token[] = {
	[ADDR_OPT] = "addr",
	[FILE_OPT] = "file",
	NULL
};

static int parse_load_subopt(struct sisa_context *sisa, char *optarg)
{
	char *value;
	char *subopts = optarg;
	uint16_t load_addr = 0;
	char *load_file_name = NULL;

	while (*subopts != '\0') {
		switch (getsubopt(&subopts, load_subopt_token, &value)) {
		case ADDR_OPT:
			if (!value)
				return -1;

			load_addr = strtol(value, NULL, 16);
			break;
		case FILE_OPT:
			if (!value)
				return -1;

			if (load_file_name)
				free(load_file_name);

			load_file_name = strdup(value);
			break;
		}
	}

	if (load_file_name) {
		if (!load_file(sisa, load_file_name, load_addr)) {
			free(load_file_name);
			return 0;
		}
		free(load_file_name);
	}

	return 1;
}

int main(int argc, char *argv[])
{
	struct sisa_context sisa;
	enum run_mode run_mode = RUN_MODE_STEP;
	int do_step;
	char c;

	int opt;
	int enable_tlb = 0;
	int show_vga = 0;
	uint16_t code_addr = SISA_CODE_LOAD_ADDR;
	uint16_t data_addr = SISA_DATA_LOAD_ADDR;
	uint16_t pc_addr = SISA_CODE_LOAD_ADDR;

	struct option long_options[] = {
		{"enable-tlb", no_argument, NULL, 't'},
		{"show-vga", no_argument, NULL, 'v'},
		{"code-addr", required_argument, NULL, 'c'},
		{"data-addr", required_argument, NULL, 'd'},
		{"pc-addr", required_argument, NULL, 'p'},
		{"help", no_argument, NULL, 'h'},
		{"load", required_argument, NULL, 'l'},
		{NULL, 0, NULL, 0}
	};

	printf("sisa-emu by xerpi\n");

	if (argc < 2) {
		usage(argv);
		return -1;
	}

	sisa_init(&sisa);

	while ((opt = getopt_long(argc, argv, "tvc:d:l:p:h", long_options, NULL)) != -1) {
		switch (opt) {
		case 't':
			enable_tlb = 1;
			break;
		case 'v':
			show_vga = 1;
			break;
		case 'c':
			code_addr = strtol(optarg, NULL, 16);
			break;
		case 'd':
			data_addr = strtol(optarg, NULL, 16);
			break;
		case 'p':
			pc_addr = strtol(optarg, NULL, 16);
			break;
		case 'h':
			usage(argv);
			return -1;
		case 'l':
			if (!parse_load_subopt(&sisa, optarg))
				return -1;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	/* If there are any arguments left, they are the plain
	 * code (and data) file arguments. */

	if (argc > 0) {
		if (!load_file(&sisa, argv[0], code_addr))
			return -1;
	}

	if (argc > 1) {
		if (!load_file(&sisa, argv[1], data_addr))
			return -1;
	}

	sisa_tlb_set_enabled(&sisa, enable_tlb);
	sisa_set_pc(&sisa, pc_addr);

	printf("TLB enabled: %s\n", enable_tlb ? "yes" : "no");
	printf("Show VGA: %s\n", show_vga ? "yes" : "no");
	printf("Code load address: 0x%04X\n", code_addr);
	printf("Data load address: 0x%04X\n", data_addr);
	printf("PC address: 0x%04X\n\n", pc_addr);

	stdin_setup();

	while (1) {
		do_step = 0;

		/* Avoid wasting CPU when step mode */
		if (run_mode == RUN_MODE_STEP ||
		    (run_mode == RUN_MODE_RUN && kbhit())) {
			c = getchar();

			if (c == 's') {
				do_step = 1;
			} else if (c == 'c') {
				if (run_mode == RUN_MODE_STEP)
					run_mode = RUN_MODE_RUN;
				else
					run_mode = RUN_MODE_STEP;
			} else if (c == 'r') {
				printf("CPU reseted\n");
				sisa_init(&sisa);
				run_mode = RUN_MODE_STEP;
			} else if (c == 'i') {
				sisa_print_dump(&sisa);
			} else if (c == 't') {
				sisa_print_tlb_dump(&sisa);
			} else if (c == 'v') {
				sisa_print_vga_dump(&sisa);
			} else if (c == 'h') {
				print_help();
			} else if (c == 'q') {
				break;
			}
		}

		if (run_mode == RUN_MODE_STEP && do_step) {
			sisa_step_cycle(&sisa);
			sisa_print_dump(&sisa);
		} else if (run_mode == RUN_MODE_RUN) {
			sisa_step_cycle(&sisa);
			sisa_print_dump(&sisa);

			if (show_vga) {
				/* Set cursor to 0,0 */
				printf("\e[1;1H\e[2J");
				sisa_print_vga_dump(&sisa);
			}
		}

		if (sisa_cpu_is_halted(&sisa)) {
			printf("CPU halted at 0x%04X\n", sisa.cpu.pc);
			run_mode = RUN_MODE_STEP;
		}
	}

	stdin_restore();

	return 0;
}
