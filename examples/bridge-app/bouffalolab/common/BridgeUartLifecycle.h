#pragma once

#include <stdint.h>

class BridgeUartLifecycle
{
public:
    static constexpr uint8_t kMaxDevices         = 32;
    static constexpr uint8_t kMaxRetries         = 3;
    static constexpr uint32_t kResponseTimeoutMs = 500;

    enum class Phase : uint8_t
    {
        Cold,
        Hello,
        Listing,
        Binding,
        Ready,
        Offline,
    };

    enum class PollResult : uint8_t
    {
        None,
        Retry,
        Offline,
    };

    struct ListEntry
    {
        uint32_t deviceId;
        uint8_t deviceType;
        uint32_t capabilityFlags;
        uint8_t stateFlags;
        uint32_t stateVersion;
    };

    void Reset(uint32_t bridgeBootId);
    bool AcceptHelloResponse(uint16_t sequence, uint16_t expectedSequence, uint32_t sessionId, uint8_t status,
                             uint32_t deviceListVersion);
    bool BeginList(uint32_t transactionId, uint32_t deviceListVersion, uint16_t entryCount, uint16_t entrySize, uint32_t checksum);
    void BeginListRefresh();
    bool AddListEntry(uint32_t transactionId, const ListEntry & entry);
    bool FinishList(uint32_t transactionId, uint16_t receivedCount, uint32_t checksum, uint8_t status);

    bool BeginRequest(uint8_t messageType, uint16_t sequence, uint32_t nowMs);
    bool CompleteResponse(uint8_t messageType, uint16_t sequence, uint8_t status);
    PollResult Poll(uint32_t nowMs);
    void HeartbeatResponse(uint32_t nowMs);

    Phase GetPhase() const { return mPhase; }

    uint32_t GetSessionId() const { return mSessionId; }

    uint32_t GetDeviceListVersion() const { return mDeviceListVersion; }

    uint16_t GetListCount() const { return mListCount; }

    uint8_t GetRetryCount() const { return mRetries; }

    uint8_t GetHeartbeatMisses() const { return mHeartbeatMisses; }

    const ListEntry * GetListEntries() const { return mEntries; }

    void BindingsComplete()
    {
        if (mPhase == Phase::Binding)
        {
            mPhase = Phase::Ready;
        }
    }

private:
    struct Pending
    {
        bool active       = false;
        uint8_t type      = 0;
        uint16_t sequence = 0;
        uint32_t sentAtMs = 0;
    };

    Phase mPhase                    = Phase::Cold;
    uint32_t mBridgeBootId          = 0;
    uint32_t mSessionId             = 0;
    uint32_t mDeviceListVersion     = 0;
    uint32_t mListTransactionId     = 0;
    uint32_t mListChecksum          = 0;
    uint32_t mComputedChecksum      = 0xFFFFFFFFu;
    bool mListStarted               = false;
    bool mListInvalid               = false;
    uint16_t mListCountExpected     = 0;
    uint16_t mListCount             = 0;
    ListEntry mEntries[kMaxDevices] = {};
    Pending mPending;
    uint8_t mRetries         = 0;
    uint8_t mHeartbeatMisses = 0;
};
