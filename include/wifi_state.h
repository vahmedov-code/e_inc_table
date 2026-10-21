#pragma once

struct WifiEvidence {
  bool modeKnown,stationEnabled,associationKnown,associated,interfaceUp,hasAddress;
  int arduinoStatus;
};
enum class StationState { Unknown,Disabled,Disconnected,Associated,Connected };
// Arduino's event-cached numeric status is diagnostic only. A cached IP alone
// also must not claim connectivity after the radio has disconnected.
constexpr StationState stationState(const WifiEvidence& e){
  if(!e.modeKnown)return StationState::Unknown;
  if(!e.stationEnabled)return StationState::Disabled;
  if(!e.associationKnown)return StationState::Unknown;
  if(!e.associated)return StationState::Disconnected;
  if(!e.interfaceUp||!e.hasAddress)return StationState::Associated;
  return StationState::Connected;
}
constexpr const char* stationStateName(StationState s){
  switch(s){
    case StationState::Connected:return "connected";
    case StationState::Associated:return "associated";
    case StationState::Disconnected:return "disconnected";
    case StationState::Disabled:return "disabled";
    default:return "unknown";
  }
}
