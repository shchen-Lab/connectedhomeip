#include "BridgeUartLifecycle.h"

#include <cassert>
#include <cstdio>

int main()
{
    BridgeUartLifecycle lifecycle;
    lifecycle.Reset(42);
    assert(lifecycle.GetPhase() == BridgeUartLifecycle::Phase::Hello);
    assert(!lifecycle.AcceptHelloResponse(2, 1, 7, 0, 1));
    assert(lifecycle.AcceptHelloResponse(1, 1, 7, 0, 1));

    assert(lifecycle.BeginList(9, 1, 1, 18, 0x70785ADD));
    BridgeUartLifecycle::ListEntry entry{ 11, 0x11, 0x17, 0x07, 3 };
    assert(lifecycle.AddListEntry(9, entry));
    assert(!lifecycle.AddListEntry(9, entry));
    assert(!lifecycle.FinishList(9, 1, 0, 0));
    // Identical BEGIN/END checksums must not hide a checksum mismatch with ENTRY bytes.
    assert(lifecycle.BeginList(9, 1, 1, 18, 0x12345678));
    assert(lifecycle.AddListEntry(9, entry));
    assert(!lifecycle.FinishList(9, 1, 0x12345678, 0));
    assert(lifecycle.BeginList(9, 1, 1, 18, 0x70785ADD));
    assert(lifecycle.AddListEntry(9, entry));
    assert(lifecycle.FinishList(9, 1, 0x70785ADD, 0));
    assert(lifecycle.GetPhase() == BridgeUartLifecycle::Phase::Binding);
    lifecycle.BindingsComplete();
    assert(lifecycle.GetPhase() == BridgeUartLifecycle::Phase::Ready);

    assert(lifecycle.BeginRequest(0x10, 3, 100));
    assert(!lifecycle.BeginRequest(0x11, 4, 100));
    assert(lifecycle.Poll(599) == BridgeUartLifecycle::PollResult::None);
    assert(lifecycle.Poll(600) == BridgeUartLifecycle::PollResult::Retry);
    assert(lifecycle.CompleteResponse(0x12, 3, 0));
    assert(lifecycle.GetRetryCount() == 0);

    assert(lifecycle.BeginRequest(0x42, 5, 1000));
    assert(lifecycle.Poll(1500) == BridgeUartLifecycle::PollResult::Retry);
    assert(lifecycle.Poll(2000) == BridgeUartLifecycle::PollResult::Retry);
    assert(lifecycle.Poll(2500) == BridgeUartLifecycle::PollResult::Retry);
    assert(lifecycle.Poll(3000) == BridgeUartLifecycle::PollResult::Offline);
    assert(lifecycle.GetPhase() == BridgeUartLifecycle::Phase::Offline);
    lifecycle.HeartbeatResponse(3100);
    assert(lifecycle.GetPhase() == BridgeUartLifecycle::Phase::Hello);

    std::puts("bridge_uart_v2 lifecycle tests: PASS");
    return 0;
}
