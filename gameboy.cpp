#include <stdio.h>
#include <unistd.h>
#include <cstring>
#include "gameboy.h"

u8 BOOTROM = 0; // use bootrom?
u8 renderscan = 1; // enable or disable rendering
extern u8 debug_name[0x100];
u32 unimpl=0; // err_counter

// mcycle count per op for emulation
u8 mcycles[256] = {
   4, 12,  8,  8,  4,  4,  8,  4, 20,  8,  8,  8,  4,  4,  8,  4,  // 00-0f
   4, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4,  // 10-1f
  12, 12,  8,  8,  4,  4,  8,  4, 12,  8,  8,  8,  4,  4,  8,  4,  // 20-2f
  12, 12,  8,  8, 12, 12, 12,  4, 12,  8,  8,  8,  4,  4,  8,  4,  // 30-3f
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // 40-4f
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // 50-5f
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // 60-6f
   8,  8,  8,  8,  8,  8,  4,  8,  4,  4,  4,  4,  4,  4,  8,  4,  // 70-7f
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // 80-8f
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // 90-9f
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // a0-af
   4,  4,  4,  4,  4,  4,  8,  4,  4,  4,  4,  4,  4,  4,  8,  4,  // b0-bf
  20, 12, 16, 16, 24, 16,  8, 16, 20, 16, 16,  4, 24, 24,  8, 16,  // c0-cf
  20, 12, 16,  0, 24, 16,  8, 16, 20, 16, 16,  0, 24,  0,  8, 16,  // d0-df
  12, 12,  8,  0,  0, 16,  8, 16, 16,  4, 16,  0,  0,  0,  8, 16,  // e0-ef
  12, 12,  8,  4,  0, 16,  8, 16, 12,  8, 16,  4,  0,  0,  8, 16}; // f0-ff

uint32_t cpu_ticks = 0;
uint32_t gpu_ticks = 0;
uint32_t total_cpu_ticks = 0;
uint32_t total_gpu_ticks = 0;

u8 halted = 0;
u8 stopped = 0;
u8 irq_en = 1;
u8 op = 0;

// 'MMU'
u8 rom_bank_no = 0; // id of selected bank in bank #1
u32 rom_offs = 0x4000;
u8 ram_bank_no = 0;
u16 ram_offs = 0x0000;
u8 ram_on = 0;
u8 mbc_mode = 0;

u8 enable_int = 0;
u8 disable_int = 0;

void na()   { /*printf("%s, %2x unimplemented\n", debug_name, op); */ unimpl+=1; }
void nop()   { /* nop */  };
void ei()    { enable_int=1; };
void di()    { disable_int=1;  };
void halt()  { halted=1;  };
void stop()  { stopped=1; PC++; };

void oam_ram() { // OAM 'dma' transfer
  if (REG_OAMDMA>0) {
    u16 a = (u16)(REG_OAMDMA) << 8;
    for (u8 i=0; i<160; i++) oam[i] = r8(a+i); 
    REG_OAMDMA = 0;
  }
}

// 8-bit load/store
u8 r8(u16 a) {
  switch (a & 0xf000) {
    case 0x0000 ... 0x3000: // ROM bank 0 : 0x0000 - 0x3fff
      if (a >= 0x100 || REG_BOOTROM) return cart[a]; else return rom[a];
    case 0x4000 ... 0x7000: // selectable ROM bank 1 : 0x4000 - 0x7fff
      return cart[(u32)(a & 0x3fff) + rom_offs];
    case 0x8000 ... 0x9000: // GPU mem
      return vram[a & 0x1fff];
    case 0xa000 ... 0xb000: // external RAM
      return eram[ram_offs + (a & 0x1fff)];
    case 0xc000 ... 0xf000: // wRAM
      if (a < 0xfe00) return ram[a & 0x1fff];
      else if (a < 0xff00) return oam[a & 0xff];
      else return hram[a & 0xff];
  }
  return 0;
}

void w8(u16 a, u8 v) {
  switch (a & 0xf000) {
    case 0x0000 ... 0x1000: // external ram switch
      switch (REG_MBC) { case 2 ... 3: ram_on = ((v & 0x0f) == 0x0a) ? 1 : 0; break; }
      break;
    case 0x2000 ... 0x3000: // rom bank select
      switch (REG_MBC) {
        case 1 ... 3:
          v &= 0x1f; v = v ? v : 1;
          rom_bank_no = (rom_bank_no & 0x60) + v;
          rom_offs = (u32)(rom_bank_no) * 0x00004000;
          break;
      }
      break;
    case 0x4000 ... 0x5000: // ram select
      switch (REG_MBC) {
        case 1 ... 3:
          if (mbc_mode) { ram_bank_no = v & 3; ram_offs = ram_bank_no * 0x2000; } // ram mode
          else { rom_bank_no = rom_bank_no & 0x1f + ((v & 3) << 5);
                 rom_offs = (u32)(rom_bank_no) * 0x00004000;
          } // rom mode
          break;
      } break;
    case 0x6000 ... 0x7000: // mode switch
      switch (REG_MBC) { case 2 ... 3: mbc_mode = v & 1; break; } break;
    case 0x8000 ... 0x9000: // gpu
      vram[a & 0x1fff] = v; break;
    case 0xa000 ... 0xb000: // external ram
      eram[ram_offs + (a & 0x1fff)] = v; break;
    case 0xc000 ... 0xf000:
      if (a < 0xfe00) ram[a & 0x1fff] = v; else
      if (a < 0xff00) oam[a & 0xff] = v;
      else {
        hram[a & 0xff] = v;
        if (a == 0xff46) { oam_ram(); }
      } break;
  }
}

//16-bit load/store
void w16(u16 a, u16 v) { w8(a,v&0xff); w8(a+1,v>>8); } // write 2 bytes
u16  r16(u16 a) { return ((u16)(r8(a+1)) << 8) | (u16)(r8(a)); } // read 2 bytes

// operand fetch
u8 f8()   { u8 r = r8(PC); PC+=1; return r;  } // fetch operand data (byte)
u16 f16() { u16 r = r16(PC); PC+=2; return r; } // fetch operand data (2 bytes)

//16-bit stack push/pop
void push16(u16 v) { SP -= 2; w16(SP, v); } // push onto the stack
u16 pop16() { u16 v = r16(SP); SP+=2; return v; } // pop

// 8-bit alu ops
void _add8(u8 v, u8 carry) {
  u8 c = carry ? fC : 0;
  u8 r = A;
  r = A + v + c;
  fH = (((A & 0xf) + (v & 0xf) + c) > 0xf) ? 1 : 0;
  fN = 0; fC = (((u16)(A) + (u16)(v) + (u16)(c)) > 0x00ff) ? 1 : 0;
  A = r;
  fZ = (A == 0);
}

void _sub8(u8 v, u8 carry) {
  // use carry?
  u8 c = carry ? fC : 0;
  u8 r = A;
  r = A - v - c;
  // update flags
  fZ = (r == 0); fH = (((A & 0xf) < ((v & 0xf) + c))) ? 1 : 0; fN = 1;
  fC = (((u16)(A) < (u16)(v) + (u16)(c))) ? 1 : 0;
  A = r;
}

void add8(u8 v) { _add8(v, 0); }
void adc8(u8 v) { _add8(v, 1); }
void sub8(u8 v) { _sub8(v, 0); }
void sbc8(u8 v) { _sub8(v, 1); }
void and8(u8 v) { A &= v; fZ = (A == 0); fH = 1; fC = 0; fN = 0; }
void or8(u8 v)  { A |= v; fZ = (A == 0); fH = 0; fC = 0; fN = 0; }
void xor8(u8 v) { A ^= v; fZ = (A == 0); fH = 0; fC = 0; fN = 0; }
void cp8(u8 v)  { u8 r = A; _sub8(v, 0); A = r; }

u8 inc8(u8 v) {
  u8 r = v + 1;
  fZ = (r == 0); fH = ((v & 0x0f) + 1 > 0x0f); fN = 0;
  return r;
}

u8 dec8(u8 v) {
  u8 r = v - 1;
  fZ = (r == 0); fH = ((v & 0x0f) == 0); fN = 1;
  return r;
}

void cpl() { A = (A ^ 0xff) & 0xff; fH = 1; fN = 1; }
void ccf() { fC = !fC; fH = 0; fN = 0; } // complement carry flag
void scf() { fH = 0; fN = 0; fC = 1; }
void daa() {
  u8 a = A; u8 adj = fC ? 0x60 : 0x00;
  if (fH) adj |= 0x06;
  if (!fN) {
    if ((a & 0x0f) > 0x09) adj |= 0x06;
    if (a > 0x99) adj |= 0x60;
    a += adj;
  } else a -= adj;
  fC = (adj >= 0x60);
  fH = 0; fZ = (a == 0);
  A = a;
}

// 16 bit alu ops
void add16hl(u16 v) {
  fH = ((HL & 0x07ff) + (v & 0x07ff) > 0x07ff);
  fN = 0; fC = (HL > (0xffff - v));
  HL += v;
}

u16 add16sp(s8 v) {
  fN = 0; fZ = 0;
  fH = ((SP & 0x000f) + (v & 0x000f) > 0x000f);
  fC = ((SP & 0x00ff) + (((u16)((s16)v)) & 0x00ff) > 0x00ff); //?
  return SP + v;
}

// jumps
void jr()  { PC += (s8)(f8()); } // jump relative
void jp()  { PC = r16(PC); } // jump absolute
void jphl(){ PC = HL; } // jp [hl]

void call(){ push16(PC+2); PC=r16(PC); } // call
void ret() { PC = pop16(); } // return
void rst(u8 v) { push16(PC); PC = (u16)(v); }

// bitwise ops
u8 rlc(u8 v) { // rotate left with carry
  u8 c = ((v >> 7) == 0x01); // carry if bit 7 set
  u8 r = (v << 1) | c; // shift and carry previous bit 7 into 0
  fH = 0; fN = 0; fZ = (r == 0); fC = c;
  return r;
}

u8 rrc(u8 v) { // rotate right with carry
  u8 c = (v & 0x01); // carry if bit 0 set
  u8 r = (v >> 1) | (c << 7); // shift and carry previous bit 0 into 7
  fH = 0; fN = 0; fZ = (r == 0); fC = c;
  return r;
}

u8 rl(u8 v) { // rotate left
  u8 c = ((v >> 7) == 0x01); // carry if bit 7 set
  u8 r = (0xff & (v << 1)) | fC;      // shift and carry from flags into 0
  if (r==0) {fZ = 1;} else {fZ = 0;}
  fH = 0; fN = 0; fC = c;
  return r;
}

u8 rr(u8 v) { // rotate right
  u8 c = (v & 0x01); // carry if bit 0 set
  u8 r = (v >> 1) | (fC << 7); // shift and carry from flags into 7
  fH = 0; fN = 0; fZ = (r == 0); fC = c;
  return r;
}

u8 sla(u8 v) { // shift left arithmetic
  u8 c = (v >> 7) & 0x1; // if bit 7 set
  u8 r = (v << 1);
  fH = 0; fN = 0; fZ = (r == 0); fC = c;
  return r;
}

u8 sra(u8 v) { //shift right arithmetic
  u8 c = (v & 0x1); //if bit 0 set
  u8 r = (v >> 1) | (v & 0x80); // shift and extend sign
  fH = 0; fN = 0; fZ = (r == 0); fC = c;
  return r;
}

u8 srl(u8 v) { //shift right logical
  u8 c = (v & 0x1); //if bit 0 set
  u8 r = (v >> 1); // shift
  fH = 0; fN = 0; fZ = (r == 0); fC = c;
  return r;
}

u8 swap(u8 v) {
  fZ = (v==0); fC=0; fN=0; fH=0;
  return ( (v >> 4) | (v << 4) );
}

void bitchk(u8 n, u8 v) {
  u8 r = ((v >> n) & 0x1) == 0;
  //fZ = 0 of bit was 1
  fN = 0; fH = 1; fZ = r;
}

// ops on accumulator (non-CB)
void rlca() { A = rlc(A); fZ = 0;}
void rrca() { A = rrc(A); fZ = 0;}
void rla()  { A = rl(A);  fZ = 0;}
void rra()  { A = rr(A);  fZ = 0;}

extern const char* disasm[256];
extern void print_regs();

u8 read_reg(u8 idx)
{
  switch(idx){
    case 0: return B; 
    case 1: return C;
    case 2: return D; 
    case 3: return E;
    case 4: return H; 
    case 5: return L;
    case 7: return A;
    default: return 0;
  }
}

u8 write_reg(u8 idx, u8 v)
{
  switch(idx){
    case 0: return B=v; 
    case 1: return C=v;
    case 2: return D=v; 
    case 3: return E=v;
    case 4: return H=v; 
    case 5: return L=v;
    case 7: return A=v;
    default: return 0;
  }
}

void cb_ex(u8 x) { // cb extension

  u8 src_idx = x & 0x7; //last 3 bits are reg#
  u8 src = src_idx == 6 ? r8(HL) : read_reg(src_idx);
  u8 op_group = (x >> 6) & 0x03;
  u8 n = (x >> 3) & 0x07;
  u8 res = src;

  switch (op_group) {
    case 0:  // opcode == 00xxxyyy
      switch (n) { // subgroup, bits xxNNNyyy
        case 0: res = rlc(src);  break; // 00000yyy
        case 1: res = rrc(src);  break; // 00001yyy
        case 2: res = rl(src);   break; // 00010yyy
        case 3: res = rr(src);   break; // 00011yyy
        case 4: res = sla(src);  break; // 00100yyy
        case 5: res = sra(src);  break; // 00101yyy
        case 6: res = swap(src); break; // 00110yyy
        case 7: res = srl(src);  break; // 00111yyy
      }; break;
    case 1:  // opcode == 01xxxyyy, test bit n
      bitchk(n, src); break;
    case 2:  // opcode == 10xxxyyy, clear bit n
      res &= ~(1<<n); break;
    case 3:  // opcode == 11xxxyyy, set bit n
      res |= (1<<n);  break;
  }

  if (src_idx != 6)  write_reg(src_idx, res); else w8(HL, res);
  u8 mcycl = (src_idx == 6)  ? 16 : 8; // 16 cycles if hl
  cpu_ticks += mcycl;
}

// 8-bit ld group (85 opcodes)
// 0x02, 0x12, 0x22, 0x32, 0x0a, 0x1a, 0x2a, 0x3a
void x02() { w8(BC,A);     }
void x0a() { A = r8(BC);   }
void x12() { w8(DE,A);     }
void x1a() { A = r8(DE);   }
void x22() { w8(HL++,A);   }
void x2a() { A = r8(HL++); }
void x32() { w8(HL--,A);   }
void x3a() { A = r8(HL--); }
// 0xe0, 0xe2, 0xf0, 0xf2, 0xea, 0xfa
void xe0() { w8(0xff00 | f8(), A); }
void xe2() { w8(0xff00 | C, A);        }
void xea() { w8(f16(), A);             }
void xf0() { A = r8(0xff00 | f8());    }
void xf2() { A = r8(0xff00 | C);       }
void xfa() { A = r8(f16());            }

void ldrr() {
  u8 src_idx = op & 0x7; //last 3 bits are reg#
  u8 dst_idx = (op >> 3) & 0x7; //last 3 bits are reg#
  u8 src = (((op >> 6) & 0x3) == 0) ? f8() : src_idx == 6 ? r8(HL) : read_reg(src_idx);
  if (dst_idx != 6) write_reg(dst_idx, src); else w8(HL, src);
}

// 16-bit ld group
// pop/push
void xc1() { BC = pop16(); }
void xd1() { DE = pop16(); }
void xe1() { HL = pop16(); }
void xf1() { AF = pop16() & 0xfff0; }
void xc5() { push16(BC); }
void xd5() { push16(DE); }
void xe5() { push16(HL); }
void xf5() { push16(AF); }

// load 16-bit imm
void x01() { BC = r16(PC); PC+=2; }
void x11() { DE = r16(PC); PC+=2; }
void x21() { HL = r16(PC); PC+=2; }
void x31() { SP = r16(PC); PC+=2; }
void x08() { u16 a = r16(PC); w16(a, SP); PC+=2; } // LD (a16), SP
void xf9() { SP = HL; }

// 8-bit alu subgroup
void alu() {
  u8 src_idx = op & 0x7; //last 3 bits are reg#
  u8 src = (((op >> 6) & 0x3) == 3) ? f8() : src_idx == 6 ? r8(HL) : read_reg(src_idx);
  u8 n = (op >> 3) & 0x07;

  switch (n) { // subgroup, bits xxNNNyyy
    case 0: add8(src); break; // 00000yyy
    case 1: adc8(src); break; // 00001yyy
    case 2: sub8(src); break; // 00010yyy
    case 3: sbc8(src); break; // 00011yyy
    case 4: and8(src); break; // 00100yyy
    case 5: xor8(src); break; // 00101yyy
    case 6: or8(src);  break; // 00110yyy
    case 7: cp8(src);  break; // 00111yyy
  };
}

void incdec() {
  u8 n = op & 0x3; //dec/inc
  u8 dst_idx = (op >> 3) & 0x7; //last 3 bits are reg#
  u8 src = dst_idx == 6 ? r8(HL) : read_reg(dst_idx);
  src = n ? dec8(src) : inc8(src);
  if (dst_idx != 6) write_reg(dst_idx, src); else w8(HL, src);
}

// misc alu
void x27() { daa(); }
void x37() { scf(); }
void x2f() { cpl(); }
void x3f() { ccf(); }

// 16-bit alu
// inc 16
void x03() { BC++; }
void x13() { DE++; }
void x23() { HL++; }
void x33() { SP++; }
// dec 16
void x0b() { BC--; }
void x1b() { DE--; }
void x2b() { HL--; }
void x3b() { SP--; }
// add 16 to hl
void x09() { add16hl(BC); }
void x19() { add16hl(DE); }
void x29() { add16hl(HL); }
void x39() { add16hl(SP); }
// add to sp
void xe8() { SP = add16sp((s8)(f8()));   } // SP = SP + s8
void xf8() { HL = add16sp((s8)(f8()));   } // LD = SP + s8

//////////////////////
// JMP/RET/CALL/RST
void x20() { if (!fZ) jr(); else { cpu_ticks -= 4; PC+=1;} } // jr nz,s8
void x30() { if (!fC) jr(); else { cpu_ticks -= 4; PC+=1;} } // jr nc,s8
void x28() { if (fZ)  jr(); else { cpu_ticks -= 4; PC+=1;} } // jr z, s8
void x38() { if (fC)  jr(); else { cpu_ticks -= 4; PC+=1;} } // jr c, s8
void x18() { jr(); } // jr s8

void xc2() { if (!fZ) jp(); else { cpu_ticks -= 4; PC+=2;} } // jp nz, imm16
void xd2() { if (!fC) jp(); else { cpu_ticks -= 4; PC+=2;} } // jp nc, imm16
void xca() { if (fZ)  jp(); else { cpu_ticks -= 4; PC+=2;} } // jp z, imm16
void xda() { if (fC)  jp(); else { cpu_ticks -= 4; PC+=2;} } // jp c, imm16
void xc3() { jp(); } // jp imm16
void xe9() { jphl(); }; // jp [hl]

void xc4() { if (!fZ) call(); else { cpu_ticks -= 12; PC+=2;} } // call nz imm16
void xd4() { if (!fC) call(); else { cpu_ticks -= 12; PC+=2;} } // call nc imm16
void xcc() { if (fZ)  call(); else { cpu_ticks -= 12; PC+=2;} } // call z imm16
void xdc() { if (fC)  call(); else { cpu_ticks -= 12; PC+=2;} } // call c imm16
void xcd() { call(); } // call imm16

void xc0() { if (!fZ) ret(); else cpu_ticks -= 12; } // ret nz
void xd0() { if (!fC) ret(); else cpu_ticks -= 12; } // ret nc
void xc8() { if (fZ)  ret(); else cpu_ticks -= 12; } // ret z
void xd8() { if (fC)  ret(); else cpu_ticks -= 12; } // ret c
void xc9() { ret(); } // ret

void reti() { ei(); ret(); } // reti

void xc7() { rst(0x00); }
void xcf() { rst(0x08); }
void xd7() { rst(0x10); }
void xdf() { rst(0x18); }
void xe7() { rst(0x20); }
void xef() { rst(0x28); }
void xf7() { rst(0x30); }
void xff() { rst(0x38); }

// bitwise
void x07() { rlca(); }
void x17() { rla();  }
void x0f() { rrca(); }
void x1f() { rra();  }

// prefix cb
void xcb() { cb_ex(f8()); }


void reset() {
  unimpl=0;
  total_cpu_ticks=0;
  if (BOOTROM) {
    AF=0; BC=0; DE=0;
    HL=0; SP=0; PC=0;
    REG_BOOTROM=0x00;
  } else {
    // state after bootrom
    AF=0x01b0; BC=0x0013;
    DE=0x00d8; HL=0x014d;
    SP=0xfffe; PC=0x0100;
    if (GBC) A=0x11;
    REG_LCDC = 0x91; // lcdc register
    REG_BOOTROM = 0x01; // bootrom off
    REG_TIM_TIMA = 0x0; REG_TIM_TMA = 0x0;
    REG_TIM_TAC = 0x0;
    // init sound regs
    ///////////
    REG_BGRDPAL = 0xfc;
    REG_OBJPAL0 = 0xff;
    REG_OBJPAL0 = 0xff;
  }
}


void exec(u8 op) {
  switch(op){

  // misc (1)
    case 0x00: return nop();
    case 0x10: return stop();
    case 0xf3: return di();   // interrupt disable
    case 0xfb: return ei();   // interrupt enable
    case 0xcb: return xcb();  // prefix cb
    case 0xd9: return reti();

    // ld8 group
    case 0x76: return halt();

    // 06, 0e, 16, 1e, 26, 2e, 36, 3e: load 8 imm
    case 0x06: return ldrr(); case 0x0e: return ldrr();
    case 0x16: return ldrr(); case 0x1e: return ldrr();
    case 0x26: return ldrr(); case 0x2e: return ldrr();
    case 0x36: return ldrr(); case 0x3e: return ldrr();

    // 0x02, 0x12, 0x22, 0x32, 0x0a, 0x1a, 0x2a, 0x3a: load 8bit indirect
    case 0x02: return x02(); case 0x12: return x12();
    case 0x22: return x22(); case 0x32: return x32();
    case 0x0a: return x0a(); case 0x1a: return x1a();
    case 0x2a: return x2a(); case 0x3a: return x3a();

    // 0xe0, 0xe2, 0xf0, 0xf2, 0xea, 0xfa
    case 0xe0: return xe0(); case 0xe2: return xe2();
    case 0xea: return xea(); case 0xf0: return xf0();
    case 0xf2: return xf2(); case 0xfa: return xfa();

    case 0xc6: return alu(); case 0xce: return alu();
    case 0xd6: return alu(); case 0xde: return alu();
    case 0xe6: return alu(); case 0xee: return alu();
    case 0xf6: return alu(); case 0xfe: return alu();

    case 0x04: return incdec(); case 0x05: return incdec(); case 0x0c: return incdec(); case 0x0d: return incdec();
    case 0x14: return incdec(); case 0x15: return incdec(); case 0x1c: return incdec(); case 0x1d: return incdec();
    case 0x24: return incdec(); case 0x25: return incdec(); case 0x2c: return incdec(); case 0x2d: return incdec();
    case 0x34: return incdec(); case 0x35: return incdec(); case 0x3c: return incdec(); case 0x3d: return incdec();

    // misc alu
    case 0x27: return x27(); // daa
    case 0x37: return x37(); // scf
    case 0x2f: return x2f(); // cpl
    case 0x3f: return x3f(); // ccf

  // 16-bit ld
    case 0x01: return x01(); case 0x11: return x11();
    case 0x21: return x21(); case 0x31: return x31();
    //16-bit pop
    case 0xc1: return xc1(); case 0xd1: return xd1();
    case 0xe1: return xe1(); case 0xf1: return xf1();
    //16-bit push
    case 0xc5: return xc5(); case 0xd5: return xd5();
    case 0xe5: return xe5(); case 0xf5: return xf5();
    case 0x08: return x08(); case 0xf9: return xf9();

  // 16-bit ALU
  // inc
    case 0x03: return x03(); case 0x13: return x13();
    case 0x23: return x23(); case 0x33: return x33();
  // dec
    case 0x0b: return x0b(); case 0x1b: return x1b();
    case 0x2b: return x2b(); case 0x3b: return x3b();
  // add 16
    case 0x09: return x09(); case 0x19: return x19();
    case 0x29: return x29(); case 0x39: return x39();
    case 0xe8: return xe8(); case 0xf8: return xf8();
  // end 16-bit ALU

  // jmp
    case 0x20: return x20(); case 0x30: return x30();
    case 0x18: return x18(); case 0x28: return x28();
    case 0xc2: return xc2(); case 0xd2: return xd2();
    case 0xc3: return xc3(); case 0x38: return x38();
    case 0xca: return xca(); //case 0x3a: return x3a();
    case 0xe9: return xe9();
  // ret
    case 0xc0: return xc0(); case 0xd0: return xd0();
    case 0xc8: return xc8(); case 0xd8: return xd8();
    case 0xc9: return xc9();
  // calls
    case 0xc4: return xc4(); case 0xd4: return xd4();
    case 0xcc: return xcc(); case 0xdc: return xdc();
    case 0xcd: return xcd();
  //resets
    case 0xc7: return xc7(); case 0xd7: return xd7();
    case 0xe7: return xe7(); case 0xf7: return xf7();
    case 0xcf: return xcf(); case 0xdf: return xdf();
    case 0xef: return xef(); case 0xff: return xff();
  //bitwise
    case 0x07: return x07(); case 0x17: return x17();
    case 0x0f: return x0f(); case 0x1f: return x1f();
    case 0xda: return xda();

    default:
      if(op >= 0x40 && op < 0x80) return ldrr();
      if(op >= 0x80 && op < 0xc0) return alu();
      return na();
  }
}

void cpu_step(u32 count) {
  cpu_ticks = 0;
  for (u32 i=0; i<count; i++) {
    op = r8(PC++);
    exec(op);
    cpu_ticks += mcycles[op];
  }
}
//gpu regs
u8 gpu_hblanking = 0;
u8 gpu_mode=0; // 0 - hblank, 1 - vblank, 2 - scanline oam, 3 - scanline vram
u32 gpu_mode_clk=0;

u8 pix[2][160*144*3]; // screen
u8 bgprio[160];

void gpu_draw_bg() {

  u8 gpu_win_on = (REG_LCDC >> 5) & 0x1;//off,on
  u8 lcd_on     = (REG_LCDC >> 7) & 0x1;//off,on

  if (lcd_on) {
    u8 bgy = (REG_SCANLINE+REG_SCY);
    u16 bgtiley = (((u16)bgy) >> 3) & 31;
    s32 winy = gpu_win_on ? (REG_SCANLINE) - (REG_WINY) : -1;
    u16 wintiley = (((u16)winy) >> 3) & 31;
    for (u8 x=0; x<160; x++) {
      u32 bgx = (u32)REG_SCX + (u32)x;
      s32 winx = -(((s32)REG_WINX) - 7) + x;
      u16 tilemapbase;
      u16 tilex, tiley, pixelx, pixely;
      u8 gpu_bgmap =   (REG_LCDC >> 3) & 0x1;//9800-9bff, 9c00-9fff
      u8 gpu_tilemap = (REG_LCDC >> 4) & 0x1;//8800-97ff, 8000-8fff
      u8 gpu_drawbg = (REG_LCDC >> 0) & 0x1;//off,on
      u16 tilebase = (gpu_tilemap ? 0x8000 : 0x8800);
      if (winx >= 0 && winy >=0 ) { // draw window
        u8 gpu_win_map=(REG_LCDC >> 6) & 0x1;//9800-9bff, 9c00-9fff
        tilemapbase=gpu_win_map ? 0x9c00 : 0x9800;
        tiley=wintiley; tilex=(((u16)winx) >> 3); pixely=((u16)winy) & 0x7; pixelx=((u8)winx) & 0x7;
      } else if (gpu_drawbg) { // draw bg
        tilemapbase=gpu_bgmap ? 0x9c00 : 0x9800;
        tiley=bgtiley; tilex=(((u16)bgx) >> 3) & 31; pixely=((u16)bgy) & 0x7; pixelx=((u8)bgx) & 0x7;
      } else {};

      u8 _tilenr = r8(tilemapbase + tiley * 32 + tilex);
      u16 tilenr, tileaddress;
      if (tilebase == 0x8800) {
        int8_t nr_s = (s8)_tilenr; s16 nr_s16 = (s16)nr_s + 128; tilenr = (u16)nr_s16;
      }
      else { tilenr = (u16)_tilenr; }
      tileaddress = tilenr * 16 + tilebase;

      u16 a0 = tileaddress + pixely*2;
      u8 data0 = r8(a0);
      u8 data1 = r8(a0+1);
      u8 color0_idx = ((data0 >> (7-pixelx)) & 0x1);
      u8 color1_idx = ((data1 >> (7-pixelx)) & 0x1);
      u8 color_idx = color0_idx + color1_idx*2;
      u8 r,g,b; u8 color = (REG_BGRDPAL>>(color_idx*2))&0x3;
      bgprio[x] = color_idx;
      if (color == 0) {r=255; g=255; b=255;}
      if (color == 1) {r=192; g=192; b=192;}
      if (color == 2) {r=96; g=96; b=96;}
      if (color == 3) {r=0; g=0; b=0;}

      u32 screen_off = ((u32)REG_SCANLINE)*160*3 + x*3;
      if (screen_off < 144*160*3) {
        pix[!buffer][screen_off+0] = r;
        pix[!buffer][screen_off+1] = g;
        pix[!buffer][screen_off+2] = b;
      }
    }
  } else { } // lcd off
}

void gpu_draw_sprites() {

  u8 gpu_sprite_on = (REG_LCDC >> 1) & 0x1;
  u8 gpu_sprite_size = (REG_LCDC >> 2) & 0x1 ? 16 : 8;//8x8, 8x16
  if (gpu_sprite_on) {
    for (u8 idx=0; idx<40; idx++) {
      u8 i=39-idx;
      u16 spriteaddr=0xfe00 + ((s16)i) * 4;
      s32 spritey = ((s32)((u16)r8(spriteaddr+0)))-16;
      s32 spritex = ((s32)((u16)r8(spriteaddr+1)))-8;
      u16 tilenum = r8(spriteaddr+2);
      if (gpu_sprite_size == 8) tilenum &= 0xff; else tilenum &= 0xfe;
      u8 flags = r8(spriteaddr+3);
      u8 usepal1 = (flags >> 4) & 0x1;
      u8 xflip = (flags >> 5) & 0x1;
      u8 yflip = (flags >> 6) & 0x1;
      u8 belowbg = (flags >> 7) & 0x1;
      u8 c_palnr = flags & 0x7;
      //u8 c_vram1 = (flags >> 3) & 0x1;

      u8 line = REG_SCANLINE;
      if (line < spritey || line >= (spritey + gpu_sprite_size)) { continue; }
      if (spritex < -7 || spritex >= (160)) { continue; }

      u16 tiley;
      tiley = yflip ? (gpu_sprite_size-1-(line-spritey)) : (line-spritey);
      u16 tileaddress = 0x8000 + tilenum*16 + tiley*2;
      u16 b0 = tileaddress;
      u16 b1 = tileaddress+1;
      u8 data0 = r8(b0);
      u8 data1 = r8(b1);

      u32 screen_off = line*160*3;

      for (u8 x=0; x<8; x++) {
        if (((spritex+x)<0) || ((spritex+x) >= 160)) continue;
        if (belowbg && bgprio[(spritex+x)] != 0) continue;
        u8 off = xflip ? x : (7-x);
        u8 pal = usepal1 ? REG_OBJPAL1 : REG_OBJPAL0;
        u8 color0_idx = ((data0 >> (off)) & 0x1);
        u8 color1_idx = ((data1 >> (off)) & 0x1);
        u8 color_idx = color0_idx + color1_idx*2;
        if (color_idx == 0) continue;
        u8 r,g,b;
        u8 color = (pal>>(color_idx*2))&0x3;
        if (color == 0) {r=255; g=255; b=255;}
        if (color == 1) {r=192; g=192; b=192;}
        if (color == 2) {r=96; g=96; b=96;}
        if (color == 3) {r=0; g=0; b=0;}
        pix[!buffer][screen_off+3*(spritex+x)+0] = r;
        pix[!buffer][screen_off+3*(spritex+x)+1] = g;
        pix[!buffer][screen_off+3*(spritex+x)+2] = b;
      }
    }
  }
}

void gpu_renderscan() {
  gpu_draw_bg();
  gpu_draw_sprites();
}

//mode = 0, 00, display ram can be accessed. cpu ok
//       1, 01 vblank, cpu access ok
//       2, 10 during oam-ram, cpu not ok
//       3, 11,during transferring data to lcd, cpu not ok
void gpu_change_mode(u8 new_mode) {
  gpu_mode = new_mode;
  REG_LCDSTAT &= ~(0x3); REG_LCDSTAT |= gpu_mode & 0x3;
  REG_LCDSTAT &= ~(0x4); REG_LCDSTAT |= ((REG_SCANLINE == REG_LYC) << 2);
  //if (REG_LCDSTAT & 0x4) printf("lyc\n");
  u8 irq = 1;
  u8 m0e = (REG_LCDSTAT >> 3) & 0x1; //hblank int
  u8 m1e = (REG_LCDSTAT >> 4) & 0x1; //vblank int
  u8 m2e = (REG_LCDSTAT >> 5) & 0x1; //oam int
  switch (gpu_mode) {
    case 0: irq &= m0e; if (renderscan) gpu_renderscan(); gpu_hblanking = 1; break;
    case 1: irq &= m1e; REG_INTF |= 0x01; break;
    case 2: irq &= m2e; /*oam_ram();*/ break;
    case 3: irq = 0; break;
  }
  if (irq) REG_INTF |= 0x2;
}

void check_interrupt_lyc() {
  u8 lyc_inte = (REG_LCDSTAT >> 6) & 0x1;
  if (lyc_inte && (REG_SCANLINE == REG_LYC )) REG_INTF |= 0x02;
}

u32 frame_no = 0;
u8 buffer=0;
u8 new_frame=0;

void blit() { buffer ^= 1; new_frame = 1; frame_no++; }

void gpu_step() {
  u8 lcd_on = (REG_LCDC & 0x80);
  gpu_hblanking = 0;
  u32 gpu_ticks_left = cpu_ticks;

  gpu_mode_clk += gpu_ticks_left;

  if (gpu_mode_clk >= 456) {
    gpu_mode_clk -= 456;
    REG_SCANLINE = (REG_SCANLINE + 1) % 154;
    check_interrupt_lyc();
    if (REG_SCANLINE >= 144 && gpu_mode != 1) { gpu_change_mode(1); blit(); } // vblank
  }
  if (REG_SCANLINE < 144) { // normal line
    if (gpu_mode_clk <= 80) {
      if (gpu_mode != 2) gpu_change_mode(2);
    } else if (gpu_mode_clk <= (80 + 172)) { // 252 cycles
        if (gpu_mode != 3) { gpu_change_mode(3); }
    } else { // remaining 204
      if (gpu_mode != 0) { gpu_change_mode(0); }
    }
  }
}

keyboard keys;

u32 timer_internal_div=0;
u32 timer_internal_cnt=0;

void timer_step() {
  timer_internal_div += cpu_ticks;
  while (timer_internal_div >= 256) {
    REG_TIM_DIV++;
    timer_internal_div -= 256;
  }

  u16 _tt = 256; // ticks threshold

  if (REG_TIM_TAC & 0x4) { // if on
    switch (REG_TIM_TAC & 0x3) {
      case 0: _tt = 1024; break; //  4k
      case 1: _tt =  16; break; // 256k
      case 2: _tt =  64; break; // 64k
      case 3: _tt = 256; break; // 16k
    }
    timer_internal_cnt += cpu_ticks;

    while (timer_internal_cnt >= _tt) {
      // actual step
      REG_TIM_TIMA++;
      if (REG_TIM_TIMA == 0) {
        REG_TIM_TIMA = REG_TIM_TMA;
        REG_INTF |= 0x4; // interrupt
      }
      timer_internal_cnt -= _tt;
    }
  }

}

void interrupts() {

  u8 joydata = (~r8(0xff00)) & 0xf0;
  u8 val = 0;
  if ((joydata >> 4) & 0x2) val = (0x20) | ((keys.a << 0) | (keys.b << 1) | (keys.select << 2) | (keys.start << 3));
  if ((joydata >> 4) & 0x1) val = (0x10) | ((keys.right << 0) | (keys.left << 1) | (keys.up << 2) | (keys.down << 3));
  if (val) REG_INTF |= 0x10;
  w8(0xff00, ~(val));

  if (irq_en==1) {
    u8 trig = REG_INTE & REG_INTF;
    if (trig) {
      irq_en = 0;
      halted=0;
      if (trig & 0x1)  // vblank
        { irq_en = 0; REG_INTF &= ~0x1; rst(0x40); }
       else if (trig & 0x2) { // lcdstat
        irq_en = 0; REG_INTF &= ~0x2; rst(0x48); }
       else if (trig & 0x4) { // timer
        irq_en = 0; REG_INTF &= ~0x4; rst(0x50); }
       else if (trig & 0x10) { //  joypad
        irq_en = 0; REG_INTF &= ~0x10; rst(0x60); }
     }
  }

  if (enable_int == 1) { irq_en = 1; enable_int = 0; }
  if (disable_int == 1) { irq_en = 0; disable_int = 0; }
}

u8 counter=0;
u8 key_turbo=0;
u8 key_reset=0;

static void step() {

  if (key_reset == 1) { reset(); key_reset = 0; }

  cpu_step(1);
  gpu_step();
  total_cpu_ticks += cpu_ticks;
  total_gpu_ticks += gpu_ticks;
  timer_step();
  interrupts();
}

void next_frame() {
  while (!new_frame) step();
  new_frame=0;
}

void next_frame_skip(u8 skip) {
  renderscan=0;
  for (u8 i=0; i<skip; i++) {
    if (i==(skip-1)) renderscan=1; // render only the last frame needed
    while (!new_frame) step(); new_frame=0;
  }
}

void set_keys(u8 k) { 
  keys.keys_packed = k; 
}

/*
void dump_state(const char* fname) {
  FILE *fp = fopen(fname, "wb");
  fwrite(g.regs, 2, 6, fp);
  fwrite(ram, 1, 0x2000, fp);
  fwrite(vram, 1, 0x2000, fp);
  fwrite(oam, 1, 0x100, fp);
  fwrite(hram, 1, 0x100, fp);
  fclose(fp);
}

void restore_state(const char* fname) {
  FILE *fp = fopen(fname, "rb");
  int status;
  status=fread(g.regs, 2, 6, fp);
  status=fread(ram, 1, 0x2000, fp);
  status=fread(vram, 1, 0x2000, fp);
  status=fread(oam, 1, 0x100, fp);
  status=fread(hram, 1, 0x100, fp);
  fclose(fp);
}
*/


u32 interface_count = 0; 
void interface(u8 cmd, u8 data, u8* ptr, u32* result)
{
#pragma HLS INTERFACE s_axilite     port=return bundle=control_bus
#pragma HLS INTERFACE s_axilite     port=cmd    bundle=control_bus
#pragma HLS INTERFACE s_axilite     port=data   bundle=control_bus
#pragma HLS INTERFACE s_axilite     port=ptr    bundle=control_bus
#pragma HLS INTERFACE s_axilite     port=result bundle=control_bus
#pragma HLS INTERFACE m_axi depth=8 port=ptr    bundle=data_bus

  interface_count++;

  switch(cmd)
  {
    case 1: 
      memcpy(cart, ptr, sizeof(cart));  
      *result = cmd;
      break;
    case 2: 
      memcpy(ptr, pix[buffer], sizeof(pix[buffer])); 
      *result = cmd;
      break; 
    case 3: 
      reset(); 
      *result = cmd;
      break; 
    case 4: 
      next_frame_skip(data); 
      *result = cmd;
      break; 
    case 5: 
      set_keys(data); 
      *result = cmd;
      break; 
    default: 
      *result = interface_count;
      break;
  }
}

