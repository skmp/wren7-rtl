/* timing_check -- the model's bus cycles, one instruction at a time, against the data sheet's chapter 9 tables.
 *
 * Every case places one instruction at PC = 0x10 (preceded by MOV r0,r0, so its cycle 1 is an S cycle as in the
 * tables), runs it, and compares each bus cycle (type, address, nRW, nBW, nOPC, LOCK) with the table rows, plus the
 * type the instruction announces for the cycle after it (the tables' last nMREQ/SEQ).  Some cases also check a
 * documented value (R15 offsets, R14 on BL/SWI).  Encodings come from arm-eabi-as -mcpu=arm7
 * (tests/timing_ops.s).  Zero wait states.
 *
 * Exit status 0 iff every case matches.
 */
#include "../src/arm7di.h"
#include <functional>
#include <string>
#include <vector>

using namespace wren7;

struct RecBus : Arm7Bus {
    std::vector<uint8_t> mem = std::vector<uint8_t>(1 << 16, 0);
    std::vector<BusCycle> log;
    bool rec = false;
    uint32_t rd32(uint32_t a) const {
        a &= 0xFFFC;
        return mem[a] | (mem[a + 1] << 8) | (mem[a + 2] << 16) | ((uint32_t)mem[a + 3] << 24);
    }
    void wr32(uint32_t a, uint32_t v) { a &= 0xFFFC; for (int i = 0; i < 4; i++) mem[a + i] = v >> (8 * i); }
    void cycle(const BusCycle &c, BusResult &r) override {
        if (rec) log.push_back(c);
        if (c.type == CYC_I || c.type == CYC_C) return;
        if (c.flags & BF_WRITE) {
            if (c.flags & BF_BYTE) mem[c.addr & 0xFFFF] = c.wdata >> (8 * (c.addr & 3));
            else wr32(c.addr, c.wdata);
        } else {
            r.rdata = rd32(c.addr);
        }
    }
};

enum : uint8_t { W = BF_WRITE, B = BF_BYTE, O = BF_OPC, L = BF_LOCK };
struct Row { uint8_t type; uint32_t addr; uint8_t flags; };

struct Case {
    std::string name;
    uint32_t op;
    std::function<void(Arm7DI &, RecBus &)> setup;
    std::vector<Row> rows;
    uint8_t next;
    std::function<std::string(Arm7DI &, RecBus &)> check;   /* "" = ok */
};

static const uint32_t PC = 0x10, NOP = 0xE1A00000, T = 0x200, D = 0x400;

static std::string hex(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08x", v); return b; }

static std::string row_str(uint8_t type, uint32_t addr, uint8_t flags)
{
    std::string s;
    s += "NSIC"[type];
    s += " " + hex(addr) + " ";
    s += (flags & W) ? 'W' : 'R';
    s += (flags & B) ? 'b' : 'w';
    s += (flags & O) ? 'O' : '-';
    s += (flags & L) ? 'L' : '-';
    return s;
}

/* multiply cycle count from 4.6.3, independent of the model's Booth loop */
static int datasheet_m(uint32_t rs)
{
    if (rs <= 1) return 1;
    if (rs >= (1u << 29)) return 16;
    for (int m = 2; m <= 16; m++)
        if (rs >= (1u << (2 * m - 3)) && rs <= (1u << (2 * m - 1)) - 1) return m;
    return -1;
}

static std::vector<Row> mul_rows(uint32_t rs)
{
    std::vector<Row> r{{CYC_S, PC + 8, O}};
    int m = datasheet_m(rs);
    for (int i = 0; i < m; i++) r.push_back({CYC_I, PC + 12, 0});
    return r;
}

int main()
{
    std::vector<Case> cases;
    auto regs = [](std::initializer_list<std::pair<int, uint32_t>> l) {
        std::vector<std::pair<int, uint32_t>> v(l);
        return [v](Arm7DI &c, RecBus &) { for (auto &p : v) c.set_reg(p.first, p.second); };
    };
    /* PC-loading cases: r2 = D, the words at D and D+8 hold the target T */
    auto pcmem = [](Arm7DI &c, RecBus &b) { c.set_reg(2, D); b.wr32(D, T); b.wr32(D + 8, T); };

    /* Table 29 */
    cases.push_back({"unexecuted (moveq, Z=0)", 0x03A01001, [](Arm7DI &c, RecBus &) { c.set_cpsr(c.cpsr() & ~PSR_Z); },
                     {{CYC_S, PC + 8, O}}, CYC_S, nullptr});
    /* Table 14 */
    cases.push_back({"b", 0xEA00007A, nullptr, {{CYC_S, PC + 8, O}, {CYC_N, T, O}, {CYC_S, T + 4, O}}, CYC_S, nullptr});
    cases.push_back({"bl", 0xEB00007A, nullptr, {{CYC_S, PC + 8, O}, {CYC_N, T, O}, {CYC_S, T + 4, O}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(14) == PC + 4 ? "" : "R14 " + hex(c.reg(14)); }});
    /* Table 15 */
    cases.push_back({"data normal (mov r1,#1)", 0xE3A01001, nullptr, {{CYC_S, PC + 8, O}}, CYC_S, nullptr});
    cases.push_back({"data shift(Rs) (add r1,r2,r3,lsl r4)", 0xE0821413, regs({{2, 1}, {3, 1}, {4, 3}}),
                     {{CYC_S, PC + 8, O}, {CYC_I, PC + 12, 0}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(1) == 9 ? "" : "r1 " + hex(c.reg(1)); }});
    cases.push_back({"data dest=pc (mov pc,r2)", 0xE1A0F002, regs({{2, T}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, T, O}, {CYC_S, T + 4, O}}, CYC_S, nullptr});
    cases.push_back({"data shift(Rs) dest=pc", 0xE082F413, regs({{2, T}, {3, 0}, {4, 0}}),
                     {{CYC_S, PC + 8, O}, {CYC_I, PC + 12, 0}, {CYC_N, T, O}, {CYC_S, T + 4, O}}, CYC_S, nullptr});
    cases.push_back({"R15 operand, imm shift = pc+8 (mov r1,pc)", 0xE1A0100F, nullptr, {{CYC_S, PC + 8, O}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(1) == PC + 8 ? "" : "r1 " + hex(c.reg(1)); }});
    cases.push_back({"R15 operand, reg shift = pc+12 (add r1,pc,r3,lsl r4)", 0xE08F1413, regs({{3, 0}, {4, 0}}),
                     {{CYC_S, PC + 8, O}, {CYC_I, PC + 12, 0}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(1) == PC + 12 ? "" : "r1 " + hex(c.reg(1)); }});
    cases.push_back({"mrs r1,cpsr", 0xE10F1000, nullptr, {{CYC_S, PC + 8, O}}, CYC_S, nullptr});
    cases.push_back({"msr cpsr_flg,r1", 0xE128F001, regs({{1, 0xF0000000}}), {{CYC_S, PC + 8, O}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return (c.cpsr() >> 28) == 0xF ? "" : "cpsr " + hex(c.cpsr()); }});
    /* Table 16 + 4.6.3 */
    for (uint32_t rs : {0u, 1u, 2u, 3u, 7u, 8u, 31u, 32u, 127u, 128u, 0x7FFFu, 0x8000u, 0x1FFFFFFFu, 0x20000000u,
                        0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
        char n[64];
        snprintf(n, sizeof n, "mul Rs=%08x (m=%d)", rs, datasheet_m(rs));
        cases.push_back({n, 0xE0010392, regs({{2, 0x12345679}, {3, rs}}), mul_rows(rs), CYC_S,
                         [rs](Arm7DI &c, RecBus &) {
                             uint32_t e = 0x12345679u * rs;
                             return c.reg(1) == e ? std::string() : "r1 " + hex(c.reg(1)) + " want " + hex(e);
                         }});
    }
    cases.push_back({"mla Rs=0x100", 0xE0214392, regs({{2, 3}, {3, 0x100}, {4, 5}}), mul_rows(0x100), CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(1) == 0x305 ? "" : "r1 " + hex(c.reg(1)); }});
    /* Table 17 */
    cases.push_back({"ldr r1,[r2]", 0xE5921000, regs({{2, D}}), {{CYC_S, PC + 8, O}, {CYC_N, D, 0}, {CYC_I, PC + 12, 0}},
                     CYC_S, nullptr});
    cases.push_back({"ldrb r1,[r2,#1]", 0xE5D21001, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D + 1, B}, {CYC_I, PC + 12, 0}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(1) == 0x01 ? "" : "r1 " + hex(c.reg(1)); }});
    cases.push_back({"ldr pc,[r2]", 0xE592F000, pcmem,
                     {{CYC_S, PC + 8, O}, {CYC_N, D, 0}, {CYC_I, PC + 12, 0}, {CYC_N, T, O}, {CYC_S, T + 4, O}}, CYC_S,
                     nullptr});
    cases.push_back({"ldr r2,[r2,#4]! (load wins over write-back)", 0xE5B22004, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D + 4, 0}, {CYC_I, PC + 12, 0}}, CYC_S,
                     [](Arm7DI &c, RecBus &) { return c.reg(2) == 0x07060504 ? "" : "r2 " + hex(c.reg(2)); }});
    /* Table 18 */
    cases.push_back({"str r1,[r2]", 0xE5821000, regs({{1, 0x11223344}, {2, D}}), {{CYC_S, PC + 8, O}, {CYC_N, D, W}},
                     CYC_N, nullptr});
    cases.push_back({"str pc,[r2] stores pc+12", 0xE582F000, regs({{2, D}}), {{CYC_S, PC + 8, O}, {CYC_N, D, W}}, CYC_N,
                     [](Arm7DI &, RecBus &b) { return b.rd32(D) == PC + 12 ? "" : "mem " + hex(b.rd32(D)); }});
    /* Table 19 */
    cases.push_back({"ldm 1 register", 0xE8920002, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, 0}, {CYC_I, PC + 12, 0}}, CYC_S, nullptr});
    cases.push_back({"ldm 1 register, pc", 0xE8928000, pcmem,
                     {{CYC_S, PC + 8, O}, {CYC_N, D, 0}, {CYC_I, PC + 12, 0}, {CYC_N, T, O}, {CYC_S, T + 4, O}}, CYC_S,
                     nullptr});
    cases.push_back({"ldm 3 registers", 0xE892001A, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, 0}, {CYC_S, D + 4, 0}, {CYC_S, D + 8, 0}, {CYC_I, PC + 12, 0}},
                     CYC_S, nullptr});
    cases.push_back({"ldm 3 registers incl pc", 0xE892800A, pcmem,
                     {{CYC_S, PC + 8, O}, {CYC_N, D, 0}, {CYC_S, D + 4, 0}, {CYC_S, D + 8, 0}, {CYC_I, PC + 12, 0},
                      {CYC_N, T, O}, {CYC_S, T + 4, O}},
                     CYC_S, nullptr});
    /* Table 20 */
    cases.push_back({"stm 1 register", 0xE8820002, regs({{2, D}}), {{CYC_S, PC + 8, O}, {CYC_N, D, W}}, CYC_N, nullptr});
    cases.push_back({"stm 3 registers", 0xE882001A, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, W}, {CYC_S, D + 4, W}, {CYC_S, D + 8, W}}, CYC_N, nullptr});
    cases.push_back({"stm {r1,pc} stores pc+12", 0xE8828002, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, W}, {CYC_S, D + 4, W}}, CYC_N,
                     [](Arm7DI &, RecBus &b) { return b.rd32(D + 4) == PC + 12 ? "" : "mem " + hex(b.rd32(D + 4)); }});
    cases.push_back({"stmia r2!,{r2,r3}: base 1st stores old base (4.8.6)", 0xE8A2000C, regs({{2, D}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, W}, {CYC_S, D + 4, W}}, CYC_N,
                     [](Arm7DI &c, RecBus &b) {
                         if (c.reg(2) != D + 8) return "r2 " + hex(c.reg(2));
                         return b.rd32(D) == D ? std::string() : "stored base " + hex(b.rd32(D));
                     }});
    cases.push_back({"stmdb r2!,{r1,r2,r3}: base 2nd stores new base (4.8.6)", 0xE922000E, regs({{2, D + 12}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, W}, {CYC_S, D + 4, W}, {CYC_S, D + 8, W}}, CYC_N,
                     [](Arm7DI &c, RecBus &b) {
                         if (c.reg(2) != D) return "r2 " + hex(c.reg(2));
                         return b.rd32(D + 4) == D ? std::string() : "stored base " + hex(b.rd32(D + 4));
                     }});
    /* Table 21 */
    cases.push_back({"swp", 0xE1021093, regs({{2, D}, {3, 0xAABBCCDD}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, L}, {CYC_N, D, W | L}, {CYC_I, PC + 12, 0}}, CYC_S,
                     [](Arm7DI &c, RecBus &b) {
                         if (c.reg(1) != 0x03020100) return "r1 " + hex(c.reg(1));
                         return b.rd32(D) == 0xAABBCCDD ? std::string() : "mem " + hex(b.rd32(D));
                     }});
    cases.push_back({"swpb", 0xE1421093, regs({{2, D}, {3, 0xAABBCCDD}}),
                     {{CYC_S, PC + 8, O}, {CYC_N, D, B | L}, {CYC_N, D, W | B | L}, {CYC_I, PC + 12, 0}}, CYC_S, nullptr});
    /* Table 22 */
    cases.push_back({"swi", 0xEF000000, nullptr, {{CYC_S, PC + 8, O}, {CYC_N, 0x08, O}, {CYC_S, 0x0C, O}}, CYC_S,
                     [](Arm7DI &c, RecBus &) {
                         if ((c.cpsr() & PSR_M) != M_SVC || !(c.cpsr() & PSR_I)) return "cpsr " + hex(c.cpsr());
                         return c.reg(14) == PC + 4 ? std::string() : "R14_svc " + hex(c.reg(14));
                     }});
    /* Table 28 (undefined and coprocessor absent) */
    const std::vector<Row> und{{CYC_S, PC + 8, O}, {CYC_I, PC + 8, O}, {CYC_N, 0x04, O}, {CYC_S, 0x08, O}};
    auto und_chk = [](Arm7DI &c, RecBus &) {
        if ((c.cpsr() & PSR_M) != M_UND) return "cpsr " + hex(c.cpsr());
        return c.reg(14) == PC + 4 ? std::string() : "R14_und " + hex(c.reg(14));
    };
    cases.push_back({"undefined (cond 011...1)", 0xE7F000F0, nullptr, und, CYC_S, und_chk});
    cases.push_back({"cdp, coprocessor absent", 0xEE000100, nullptr, und, CYC_S, und_chk});
    cases.push_back({"ldc, coprocessor absent", 0xED920100, regs({{2, D}}), und, CYC_S, und_chk});
    cases.push_back({"mcr, coprocessor absent", 0xEE001110, nullptr, und, CYC_S, und_chk});
    cases.push_back({"mrc, coprocessor absent", 0xEE101110, nullptr, und, CYC_S, und_chk});
    /* 9.9: IRQ entry in place of the instruction at PC */
    cases.push_back({"irq entry", 0xE3A01001,
                     [](Arm7DI &c, RecBus &) { c.set_cpsr(c.cpsr() & ~PSR_I); c.set_irq(true); },
                     {{CYC_S, PC + 8, O}, {CYC_N, 0x18, O}, {CYC_S, 0x1C, O}}, CYC_S,
                     [](Arm7DI &c, RecBus &) {
                         if ((c.cpsr() & PSR_M) != M_IRQ) return "cpsr " + hex(c.cpsr());
                         return c.reg(14) == PC + 4 ? std::string() : "R14_irq " + hex(c.reg(14));
                     }});
    cases.push_back({"fiq entry", 0xE3A01001,
                     [](Arm7DI &c, RecBus &) { c.set_cpsr(c.cpsr() & ~PSR_F); c.set_fiq(true); },
                     {{CYC_S, PC + 8, O}, {CYC_N, 0x1C, O}, {CYC_S, 0x20, O}}, CYC_S,
                     [](Arm7DI &c, RecBus &) {
                         if ((c.cpsr() & (PSR_M | PSR_F | PSR_I)) != (M_FIQ | PSR_F | PSR_I)) return "cpsr " + hex(c.cpsr());
                         return c.reg(14) == PC + 4 ? std::string() : "R14_fiq " + hex(c.reg(14));
                     }});

    int ok = 0, bad = 0;
    for (auto &k : cases) {
        RecBus bus;
        for (uint32_t a = 0; a < 0x800; a += 4) bus.wr32(a, NOP);
        bus.wr32(PC, k.op);
        for (uint32_t i = 0; i < 16; i++) bus.wr32(D + 4 * i, 0x03020100 + 0x04040404 * i);
        bus.wr32(D, 0x03020100);
        Arm7DI cpu(&bus);
        auto insn = [&]() { do cpu.bus_cycle(); while (!cpu.at_boundary()); };   /* one instruction / entry */
        while (cpu.exec_addr() != PC) insn();
        if (k.setup) k.setup(cpu, bus);
        bus.rec = true;
        insn();
        const size_t n = bus.log.size();
        cpu.set_irq(false);
        cpu.set_fiq(false);
        insn();   /* first cycle of the following instruction carries the announced type */
        std::string err;
        if (n != k.rows.size()) err = "cycles " + std::to_string(n) + " want " + std::to_string(k.rows.size());
        for (size_t i = 0; err.empty() && i < n; i++) {
            const BusCycle &c = bus.log[i];
            const uint8_t f = c.flags & (W | B | O | L);
            if (c.type != k.rows[i].type || c.addr != k.rows[i].addr || f != k.rows[i].flags)
                err = "row " + std::to_string(i + 1) + ": " + row_str(c.type, c.addr, f) + " want " +
                      row_str(k.rows[i].type, k.rows[i].addr, k.rows[i].flags);
        }
        if (err.empty() && bus.log.size() > n && bus.log[n].type != k.next)
            err = std::string("next cycle ") + "NSIC"[bus.log[n].type] + " want " + "NSIC"[k.next];
        if (err.empty() && k.check) err = k.check(cpu, bus);
        printf("%-4s %s%s%s\n", err.empty() ? "ok" : "FAIL", k.name.c_str(), err.empty() ? "" : "  -- ", err.c_str());
        (err.empty() ? ok : bad)++;
    }
    printf("%d ok, %d failed\n", ok, bad);
    return bad ? 1 : 0;
}
