#include "data/airlines.h"

#include <cctype>
#include <cstring>

namespace data::airlines {

namespace {

// Curated list of common/major airlines, keyed by ICAO designator (callsign
// prefix). Extend as needed. Order is not significant (linear lookup).
constexpr Airline kAirlines[] = {
    // {ICAO, IATA, full name, friendly short name}
    // North America
    {"AAL", "AA", "American Airlines", "American"},
    {"UAL", "UA", "United Airlines", "United"},
    {"DAL", "DL", "Delta Air Lines", "Delta"},
    {"SWA", "WN", "Southwest Airlines", "Southwest"},
    {"JBU", "B6", "JetBlue Airways", "JetBlue"},
    {"ASA", "AS", "Alaska Airlines", "Alaska"},
    {"FFT", "F9", "Frontier Airlines", "Frontier"},
    {"NKS", "NK", "Spirit Airlines", "Spirit"},
    {"HAL", "HA", "Hawaiian Airlines", "Hawaiian"},
    {"ACA", "AC", "Air Canada", "Air Canada"},
    {"WJA", "WS", "WestJet", "WestJet"},
    {"AMX", "AM", "Aeromexico", "Aeromexico"},
    {"VOI", "Y4", "Volaris", "Volaris"},
    {"FDX", "FX", "FedEx Express", "FedEx"},
    {"UPS", "5X", "UPS Airlines", "UPS"},
    {"GTI", "5Y", "Atlas Air", "Atlas"},
    // United Kingdom & Ireland
    {"BAW", "BA", "British Airways", "British Airways"},
    {"VIR", "VS", "Virgin Atlantic", "Virgin"},
    {"EZY", "U2", "easyJet", "easyJet"},
    {"EXS", "LS", "Jet2", "Jet2"},
    {"RYR", "FR", "Ryanair", "Ryanair"},
    {"TOM", "BY", "TUI Airways", "TUI"},
    // Western Europe
    {"DLH", "LH", "Lufthansa", "Lufthansa"},
    {"AFR", "AF", "Air France", "Air France"},
    {"KLM", "KL", "KLM", "KLM"},
    {"IBE", "IB", "Iberia", "Iberia"},
    {"SWR", "LX", "Swiss", "Swiss"},
    {"AUA", "OS", "Austrian Airlines", "Austrian"},
    {"BEL", "SN", "Brussels Airlines", "Brussels"},
    {"TAP", "TP", "TAP Air Portugal", "TAP"},
    {"ITY", "AZ", "ITA Airways", "ITA"},
    {"VLG", "VY", "Vueling", "Vueling"},
    {"EWG", "EW", "Eurowings", "Eurowings"},
    {"CFG", "DE", "Condor", "Condor"},
    {"TVF", "TO", "Transavia France", "Transavia"},
    {"TRA", "HV", "Transavia", "Transavia"},
    // Nordics & Baltics
    {"SAS", "SK", "Scandinavian Airlines", "SAS"},
    {"FIN", "AY", "Finnair", "Finnair"},
    {"NAX", "DY", "Norwegian", "Norwegian"},
    {"NSZ", "DY", "Norwegian", "Norwegian"},
    {"ICE", "FI", "Icelandair", "Icelandair"},
    {"BTI", "BT", "airBaltic", "airBaltic"},
    // Central & Eastern Europe
    {"WZZ", "W6", "Wizz Air", "Wizz"},
    {"LOT", "LO", "LOT Polish Airlines", "LOT"},
    {"CSA", "OK", "Czech Airlines", "Czech"},
    {"AEE", "A3", "Aegean Airlines", "Aegean"},
    {"THY", "TK", "Turkish Airlines", "Turkish"},
    {"PGT", "PC", "Pegasus Airlines", "Pegasus"},
    {"AFL", "SU", "Aeroflot", "Aeroflot"},
    {"SBI", "S7", "S7 Airlines", "S7"},
    {"ROT", "RO", "Tarom", "Tarom"},
    // Middle East
    {"UAE", "EK", "Emirates", "Emirates"},
    {"QTR", "QR", "Qatar Airways", "Qatar"},
    {"ETD", "EY", "Etihad Airways", "Etihad"},
    {"FDB", "FZ", "flydubai", "flydubai"},
    {"ABY", "G9", "Air Arabia", "Air Arabia"},
    {"SVA", "SV", "Saudia", "Saudia"},
    {"ELY", "LY", "El Al", "El Al"},
    {"MEA", "ME", "Middle East Airlines", "MEA"},
    {"RJA", "RJ", "Royal Jordanian", "Royal Jordanian"},
    {"GFA", "GF", "Gulf Air", "Gulf Air"},
    {"OMA", "WY", "Oman Air", "Oman Air"},
    {"KAC", "KU", "Kuwait Airways", "Kuwait"},
    // Africa
    {"SAA", "SA", "South African Airways", "South African"},
    {"ETH", "ET", "Ethiopian Airlines", "Ethiopian"},
    {"MSR", "MS", "EgyptAir", "EgyptAir"},
    {"RAM", "AT", "Royal Air Maroc", "Royal Air Maroc"},
    {"KQA", "KQ", "Kenya Airways", "Kenya"},
    {"DAH", "AH", "Air Algerie", "Air Algerie"},
    {"TAR", "TU", "Tunisair", "Tunisair"},
    // Asia-Pacific
    {"SIA", "SQ", "Singapore Airlines", "Singapore"},
    {"CPA", "CX", "Cathay Pacific", "Cathay"},
    {"ANA", "NH", "All Nippon Airways", "ANA"},
    {"JAL", "JL", "Japan Airlines", "Japan"},
    {"KAL", "KE", "Korean Air", "Korean"},
    {"AAR", "OZ", "Asiana Airlines", "Asiana"},
    {"CCA", "CA", "Air China", "Air China"},
    {"CES", "MU", "China Eastern Airlines", "China Eastern"},
    {"CSN", "CZ", "China Southern Airlines", "China Southern"},
    {"CHH", "HU", "Hainan Airlines", "Hainan"},
    {"CAL", "CI", "China Airlines", "China Airlines"},
    {"EVA", "BR", "EVA Air", "EVA"},
    {"THA", "TG", "Thai Airways", "Thai"},
    {"MAS", "MH", "Malaysia Airlines", "Malaysia"},
    {"AXM", "AK", "AirAsia", "AirAsia"},
    {"GIA", "GA", "Garuda Indonesia", "Garuda"},
    {"AIC", "AI", "Air India", "Air India"},
    {"IGO", "6E", "IndiGo", "IndiGo"},
    {"PAL", "PR", "Philippine Airlines", "Philippine"},
    {"CEB", "5J", "Cebu Pacific", "Cebu Pacific"},
    {"VJC", "VJ", "VietJet Air", "VietJet"},
    {"HVN", "VN", "Vietnam Airlines", "Vietnam"},
    {"QFA", "QF", "Qantas", "Qantas"},
    {"VOZ", "VA", "Virgin Australia", "Virgin Australia"},
    {"JST", "JQ", "Jetstar", "Jetstar"},
    {"ANZ", "NZ", "Air New Zealand", "Air New Zealand"},
    // Latin America
    {"LAN", "LA", "LATAM Airlines", "LATAM"},
    {"TAM", "JJ", "LATAM Brasil", "LATAM"},
    {"GLO", "G3", "Gol", "Gol"},
    {"AZU", "AD", "Azul", "Azul"},
    {"ARG", "AR", "Aerolineas Argentinas", "Aerolineas"},
    {"AVA", "AV", "Avianca", "Avianca"},
    {"CMP", "CM", "Copa Airlines", "Copa"},
};

constexpr size_t kAirlineCount = sizeof(kAirlines) / sizeof(kAirlines[0]);

}  // namespace

const Airline* forCallsign(const char* callsign) {
  if (callsign == nullptr) {
    return nullptr;
  }
  // Airline callsign = 3-letter ICAO designator + flight number (starts digit).
  if (!std::isalpha(static_cast<unsigned char>(callsign[0])) ||
      !std::isalpha(static_cast<unsigned char>(callsign[1])) ||
      !std::isalpha(static_cast<unsigned char>(callsign[2])) ||
      !std::isdigit(static_cast<unsigned char>(callsign[3]))) {
    return nullptr;
  }

  char code[4] = {
      static_cast<char>(std::toupper(static_cast<unsigned char>(callsign[0]))),
      static_cast<char>(std::toupper(static_cast<unsigned char>(callsign[1]))),
      static_cast<char>(std::toupper(static_cast<unsigned char>(callsign[2]))),
      '\0'};

  for (size_t i = 0; i < kAirlineCount; ++i) {
    if (std::strcmp(kAirlines[i].icao, code) == 0) {
      return &kAirlines[i];
    }
  }
  return nullptr;
}

}  // namespace data::airlines
