// محاكي تعليمي لمعالج PS1 (MIPS R3000A)
// الترجمة: g++ -O2 -std=c++17 ps1cpu.cpp -o ps1cpu
// ملاحظة: لا يوجد GPU ولا صوت ولا BIOS ولا load delay slot (للتبسيط)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>

constexpr uint32_t RAM_SIZE = 2 * 1024 * 1024; // 2MB مثل PS1

struct CPU {
    uint32_t r[32] = {0};
    uint32_t hi = 0, lo = 0;
    uint32_t pc = 0x80010000;      // KSEG0: بداية البرنامج
    uint32_t next_pc = 0x80010004; // لدعم branch delay slot
    bool halted = false;
    std::vector<uint8_t> ram = std::vector<uint8_t>(RAM_SIZE, 0);

    // تحويل العنوان الافتراضي إلى فعلي (يزيل KSEG0/KSEG1)
    uint32_t phys(uint32_t a) const { return a & 0x1FFFFFFF; }

    bool inRam(uint32_t p, uint32_t n) const { return p + n <= RAM_SIZE; }

    uint32_t read(uint32_t addr, int size) {
        uint32_t p = phys(addr);
        if (!inRam(p, size)) { printf("قراءة خارج الذاكرة: %08x\n", addr); halted = true; return 0; }
        uint32_t v = 0;
        memcpy(&v, &ram[p], size); // little-endian مثل PS1
        return v;
    }

    void write(uint32_t addr, uint32_t val, int size) {
        uint32_t p = phys(addr);
        if (!inRam(p, size)) { printf("كتابة خارج الذاكرة: %08x\n", addr); halted = true; return; }
        memcpy(&ram[p], &val, size);
    }

    void load(const uint32_t* code, size_t n, uint32_t addr = 0x80010000) {
        memcpy(&ram[phys(addr)], code, n * 4);
        pc = addr;
        next_pc = addr + 4;
    }

    void syscall() {
        // محاكاة بسيطة: v0=1 اطبع a0، v0=10 إيقاف
        if (r[2] == 1) printf("%d\n", (int32_t)r[4]);
        else if (r[2] == 10) halted = true;
        else printf("syscall غير معروف: %u\n", r[2]);
    }

    void step() {
        uint32_t instr = read(pc, 4);
        pc = next_pc;
        next_pc += 4;

        uint32_t op = instr >> 26;
        uint32_t rs = (instr >> 21) & 31;
        uint32_t rt = (instr >> 16) & 31;
        uint32_t rd = (instr >> 11) & 31;
        uint32_t sh = (instr >> 6) & 31;
        uint32_t fn = instr & 63;
        uint32_t imm = instr & 0xFFFF;
        int32_t simm = (int16_t)imm;
        uint32_t target = instr & 0x03FFFFFF;

        auto branch = [&](bool cond) { if (cond) next_pc = pc + (simm << 2); };

        switch (op) {
        case 0x00: // SPECIAL
            switch (fn) {
            case 0x00: r[rd] = r[rt] << sh; break;                      // SLL
            case 0x02: r[rd] = r[rt] >> sh; break;                      // SRL
            case 0x03: r[rd] = (int32_t)r[rt] >> sh; break;             // SRA
            case 0x04: r[rd] = r[rt] << (r[rs] & 31); break;            // SLLV
            case 0x06: r[rd] = r[rt] >> (r[rs] & 31); break;            // SRLV
            case 0x07: r[rd] = (int32_t)r[rt] >> (r[rs] & 31); break;   // SRAV
            case 0x08: next_pc = r[rs]; break;                          // JR
            case 0x09: { uint32_t ra = next_pc; next_pc = r[rs]; r[rd] = ra; break; } // JALR
            case 0x0C: syscall(); break;                                // SYSCALL
            case 0x10: r[rd] = hi; break;                               // MFHI
            case 0x11: hi = r[rs]; break;                               // MTHI
            case 0x12: r[rd] = lo; break;                               // MFLO
            case 0x13: lo = r[rs]; break;                               // MTLO
            case 0x18: { int64_t m = (int64_t)(int32_t)r[rs] * (int32_t)r[rt];
                         lo = (uint32_t)m; hi = (uint32_t)(m >> 32); break; } // MULT
            case 0x19: { uint64_t m = (uint64_t)r[rs] * r[rt];
                         lo = (uint32_t)m; hi = (uint32_t)(m >> 32); break; } // MULTU
            case 0x1A: { int32_t a = r[rs], b = r[rt];
                         if (b) { lo = a / b; hi = a % b; } break; }    // DIV
            case 0x1B: if (r[rt]) { lo = r[rs] / r[rt]; hi = r[rs] % r[rt]; } break; // DIVU
            case 0x20: case 0x21: r[rd] = r[rs] + r[rt]; break;         // ADD/ADDU
            case 0x22: case 0x23: r[rd] = r[rs] - r[rt]; break;         // SUB/SUBU
            case 0x24: r[rd] = r[rs] & r[rt]; break;                    // AND
            case 0x25: r[rd] = r[rs] | r[rt]; break;                    // OR
            case 0x26: r[rd] = r[rs] ^ r[rt]; break;                    // XOR
            case 0x27: r[rd] = ~(r[rs] | r[rt]); break;                 // NOR
            case 0x2A: r[rd] = (int32_t)r[rs] < (int32_t)r[rt]; break;  // SLT
            case 0x2B: r[rd] = r[rs] < r[rt]; break;                    // SLTU
            default: printf("SPECIAL غير معروف: %02x عند %08x\n", fn, pc - 4); halted = true;
            }
            break;
        case 0x01: { // REGIMM
            bool ge = rt & 1;
            bool link = (rt & 0x1E) == 0x10;
            bool neg = (int32_t)r[rs] < 0;
            if (link) r[31] = next_pc;
            branch(ge ? !neg : neg);
            break; }
        case 0x02: next_pc = (pc & 0xF0000000) | (target << 2); break;               // J
        case 0x03: r[31] = next_pc; next_pc = (pc & 0xF0000000) | (target << 2); break; // JAL
        case 0x04: branch(r[rs] == r[rt]); break;                       // BEQ
        case 0x05: branch(r[rs] != r[rt]); break;                       // BNE
        case 0x06: branch((int32_t)r[rs] <= 0); break;                  // BLEZ
        case 0x07: branch((int32_t)r[rs] > 0); break;                   // BGTZ
        case 0x08: case 0x09: r[rt] = r[rs] + simm; break;              // ADDI/ADDIU
        case 0x0A: r[rt] = (int32_t)r[rs] < simm; break;                // SLTI
        case 0x0B: r[rt] = r[rs] < (uint32_t)simm; break;               // SLTIU
        case 0x0C: r[rt] = r[rs] & imm; break;                          // ANDI
        case 0x0D: r[rt] = r[rs] | imm; break;                          // ORI
        case 0x0E: r[rt] = r[rs] ^ imm; break;                          // XORI
        case 0x0F: r[rt] = imm << 16; break;                            // LUI
        case 0x20: r[rt] = (int8_t)read(r[rs] + simm, 1); break;        // LB
        case 0x21: r[rt] = (int16_t)read(r[rs] + simm, 2); break;       // LH
        case 0x23: r[rt] = read(r[rs] + simm, 4); break;                // LW
        case 0x24: r[rt] = read(r[rs] + simm, 1); break;                // LBU
        case 0x25: r[rt] = read(r[rs] + simm, 2); break;                // LHU
        case 0x28: write(r[rs] + simm, r[rt], 1); break;                // SB
        case 0x29: write(r[rs] + simm, r[rt], 2); break;                // SH
        case 0x2B: write(r[rs] + simm, r[rt], 4); break;                // SW
        default: printf("opcode غير معروف: %02x عند %08x\n", op, pc - 4); halted = true;
        }
        r[0] = 0; // $zero دائمًا صفر
    }
};

// مساعدات لبناء التعليمات يدويًا (مجمّع بسيط)
static uint32_t R(uint32_t fn, int rs, int rt, int rd, int sh = 0) { return (rs << 21) | (rt << 16) | (rd << 11) | (sh << 6) | fn; }
static uint32_t I(uint32_t op, int rs, int rt, int imm) { return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF); }

int main(int argc, char** argv) {
    CPU cpu;

    if (argc > 1) {
        // تشغيل ملف ثنائي خام يُحمَّل عند 0x80010000
        std::ifstream f(argv[1], std::ios::binary);
        if (!f) { printf("تعذّر فتح الملف\n"); return 1; }
        std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
        memcpy(&cpu.ram[0x10000], data.data(), data.size());
    } else {
        // برنامج تجريبي: مجموع الأعداد من 1 إلى 10
        uint32_t prog[] = {
            I(0x09, 0, 8, 0),      // addiu $t0,$zero,0   (sum)
            I(0x09, 0, 9, 1),      // addiu $t1,$zero,1   (i)
            I(0x09, 0, 10, 11),    // addiu $t2,$zero,11
            R(0x21, 8, 9, 8),      // loop: addu $t0,$t0,$t1
            I(0x09, 9, 9, 1),      // addiu $t1,$t1,1
            I(0x05, 9, 10, -3),    // bne $t1,$t2,loop
            0,                     // nop (delay slot)
            R(0x21, 8, 0, 4),      // addu $a0,$t0,$zero
            I(0x09, 0, 2, 1),      // addiu $v0,$zero,1  (print)
            R(0x0C, 0, 0, 0),      // syscall
            I(0x09, 0, 2, 10),     // addiu $v0,$zero,10 (halt)
            R(0x0C, 0, 0, 0),      // syscall
        };
        cpu.load(prog, sizeof(prog) / 4);
    }

    uint64_t steps = 0;
    while (!cpu.halted && steps < 100000000ULL) { cpu.step(); steps++; }
    printf("انتهى بعد %llu تعليمة\n", (unsigned long long)steps);
    return 0;
}
