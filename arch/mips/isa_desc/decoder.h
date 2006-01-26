////////////////////////////////////////////////////////////////////
//
// The actual MIPS32 ISA decoder
// -----------------------------
// The following instructions are specified in the MIPS32 ISA
// Specification. Decoding closely follows the style specified
// in the MIPS32 ISAthe specification document starting with Table
// A-2 (document available @ www.mips.com)
//
//@todo: Distinguish "unknown/future" use insts from "reserved"
// ones
decode OPCODE_HI default FailUnimpl::unknown() {

    // Derived From ... Table A-2 MIPS32 ISA Manual
    0x0: decode OPCODE_LO default FailUnimpl::reserved(){

        0x0: decode FUNCTION_HI {
            0x0: decode FUNCTION_LO {
              0x1: decode MOVCI {
                format Move {
                  0: movc({{ }});
                  1: movt({{ }});
                }
              }

              format ShiftRotate {
                //Table A-3 Note: "1. Specific encodings of the rt, rd, and sa fields
                //are used to distinguish among the SLL, NOP, SSNOP and EHB functions."
                0x0: sll({{ }});

                0x2: decode SRL {
                   0: srl({{ }});
                   1: rotr({{ }});
                 }

                 0x3: sar({{ }});

                 0x4: sllv({{ }});

                 0x6: decode SRLV {
                   0: srlv({{ }});
                   1: rotrv({{ }});
                 }

                 0x7: srav({{ }});
              }
            }

            0x1: decode FUNCTION_LO {

              //Table A-3 Note: "Specific encodings of the hint field are used
              //to distinguish JR from JR.HB and JALR from JALR.HB"
              format Jump {
                0x0: jr({{ }});
                0x1: jalr({{ }});
              }

              format Move {
                0x2: movz({{ }});
                0x3: movn({{ }});
              }

              0x4: Syscall::syscall({{ }});
              0x5: Break::break({{ }});
              0x7: Synchronize::synch({{ }});
            }

            0x2: decode FUNCTION_LO {
              format MultDiv {
                0x0: mfhi({{ }});
                0x1: mthi({{ }});
                0x2: mflo({{ }});
                0x3: mtlo({{ }});
              }
            };

            0x3: decode FUNCTION_LO {
              format MultDiv {
                0x0: mult({{ }});
                0x1: multu({{ }});
                0x2: div({{ }});
                0x3: divu({{ }});
              }
            };

            0x4: decode FUNCTION_LO {
              format Arithmetic {
                0x0: add({{ }});
                0x1: addu({{ }});
                0x2: sub({{ }});
                0x3: subu({{ }});
              }

              format Logical {
                0x0: and({{ }});
                0x1: or({{ }});
                0x2: xor({{ }});
                0x3: nor({{ }});
              }
            }

            0x5: decode FUNCTION_LO {
              format SetInstructions{
                0x2: slt({{ }});
                0x3: sltu({{ }});
              }
            };

            0x6: decode FUNCTION_LO {
              format Trap {
                 0x0: tge({{ }});
                 0x1: tgeu({{ }});
                 0x2: tlt({{ }});
                 0x3: tltu({{ }});
                 0x4: teq({{ }});
                 0x6: tne({{ }});
              }
            }
        }

        0x1: decode REGIMM_HI {
            0x0: decode REGIMM_LO {
              format Branch {
                0x0: bltz({{ }});
                0x1: bgez({{ }});

                //MIPS obsolete instructions
                0x2: bltzl({{ }});
                0x3: bgezl({{ }});
              }
            }

            0x1: decode REGIMM_LO {
              format Trap {
                 0x0: tgei({{ }});
                 0x1: tgeiu({{ }});
                 0x2: tlti({{ }});
                 0x3: tltiu({{ }});
                 0x4: teqi({{ }});
                 0x6: tnei({{ }});
              }
            }

            0x2: decode REGIMM_LO {
              format Branch {
                0x0: bltzal({{ }});
                0x1: bgezal({{ }});

                //MIPS obsolete instructions
                0x2: bltzall({{ }});
                0x3: bgezall({{ }});
              }
            }

            0x3: decode REGIMM_LO {
              0x7: synci({{ }});
            }
        }

        format Jump {
            0x2: j({{ }});
            0x3: jal({{ }});
        }

        format Branch {
            0x4: beq({{ }});
            0x5: bne({{ }});
            0x6: blez({{ }});
            0x7: bgtz({{ }});
        }
    };

    0x1: decode OPCODE_LO default FailUnimpl::reserved(){
        format IntImmediate {
            0x0: addi({{ }});
            0x1: addiu({{ }});
            0x2: slti({{ }});
            0x3: sltiu({{ }});
            0x4: andi({{ }});
            0x5: ori({{ }});
            0x6: xori({{ }});
            0x7: lui({{ }});
        };
    };

    0x2: decode OPCODE_LO default FailUnimpl::reserved(){

      //Table A-11 MIPS32 COP0 Encoding of rs Field
      0x0: decode RS_MSB {
        0x0: decode RS {
          0x0: mfc0({{ }});
          0xC: mtc0({{ }});
          0xA: rdpgpr({{ }});

          0xB: decode SC {
            0x0: di({{ }});
            0x1: ei({{ }});
          }

          0xE: wrpgpr({{ }});
        }

        //Table A-12 MIPS32 COP0 Encoding of Function Field When rs=CO
        0x1: decode FUNCTION {
          0x01: tlbr({{ }});
          0x02: tlbwi({{ }});
          0x06: tlbwr({{ }});
          0x08: tlbp({{ }});
          0x18: eret({{ }});
          0x1F: deret({{ }});
          0x20: wait({{ }});
        }
      }

      //Table A-13 MIPS32 COP1 Encoding of rs Field
      0x1: decode RS_MSB {

        0x0: decode RS_HI {
          0x0: decode RS_LO {
            0x0: mfc1({{ }});
            0x2: cfc1({{ }});
            0x3: mfhc1({{ }});
            0x4: mtc1({{ }});
            0x6: ctc1({{ }});
            0x7: mftc1({{ }});
          }

          0x1: decode ND {
            0x0: decode TF {
              0x0: bc1f({{ }});
              0x1: bc1t({{ }});
            }

            0x1: decode TF {
              0x0: bc1fl({{ }});
              0x1: bc1tl({{ }});
            }
          }
        }

        0x1: decode RS_HI {
          0x2: decode RS_LO {

            //Table A-14 MIPS32 COP1 Encoding of Function Field When rs=S
            //(( single-word ))
            0x0: decode RS_HI {
              0x0: decode RS_LO {
                0x0: add_fmt({{ }});
                0x1: sub_fmt({{ }});
                0x2: mul_fmt({{ }});
                0x3: div_fmt({{ }});
                0x4: sqrt_fmt({{ }});
                0x5: abs_fmt({{ }});
                0x6: mov_fmt({{ }});
                0x7: neg_fmt({{ }});
              }

              0x1: decode RS_LO {
                //only legal for 64 bit
                format mode64 {
                  0x0: round_l({{ }});
                  0x1: trunc_l({{ }});
                  0x2: ceil_l({{ }});
                  0x3: floor_l({{ }});
                }

                0x4: round_w({{ }});
                0x5: trunc_w({{ }});
                0x6: ceil_w({{ }});
                0x7: floor_w({{ }});
              }

              0x2: decode RS_LO {
                0x1: decode MOVCF {
                  0x0: movf_fmt({{ }});
                  0x1: movt_fmt({{ }});
                }

                0x2: movz({{ }});
                0x3: movn({{ }});

                format mode64 {
                  0x2: recip({{ }});
                  0x3: rsqrt{{ }});
                }
              }

              0x4: decode RS_LO {
                0x1: cvt_d({{ }});
                0x4: cvt_w({{ }});

                //only legal for 64 bit
                format mode64 {
                  0x5: cvt_l({{ }});
                  0x6: cvt_ps({{ }});
                }
              }
            }

            //Table A-15 MIPS32 COP1 Encoding of Function Field When rs=D
            0x1: decode RS_HI {
              0x0: decode RS_LO {
                0x0: add_fmt({{ }});
                0x1: sub_fmt({{ }});
                0x2: mul_fmt({{ }});
                0x3: div_fmt({{ }});
                0x4: sqrt_fmt({{ }});
                0x5: abs_fmt({{ }});
                0x6: mov_fmt({{ }});
                0x7: neg_fmt({{ }});
              }

              0x1: decode RS_LO {
                //only legal for 64 bit
                format mode64 {
                  0x0: round_l({{ }});
                  0x1: trunc_l({{ }});
                  0x2: ceil_l({{ }});
                  0x3: floor_l({{ }});
                }

                0x4: round_w({{ }});
                0x5: trunc_w({{ }});
                0x6: ceil_w({{ }});
                0x7: floor_w({{ }});
              }

              0x2: decode RS_LO {
                0x1: decode MOVCF {
                  0x0: movf_fmt({{ }});
                  0x1: movt_fmt({{ }});
                }

                0x2: movz({{ }});
                0x3: movn({{ }});

                format mode64 {
                  0x5: recip({{ }});
                  0x6: rsqrt{{ }});
                }
              }

              0x4: decode RS_LO {
                0x0: cvt_s({{ }});
                0x4: cvt_w({{ }});

                //only legal for 64 bit
                format mode64 {
                  0x5: cvt_l({{ }});
                }
              }
            }

            //Table A-16 MIPS32 COP1 Encoding of Function Field When rs=W
            0x4: decode FUNCTION {
              0x10: cvt_s({{ }});
              0x10: cvt_d({{ }});
            }

            //Table A-16 MIPS32 COP1 Encoding of Function Field When rs=L1
            //Note: "1. Format type L is legal only if 64-bit floating point operations
            //are enabled."
            0x5: decode FUNCTION_HI {
              0x10: cvt_s({{ }});
              0x11: cvt_d({{ }});
            }

            //Table A-17 MIPS64 COP1 Encoding of Function Field When rs=PS1
            //Note: "1. Format type PS is legal only if 64-bit floating point operations
            //are enabled. "
            0x6: decode RS_HI {
              0x0: decode RS_LO {
                0x0: add_fmt({{ }});
                0x1: sub_fmt({{ }});
                0x2: mul_fmt({{ }});
                0x5: abs_fmt({{ }});
                0x6: mov_fmt({{ }});
                0x7: neg_fmt({{ }});
              }

              0x2: decode RS_LO {
                0x1: decode MOVCF {
                  0x0: movf_fmt({{ }});
                  0x1: movt_fmt({{ }});
                }

                0x2: movz({{ }});
                0x3: movn({{ }});
              }

              0x4: decode RS_LO {
                0x0: cvt_s_pu({{ }});
              }

              0x5: decode RS_LO {
                0x0: cvt_s_pl({{ }});
                0x4: pll_s_pl({{ }});
                0x5: plu_s_pl({{ }});
                0x6: pul_s_pl({{ }});
                0x7: puu_s_pl({{ }});
              }
            }
      }

      //Table A-19 MIPS32 COP2 Encoding of rs Field
      0x2: decode RS_MSB {
        0x0: decode RS_HI {
          0x0: decode RS_LO {
            0x0: mfc2({{ }});
            0x2: cfc2({{ }});
            0x3: mfhc2({{ }});
            0x4: mtc2({{ }});
            0x6: ctc2({{ }});
            0x7: mftc2({{ }});
          }

          0x1: decode ND {
            0x0: decode TF {
              0x0: bc2f({{ }});
              0x1: bc2t({{ }});
            }

            0x1: decode TF {
              0x0: bc2fl({{ }});
              0x1: bc2tl({{ }});
            }
          }
        }
      }

      //Table A-20 MIPS64 COP1X Encoding of Function Field 1
      //Note: "COP1X instructions are legal only if 64-bit floating point
      //operations are enabled."
      0x3: decode FUNCTION_HI {
        0x0: decode FUNCTION_LO {
          0x0: lwxc1({{ }});
          0x1: ldxc1({{ }});
          0x5: luxc1({{ }});
        }

        0x1: decode FUNCTION_LO {
          0x0: swxc1({{ }});
          0x1: sdxc1({{ }});
          0x5: suxc1({{ }});
          0x7: prefx({{ }});
        }

        0x3: alnv_ps({{ }});

        0x4: decode FUNCTION_LO {
          0x0: madd_s({{ }});
          0x1: madd_d({{ }});
          0x6: madd_ps({{ }});
        }

        0x5: decode FUNCTION_LO {
          0x0: msub_s({{ }});
          0x1: msub_d({{ }});
          0x6: msub_ps({{ }});
        }

        0x6: decode FUNCTION_LO {
          0x0: nmadd_s({{ }});
          0x1: nmadd_d({{ }});
          0x6: nmadd_ps({{ }});
        }

        0x7: decode FUNCTION_LO {
          0x0: nmsub_s({{ }});
          0x1: nmsub_d({{ }});
          0x6: nmsub_ps({{ }});
        }
      }

      //MIPS obsolete instructions
      0x4: beql({{ }});
      0x5: bnel({{ }});
      0x6: blezl({{ }});
      0x7: bgtzl({{ }});
    };

    0x3: decode OPCODE_LO default FailUnimpl::reserved(){

        //Table A-5 MIPS32 SPECIAL2 Encoding of Function Field
        0x4: decode FUNCTION_HI {

            0x0: decode FUNCTION_LO {
              0x0: madd({{ }});
              0x1: maddu({{ }});
              0x2: mult({{ }});
              0x4: msub({{ }});
              0x5: msubu({{ }});
            }

            0x4: decode FUNCTION_LO {
              0x0: clz({{ }});
              0x1: clo({{ }});
            }

            0x7: decode FUNCTION_LO {
              0x7: sdbbp({{ }});
            }
        }

        //Table A-6 MIPS32 SPECIAL3 Encoding of Function Field for Release 2 of the Architecture
        0x7: decode FUNCTION_HI {

          0x0: decode FUNCTION_LO {
            0x1: ext({{ }});
            0x4: ins({{ }});
          }

          //Table A-10 MIPS32 BSHFL Encoding of sa Field
          0x4: decode SA {
            0x02: wsbh({{ }});
            0x10: seb({{ }});
            0x18: seh({{ }});
          }

          0x6: decode FUNCTION_LO {
            0x7: rdhwr({{ }});
          }
        }
    };

    0x4: decode OPCODE_LO default FailUnimpl::reserved(){
        format LoadMemory{
            0x0: lb({{ }});
            0x1: lh({{ }});
            0x2: lwl({{ }});
            0x3: lw({{ }});
            0x4: lbu({{ }});
            0x5: lhu({{ }});
            0x6: lhu({{ }});
        };

        0x7: FailUnimpl::reserved({{ }});
    };

    0x5: decode OPCODE_LO default FailUnimpl::reserved(){
        format StoreMemory{
            0x0: sb({{ }});
            0x1: sh({{ }});
            0x2: swl({{ }});
            0x3: sw({{ }});
            0x6: swr({{ }});
        };

        format FailUnimpl{
            0x4: reserved({{ }});
            0x5: reserved({{ }});
            0x7: cache({{ }});
        };

    };

    0x6: decode OPCODE_LO default FailUnimpl::reserved(){
        format LoadMemory{
            0x0: ll({{ }});
            0x1: lwc1({{ }});
            0x5: ldc1({{ }});
        };
    };

    0x7: decode OPCODE_LO default FailUnimpl::reserved(){
        format StoreMemory{
            0x0: sc({{ }});
            0x1: swc1({{ }});
            0x5: sdc1({{ }});
        };

    }
}


