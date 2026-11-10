#pragma once

#include <cstring>

namespace ui::aircraftnames {

struct NameEntry {
  const char* code;
  const char* name;
};

/** ICAO type code (e.g. "B738") -> readable model. Unknown codes are returned as-is. */
inline const char* modelFromType(const char* type) {
  static const NameEntry kTypes[] = {
      {"A318", "Airbus A318"},     {"A319", "Airbus A319"},
      {"A320", "Airbus A320"},     {"A321", "Airbus A321"},
      {"A19N", "Airbus A319neo"},  {"A20N", "Airbus A320neo"},
      {"A21N", "Airbus A321neo"},  {"A332", "Airbus A330-200"},
      {"A333", "Airbus A330-300"}, {"A338", "Airbus A330-800"},
      {"A339", "Airbus A330-900"}, {"A343", "Airbus A340-300"},
      {"A346", "Airbus A340-600"}, {"A359", "Airbus A350-900"},
      {"A35K", "Airbus A350-1000"}, {"A388", "Airbus A380-800"},
      {"A400", "Airbus A400M"},    {"BCS1", "Airbus A220-100"},
      {"BCS3", "Airbus A220-300"}, {"B733", "Boeing 737-300"},
      {"B734", "Boeing 737-400"},  {"B736", "Boeing 737-600"},
      {"B737", "Boeing 737-700"},  {"B738", "Boeing 737-800"},
      {"B739", "Boeing 737-900"},  {"B37M", "Boeing 737 MAX 7"},
      {"B38M", "Boeing 737 MAX 8"}, {"B39M", "Boeing 737 MAX 9"},
      {"B744", "Boeing 747-400"},  {"B748", "Boeing 747-8"},
      {"B752", "Boeing 757-200"},  {"B753", "Boeing 757-300"},
      {"B762", "Boeing 767-200"},  {"B763", "Boeing 767-300"},
      {"B764", "Boeing 767-400"},  {"B772", "Boeing 777-200"},
      {"B77L", "Boeing 777-200LR"}, {"B773", "Boeing 777-300"},
      {"B77W", "Boeing 777-300ER"}, {"B788", "Boeing 787-8"},
      {"B789", "Boeing 787-9"},    {"B78X", "Boeing 787-10"},
      {"MD11", "McDonnell MD-11"}, {"E145", "Embraer 145"},
      {"E170", "Embraer 170"},     {"E75L", "Embraer 175"},
      {"E75S", "Embraer 175"},     {"E190", "Embraer 190"},
      {"E195", "Embraer 195"},     {"E290", "Embraer E190-E2"},
      {"E295", "Embraer E195-E2"}, {"CRJ2", "CRJ-200"},
      {"CRJ7", "CRJ-700"},         {"CRJ9", "CRJ-900"},
      {"CRJX", "CRJ-1000"},        {"AT72", "ATR 72"},
      {"AT76", "ATR 72-600"},      {"AT45", "ATR 42"},
      {"DH8D", "Dash 8 Q400"},     {"SF34", "Saab 340"},
      {"B190", "Beech 1900"},      {"C172", "Cessna 172"},
      {"C208", "Cessna Caravan"},  {"PC12", "Pilatus PC-12"},
      {"SR22", "Cirrus SR22"},     {"PA28", "Piper PA-28"},
      {"DA40", "Diamond DA40"},    {"GLF5", "Gulfstream V"},
      {"GLF6", "Gulfstream G650"}, {"C56X", "Citation Excel"},
      {"FA7X", "Falcon 7X"},       {"E55P", "Phenom 300"},
      {"EC35", "Airbus H135"},     {"EC45", "Airbus H145"},
      {"A139", "AW139"},           {"R44", "Robinson R44"},
      {"C17", "C-17 Globemaster"}, {"C130", "C-130 Hercules"},
      {"A124", "Antonov An-124"},
  };
  if (type == nullptr || type[0] == '\0') {
    return "";
  }
  for (const NameEntry& e : kTypes) {
    if (strcmp(e.code, type) == 0) {
      return e.name;
    }
  }
  return type;
}

/** 3-letter ICAO airline prefix from a callsign like "BAW123"; "" if unknown. */
inline const char* airlineFromCallsign(const char* callsign) {
  static const NameEntry kAirlines[] = {
      {"BAW", "British Airways"}, {"EZY", "easyJet"},
      {"RYR", "Ryanair"},         {"VIR", "Virgin Atlantic"},
      {"EIN", "Aer Lingus"},      {"EXS", "Jet2"},
      {"TOM", "TUI Airways"},     {"LOG", "Loganair"},
      {"CFE", "BA CityFlyer"},    {"SHT", "BA Shuttle"},
      {"WZZ", "Wizz Air"},        {"DLH", "Lufthansa"},
      {"AFR", "Air France"},      {"KLM", "KLM"},
      {"IBE", "Iberia"},          {"VLG", "Vueling"},
      {"SAS", "SAS"},             {"NAX", "Norwegian"},
      {"NOZ", "Norwegian"},       {"FIN", "Finnair"},
      {"SWR", "Swiss"},           {"AUA", "Austrian"},
      {"BEL", "Brussels Airlines"}, {"TAP", "TAP Air Portugal"},
      {"LOT", "LOT Polish"},      {"THY", "Turkish Airlines"},
      {"TRA", "Transavia"},       {"TVF", "Transavia France"},
      {"ICE", "Icelandair"},      {"LGL", "Luxair"},
      {"UAE", "Emirates"},        {"QTR", "Qatar Airways"},
      {"ETD", "Etihad"},          {"SVA", "Saudia"},
      {"ELY", "El Al"},           {"MSR", "EgyptAir"},
      {"ETH", "Ethiopian"},       {"SAA", "South African"},
      {"AIC", "Air India"},       {"IGO", "IndiGo"},
      {"SIA", "Singapore Airlines"}, {"CPA", "Cathay Pacific"},
      {"ANA", "ANA"},             {"JAL", "Japan Airlines"},
      {"KAL", "Korean Air"},      {"AAR", "Asiana"},
      {"CCA", "Air China"},       {"CES", "China Eastern"},
      {"CSN", "China Southern"},  {"THA", "Thai Airways"},
      {"MAS", "Malaysia Airlines"}, {"QFA", "Qantas"},
      {"ANZ", "Air New Zealand"}, {"AAL", "American Airlines"},
      {"DAL", "Delta"},           {"UAL", "United"},
      {"SWA", "Southwest"},       {"JBU", "JetBlue"},
      {"ASA", "Alaska Airlines"}, {"ACA", "Air Canada"},
      {"WJA", "WestJet"},         {"AMX", "Aeromexico"},
      {"AVA", "Avianca"},         {"CMP", "Copa Airlines"},
      {"GLO", "Gol"},             {"AZU", "Azul"},
      {"FDX", "FedEx"},           {"UPS", "UPS"},
      {"DHK", "DHL"},             {"BCS", "DHL"},
      {"RRR", "Royal Air Force"},
  };
  if (callsign == nullptr || strlen(callsign) < 4) {
    return "";
  }
  for (int i = 0; i < 3; ++i) {
    if (callsign[i] < 'A' || callsign[i] > 'Z') {
      return "";
    }
  }
  if (callsign[3] < '0' || callsign[3] > '9') {
    return "";
  }
  for (const NameEntry& e : kAirlines) {
    if (strncmp(e.code, callsign, 3) == 0) {
      return e.name;
    }
  }
  return "";
}

}  // namespace ui::aircraftnames