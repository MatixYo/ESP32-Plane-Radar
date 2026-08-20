#pragma once

/**
 * The config portal's field table — one definition, both destinations.
 *
 * The device renders these as WiFiManagerParameters inside the captive portal;
 * the native harness renders the same fields as plain HTML from its local
 * server. Keeping one table is what stops the two forms drifting apart.
 */

#include <cstddef>

namespace core::portal {

enum class Kind {
  kText,      ///< plain text (ICAO codes)
  kCheckbox,  ///< see core::settings::portalCheckboxChecked for the quirk
};

struct Field {
  const char* id;          ///< form field name, and the NVS-facing identity
  const char* label;       ///< human-readable label (empty => no label)
  const char* html_attrs;  ///< extra attributes injected into the <input>
  Kind kind;
  int max_len;      ///< WiFiManagerParameter buffer length
  bool label_after; ///< checkboxes put their label after the box
};

const Field* fields();
size_t fieldCount();

/**
 * Current value for prefilling the form.
 *
 * Text fields return their stored value. Checkboxes always return "T" — their
 * state is carried by the `checked` attribute from fieldHtmlAttrs(), which is
 * how WiFiManager expects it.
 */
void currentValue(const Field& field, char* buf, size_t len);

/**
 * Write this field's current HTML attributes, including ` checked` for a ticked
 * checkbox.
 *
 * Writes into a caller-owned buffer rather than returning internal storage:
 * WiFiManagerParameter keeps the `custom` string as a pointer instead of
 * copying it, so each field needs its own buffer that outlives the portal, and
 * a refresh must rewrite this buffer in place.
 */
void htmlAttrs(const Field& field, char* buf, size_t len);

/** Suggested size for the htmlAttrs() buffer. */
constexpr size_t kHtmlAttrsMax = 64;

/** Apply one submitted value. Invalid input is logged and ignored. */
void applyValue(const Field& field, const char* value);

/** Apply a submitted value by field id. Returns false if the id is unknown. */
bool applyValueById(const char* id, const char* value);

/**
 * Commit a form submission.
 *
 * Call after feeding every field through applyValue(): the airport slots are
 * only meaningful as a whole list, so they are staged during apply and
 * persisted here. Blanking every slot restores config::kDefaultSiteIdent.
 */
void commit();

}  // namespace core::portal
