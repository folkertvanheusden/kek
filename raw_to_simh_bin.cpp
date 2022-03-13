// this program is in the public domain
// written by Folkert van Heusden <mail@vanheusden.com>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void put_byte(const uint8_t byte, int32_t *const csum, FILE *const fh)
{
	*csum -= byte;

	fputc(byte, fh);
}

int main(int argc, char *argv[])
{
	if (argc != 5) {
		fprintf(stderr, "Usage: %s file_in load_offset start_offset file_out\n", argv[0]);
		fprintf(stderr, "Note: offsets must be decimal\n");

		return 1;
	}

	const char     *file_in  = argv[1];
	const uint32_t  offset   = atol(argv[2]);
	const uint32_t  start    = atol(argv[3]);
	const char     *file_out = argv[4];

	FILE           *fh_in    = fopen(file_in,  "r");
	FILE           *fh_out   = fopen(file_out, "wb");

	fseek(fh_in, 0, SEEK_END);
	uint32_t        file_size = ftell(fh_in);
	fseek(fh_in, 0, SEEK_SET);

	int32_t         csum = 0;

	// header for the data itself
	// after the data, a header will follow setting
	// the start address
	put_byte(0x01, &csum, fh_out);
	put_byte(0x00, &csum, fh_out);

	uint32_t        temp = file_size + 6;
	put_byte(temp & 255,   &csum, fh_out);
	put_byte(temp >> 8,    &csum, fh_out);

	put_byte(offset & 255, &csum, fh_out);
	put_byte(offset >> 8,  &csum, fh_out);

	for(int i=0; i<file_size; i++) {
		uint8_t c = fgetc(fh_in);

		put_byte(c, &csum, fh_out);
	}

	fputc(csum & 0xff, fh_out);

	put_byte(0x01, &csum, fh_out);
	put_byte(0x00, &csum, fh_out);

	put_byte(6,    &csum, fh_out);
	put_byte(0,    &csum, fh_out);

	put_byte(start & 255, &csum, fh_out);
	put_byte(start >> 8,  &csum, fh_out);

	fclose(fh_out);

	fclose(fh_in);

	return 0;
}
