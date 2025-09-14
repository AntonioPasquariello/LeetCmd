/*
    LeetCmd - controls the electronic ink display of WD My Book HDDs
    Copyright (C) 2015-16 Stefan Pöschel

	Note: This tool is not related in any way to WD.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <scsi/sg_cmds_basic.h>
#include <scsi/sg_cmds_extra.h>

#include <sys/statvfs.h>

#define LABEL_LEN 12
#define LABEL_LEN_RAW (LABEL_LEN * 2)



/*
 * supported models
 */
static const char* SUPPORTED_MODELS[] = {
		"WD      ",
		"My Book 1111    ",
                "WD      ",
                "My Book 1112    ",		
		NULL
};


/*
 * mapping of label segments to console representation
 */

struct SEGMENT_MAPPING {
	int row_offset;
	int col_offset;
	const char* text;
};

static const struct SEGMENT_MAPPING segment_mappings[] = {
		// 0x___X
		{0, 1, "---"},
		{1, 4, "|"},
		{3, 4, "|"},
		{4, 1, "---"},

		// 0x__X_
		{3, 0, "|"},
		{1, 0, "|"},
		{1, 2, "|"},
		{1, 3, "/"},

		// 0x_X__
		{2, 3, "-"},
		{3, 3, "\\"},
		{3, 2, "|"},
		{3, 1, "/"},

		// 0xX___
		{2, 1, "-"},
		{1, 1, "\\"}
};


/*
 * mapping of ASCII chars to label segments
 *
 * sources:
 * - WD SmartWare output
 * - ETSI TS 101 756 V1.7.1, Table C.3
 * - own fantasy
 */

#define LABEL_ASCII_CHARS_START 0x20
#define LABEL_ASCII_CHARS_END 0x7E

static const uint16_t LABEL_ASCII_CHARS[] = {
		// 0x20 - 0x2F
		0x0000,	// (space)
		0x0048,	// !
		0x0022,	// "
		0x154E,	// #
		0x156D,	// $
		0x08A4,	// %
		0x329D,	// &
		0x0040,	// '
		0x0280,	// ( => equal to: <
		0x2800,	// ) => equal to: >
		0x3FC0,	// *
		0x1540,	// +
		0x0800,	// ,
		0x1100,	// - => equal to: ~
		0x0400,	// .
		0x0880,	// /

		// 0x30 - 0x3F
		0x08BF,	// 0
		0x0006,	// 1
		0x111B,	// 2
		0x110F,	// 3
		0x1126,	// 4
		0x112D,	// 5 => equal to: S
		0x113D,	// 6
		0x0007,	// 7
		0x113F,	// 8
		0x112F,	// 9
		0x0440,	// : => equal to: |
		0x1800,	// ,
		0x0280,	// < => equal to: (
		0x1108,	// =
		0x2800,	// > => equal to: )
		0x0503,	// ?

		// 0x40 - 0x4F
		0x121F,	// @
		0x1137,	// A
		0x054F,	// B
		0x0039,	// C => equal to: [
		0x044F,	// D
		0x1039,	// E
		0x1031,	// F
		0x013D,	// G
		0x1136,	// H
		0x0449,	// I
		0x001E,	// J
		0x12B0,	// K
		0x0038,	// L
		0x20B6,	// M
		0x2236,	// N
		0x003F,	// O

		// 0x50 - 0x5F
		0x1133,	// P
		0x023F,	// Q
		0x1333,	// R
		0x112D,	// S => equal to: 5
		0x0441,	// T
		0x003E,	// U
		0x08B0,	// V
		0x0A36,	// W
		0x2A80,	// X
		0x2480,	// Y
		0x0889,	// Z
		0x0039,	// [ => equal to: C
		0x2200,	// (backslash)
		0x000F,	// ]
		0x0A00,	// ^
		0x0008,	// _

		// 0x60 - 0x6F
		0x2000,	// `
		0x1137,	// A
		0x054F,	// B
		0x0039,	// C => equal to: [
		0x044F,	// D
		0x1039,	// E
		0x1031,	// F
		0x013D,	// G
		0x1136,	// H
		0x0449,	// I
		0x001E,	// J
		0x12B0,	// K
		0x0038,	// L
		0x20B6,	// M
		0x2236,	// N
		0x003F,	// O

		// 0x70 - 0x7E
		0x1133,	// P
		0x023F,	// Q
		0x1333,	// R
		0x112D,	// S => equal to: 5
		0x0441,	// T
		0x003E,	// U
		0x08B0,	// V
		0x0A36,	// W
		0x2A80,	// X
		0x2480,	// Y
		0x0889,	// Z
		0x1280,	// {
		0x0440,	// | => equal to: :
		0x2900,	// }
		0x1100	// ~ => equal to: -
};


static int device_fd = -1;
static int opt_verbose = 0;

void clean_up() {
	if(device_fd >= 0) {
		int result = sg_cmds_close_device(device_fd);
		if(result != 0)
			perror("Error while sg_cmds_close_device");
		device_fd = -1;
	}
}



int check_mode_page(const uint8_t *data, uint8_t page, size_t len) {
	// assuming subpage 0!

	// Mode parameter header(6) - 4 bytes

	// MODE DATA LENGTH
	if(data[0] != len + 5)
		return 1;

	// BLOCK DESCRIPTOR LENGTH
	if(data[3] != 0)
		return 1;

	// Page_0 mode page - 2+len bytes

	// SPF
	if(data[4] & 0x40)
		return 1;

	// PAGE CODE
	if((data[4] & 0x3F) != page)
		return 1;

	// PAGE LENGTH
	if(data[5] != len)
		return 1;

	return 0;
}


int check_diag_page(const uint8_t *data, uint8_t page, size_t len) {
	// Diagnostic page - 4+len bytes

	// PAGE CODE
	if(data[0] != page)
		return 1;

	// PAGE LENGTH
	size_t page_length = (data[2] << 8) + data[3];
	if(page_length != len)
		return 1;

	return 0;
}


int get_bit(const uint8_t *data, size_t offset, size_t bit) {
	// return bit
	return data[offset] & (1 << bit) ? 1 : 0;
}


void set_bit(uint8_t *data, size_t offset, size_t bit, int value) {
	// set/clear bit
	if(value)
		data[offset] |=  (1 << bit);
	else
		data[offset] &= ~(1 << bit);
}


void dump_data(const uint8_t *data, size_t len) {
	size_t i;
	for(i = 0; i < len; i++) {
		if(i > 0 && i % 8 == 0)
			printf("\n");
		printf(" %02X", data[i]);
	}
	printf("\n");
}


int check_device(const char *opt_device, int opt_force) {
	struct sg_simple_inquiry_resp data;

	if(opt_verbose)
		printf("Reading device information...\n");
	int check_result = sg_simple_inquiry(device_fd, &data, 0, opt_verbose);
	if(check_result != 0) {
		fprintf(stderr, "Error while sg_simple_inquiry: %d\n", check_result);
		return 1;
	}

	// check against model list
	int valid = 0;
	const char** model = SUPPORTED_MODELS;
	while(*model) {
		if(!strcmp(data.vendor, model[0]) && !strcmp(data.product, model[1])) {
			valid = 1;
			break;
		}
		model += 2;
	}

	const char* check_result_text = valid ? "supported" : (opt_force ? "unsupported; continuing forced" : "unsupported; aborting");
	check_result = (valid || opt_force) ? 0 : 1;
	FILE *target = check_result ? stderr : stdout;

	fprintf(target, "Device: %s (%s)\n", opt_device, check_result_text);
	fprintf(target, "%s - %s (rev %s)\n", data.vendor, data.product, data.revision);

	return check_result;
}


int handle_mode_page_flag_value(int opt_flag, const char *name, uint8_t page, size_t result_len, size_t flag_offset, size_t flag_bit) {
	int result;
	uint8_t data[6 + result_len];
	uint8_t* page_data = data + 6;

	// load setting
	if(opt_verbose)
		printf("Reading %s value...\n", name);
	result = sg_ll_mode_sense6(device_fd, 1, 0, page, 0x00, data, sizeof(data), 0, opt_verbose);
	if(result != 0) {
		fprintf(stderr, "Error while sg_ll_mode_sense6: %d\n", result);
		return 1;
	}
	if(opt_verbose)
		dump_data(data, sizeof(data));


	if(check_mode_page(data, page, result_len)) {
		fprintf(stderr, "Error getting %s value - aborting!\n", name);
		return 1;
	}

	int flag_value = get_bit(page_data, flag_offset, flag_bit);

	if(opt_flag != -1 && flag_value != opt_flag) {
		// reset PS flag (as reserved when using MODE SELECT)
		data[4] &= 0x7F;

		set_bit(page_data, flag_offset, flag_bit, opt_flag);
		printf("%s state: %d -> %d\n", name, flag_value, opt_flag);

		// save setting
		if(opt_verbose) {
			printf("Writing %s value...\n", name);
			dump_data(data, sizeof(data));
		}
		result = sg_ll_mode_select6(device_fd, 1, 1, data, sizeof(data), 0, opt_verbose);
		if(result != 0) {
			fprintf(stderr, "Error while sg_ll_mode_select6: %d\n", result);
			return 1;
		}
	} else {
		printf("%s state: %d%s\n", name, flag_value, flag_value == opt_flag ? " (already)" : "");
	}

	return 0;
}


void print_label(const uint8_t *data) {
	int i;
	int j;
	uint8_t lines[5][72];

	// init output
	memset(lines, 0x20, 5 * 72);

	// prepare output
	int term_offset = 0;
	for(i = 0; i < 12; i++) {
		uint16_t char_value = (data[i * 2] << 8) | data[i * 2 + 1];

		// if no segment displayed, skip digit
		if(char_value == 0x0000)
			continue;

		// map segments
		int col_offset = i * 6;
		for(j = 0; j < 14; j++) {
			if(char_value & (1 << j)) {
				const struct SEGMENT_MAPPING* m = &segment_mappings[j];
				memcpy(&lines[m->row_offset][col_offset + m->col_offset], m->text, strlen(m->text));
			}
		}
		term_offset = col_offset + 5;
	}

	// set terminator + print output
	for(i = 0; i < 5; i++) {
		lines[i][term_offset] = 0x00;
		printf("%s\n", lines[i]);
	}
}


int get_label_char(char c) {
	if(c < LABEL_ASCII_CHARS_START || c > LABEL_ASCII_CHARS_END)
		return -1;

	return LABEL_ASCII_CHARS[c - LABEL_ASCII_CHARS_START];
}


int handle_label_value(const uint8_t* label) {
	int result;
	uint8_t data[4 + 8 + LABEL_LEN_RAW];
	uint8_t* page_data = data + 4;
	uint8_t* label_data = page_data + 8;

	// load setting
	if(opt_verbose)
		printf("Reading label value...\n");
	result = sg_ll_receive_diag(device_fd, 1, 0x87, data, sizeof(data), 0, opt_verbose);
	if(result != 0) {
		fprintf(stderr, "Error while sg_ll_receive_diag: %d\n", result);
		return 1;
	}
	if(opt_verbose)
		dump_data(data, sizeof(data));


	if(check_diag_page(data, 0x87, 8 + LABEL_LEN_RAW)) {
		fprintf(stderr, "Error getting label value - aborting!\n");
		return 1;
	}

	printf("Label:\n");
	print_label(label_data);

	// return, if no new label
	if(!label)
		return 0;

	// return, if no change
	if(!memcmp(label_data, label, LABEL_LEN_RAW))
		return 0;

	memcpy(label_data, label, LABEL_LEN_RAW);

	printf("New label:\n");
	print_label(label_data);

	// save setting
	if(opt_verbose) {
		printf("Writing label value...\n");
		dump_data(data, sizeof(data));
	}
	result = sg_ll_send_diag(device_fd, 0, 1, 0, 0, 0, 0, data, sizeof(data), 0, opt_verbose);
	if(result != 0) {
		fprintf(stderr, "Error while sg_ll_send_diag: %d\n", result);
		return 1;
	}

	return 0;
}


int set_free_space(uint64_t space_free, uint64_t space_total, double kb_factor) {
	int result;
	uint8_t data[4 + 16];
	uint8_t *page_data = data + 4;

	// load page content
	if(opt_verbose)
		printf("Reading page content...\n");
	result = sg_ll_receive_diag(device_fd, 1, 0x86, data, sizeof(data), 0, opt_verbose);
	if(result != 0) {
		fprintf(stderr, "Error while sg_ll_receive_diag: %d\n", result);
		return 1;
	}
	if(opt_verbose)
		dump_data(data, sizeof(data));

	// reset all bits with known meaning
	page_data[4] &= ~(0x80);
	page_data[6] = 0x00;
	page_data[7] &= ~(0xC0);
	page_data[10] &= ~(0x16);
	page_data[11] &= ~(0x02);
	page_data[12] &= ~(0x8F);
	page_data[13] &= ~(0x8F);
	page_data[14] &= ~(0x8F);

	if(space_total) {
		// calculate segment value
		double segments_free_precise = ((double) space_free) / ((double) space_total) * 10.0;
		int segments_used = 10 - (int) (segments_free_precise + 0.5);
		int i;

		uint16_t segments_raw = 0x0000;
		for(i = 0; i < segments_used; i++)
			segments_raw |= 1 << (9 - i);

		// segment frame
		set_bit(page_data, 4, 7, 1);

		// segments
		page_data[6] = segments_raw >> 2;
		page_data[7] |= (segments_raw & 0x03) << 6;


		// calculate display value
		double displayed_space = space_free / kb_factor / kb_factor / kb_factor;	// GB
		int tb_mode = displayed_space >= 1000.0 ? 1 : 0;
		if(tb_mode)
			displayed_space /= kb_factor;	// TB

		// abort on non-displayable value
		if(displayed_space >= 1000.0) {
			fprintf(stderr, "Free space too large for display: %f.2 TB\n", displayed_space);
			return 1;
		}

		// digits + decimal point
		char displayed_digits[5];
		uint8_t digit_100s = 0;
		uint8_t digit_10s = 0;
		uint8_t digit_1s = 0;
		int dec_point = 0;

		if(displayed_space >= 10.0) {	// range ' 10' to '999'
			int displayed_space_int = (int) displayed_space;
			snprintf(displayed_digits, sizeof(displayed_digits), "%3d", displayed_space_int);

			if(displayed_space_int >= 100)
				digit_100s = 0x80 | (displayed_digits[0] - 0x30);
			digit_10s  = 0x80 | (displayed_digits[1] - 0x30);
			digit_1s   = 0x80 | (displayed_digits[2] - 0x30);
		} else {						// range '0.00' to '9.99'
			snprintf(displayed_digits, sizeof(displayed_digits), "%4f.2", displayed_space);

			digit_100s = 0x80 | (displayed_digits[0] - 0x30);
			dec_point = 1;
			digit_10s  = 0x80 | (displayed_digits[2] - 0x30);
			digit_1s   = 0x80 | (displayed_digits[3] - 0x30);
		}

		page_data[12] |= digit_100s;
		page_data[13] |= digit_10s;
		page_data[14] |= digit_1s;
		if(dec_point)
			set_bit(page_data, 11, 1, 1);

		// TB/GB indicator
		set_bit(page_data, 10, tb_mode ? 2 : 1, 1);

		// FREE indicator
		set_bit(page_data, 10, 4, 1);

		printf("Free space: %s %s\n", displayed_digits, tb_mode ? "TB" : "GB");
	} else {
		printf("Free space: (cleared)\n");
	}

	// save setting
	if(opt_verbose) {
		printf("Writing free space value...\n");
		dump_data(data, sizeof(data));
	}
	result = sg_ll_send_diag(device_fd, 0, 1, 0, 0, 0, 0, data, sizeof(data), 0, opt_verbose);
	if(result != 0) {
		fprintf(stderr, "Error while sg_ll_send_diag: %d\n", result);
		return 1;
	}

	return 0;
}


void usage(const char* exe) {
	printf("Controls the electronic ink display of WD My Book HDDs.\n");
	printf("Note: This tool is not related in any way to WD.\n");
	printf("\n");
	printf("Usage: %s [OPTIONS] <device> [<path>]\n", exe);
	printf("\n");
	printf("  <device>      device path (e.g. /dev/sdh)\n");
	printf("  <path>        file system path whose free space to display (\"-\" to clear)\n");
	printf("\n");
	printf("  -v            verbose output\n");
	printf("  -f            force mode (continue on unsupported model)\n");
	printf("  -k            compute with 1 kB = 1000 bytes (instead of 1024 bytes)\n");
	printf("\n");
	printf("  -D/-d         set/unset VCD disabled flag\n");
	printf("  -I/-i         set/unset inverse display flag\n");
	printf("  -l <text>     set label (text)\n");
	printf("  -L <hex>      set label (raw hex)\n");
	printf("\n");
	printf("Models supported so far:\n");

	const char** models = SUPPORTED_MODELS;
	while(*models) {
		printf("%s - %s\n", models[0], models[1]);
		models += 2;
	}
}


int main(int argc, char *argv[]) {
	atexit(clean_up);

	const char* opt_device = NULL;
	const char* opt_path = NULL;

	int opt_force = 0;
	int opt_kb_factor = 0;

	int opt_disable_vcd = -1;
	int opt_inverse = -1;
	const char* opt_label_text = NULL;
	const char* opt_label_raw = NULL;

	uint8_t new_label[LABEL_LEN_RAW];
	memset(new_label, 0x00, sizeof(new_label));


	printf("LeetCmd v1.0 - Copyright Stefan Poeschel 2015-16\n");

	// option args
	int c;
	while((c = getopt(argc, argv, "vfkDdIil:L:")) != -1) {
		switch(c) {
		case 'v':
			opt_verbose = 1;
			break;
		case 'f':
			opt_force = 1;
			break;
		case 'k':
			opt_kb_factor = 1;
			break;
		case 'D':
			opt_disable_vcd = 1;
			break;
		case 'd':
			opt_disable_vcd = 0;
			break;
		case 'I':
			opt_inverse = 1;
			break;
		case 'i':
			opt_inverse = 0;
			break;
		case 'l':
			opt_label_text = optarg;
			break;
		case 'L':
			opt_label_raw = optarg;
			break;
		case '?':
		default:
			usage(argv[0]);
			return 1;
		}
	}

	// non-option args
	switch(argc - optind) {
	case 1:
		opt_device = argv[optind];
		break;
	case 2:
		opt_device = argv[optind];
		opt_path = argv[optind + 1];
		break;
	default:
		usage(argv[0]);
		return 1;
	}

	printf("\n");


	// check args

	if(opt_label_text && opt_label_raw) {
		fprintf(stderr, "Only one label option can be used at the same time!\n");
		return 1;
	}

	if(opt_label_text) {
		// len
		size_t len = strlen(opt_label_text);
		if(len > LABEL_LEN) {
			fprintf(stderr, "Text label too long: %zu > %d\n", len, LABEL_LEN);
			return 1;
		}

		// chars
		size_t i;
		for(i = 0; i < len; i++) {
			char c = opt_label_text[i];
			int value = get_label_char(c);

			if(value == -1) {
				fprintf(stderr, "Text label char '%c' at offset %zu unsupported!\n", c, i);
				return 1;
			}

			new_label[i*2] = (value >> 8) & 0xFF;
			new_label[i*2 + 1] = value & 0xFF;
		}
	}

	if(opt_label_raw) {
		// len
		size_t len = strlen(opt_label_raw);
		const size_t max_len = LABEL_LEN_RAW * 2;	// 2 nibbles per byte
		if(len > max_len) {
			fprintf(stderr, "Raw label too long: %zu > %zu\n", len, max_len);
			return 1;
		}
		if(len % 4) {
			fprintf(stderr, "Raw label len not multiple of 4: %zu\n", len);
			return 1;
		}

		// bytes
		size_t i;
		char word[5];
		word[4] = 0x00;
		char *endp;
		for(i = 0; i < (len / 4); i++) {
			memcpy(word, opt_label_raw + i * 4, 4);

			int value = strtol(word, &endp, 16);
			if(*endp != 0x00) {
				fprintf(stderr, "Raw label is no hex number at char offset: %zu\n", i);
				return 1;
			}
			if(value > 0x3FFF) {
				fprintf(stderr, "Raw label invalid at char offset: %zu\n", i);
				return 1;
			}

			new_label[i*2] = (value >> 8) & 0xFF;
			new_label[i*2 + 1] = value & 0xFF;
		}
	}


	// derive space info
	struct statvfs space_info;
	if(opt_path && strcmp(opt_path, "-")) {
		int statvfs_result = statvfs(opt_path, &space_info);
		if(statvfs_result) {
			perror("Error while statvfs");
			return 1;
		}
	}


	// open device
	device_fd = sg_cmds_open_device(opt_device, 1, opt_verbose);
	if(device_fd < 0) {
		perror("Error while sg_cmds_open_device");
		return 1;
	}

	// check device support
	if(check_device(opt_device, opt_force))
		return 1;
	printf("\n");


	// handle Disable VCD flag
	if(handle_mode_page_flag_value(opt_disable_vcd, "Disable VCD", 0x20, 6, 2, 1))
		return 1;

	// handle Inverse Display flag
	if(handle_mode_page_flag_value(opt_inverse, "Inverse Display", 0x21, 10, 8, 0))
		return 1;

	// handle label
	if(handle_label_value(opt_label_text || opt_label_raw ? new_label : NULL))
		return 1;

	// handle free space
	if(opt_path) {
		int result;
		if(strcmp(opt_path, "-")) {
                        uint64_t bytes_free = space_info.f_bfree;
                        bytes_free *= space_info.f_frsize;

                        uint64_t bytes_total = space_info.f_blocks;
                        bytes_total *= space_info.f_frsize;

			result = set_free_space(bytes_free, bytes_total, opt_kb_factor ? 1000.0 : 1024.0);
		} else {
			result = set_free_space(0, 0, 0);
		}
		if(result)
			return 1;
	}

	return 0;
}
