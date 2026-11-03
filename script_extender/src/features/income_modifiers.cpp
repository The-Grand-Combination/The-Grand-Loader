#include "features/income_modifiers.h"

#include <cstdint>

#include "game/objects.h"
#include "hooks/memory.h"
#include "hooks/thunk.h"
#include "log.h"

// rich_income_modifier (modifier 87), middle_income_modifier (88) and poor_income_modifier (89):
//
// - Pop_AddMoney (0x0095A5F0) is how a pop is paid: EAX = pop, ESI = income category, stack i64 amount
//   (fixed point 2^15), RET 8; it adds to the pop's money (+0x180), the category (+0x1D8 + 8 x category)
//   and the day's income (+0x218), and returns with EAX = the pop (0x0050C112 reads it). It applies no
//   modifier. Its income calls are the seven that first take income tax (PopIncome_Tax, 0x00439780) and
//   pass the rest: artisan sales (Market_PostProcess, 0x00489812), the pay a pop type gets from its
//   factories and RGOs (0x004CFFB7, 0x004D002B, 0x004EF365, 0x004EF3DA), pensions and unemployment
//   benefit (0x0050C110, 0x0050C1C0). Its other callers move money (bank withdrawals, pop merges, needs,
//   the money effect) and are left alone.
// - Pop_AddWorkingCash (0x0095A6A0), the only reader of the three modifiers, scales by them a change of
//   the artisan's input budget (+0x188): transfers into it from money and bank, input purchases, the part
//   of sales kept for inputs. It reads only the province's sum (province+0x9C), which no country modifier
//   reaches; the country's sum is at country+0xA60 (as rich_vote is read, 0x00520389).
//
// The seven calls go to income_thunk instead: the amount (after tax) is multiplied by
// (1000 + province + country modifier of the pop's strata) / 1000, at least 0, then Pop_AddMoney runs as
// before. In Pop_AddWorkingCash the strata switch at 0x0095A6B1 (`JZ +0x28`) becomes `JMP +0x3C` to
// 0x0095A6EF with EAX = 1000 (loaded at 0x0095A6AC): factor one, so the budget is no longer scaled.
// Integer arithmetic on game state: the same on every machine. Changes results by design.

namespace se::features {

namespace {

using game::read;

constexpr uint32_t kAddMoney = 0x0095A5F0;  // Pop_AddMoney
constexpr uint32_t kIncomeCalls[] = {0x00489812, 0x004CFFB7, 0x004D002B, 0x004EF365,
                                     0x004EF3DA, 0x0050C110, 0x0050C1C0};

constexpr uint32_t kWorkingCashSwitch = 0x0095A6B1;  // in Pop_AddWorkingCash
constexpr unsigned char kSwitchBytes[] = {0x74, 0x28};  // JZ 0x0095A6DB (strata 0)
constexpr unsigned char kSwitchPatch[] = {0xEB, 0x3C};  // JMP 0x0095A6EF

constexpr uint32_t kPopProvince = 0x64;
constexpr uint32_t kPopType = 0x68;
constexpr uint32_t kPopTypeStrata = 0x3C;  // 0 poor, 1 middle, 2 rich
constexpr uint32_t kProvinceModifiers = 0x9C;  // i64 per modifier definition
constexpr uint32_t kProvinceOwner = 0x12C;
constexpr uint32_t kCountryModifiers = 0xA60;
constexpr uint32_t kRichIncome = 87;  // then middle 88, poor 89

uintptr_t g_add_money = 0;

// The modifier of strata `strata` in a modifier sum (i64 entries, value x1000 in the low half).
int income_modifier(uintptr_t sum, int strata) {
    return read<int>(sum + (kRichIncome + 2 - static_cast<uint32_t>(strata)) * 8);
}

void __stdcall scale_income(uintptr_t pop, int64_t* amount) {
    if (*amount <= 0) {
        return;
    }
    int strata = read<int>(read<uintptr_t>(pop + kPopType) + kPopTypeStrata);
    uintptr_t province = read<uintptr_t>(pop + kPopProvince);
    if (strata < 0 || strata > 2 || !province) {
        return;
    }
    int64_t factor = 1000 + income_modifier(read<uintptr_t>(province + kProvinceModifiers), strata);
    if (int owner = read<int>(province + kProvinceOwner)) {
        factor += income_modifier(read<uintptr_t>(game::country(owner) + kCountryModifiers), strata);
    }
    if (factor == 1000) {
        return;
    }
    *amount = factor > 0 ? *amount * factor / 1000 : 0;
}

// Replaces the CALL Pop_AddMoney of the income sites: EAX = pop, [esp + 4] = the i64 amount.
__declspec(naked) void income_thunk() {
    __asm {
        SE_SAVE_ALL
        lea ecx, [esp + SE_SAVED_ALL_BYTES + 4]
        push ecx
        push eax
        call scale_income
        SE_RESTORE_ALL
        jmp g_add_money
    }
}

}  // namespace

void install_income_modifiers(const Config& config) {
    if (!config.income_modifiers) {
        logf("income_modifiers: disabled");
        return;
    }
    g_add_money = hooks::exe_va(kAddMoney);
    for (uint32_t site : kIncomeCalls) {
        if (hooks::call_target(hooks::exe_va(site)) != g_add_money) {
            logf("income_modifiers: site %08X not recognised, off", site);
            return;
        }
    }
    uintptr_t sw = hooks::exe_va(kWorkingCashSwitch);
    if (!hooks::expect_bytes(sw, kSwitchBytes, sizeof(kSwitchBytes))) {
        logf("income_modifiers: site %08X not recognised, off", kWorkingCashSwitch);
        return;
    }
    for (uint32_t site : kIncomeCalls) {
        hooks::redirect_call(hooks::exe_va(site), g_add_money, reinterpret_cast<uintptr_t>(&income_thunk));
    }
    hooks::write_bytes(sw, kSwitchPatch, sizeof(kSwitchPatch));
    logf("income_modifiers: active, the 7 pop income calls of Pop_AddMoney %08X scaled by the country and "
         "province modifiers; the artisan budget (%08X) no longer",
         kAddMoney, kWorkingCashSwitch);
}

}  // namespace se::features
