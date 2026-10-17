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
} // namespace PurchasePolicy
