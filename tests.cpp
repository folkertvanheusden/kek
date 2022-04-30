// (C) 2018 by Folkert van Heusden
// Released under Apache License v2.0
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"

void do_test(cpu *const c, const int nInstr)
{
	FILE *fh = fopen("test.dat", "w");
	for(int i=0; i<256; i++)
		fputc(c -> getBus() -> readByte(i), fh);
	fclose(fh);

	for(int i=0; i<nInstr; i++) {
		c->step_a();
		c->step_b();
	}
}

void test__initial(cpu *const c)
{
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	for(int i=0; i<8; i++)
		assert(c -> getRegister(i) == 0);

	c -> setPSW_n(true);
	assert(c -> getPSW_n() == true);
}

void test__registers(cpu *const c)
{
	bus *const b = c -> getBus();

	// kernel/user R0...R5
	c -> reset();
	b -> writeWord(0, 0012700); // mov #1,r0 kernel
	b -> writeWord(2, 1);
	do_test(c, 1);

	c -> setBitPSW(11, true);
	c -> setPC(0);
	b -> writeWord(0, 0012700); // mov #2,r0 user
	b -> writeWord(2, 2);
	do_test(c, 1);
	c -> setBitPSW(11, false);

	assert(c -> getRegister(false, 0) == 1);
	assert(c -> getRegister(true, 0) == 2);
	assert(b -> readWord(0177700) == 1);
	assert(b -> readWord(0177710) == 2);

	// SP
	b -> writeWord(0177706, 3);
	b -> writeWord(0177716, 4);
	b -> writeWord(0177717, 5);
	assert(c -> getStackPointer(0) == 3);
	assert(c -> getStackPointer(1) == 4);
	assert(c -> getStackPointer(3) == 5);
	c -> setPSW(0, false);
	assert(c -> getRegister(6) == 3);
	c -> setPSW(0b0100000000000000, false);
	assert(c -> getRegister(6) == 4);
	c -> setPSW(0b1100000000000000, false);
	assert(c -> getRegister(6) == 5);

	// PSW
	c -> reset();
	assert(c -> getPSW() == (0 | (7 << 5)));

	c -> reset();
        c -> setPSW_c(true);
	assert(c -> getPSW() == (1 | (7 << 5)));

	c -> reset();
	c -> setPSW_v(true);
	assert(c -> getPSW() == (2 | (7 << 5)));

	c -> reset();
	c -> setPSW_z(true);
	assert(c -> getPSW() == (4 | (7 << 5)));

	c -> reset();
	c -> setPSW_n(true);
	assert(c -> getPSW() == (8 | (7 << 5)));

	c -> reset();
	c -> setPSW_spl(1);
	assert(c -> getPSW() == (1 << 5));
}

void test_cmp(cpu *const c)
{
	bus *const b = c -> getBus();

	/// test CMP
	// equal 1000 / 1000
	c -> reset();
	b -> writeWord(0, 0012700); // mov #100,r0
	b -> writeWord(2, 1000);
	b -> writeWord(4, 0012701); // mov #100,r1
	b -> writeWord(6, 1000);
	b -> writeWord(8, 0020001); // cmp r0,r1

	do_test(c, 3);

	assert(c -> getPSW_z());
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	// > 1400 / 1000
	c -> reset();
	b -> writeWord(2, 1400);

	do_test(c, 3);

	assert(!c -> getPSW_z());
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	// 800 / 1000
	c -> reset();
	b -> writeWord(2, 800);

	do_test(c, 3);

	assert(!c -> getPSW_z());
	assert(c -> getPSW_n());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	// overflow 32768, -1
	c -> reset();
	b -> writeWord(2, 32768);
	b -> writeWord(6, -1);

	do_test(c, 3);

	assert(!c -> getPSW_z());
	assert(c -> getPSW_n());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	//////////

	// equal 10 / 10
	c -> reset();
	b -> writeWord(0, 0012700); // mov #10,r0
	b -> writeWord(2, 10);
	b -> writeWord(4, 0012701); // mov #10,r1
	b -> writeWord(6, 10);
	b -> writeWord(8, 0120001); // cmpb r0,r1

	do_test(c, 3);

	assert(c -> getPSW_z());
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	// > 40 / 10
	c -> reset();
	b -> writeWord(2, 40);

	do_test(c, 3);

	assert(!c -> getPSW_z());
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	// 8 / 10
	c -> reset();
	b -> writeWord(2, 8);

	do_test(c, 3);

	assert(!c -> getPSW_z());
	assert(c -> getPSW_n());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	// overflow -128, -1
	c -> reset();
	b -> writeWord(2, -128);
	b -> writeWord(6, -1);

	do_test(c, 3);

	assert(!c -> getPSW_z());
	assert(c -> getPSW_n());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

}

void test_clr(cpu *const c)
{
	bus *const b = c -> getBus();

	// equal
	c -> reset();
	b -> writeWord(0, 0012700); // mov #ffff,r0
	b -> writeWord(2, 0xffff);
	b -> writeWord(4, 0b0000101000000000); // clr
	do_test(c, 2);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0);

	c -> reset();
	b -> writeWord(6, 0b0000101111000000); // tst
	do_test(c, 3);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	c -> reset();
	b -> writeWord(4, 0b1000101000000000); // clrb

	do_test(c, 2);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());
	assert(c -> getRegister(0) == 0xff00);

	c -> reset();
	b -> writeWord(6, 0b1000101111000000); // tstb
	do_test(c, 3);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	c -> reset();
	b -> writeWord(6, 0b0000101111000000); // tst
	do_test(c, 3);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());
}

void test_add(cpu *const c)
{
	bus *const b = c -> getBus();

	// no overflow
	c -> reset();
	c -> setRegister(0, 123);
	c -> setRegister(1, 456);
	b -> writeWord(0, 0060001); // add r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 123);
	assert(c -> getRegister(1) == 123 + 456);

	// overflow
	c -> reset();
	c -> setRegister(0, 0x123);
	c -> setRegister(1, 0x7fff);
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 0x123);
	assert(c -> getRegister(1) == 0x8122);

	// no overflow
	c -> reset();
	c -> setRegister(0, 0x0001);
	c -> setRegister(1, 0xfffe);
	b -> writeWord(0, 0060001); // add r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 1);
	assert(c -> getRegister(1) == 0xffff);
}

void test_sub(cpu *const c)
{
	bus *const b = c -> getBus();

	// no overflow
	c -> reset();
	c -> setRegister(0, 123);
	c -> setRegister(1, 456);
	b -> writeWord(0, 0160001); // SUB r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 123);
	assert(int16_t(c -> getRegister(1)) == 456 - 123);

	// negative
	c -> reset();
	c -> setRegister(0, 456);
	c -> setRegister(1, 123);
	b -> writeWord(0, 0160001); // SUB r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 456);
	assert(int16_t(c -> getRegister(1)) == 123 - 456);

	// overflow
	c -> reset();
	c -> setRegister(0, 1);
	c -> setRegister(1, 0x8000);
	b -> writeWord(0, 0160001); // SUB r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 1);
	assert(c -> getRegister(1) == 0x7fff);

	// from docs
	c -> reset();
	c -> setRegister(1, 011111);
	c -> setRegister(2, 012345);
	b -> writeWord(0, 0160102); // SUB r1,r2
	do_test(c, 1);

	assert(c -> getRegister(1) == 011111);
	assert(c -> getRegister(2) == 001234);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());
}

void test_bit(cpu *const c)
{
	bus *const b = c -> getBus();

	// no overflow
	c -> reset();
	c -> setRegister(0, 0xf0);
	c -> setRegister(1, 0xff);
	b -> writeWord(0, 0130001); // bit r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0);
	assert(c -> getRegister(1) == 0xff);

	c -> reset();
	c -> setRegister(0, 0xf0f0);
	c -> setRegister(1, 0xffff);
	b -> writeWord(0, 0030001); // bit r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0f0);
	assert(c -> getRegister(1) == 0xffff);
}

void test_bis(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 0xf0);
	c -> setRegister(1, 0x0f);
	c -> setPSW_c(true);
	b -> writeWord(0, 0150001); // bisb r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0);
	assert(c -> getRegister(1) == 0xff);

	//

	c -> reset();
	c -> setRegister(0, 0xf0f0);
	c -> setRegister(1, 0x0f0f);
	b -> writeWord(0, 0050001); // bis r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0f0);
	assert(c -> getRegister(1) == 0xffff);

	//

	c -> reset();
	c -> setRegister(0, 01234);
	c -> setRegister(1, 01111);
	b -> writeWord(0, 0050001); // bis r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 01234);
	assert(c -> getRegister(1) == 01335);
}

void test_condcode(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	//                             sNZVC
	b -> writeWord(0, 0b0000000010111001); // condcode
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());

	//                             cNZVC
	c -> setRegister(7, 0);
	b -> writeWord(0, 0b0000000010101001); // condcode
	do_test(c, 1);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	//                             sNZVC
	c -> setRegister(7, 0);
	b -> writeWord(0, 0b0000000010110110); // condcode
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(c -> getPSW_v());
	assert(!c -> getPSW_c());

	//                             cNZVC
	c -> setRegister(7, 0);
	b -> writeWord(0, 0b0000000010100110); // condcode
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());
}

void test_asl(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 0x4000);
	b -> writeWord(0, 0b0000110011000000); // asl
	do_test(c, 1);

	assert(c -> getRegister(0) == 0x8000);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
// FIXME	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	c -> reset();
	c -> setRegister(0, 0x80);
	b -> writeWord(0, 0b1000110011000000); // asl
	do_test(c, 1);

	assert(c -> getRegister(0) == 0);
}

void test_adc(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x7f);
	b -> writeWord(0, 0b1000101101000000); // adc r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 128);

	//

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x00);
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 1);

	//

	c -> reset();
	c -> setPSW_c(false);
	c -> setRegister(0, 0x00);
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 0);

	////////////

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x7fff);
	b -> writeWord(0, 0b0000101101000000); // adc r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 0x8000);

	//

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x0000);
	b -> writeWord(0, 0b0000101101000000); // adc r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 1);
}

void test_ror_rol(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x81);
	b -> writeWord(0, 0b1000110000000000); // rorb r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	assert(c -> getRegister(0) == 0xc0);

	//

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x8001);
	b -> writeWord(0, 0b0000110000000000); // ror r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	assert(c -> getRegister(0) == 0xc000);

	//

	c -> reset();
	c -> setRegister(0, 0x1);
	b -> writeWord(0, 0b0000110000000000); // ror r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 0);

	////

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x80);
	b -> writeWord(0, 0b1000110001000000); // rolb r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	assert(c -> getRegister(0) == 0x01);

	//

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x8000);
	b -> writeWord(0, 0b0000110001000000); // rol r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	assert(c -> getRegister(0) == 0x0001);
}

void test_neg(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 0x1000);
	b -> writeWord(0, 0b0000101100000000); // neg r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf000);

	//

	c -> reset();
	c -> setRegister(0, 0x8000);
	b -> writeWord(0, 0b0000101100000000); // neg r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 0x8000);

	//////////

	c -> reset();
	c -> setRegister(0, 0x8010);
	b -> writeWord(0, 0b1000101100000000); // neg r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x80f0);

	//////////

	c -> reset();
	c -> setRegister(0, 010);
	b -> writeWord(0, 0b0000101100000000); // neg r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());

	assert(c -> getRegister(0) == 0177770);

	//////////

	c -> reset();
	c -> setRegister(0, 0x10);
	b -> writeWord(0, 0b1000101100000000); // negb r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0);

	//

	c -> reset();
	c -> setRegister(0, 0x80);
	b -> writeWord(0, 0b1000101100000000); // neg r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 0x80);
}

void test_inc(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 00);
	b -> writeWord(0, 0005200); // INC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 1);

	c -> reset();
	c -> setRegister(0, 0x7fff);
	b -> writeWord(0, 0005200); // INC r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 0x8000);

	c -> reset();
	c -> setRegister(0, 0xffff);
	b -> writeWord(0, 0005200); // INC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x0000);

	/////////

	c -> reset();
	c -> setRegister(0, 00);
	b -> writeWord(0, 0105200); // INCB r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 1);

	c -> reset();
	c -> setRegister(0, 0x7f);
	b -> writeWord(0, 0105200); // INCB r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(c -> getPSW_v());

	assert(c -> getRegister(0) == 0x80);

	c -> reset();
	c -> setRegister(0, 0xff);
	b -> writeWord(0, 0105200); // INC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x00);

	c -> reset();
	c -> setRegister(0, 01000);
	b -> writeWord(01000, 123);
	b -> writeWord(0, 0005210); // INC (r0)
	do_test(c, 1);

	assert(c -> getRegister(0) == 01000);
	assert(b -> readWord(01000) == 124);

	c -> reset();
	c -> setRegister(0, 01000);
	b -> writeWord(01000, 123);
	b -> writeWord(0, 0005220); // INC (r0)+
	do_test(c, 1);

	assert(c -> getRegister(0) == 01002);
	assert(b -> readWord(01000) == 124);

	c -> reset();
	c -> setRegister(0, 01000);
	b -> writeWord(01000, 02000);
	b -> writeWord(02000, 123);
	b -> writeWord(0, 0005230); // INC @(R0)+
	do_test(c, 1);

	assert(c -> getRegister(0) == 01002);
	assert(b -> readWord(02000) == 124);

	c -> reset();
	c -> setRegister(0, 01000);
	b -> writeWord(0776, 123);
	b -> writeWord(0, 0005240); // INC (r0)-
	do_test(c, 1);

	assert(c -> getRegister(0) == 0776);
	assert(b -> readWord(0776) == 124);

	c -> reset();
	c -> setRegister(0, 01002);
	b -> writeWord(01000, 02000);
	b -> writeWord(02000, 123);
	b -> writeWord(0, 0005250); // INC @-(R0)
	do_test(c, 1);

	assert(c -> getRegister(0) == 01000);
	assert(b -> readWord(02000) == 124);

	c -> reset();
	c -> setRegister(0, 01000);
	b -> writeWord(01124, 100);
	b -> writeWord(0, 0005260); // INC X(r0)
	b -> writeWord(2, 0124); // X = 0124
	do_test(c, 1);

	assert(c -> getRegister(0) == 01000);
	assert(b -> readWord(01124) == 101);

	c -> reset();
	c -> setRegister(0, 01000);
	b -> writeWord(01124, 02000);
	b -> writeWord(02000, 100);
	b -> writeWord(0, 0005270); // INC @X(r0)
	b -> writeWord(2, 0124); // X = 0124
	do_test(c, 1);

	assert(c -> getRegister(0) == 01000);
	assert(b -> readWord(02000) == 101);

	// mode 4, register 6

	c -> reset();
	c -> setRegister(7, 01000);
	c -> setRegister(6, 01000);
	b -> writeWord(01000, 0005246); // INC (r6)-
	b -> writeWord(0776, 123);
	do_test(c, 1);

	fprintf(stderr, "%o\n", c -> getRegister(6));
	fprintf(stderr, "%o\n", b -> readWord(0776));
	assert(c -> getRegister(6) == 0776);
	assert(b -> readWord(0776) == 124);

	// mode 4, register 7
	c -> reset();
	c -> setRegister(7, 01000);
	b -> writeWord(0776, 123);
	b -> writeWord(01000, 0005247); // INC (r7)-
	do_test(c, 1);

	assert(c -> getRegister(7) == 01000);
	assert(b -> readWord(01000) == 0005250);
	assert(b -> readWord(0776) == 123);
}

void test_dec(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 00);
	b -> writeWord(0, 0005300); // DEC r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xffff);

	c -> reset();
	c -> setRegister(0, 0x7fff);
	b -> writeWord(0, 0005300); // DEC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x7ffe);

	c -> reset();
	c -> setRegister(0, 0x0001);
	b -> writeWord(0, 0005300); // DEC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x000);

	/////////

	c -> reset();
	c -> setRegister(0, 00);
	b -> writeWord(0, 0105300); // DEC r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xff);

	c -> reset();
	c -> setRegister(0, 0x7f);
	b -> writeWord(0, 0105300); // DEC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x7e);

	c -> reset();
	c -> setRegister(0, 0x01);
	b -> writeWord(0, 0105300); // DEC r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x00);
}

void test_bic(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 0xf0);
	c -> setRegister(1, 0x0f);
	b -> writeWord(0, 0140001); // bicb r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0);
	assert(c -> getRegister(1) == 0x0f);

	//

	c -> reset();
	c -> setRegister(0, 0xf0);
	c -> setRegister(1, 0x1f);
	b -> writeWord(0, 0140001); // bicb r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf0);
	assert(c -> getRegister(1) == 0x0f);

	//

	c -> reset();
	c -> setRegister(0, 0x00);
	c -> setRegister(1, 0xff);
	b -> writeWord(0, 0140001); // bicb r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x00);
	assert(c -> getRegister(1) == 0xff);

	//////////////

	c -> reset();
	c -> setRegister(0, 0xf000);
	c -> setRegister(1, 0x0f00);
	b -> writeWord(0, 0040001); // bic r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf000);
	assert(c -> getRegister(1) == 0x0f00);

	//

	c -> reset();
	c -> setRegister(0, 0xf000);
	c -> setRegister(1, 0x1fff);
	b -> writeWord(0, 0040001); // bic r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0xf000);
	assert(c -> getRegister(1) == 0x0fff);

	//

	c -> reset();
	c -> setRegister(0, 0x0000);
	c -> setRegister(1, 0xffff);
	b -> writeWord(0, 0040001); // bic r0,r1
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());

	assert(c -> getRegister(0) == 0x0000);
	assert(c -> getRegister(1) == 0xffff);

	//

	c -> reset();
	c -> setRegister(0, 01234);
	c -> setRegister(1, 01111);
	c -> setPSW(15, false);
	b -> writeWord(0, 0040001); // bic r0,r1
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());

	assert(c -> getRegister(0) == 01234);
	assert(c -> getRegister(1) == 00101);
}

void test_b__(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	b -> writeWord(0, 0000404);	// BR
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);

	////////

	c -> reset();
	b -> writeWord(0, 0103404);	// BCS/BLO
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_c(true);
	b -> writeWord(0, 0103404);	// BCS/BLO
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);

	////////

	c -> reset();
	b -> writeWord(0, 0001404);	// BEQ
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_z(true);
	b -> writeWord(0, 0001404);	// BEQ
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);

	////////

	c -> reset();
	b -> writeWord(0, 0100404);	// BMI
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_n(true);
	b -> writeWord(0, 0100404);	// BMI
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);

	////////

	c -> reset();
	b -> writeWord(0, 0103004);	// BCC
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_c(true);
	b -> writeWord(0, 0103004);	// BCC
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);

	////////

	c -> reset();
	b -> writeWord(0, 0100004);	// BPL
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_n(true);
	b -> writeWord(0, 0100004);	// BPL
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);

	////////

	c -> reset();
	c -> setPSW_z(true);
	b -> writeWord(0, 003404);	// BLE
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_n(true);
	b -> writeWord(0, 003404);	// BLE
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_v(true);
	b -> writeWord(0, 003404);	// BLE
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_n(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 003404);	// BLE
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);

	////////

	c -> reset();
	b -> writeWord(0, 0002404);	// BLT
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_n(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 0002404);	// BLT
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_n(true);
	b -> writeWord(0, 0002404);	// BLT
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_v(true);
	b -> writeWord(0, 0002404);	// BLT
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);

	////////

	c -> reset();
	b -> writeWord(0, 0002004);	// BGE
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_n(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 0002004);	// BGE
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_n(true);
	b -> writeWord(0, 0002004);	// BGE
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_v(true);
	b -> writeWord(0, 0002004);	// BGE
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);

	////////

	c -> reset();
	b -> writeWord(0, 0101004);	// BHI
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_c(true);
	b -> writeWord(0, 0101004);	// BHI
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_z(true);
	b -> writeWord(0, 0101004);	// BHI
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);

	////////

	c -> reset();
	c -> setPSW_z(true);
	b -> writeWord(0, 003004);	// BGT
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_n(true);
	b -> writeWord(0, 003004);	// BGT
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_v(true);
	b -> writeWord(0, 003004);	// BGT
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setPSW_n(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 003004);	// BGT
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);

	////////

	c -> reset();
	c -> setPSW_z(true);
	b -> writeWord(0, 0101404);	// BLOS
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_c(true);
	b -> writeWord(0, 0101404);	// BLOS
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	b -> writeWord(0, 0101404);	// BLOS
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);

	////////

	c -> reset();
	b -> writeWord(0, 0001004);	// BNE
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	//
	c -> reset();
	c -> setPSW_z(true);
	b -> writeWord(0, 0001004);	// BNE
	do_test(c, 1);
	assert(c -> getRegister(7) == 2);
}

void test_jmp(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(1, 10);
	b -> writeWord(0, 0000111);	// JMP
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
}

void test_jsr(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(1, 10);
	c -> setRegister(6, 01000);
	b -> writeWord(0, 0004011);	// JSR
	do_test(c, 1);
	assert(c -> getRegister(0) == 2);
	assert(c -> getRegister(6) == 0776);
	assert(c -> getRegister(7) == 10);
}

void test_rts(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 10);
	c -> setRegister(6, 01000);
	b -> writeWord(0, 0004010);
	b -> writeWord(10, 0b0000000010000000);
	do_test(c, 1);
	assert(c -> getRegister(6) == 0776);
	do_test(c, 1);
	assert(c -> getRegister(0) == 10);
	assert(c -> getRegister(6) == 01000);
	assert(c -> getRegister(7) == 2);
	//
	c -> reset();
	c -> setRegister(0, 10);
	c -> setRegister(6, 01000);
	b -> writeWord(0, 0004110);
	b -> writeWord(10, 0b0000000010000001);
	do_test(c, 2);
	assert(c -> getRegister(0) == 10);
	assert(c -> getRegister(1) == 0);
	assert(c -> getRegister(6) == 01000);
	assert(c -> getRegister(7) == 2);
}

void test_mov(cpu *const c)
{
	bus *const b = c -> getBus();

	// 0, register, movb r0 to r1, check sign extending
	c -> reset();
	c -> setRegister(0, 255);
	b -> writeWord(0, 0110001);

	do_test(c, 1);

	assert(c -> getRegister(0) == 255);
	assert(c -> getRegister(1) == 65535);
	//
	c -> reset();
	c -> setRegister(0, 123);
	b -> writeWord(0, 0110001);

	do_test(c, 1);

	assert(c -> getRegister(0) == 123);
	assert(c -> getRegister(1) == 123);

	// 
	c -> reset();
	c -> setRegister(0, 128);
	c -> setRegister(1, 200);
	b -> writeWord(0, 0010001);

	do_test(c, 1);

	assert(c -> getRegister(0) == 128);
	assert(c -> getRegister(1) == 128);

	// 1, register deferred
	// FIXME byte
	c -> reset();
	c -> setRegister(0, 100);
	c -> setRegister(1, 200);
	b -> writeWord(100, 123);
	b -> writeWord(200, 99);
	b -> writeWord(0, 011011);
	do_test(c, 1);

	assert(c -> getRegister(0) == 100);
	assert(c -> getRegister(1) == 200);
	assert(b -> readWord(100) == 123);
	assert(b -> readWord(200) == 123);

	// 2, auto increment, mov (r0)+,(r1)+
	c -> reset();
	b -> writeWord(100, 123);
	b -> writeWord(200, 456);
	c -> setRegister(0, 100);
	c -> setRegister(1, 200);
	b -> writeWord(0, 012021);

	do_test(c, 1);

	assert(c -> getRegister(0) == 102);
	assert(c -> getRegister(1) == 202);
	assert(b -> readWord(100) == 123);
	assert(b -> readWord(200) == 123);

	// movb (r0)+,(r1)+
	c -> reset();
	b -> writeByte(100, 123);
	b -> writeByte(200, 200);
	c -> setRegister(0, 100);
	c -> setRegister(1, 200);
	b -> writeWord(0, 0112021);

	do_test(c, 1);

	assert(c -> getRegister(0) == 101);
	assert(c -> getRegister(1) == 201);
	assert(b -> readByte(100) == 123);
	assert(b -> readWord(200) == 123);

	// 3, auto increment deferred, move @(r0)+, @(r1)+
	// FIXME byte
	c -> reset();
	b -> writeWord(100, 123);
	b -> writeWord(123, 19);
	b -> writeWord(200, 456);
	b -> writeWord(456, 12);
	c -> setRegister(0, 100);
	c -> setRegister(1, 200);
	b -> writeWord(0, 013031);

	do_test(c, 1);

	assert(c -> getRegister(0) == 102);
	assert(c -> getRegister(1) == 202);
	assert(b -> readWord(100) == 123);
	assert(b -> readWord(123) == 19);
	assert(b -> readWord(200) == 456);
	assert(b -> readWord(456) == 19);

	// 4a, auto decrement, mov -(r0),-(r1)
	c -> reset();
	b -> writeWord(100, 123);
	b -> writeWord(200, 456);
	c -> setRegister(0, 102);
	c -> setRegister(1, 202);
	b -> writeWord(0, 014041);

	do_test(c, 1);

	assert(c -> getRegister(0) == 100);
	assert(c -> getRegister(1) == 200);
	assert(b -> readWord(100) == 123);
	assert(b -> readWord(200) == 123);

	// 4b, auto decrement, mov -(r0),-(r6)
	c -> reset();
	b -> writeWord(0100, 123);
	b -> writeWord(0200, 456);
	c -> setRegister(0, 0102);
	c -> setRegister(6, 0202);
	b -> writeWord(0, 014046);

	do_test(c, 1);

	assert(c -> getRegister(0) == 0100);
	assert(c -> getRegister(6) == 0200);
	assert(b -> readWord(0100) == 123);
	assert(b -> readWord(0200) == 123);

	// 5, auto decrement deferred, move @-(r0), @-(r1)
	// FIXME byte
	c -> reset();
	b -> writeWord(100, 123);
	b -> writeWord(123, 19);
	b -> writeWord(200, 456);
	b -> writeWord(456, 12);
	c -> setRegister(0, 102);
	c -> setRegister(1, 202);
	b -> writeWord(0, 015051);

	do_test(c, 1);

	assert(c -> getRegister(0) == 100);
	assert(c -> getRegister(1) == 200);
	assert(b -> readWord(100) == 123);
	assert(b -> readWord(123) == 19);
	assert(b -> readWord(200) == 456);
	assert(b -> readWord(456) == 19);

	// 6, index
	// FIXME byte
	c -> reset();
	c -> setRegister(0, 100);
	c -> setRegister(1, 200);
	b -> writeWord(104, 123);
	b -> writeWord(208, 99);
	b -> writeWord(0, 016061);
	b -> writeWord(2, 4);
	b -> writeWord(4, 8);
	do_test(c, 1);

	assert(c -> getRegister(0) == 100);
	assert(c -> getRegister(1) == 200);
	assert(b -> readWord(104) == 123);
	assert(b -> readWord(208) == 123);

	// 7, index, deferred
	// FIXME byte
	c -> reset();
	c -> setRegister(0, 100);
	c -> setRegister(1, 200);
	b -> writeWord(104, 124);
	b -> writeWord(124, 98);
	b -> writeWord(208, 210);
	b -> writeWord(210, 99);
	b -> writeWord(0, 017071);
	b -> writeWord(2, 4);
	b -> writeWord(4, 8);
	do_test(c, 1);

	assert(c -> getRegister(0) == 100);
	assert(c -> getRegister(1) == 200);
	assert(b -> readWord(124) == 98);
	assert(b -> readWord(210) == 98);
}

void test_ash(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 16);
	c -> setRegister(1, 1);
	b -> writeWord(0, 0072001); // R0 <<= R1
	do_test(c, 1);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());
	assert(c -> getRegister(0) == 32);
	//
	c -> reset();
	c -> setRegister(0, -16);
	c -> setRegister(1, 1);
	b -> writeWord(0, 0072001); // R0 <<= R1
	do_test(c, 1);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());
	assert(int16_t(c -> getRegister(0)) == -32);
	//////////
	c -> reset();
	c -> setRegister(0, 16);
	c -> setRegister(1, -1);
	b -> writeWord(0, 0072001); // R0 >>= R1
	do_test(c, 1);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());
	assert(c -> getRegister(0) == 8);
	//
	c -> reset();
	c -> setRegister(0, -16);
	c -> setRegister(1, -1);
	b -> writeWord(0, 0072001); // R0 >>= R1
	do_test(c, 1);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());
	assert(int16_t(c -> getRegister(0)) == -8);
	///////
	c -> reset();
	c -> setRegister(0, 16);
	c -> setRegister(1, -5);
	b -> writeWord(0, 0072001); // R0 >>= R1
	do_test(c, 1);
	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(c -> getPSW_v());
	assert(c -> getPSW_c());
	assert(c -> getRegister(0) == 0);
	//
	c -> reset();
	c -> setRegister(0, 0x7fff);
	c -> setRegister(1, 1);
	b -> writeWord(0, 0072001); // R0 <<= R1
	do_test(c, 1);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_v());
	assert(!c -> getPSW_c());
	assert(c -> getRegister(0) == 0xfffe);
}

void test_sob(cpu *const c)
{
	bus *const b = c -> getBus();

	// not taken
	c -> reset();
	c -> setRegister(0, 2);
	b -> writeWord(10, 077001);	// SOB
	c -> setRegister(7, 10);
	do_test(c, 1);
	assert(c -> getRegister(7) == 10);
	// taken
	c -> reset();
	c -> setRegister(0, 1);
	b -> writeWord(10, 077007);	// SOB
	c -> setRegister(7, 10);
	do_test(c, 1);
	assert(c -> getRegister(7) == 12);
}

void test_swab(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setPSW_c(true);
	c -> setPSW_n(true);
	c -> setPSW_z(true);
	c -> setPSW_v(true);
	c -> setRegister(1, 077777);
	b -> writeWord(0, 000301);	// SWAB R1
	do_test(c, 1);
	assert(c -> getRegister(1) == 0177577);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());
}

void test_div(cpu *const c)
{
	bus *const b = c -> getBus();

	// regular
	c -> reset();
	c -> setRegister(0, 0);
	c -> setRegister(1, 020001);
	c -> setRegister(2, 2);
	b -> writeWord(0, 071002);	// DIV R2,R0
	do_test(c, 1);
	assert(c -> getRegister(0) == 010000);
	assert(c -> getRegister(1) == 01);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	// result does not fit
	c -> reset();
	c -> setRegister(0, 0x7fff);
	c -> setRegister(1, 0xffff);
	c -> setRegister(2, 2);
	b -> writeWord(0, 071002);	// DIV R2,R0
	do_test(c, 1);
	assert(c -> getPSW_v());

	// div by zero
	c -> reset();
	c -> setRegister(0, 0);
	c -> setRegister(1, 020001);
	c -> setRegister(2, 0);
	b -> writeWord(0, 071002);	// DIV R2,R0
	do_test(c, 1);
	assert(c -> getPSW_v());
	assert(c -> getPSW_c());
}

void test_sbc(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x80);
	b -> writeWord(0, 0105600); // sbc r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 0x7f);

	//

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x00);
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());

	assert(c -> getRegister(0) == 0xff);

	//

	c -> reset();
	c -> setPSW_c(false);
	c -> setRegister(0, 0x00);
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 0);

	////////////

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x8000);
	b -> writeWord(0, 0005600); // sbc r0
	do_test(c, 1);

	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(!c -> getPSW_c());

	assert(c -> getRegister(0) == 0x7fff);

	//

	c -> reset();
	c -> setPSW_c(true);
	c -> setRegister(0, 0x0000);
	b -> writeWord(0, 0005600); // sbc r0
	do_test(c, 1);

	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());

	assert(c -> getRegister(0) == 0xffff);
}

void test_com(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 013333);
	b -> writeWord(0, 0005100);	// COM R0
	do_test(c, 1);
	assert(c -> getRegister(0) == 0164444);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());

	c -> reset();
	c -> setRegister(0, 013333);
	b -> writeWord(0, 0105100);	// COMB R0
	do_test(c, 1);
	assert(c -> getRegister(0) == 013044);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_v());
	assert(c -> getPSW_c());
}

void test_rti(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(6, 02000);
	c -> pushStack(15); // SP
	c -> pushStack(01234); // pc

	b -> writeWord(0, 02);	// RT
	do_test(c, 1);
	assert(c -> getRegister(6) == 02000);
	assert(c -> getRegister(7) == 01234);
	assert(c -> getPSW() == 15);
}

void test_trap(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(6, 02000);
	b -> writeWord(034, 01234); // new pc
	b -> writeWord(036, 15); // new sp
	b -> writeWord(0, 0104400);	// TRAP
	do_test(c, 1);

	assert(c -> getRegister(6) == 01774);
	assert(c -> getRegister(7) == 01234);
	assert(c -> getPSW() == 15);
}

void test_asr(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 0x4001);
	b -> writeWord(0, 0006200); // asr
	do_test(c, 1);

	printf("%04x\n", c -> getRegister(0));
	assert(c -> getRegister(0) == 0x2000);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	c -> reset();
	c -> setRegister(0, 0x8001);
	b -> writeWord(0, 0006200); // asr
	do_test(c, 1);

	assert(c -> getRegister(0) == 0xc000);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	//////

	c -> reset();
	c -> setRegister(0, 0x41);
	b -> writeWord(0, 0106200); // asrb
	do_test(c, 1);

	printf("%04x\n", c -> getRegister(0));
	assert(c -> getRegister(0) == 0x20);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));

	c -> reset();
	c -> setRegister(0, 0x81);
	b -> writeWord(0, 0106200); // asrb
	do_test(c, 1);

	assert(c -> getRegister(0) == 0xc0);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(c -> getPSW_c());
	assert(c -> getPSW_v() == (c -> getPSW_n() ^ c -> getPSW_c()));
}

void test_tst(cpu *const c)
{
	bus *const b = c -> getBus();

	c -> reset();
	c -> setRegister(0, 0);
	c -> setPSW_c(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 0005700); // TST 0
	do_test(c, 1);

	assert(c -> getRegister(0) == 0);
	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	c -> reset();
	c -> setRegister(0, 0);
	c -> setPSW_c(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 0105700); // TSTB 0
	do_test(c, 1);

	assert(c -> getRegister(0) == 0);
	assert(!c -> getPSW_n());
	assert(c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	c -> reset();
	c -> setRegister(0, 0x8010);
	c -> setPSW_c(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 0005700); // TST 0
	do_test(c, 1);

	assert(c -> getRegister(0) == 0x8010);
	assert(c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());

	c -> reset();
	c -> setRegister(0, 0x8010);
	c -> setPSW_c(true);
	c -> setPSW_v(true);
	b -> writeWord(0, 0105700); // TSTB 0
	do_test(c, 1);

	assert(c -> getRegister(0) == 0x8010);
	assert(!c -> getPSW_n());
	assert(!c -> getPSW_z());
	assert(!c -> getPSW_c());
	assert(!c -> getPSW_v());
}

void test_mfp(cpu *const c)
{
	bus *const b = c -> getBus();

	// current operation mode == previous operation mode
	b -> clearmem();
	c -> reset();
	b -> writeWord(01000, 07711);
	c -> setRegister(0, 01000);
	c -> setRegister(6, 02000);
	b -> writeWord(0, 006500); // MFPD R0
	do_test(c, 1);

	assert(c -> getRegister(0) == 01000);
	// FIXME flags
	assert(c -> getRegister(6) == 01776);
	assert(b -> readWord(01776) == 01000);

	// current operation mode != previous operation mode
	b -> clearmem();
	c -> reset();
	b -> writeWord(0172340, 896); // setup memory user
	// write a word 07711 to 0100 in current mode which is kernel
	fprintf(stderr, "---\n");
	b -> write(0100, false, 07711, false);
	b -> writeWord(0177640, 0); // setup memory kernel
	c -> setPSW(3 << 14, false);
	// write a word 0123 to 0100 in current mode which is user
	fprintf(stderr, "===\n");
	b -> write(0100, false, 0123, false);
	// go back to kernel mode
	c -> setPSW(0 << 14, false);
	fprintf(stderr, "+++\n");

	c -> setRegister(0, 0100);
	c -> setRegister(6, 02000);
	c -> setPSW((0 << 14) | (3 << 12), false);
	b -> writeWord(0, 006510); // MFPD (R0)
	do_test(c, 1);

	assert(c -> getRegister(0) == 0100);
	assert(c -> getRegister(6) == 01776);
	// FIXME flags

	c -> setPSW(3 << 14, false);
	//fprintf(stderr, "%o == 07711\n", b -> read(0100, false, false)); fflush(NULL);
	assert(b -> read(0100, false, false) == 0123);

	c -> setPSW(0 << 14, false);
	fprintf(stderr, "%o == 0123\n", b -> read(0100, false, false)); fflush(NULL);
	assert(b -> read(0100, false, false) == 07711);
}

void tests(cpu *const c)
{
	test__initial(c);
	test_cmp(c);
	test_clr(c);
	test_add(c);
	test_sub(c);
	test_bit(c);
	test_bis(c);
	test_condcode(c);
	test_asl(c);
	test_adc(c);
	test_ror_rol(c);
	test_neg(c);
	test_inc(c);
	test_dec(c);
	test_bic(c);
	test_b__(c);
	test_jmp(c);
	test_jsr(c);
	test_rts(c);
	test_mov(c);
	test_ash(c);
	test_sob(c);
	test_swab(c);
	test_div(c);
	test_sbc(c);
	test_com(c);
	test_rti(c);
	test_trap(c);
	test_asr(c);
	test_tst(c);
	test__registers(c);
	test_mfp(c);

	printf("\nALL FINE\n");

	exit(0);
}
