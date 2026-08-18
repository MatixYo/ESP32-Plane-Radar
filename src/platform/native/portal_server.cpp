/**
 * The native harness's config portal: a real HTTP server on 127.0.0.1:8080.
 *
 * On the device WiFiManager runs a captive portal on a soft AP. There is no AP
 * here, so this serves the same form over loopback and feeds submissions
 * through the same core::portal field table — the form is generated from
 * fields(), never hand-written, which is what stops the two portals drifting.
 *
 * Decisions worth knowing:
 *
 *  - Loopback only. Bound to 127.0.0.1 and never INADDR_ANY: this is a
 *    hand-rolled parser reachable from a browser, and it has no business being
 *    on the LAN of whoever is doing UI work in a cafe.
 *  - Single-threaded and non-blocking. It is pumped from wifiLoop(), which runs
 *    on the same thread as the SDL panel and the radar redraw, so no call here
 *    may wait on a client. Reads and writes are bounded by poll() budgets and
 *    each connection is closed after one response (no keep-alive), so a browser
 *    holding a connection open cannot starve the pump.
 *  - The form carries one field the table does not: the Wi-Fi SSID. The table
 *    deliberately omits it because on the device WiFiManager collects
 *    credentials itself; here nothing else would.
 */

#include "portal_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/platform.h"
#include "core/portal_params.h"

namespace {

namespace pf = core::platform;

constexpr char kBindAddr[] = "127.0.0.1";
constexpr uint16_t kBindPort = 8080;

/** Hard ceiling on one request. Anything larger is dropped unanswered. */
constexpr size_t kMaxRequestBytes = 8 * 1024;

/** Work budget for one pump() call, so the radar keeps its frame rate. */
constexpr int kMaxConnectionsPerPump = 4;

/**
 * Per-connection I/O budget: at most kMaxIoPolls waits of kIoPollMs each.
 * A browser sends its whole request in one segment, so the normal path spends
 * no time here at all; the budget only bounds a client that stalls mid-request.
 */
constexpr int kMaxIoPolls = 24;
constexpr int kIoPollMs = 2;

/** Buffers for the field table's own accessors. */
constexpr size_t kValueBufLen = 32;

int s_listen_fd = -1;
bool s_credentials_pending = false;
std::string s_pending_ssid;

// --- Socket helpers ----------------------------------------------------------

bool setNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/**
 * macOS has no MSG_NOSIGNAL, so a client that closes the tab mid-response would
 * take the whole harness down with SIGPIPE. Disable the signal per socket.
 */
void suppressSigpipe(int fd) {
  const int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
}

/** Bounded non-blocking write of the whole buffer. Best effort. */
void writeAll(int fd, const std::string& data) {
  size_t sent = 0;
  for (int i = 0; i < kMaxIoPolls && sent < data.size(); ++i) {
    const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
    if (n > 0) {
      sent += static_cast<size_t>(n);
      continue;
    }
    if (n == 0) {
      return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      return;
    }
    pollfd p = {fd, POLLOUT, 0};
    ::poll(&p, 1, kIoPollMs);
  }
}

void sendResponse(int fd, const char* status, const char* content_type,
                  const std::string& body) {
  char head[256];
  const int n = snprintf(head, sizeof(head),
                         "HTTP/1.1 %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n"
                         "\r\n",
                         status, content_type, body.size());
  if (n <= 0) {
    return;
  }
  writeAll(fd, std::string(head, static_cast<size_t>(n)) + body);
}

// --- Request parsing ---------------------------------------------------------

int hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/**
 * application/x-www-form-urlencoded decode.
 *
 * A truncated escape at the very end ("...%" or "...%A") must not read past the
 * buffer, so the two hex digits are bounds-checked before they are touched and
 * an escape that is not two hex digits is passed through as a literal '%'
 * rather than guessed at.
 */
std::string urlDecode(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%') {
      if (i + 2 < in.size()) {
        const int hi = hexValue(in[i + 1]);
        const int lo = hexValue(in[i + 2]);
        if (hi >= 0 && lo >= 0) {
          out.push_back(static_cast<char>(hi * 16 + lo));
          i += 2;
          continue;
        }
      }
      out.push_back('%');
      continue;
    }
    out.push_back(c);
  }
  return out;
}

std::string htmlEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (const char c : in) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

std::string toLower(const std::string& in) {
  std::string out = in;
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

size_t declaredContentLength(const std::string& headers) {
  const std::string lower = toLower(headers);
  const size_t at = lower.find("content-length:");
  if (at == std::string::npos) {
    return 0;
  }
  size_t i = at + strlen("content-length:");
  while (i < headers.size() && (headers[i] == ' ' || headers[i] == '\t')) {
    ++i;
  }
  size_t value = 0;
  while (i < headers.size() && headers[i] >= '0' && headers[i] <= '9') {
    value = value * 10 + static_cast<size_t>(headers[i] - '0');
    if (value > kMaxRequestBytes) {
      return kMaxRequestBytes + 1;  // Caller drops it; no need for the exact size.
    }
    ++i;
  }
  return value;
}

/** True once the headers and any declared body have all arrived. */
bool requestComplete(const std::string& raw, size_t* body_start) {
  const size_t end = raw.find("\r\n\r\n");
  if (end == std::string::npos) {
    return false;
  }
  *body_start = end + 4;
  const size_t want = declaredContentLength(raw.substr(0, end));
  return raw.size() >= *body_start + want;
}

/** Reads one complete request within the I/O budget. False means "drop it". */
bool readRequest(int fd, std::string* raw, size_t* body_start) {
  raw->clear();
  char buf[2048];
  for (int i = 0; i < kMaxIoPolls; ++i) {
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      if (raw->size() + static_cast<size_t>(n) > kMaxRequestBytes) {
        pf::logf("portal: request over %zu bytes, dropped\n", kMaxRequestBytes);
        return false;
      }
      raw->append(buf, static_cast<size_t>(n));
      if (requestComplete(*raw, body_start)) {
        return true;
      }
      continue;  // Data is flowing; do not spend a poll wait.
    }
    if (n == 0) {
      return false;  // Peer closed before finishing.
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      return false;
    }
    pollfd p = {fd, POLLIN, 0};
    ::poll(&p, 1, kIoPollMs);
  }
  return false;
}

// --- The form ----------------------------------------------------------------

constexpr char kSsidFieldId[] = "wifi_ssid";
constexpr char kPassFieldId[] = "wifi_pass";

constexpr char kPageStyle[] =
    "body{font:16px system-ui,sans-serif;max-width:26rem;margin:2rem auto;"
    "padding:0 1rem;color:#111}"
    "h1{font-size:1.3rem}"
    "label{display:block;margin:.9rem 0}"
    "input[type=text],input[type=password],input[type=number]"
    "{display:block;width:100%;box-sizing:border-box;padding:.4rem;"
    "margin-top:.25rem;font-size:1rem}"
    "button{margin-top:1.2rem;padding:.5rem 1.2rem;font-size:1rem}"
    "p.note{color:#666;font-size:.85rem}";

/** Renders one table field, honouring its label_after (checkbox) placement. */
void appendField(std::string* html, const core::portal::Field& field) {
  char attrs[core::portal::kHtmlAttrsMax];
  core::portal::htmlAttrs(field, attrs, sizeof(attrs));
  char value[kValueBufLen];
  core::portal::currentValue(field, value, sizeof(value));

  const char* attrs_begin = attrs;
  while (*attrs_begin == ' ') {
    ++attrs_begin;
  }

  char input[256];
  snprintf(input, sizeof(input),
           "<input name=\"%s\" id=\"%s\" %s value=\"%s\" maxlength=\"%d\">",
           field.id, field.id, attrs_begin, htmlEscape(value).c_str(),
           field.max_len);

  *html += "<label>";
  if (field.label_after) {
    *html += input;
    *html += " ";
    *html += htmlEscape(field.label);
  } else {
    *html += htmlEscape(field.label);
    *html += input;
  }
  *html += "</label>\n";
}

std::string renderForm() {
  const std::string ssid =
      pf::KeyValueStore::getString(kWifiKvNamespace, kWifiKvSsidKey, "");

  std::string html;
  html.reserve(4096);
  html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,";
  html += "initial-scale=1\"><title>Plane Radar setup</title><style>";
  html += kPageStyle;
  html += "</style></head><body>\n";
  html += "<h1>Plane Radar setup</h1>\n";
  html += "<form method=\"POST\" action=\"/\">\n";

  html += "<label>Wi-Fi network (SSID)<input type=\"text\" name=\"";
  html += kSsidFieldId;
  html += "\" value=\"";
  html += htmlEscape(ssid);
  html += "\" maxlength=\"32\"></label>\n";
  html += "<label>Wi-Fi password<input type=\"password\" name=\"";
  html += kPassFieldId;
  html += "\" maxlength=\"63\"></label>\n";
  // The simulated radio has no PSK to check, so the password is accepted and
  // discarded; it exists so the form matches the device's shape.
  html += "<p class=\"note\">Simulated radio: any SSID associates, and the ";
  html += "password is ignored.</p>\n";

  const core::portal::Field* fields = core::portal::fields();
  for (size_t i = 0; i < core::portal::fieldCount(); ++i) {
    appendField(&html, fields[i]);
  }

  html += "<button type=\"submit\">Save</button>\n";
  html += "</form></body></html>\n";
  return html;
}

std::string renderSaved(const std::string& ssid) {
  std::string html;
  html += "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
  html += "<title>Saved</title><style>";
  html += kPageStyle;
  html += "</style></head><body><h1>Saved</h1><p>Network: <b>";
  html += htmlEscape(ssid.empty() ? "(unchanged)" : ssid);
  html += "</b></p><p><a href=\"/\">Back to settings</a></p></body></html>\n";
  return html;
}

// --- Submission --------------------------------------------------------------

struct FormPair {
  std::string key;
  std::string value;
};

std::vector<FormPair> parseFormBody(const std::string& body) {
  std::vector<FormPair> pairs;
  size_t at = 0;
  while (at <= body.size()) {
    size_t amp = body.find('&', at);
    if (amp == std::string::npos) {
      amp = body.size();
    }
    const std::string chunk = body.substr(at, amp - at);
    if (!chunk.empty()) {
      const size_t eq = chunk.find('=');
      FormPair pair;
      if (eq == std::string::npos) {
        pair.key = urlDecode(chunk);
      } else {
        pair.key = urlDecode(chunk.substr(0, eq));
        pair.value = urlDecode(chunk.substr(eq + 1));
      }
      pairs.push_back(pair);
    }
    if (amp == body.size()) {
      break;
    }
    at = amp + 1;
  }
  return pairs;
}

bool wasSubmitted(const std::vector<FormPair>& pairs, const char* id) {
  for (const FormPair& pair : pairs) {
    if (pair.key == id) {
      return true;
    }
  }
  return false;
}

/**
 * Applies one submission to the shared field table.
 *
 * The absent-checkbox pass is load-bearing. An unchecked HTML checkbox is not
 * submitted at all, so a form that only carried the ticked boxes would leave an
 * untick looking exactly like "field not in this form" — the setting would keep
 * its old value and the user's change would silently vanish. Every checkbox the
 * body did not mention is therefore explicitly applied as "" first, which
 * core::settings::portalCheckboxChecked reads as false. (The device gets this
 * for free: WiFiManager always hands back a value for every parameter it owns.)
 */
void applySubmission(const std::vector<FormPair>& pairs) {
  const core::portal::Field* fields = core::portal::fields();
  for (size_t i = 0; i < core::portal::fieldCount(); ++i) {
    if (fields[i].kind == core::portal::Kind::kCheckbox &&
        !wasSubmitted(pairs, fields[i].id)) {
      core::portal::applyValueById(fields[i].id, "");
      pf::logf("portal: %s = off\n", fields[i].id);
    }
  }

  for (const FormPair& pair : pairs) {
    if (pair.key == kSsidFieldId || pair.key == kPassFieldId) {
      continue;  // Not in the table; handled by the caller.
    }
    if (core::portal::applyValueById(pair.key.c_str(), pair.value.c_str())) {
      pf::logf("portal: %s = %s\n", pair.key.c_str(), pair.value.c_str());
    }
  }

  // Exactly once per submission: lat/lon are staged during apply and only
  // become persistent here, as a validated pair.
  core::portal::commit();
}

std::string handlePost(const std::string& body) {
  const std::vector<FormPair> pairs = parseFormBody(body);
  applySubmission(pairs);

  std::string ssid;
  for (const FormPair& pair : pairs) {
    if (pair.key == kSsidFieldId) {
      ssid = pair.value;
    }
  }

  if (!ssid.empty()) {
    pf::KeyValueStore::putString(kWifiKvNamespace, kWifiKvSsidKey, ssid.c_str());
    s_pending_ssid = ssid;
    s_credentials_pending = true;
    pf::logf("portal: wifi_ssid = %s\n", ssid.c_str());
  }

  return renderSaved(ssid);
}

// --- Connection handling -----------------------------------------------------

/** Serves one request, then the caller closes the socket. */
void serveConnection(int fd) {
  std::string raw;
  size_t body_start = 0;
  if (!readRequest(fd, &raw, &body_start)) {
    return;
  }

  const size_t line_end = raw.find("\r\n");
  const std::string line = raw.substr(0, line_end);
  const size_t sp1 = line.find(' ');
  const size_t sp2 = sp1 == std::string::npos ? std::string::npos
                                              : line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    sendResponse(fd, "400 Bad Request", "text/plain", "bad request\n");
    return;
  }

  const std::string method = line.substr(0, sp1);
  std::string path = line.substr(sp1 + 1, sp2 - sp1 - 1);
  const size_t query = path.find('?');
  if (query != std::string::npos) {
    path.resize(query);
  }

  // Whitelist, not blacklist: this parser is small enough to trust only on the
  // two shapes it was written for.
  if (method != "GET" && method != "POST") {
    sendResponse(fd, "405 Method Not Allowed", "text/plain", "no\n");
    return;
  }
  if (path != "/") {
    sendResponse(fd, "404 Not Found", "text/plain", "not found\n");
    return;
  }

  if (method == "GET") {
    sendResponse(fd, "200 OK", "text/html; charset=utf-8", renderForm());
    return;
  }
  sendResponse(fd, "200 OK", "text/html; charset=utf-8",
               handlePost(raw.substr(body_start)));
}

}  // namespace

bool portalServerStart() {
  if (s_listen_fd >= 0) {
    return true;
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    pf::logf("portal: socket() failed: %s\n", strerror(errno));
    return false;
  }
  suppressSigpipe(fd);

  const int on = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kBindPort);
  // Loopback only, never INADDR_ANY. See the file comment.
  addr.sin_addr.s_addr = inet_addr(kBindAddr);

  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    pf::logf("portal: bind %s:%u failed: %s\n", kBindAddr,
             static_cast<unsigned>(kBindPort), strerror(errno));
    ::close(fd);
    return false;
  }
  if (::listen(fd, 4) != 0 || !setNonBlocking(fd)) {
    pf::logf("portal: listen failed: %s\n", strerror(errno));
    ::close(fd);
    return false;
  }

  s_listen_fd = fd;
  pf::logf("Setup portal: http://%s:%u\n", kBindAddr,
           static_cast<unsigned>(kBindPort));
  return true;
}

void portalServerPump() {
  if (s_listen_fd < 0) {
    return;
  }
  for (int i = 0; i < kMaxConnectionsPerPump; ++i) {
    const int fd = ::accept(s_listen_fd, nullptr, nullptr);
    if (fd < 0) {
      return;  // EAGAIN in the common case: nothing waiting.
    }
    suppressSigpipe(fd);
    setNonBlocking(fd);
    serveConnection(fd);
    // One response per connection; no keep-alive, so a parked browser tab
    // cannot occupy the single-threaded pump.
    ::close(fd);
  }
}

void portalServerStop() {
  if (s_listen_fd < 0) {
    return;
  }
  ::close(s_listen_fd);
  s_listen_fd = -1;
}

bool portalServerConsumeCredentials(std::string* ssid) {
  if (!s_credentials_pending) {
    return false;
  }
  s_credentials_pending = false;
  if (ssid != nullptr) {
    *ssid = s_pending_ssid;
  }
  return true;
}
