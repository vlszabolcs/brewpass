#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Error codes. The number is shown on the display as "E<code>", so the cause can be identified
// directly from a user's report. Groups:
//   1x - network/time, 2x - Firebase, 3x - card/user, 4x - reader,
//   5x - coffee machine interface, 6x - configuration
enum class ErrorCode : uint16_t
{
    None = 0,

    WifiDisconnected = 10,
    TimeNotSynced = 11,

    CloudNotReady = 20,
    CloudAuth = 21,
    CloudRequest = 22,
    CloudTimeout = 23,
    CloudData = 24,
    CreditUpdateFailed = 25,

    CardUnknown = 30,
    CreditLow = 31,

    ReaderFault = 40,

    MachineNotReady = 50,
    VendNotAcknowledged = 51,
    MachineStuck = 52,
    ChargedNotVended = 53,

    ConfigInvalid = 60,
};

// English name of an error code - for serial logs and the /Log device events
// (the Hungarian display title is in Messages.h).
inline const char *errorCodeName(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::None: return "None";
    case ErrorCode::WifiDisconnected: return "WifiDisconnected";
    case ErrorCode::TimeNotSynced: return "TimeNotSynced";
    case ErrorCode::CloudNotReady: return "CloudNotReady";
    case ErrorCode::CloudAuth: return "CloudAuth";
    case ErrorCode::CloudRequest: return "CloudRequest";
    case ErrorCode::CloudTimeout: return "CloudTimeout";
    case ErrorCode::CloudData: return "CloudData";
    case ErrorCode::CreditUpdateFailed: return "CreditUpdateFailed";
    case ErrorCode::CardUnknown: return "CardUnknown";
    case ErrorCode::CreditLow: return "CreditLow";
    case ErrorCode::ReaderFault: return "ReaderFault";
    case ErrorCode::MachineNotReady: return "MachineNotReady";
    case ErrorCode::VendNotAcknowledged: return "VendNotAcknowledged";
    case ErrorCode::MachineStuck: return "MachineStuck";
    case ErrorCode::ChargedNotVended: return "ChargedNotVended";
    case ErrorCode::ConfigInvalid: return "ConfigInvalid";
    }
    return "Unknown";
}

struct ErrorInfo
{
    ErrorCode code = ErrorCode::None;
    // Low-level detail code (e.g. HTTP status, Firebase error code, reader register). 0 = none.
    int32_t detailCode = 0;
    // Free-text technical details (English), e.g. "GET /users/AB12CD34: Permission denied"
    char detail[112] = {0};

    bool isError() const { return code != ErrorCode::None; }

    static ErrorInfo none() { return ErrorInfo(); }

    static ErrorInfo make(ErrorCode code, int32_t detailCode, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 3, 4)))
#endif
    {
        ErrorInfo e;
        e.code = code;
        e.detailCode = detailCode;
        if (fmt != nullptr)
        {
            va_list args;
            va_start(args, fmt);
            vsnprintf(e.detail, sizeof(e.detail), fmt, args);
            va_end(args);
        }
        return e;
    }
};
