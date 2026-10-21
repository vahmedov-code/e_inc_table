#include "wifi_state.h"
constexpr WifiEvidence online255{true,true,true,true,true,true,255};
static_assert(stationState(online255)==StationState::Connected,"Live link and IP override stale cached 255");
constexpr WifiEvidence staleIP{true,true,true,false,true,true,3};
static_assert(stationState(staleIP)==StationState::Disconnected,"Stale IP and cached 3 cannot claim a live connection");
constexpr WifiEvidence apOnly{true,false,true,true,true,true,3};
static_assert(stationState(apOnly)==StationState::Disabled,"AP clients are not a station connection");
constexpr WifiEvidence awaitingIP{true,true,true,true,true,false,255};
static_assert(stationState(awaitingIP)==StationState::Associated,"Association without DHCP is not ready");
constexpr WifiEvidence interfaceDown{true,true,true,true,false,true,3};
static_assert(stationState(interfaceDown)==StationState::Associated,"A down interface cannot claim ready IP connectivity");
constexpr WifiEvidence modeError{false,true,true,true,true,true,3};
static_assert(stationState(modeError)==StationState::Unknown,"SDK mode read failure must be reported as unknown");
constexpr WifiEvidence associationError{true,true,false,false,true,true,3};
static_assert(stationState(associationError)==StationState::Unknown,"SDK association read failure must be reported as unknown");
