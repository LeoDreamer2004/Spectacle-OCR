#include "settings.h"
#include "formula_ui.h"
#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>

#include <dlfcn.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using Api = tesseract::TessBaseAPI;
using Iterator = tesseract::ResultIterator;
constexpr size_t max_image = 64 * 1024 * 1024;
constexpr size_t max_text = 1024 * 1024;
constexpr char magic[] = "SOCR0001";

template <typename F> F original(const char *symbol) {
  auto function = reinterpret_cast<F>(dlsym(RTLD_NEXT, symbol));
  if (!function) {
    std::fprintf(stderr, "SpectacleOCR: missing ABI symbol %s\n", symbol);
    std::abort();
  }
  return function;
}

using SetImage = void (*)(Api *, const unsigned char *, int, int, int, int);
using Recognize = int (*)(Api *, tesseract::ETEXT_DESC *);
using GetIterator = Iterator *(*)(Api *);
SetImage real_set_image() {
  static auto f =
      original<SetImage>("_ZN9tesseract11TessBaseAPI8SetImageEPKhiiii");
  return f;
}
Recognize real_recognize() {
  static auto f = original<Recognize>(
      "_ZN9tesseract11TessBaseAPI9RecognizeEPNS_10ETEXT_DESCE");
  return f;
}
GetIterator real_get_iterator() {
  static auto f =
      original<GetIterator>("_ZN9tesseract11TessBaseAPI11GetIteratorEv");
  return f;
}

struct State {
  int width = 0, height = 0;
  std::vector<unsigned char> rgb;
  bool recognized = false;
  std::vector<std::string> lines;
};
struct Registry {
  std::mutex mutex;
  std::unordered_map<Api *, std::shared_ptr<State>> states;
};
Registry &registry() {
  // Survives static destruction in preloaded host processes.
  static auto *value = new Registry;
  return *value;
}
void forget(Api *api) {
  auto &r = registry();
  std::lock_guard lock(r.mutex);
  r.states.erase(api);
}
void remember(Api *api, std::shared_ptr<State> state) {
  auto &r = registry();
  std::lock_guard lock(r.mutex);
  r.states[api] = std::move(state);
}
std::shared_ptr<State> lookup(Api *api) {
  auto &r = registry();
  std::lock_guard lock(r.mutex);
  auto it = r.states.find(api);
  return it == r.states.end() ? nullptr : it->second;
}

// Construct a real C++ subclass by copying a valid Tesseract iterator. Never
// fabricate object layouts or vtables. Only Spectacle's TEXTLINE API is
// supported.
class TextIterator final : public Iterator {
  std::vector<std::string> lines_;
  size_t position_ = 0;

public:
  TextIterator(const Iterator &seed, std::vector<std::string> lines)
      : Iterator(seed), lines_(std::move(lines)) {}
  void Begin() override { position_ = 0; }
  bool Next(tesseract::PageIteratorLevel level) override {
    if (level != tesseract::RIL_TEXTLINE || position_ >= lines_.size())
      return false;
    return ++position_ < lines_.size();
  }
  char *GetUTF8Text(tesseract::PageIteratorLevel level) const override {
    if (level != tesseract::RIL_TEXTLINE || position_ >= lines_.size())
      return nullptr;
    const auto &text = lines_[position_];
    // Follow Tesseract's allocation contract. Spectacle 6.7.5 currently
    // uses scalar delete instead of delete[] (also for native results).
    auto *result = new char[text.size() + 1];
    std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
  }
};

int timeout_ms(bool formula) {
  if (formula) return 25000;
  const char *value = std::getenv("SPECTACLE_OCR_TIMEOUT_MS");
  if (!value || !*value)
    return socr::timeout();
  char *end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value, &end, 10);
  if (errno || *end || parsed < 100 || parsed > 25000)
    return 3000;
  return static_cast<int>(parsed);
}

class Connection {
  int fd_ = -1;
  std::chrono::steady_clock::time_point deadline_;

public:
  ~Connection() {
    if (fd_ >= 0)
      close(fd_);
  }
  void wait(short events) {
    for (;;) {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline_ - std::chrono::steady_clock::now())
                    .count();
      if (ms <= 0)
        throw std::runtime_error("service deadline exceeded");
      pollfd p{fd_, events, 0};
      int rc = poll(&p, 1, static_cast<int>(ms));
      if (rc < 0 && errno == EINTR)
        continue;
      if (rc <= 0)
        throw std::runtime_error("service timeout or poll error");
      return;
    }
  }
  explicit Connection(const char *path, bool formula)
      : deadline_(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms(formula))) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof(address.sun_path))
      throw std::runtime_error("socket path too long");
    std::strcpy(address.sun_path, path);
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ < 0)
      throw std::runtime_error("cannot create socket");
    if (connect(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) !=
        0) {
      // Unix sockets connect immediately or fail; do not wait on a full
      // backlog.
      close(fd_);
      fd_ = -1;
      throw std::runtime_error("cannot connect to OCR service");
    }
  }
  void transfer(void *data, size_t size, bool sending) {
    auto *bytes = static_cast<unsigned char *>(data);
    while (size) {
      wait(sending ? POLLOUT : POLLIN);
      auto n = sending ? send(fd_, bytes, size, MSG_NOSIGNAL)
                       : recv(fd_, bytes, size, 0);
      if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
        continue;
      if (n <= 0)
        throw std::runtime_error("service disconnected");
      bytes += n;
      size -= static_cast<size_t>(n);
    }
  }
};
void put32(unsigned char *p, uint32_t value) {
  for (int i = 0; i < 4; ++i)
    p[i] = (value >> (i * 8)) & 255;
}
uint32_t get32(const unsigned char *p) {
  uint32_t result = 0;
  for (int i = 0; i < 4; ++i)
    result |= uint32_t(p[i]) << (i * 8);
  return result;
}
std::vector<std::string> request(State &state, const char *path, bool formula) {
  Connection connection(path, formula);
  std::array<unsigned char, 20> header{};
  std::memcpy(header.data(), formula ? "SOCRF001" : magic, 8);
  put32(header.data() + 8, state.width);
  put32(header.data() + 12, state.height);
  put32(header.data() + 16, state.rgb.size());
  connection.transfer(header.data(), header.size(), true);
  connection.transfer(state.rgb.data(), state.rgb.size(), true);
  std::array<unsigned char, 16> reply{};
  connection.transfer(reply.data(), reply.size(), false);
  if (std::memcmp(reply.data(), magic, 8) ||
      get32(reply.data() + 12) > max_text)
    throw std::runtime_error("invalid or failed service response");
  std::string text(get32(reply.data() + 12), '\0');
  connection.transfer(text.data(), text.size(), false);
  if (text.find('\0') != std::string::npos)
    throw std::runtime_error("NUL in service text");
  if (get32(reply.data() + 8) != 0)
    throw std::runtime_error(text.empty() ? "OCR service failed" : text);
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    auto line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      lines.push_back(std::move(line));
    start = end + 1;
  }
  return lines;
}
} // namespace

namespace tesseract {
void TessBaseAPI::SetImage(const unsigned char *data, int width, int height,
                           int bpp, int stride) {
  forget(this);
  real_set_image()(this, data, width, height, bpp, stride);
  const char *path = std::getenv("SPECTACLE_OCR_SOCKET");
  if (!path || !*path || std::strcmp(Api::Version(), "5.5.3") != 0 || !data ||
      bpp != 3 || width <= 0 || height <= 0 ||
      uint64_t(width) * height * 3 > max_image ||
      int64_t(stride) < int64_t(width) * 3)
    return;
  try {
    auto state = std::make_shared<State>();
    state->width = width;
    state->height = height;
    state->rgb.resize(size_t(width) * height * 3);
    for (int row = 0; row < height; ++row)
      std::memcpy(state->rgb.data() + size_t(row) * width * 3,
                  data + size_t(row) * stride, size_t(width) * 3);
    remember(this, std::move(state));
  } catch (const std::exception &error) {
    std::fprintf(stderr, "SpectacleOCR: image capture failed: %s\n",
                 error.what());
  }
}

void TessBaseAPI::SetImage(Pix *pix) {
  forget(this);
  static auto f = original<void (*)(Api *, Pix *)>(
      "_ZN9tesseract11TessBaseAPI8SetImageEP3Pix");
  f(this, pix);
}

int TessBaseAPI::Recognize(ETEXT_DESC *monitor) {
  const bool formula = socr::formulaMode();
  auto state = lookup(this);
  const char *path = std::getenv("SPECTACLE_OCR_SOCKET");
  if (formula && (!state || !path || !*path || monitor || !socr::enabled())) {
    socr::formulaError(QStringLiteral("公式识别不可用，请确认 SpectacleOCR 已启用。"));
    return -1;
  }
  if (!state)
    return real_recognize()(this, monitor);
  bool replaced = state->recognized;
  state->recognized = false;
  if (!path || !*path || monitor || !socr::enabled()) {
    if (replaced)
      real_set_image()(this, state->rgb.data(), state->width, state->height, 3,
                       state->width * 3);
    remember(this, state);
    return real_recognize()(this, monitor);
  }
  try {
    auto lines = request(*state, path, formula);
    // Seed a valid iterator with a tiny blank page. This retains Tesseract's
    // own allocations and avoids running native OCR on the screenshot.
    std::array<unsigned char, 32 * 32 * 3> blank;
    blank.fill(255);
    replaced = true;
    real_set_image()(this, blank.data(), 32, 32, 3, 32 * 3);
    if (real_recognize()(this, nullptr) != 0)
      throw std::runtime_error("iterator seed recognition failed");
    std::unique_ptr<Iterator> seed(real_get_iterator()(this));
    if (!seed)
      throw std::runtime_error("Tesseract did not provide an iterator seed");
    state->lines = std::move(lines);
    state->recognized = true;
    remember(this, state);
    std::fprintf(stderr, "SpectacleOCR: service returned %zu lines\n",
                 state->lines.size());
    return 0;
  } catch (const std::exception &error) {
    if (formula) {
      state->recognized = false;
      remember(this, state);
      socr::formulaError(QString::fromUtf8(error.what()));
      return -1;
    }
    std::fprintf(stderr, "SpectacleOCR: %s; using Tesseract\n", error.what());
    if (replaced)
      real_set_image()(this, state->rgb.data(), state->width, state->height, 3,
                       state->width * 3);
    state->recognized = false;
    remember(this, state);
    return real_recognize()(this, monitor);
  }
}

ResultIterator *TessBaseAPI::GetIterator() {
  auto state = lookup(this);
  if (!state || !state->recognized)
    return real_get_iterator()(this);
  if (state->lines.empty())
    return nullptr;
  std::unique_ptr<Iterator> seed(real_get_iterator()(this));
  return seed ? new TextIterator(*seed, state->lines) : nullptr;
}

void TessBaseAPI::Clear() {
  forget(this);
  static auto f =
      original<void (*)(Api *)>("_ZN9tesseract11TessBaseAPI5ClearEv");
  f(this);
}
void TessBaseAPI::End() {
  forget(this);
  static auto f = original<void (*)(Api *)>("_ZN9tesseract11TessBaseAPI3EndEv");
  f(this);
}
} // namespace tesseract
