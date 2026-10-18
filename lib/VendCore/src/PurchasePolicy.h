#pragma once

#include <cstdint>

// Purchase rule.
//
// Keeps the logic of the original rfidChecker: minus(credit, loan) => credit > loan, where loan was a bool,
// so in practice the condition is "credit > 0" (with loan=false). As long as the credit is positive the
// purchase is allowed, and after the deduction the credit may go negative (by at most price-1).
// The loan / loanMax logic is left out on request for now - extend it here if needed later.
namespace PurchasePolicy
{
    inline bool canPurchase(int32_t credit, int32_t /*price*/)
    {
        return credit > 0;
    }

    inline int32_t creditAfterPurchase(int32_t credit, int32_t price)
    {
        return credit - price;
    }

    // Credit transfer between two cards (app): whole hundreds only, and unlike a purchase the sender's credit
    // may not go negative.
    constexpr int32_t kTransferStep = 100;
    constexpr int32_t kTransferMax = 100000;

    inline bool validTransferAmount(int32_t amount)
    {
        return amount > 0 && amount <= kTransferMax && amount % kTransferStep == 0;
    }

    inline bool canTransfer(int32_t credit, int32_t amount)
    {
        return validTransferAmount(amount) && credit >= amount;
    }
} // namespace PurchasePolicy
