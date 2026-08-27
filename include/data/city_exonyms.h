// Italian exonyms for airport cities — used to render flight route endpoints
// (origin / destination) on the aircraft tag in the operator's language.
//
// `en` keys are the OurAirports / adsbdb `municipality` string, normalised to
// lower case with diacritics stripped (see services::route::normalizeCity).
// `it` values are ASCII-only on purpose: the embedded VLW UI font only carries
// the Latin basic glyph set, so accented forms are transliterated
// ("Citta del Capo", not "Città del Capo").
//
// Curated list — seed/refresh candidates with scripts/build_city_exonyms.py,
// then hand-review (Wikidata mixes in transliterations and region names).

#pragma once

#include <cstddef>

namespace data::city_exonyms {

struct Exonym {
  const char* en;  // normalised municipality (lower case, no diacritics)
  const char* it;  // Italian name, ASCII
};

extern const Exonym kExonyms[];
extern const size_t kExonymCount;

}  // namespace data::city_exonyms
