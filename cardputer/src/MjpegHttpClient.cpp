#include "MjpegHttpClient.h"
#include "Global.h"   // capFreeze (v0.8.1 拍照冻结)

static const char* TAG = "MJPEG";
const char* MjpegHttpClient::BOUNDARY = "123456789000000000000987654321";

MjpegHttpClient::MjpegHttpClient(uint8_t* buffer, size_t bufferSize)
  : _buf(buffer), _bufSize(bufferSize) {}

MjpegHttpClient::~MjpegHttpClient() { end(); }

void MjpegHttpClient::_setError(const char* msg) {
  strncpy(_error, msg, sizeof(_error) - 1);
}

void MjpegHttpClient::_resetParser() {
  _state = ST_HTTP_HEADERS;
  _lineIdx = 0;
  _chunkRemain = 0;
  _frameRemain = 0;
  _dataLen = 0;
  _inChunk = false;
  _pendingFrame   = false;   // v0.8.7
  _frameTruncated = false;
}

bool MjpegHttpClient::begin() {
  end();
  _connected = false;
  _error[0] = 0;

  if (!_client.connect("192.168.4.1", 80)) {
    _setError("TCP connect failed");
    Serial.printf("[mjpeg] TCP connect FAIL (WiFi=%d)\n", WiFi.status());
    return false;
  }

  _client.setTimeout(5);
  _client.printf("GET /api/v1/stream HTTP/1.1\r\n"
                 "Host: 192.168.4.1\r\n"
                 "Connection: keep-alive\r\n"
                 "\r\n");

  _resetParser();
  _connected = true;
  Serial.println("[mjpeg] stream connected");
  return true;
}

void MjpegHttpClient::end() {
  _connected = false;
  _client.stop();
  _resetParser();
}

// Append byte to line buffer; returns true when a full line is collected.
static inline bool _feedLine(uint8_t c, char* line, int& idx, int maxLen) {
  if (c == '\n') {
    if (idx > 0 && line[idx - 1] == '\r') line[idx - 1] = 0;
    else if (idx < maxLen) line[idx] = 0;
    idx = 0;
    return true;
  }
  if (idx < maxLen - 1) line[idx++] = (char)c;
  return false;
}

// Line is exactly "--<boundary>" (or "--<boundary>--" final, or with trailing spaces)
bool MjpegHttpClient::_isBoundaryLine() {
  const char* b = BOUNDARY;
  if (_line[0] != '-' || _line[1] != '-') return false;
  int i = 0;
  for (; b[i]; i++) {
    if (_line[i + 2] != b[i]) return false;
  }
  return _line[i + 2] == 0 || _line[i + 2] == '-' || _line[i + 2] == ' ';
}

void MjpegHttpClient::_startFrame() {
  _frameRemain = 0;  // set by Content-Length header
  _dataLen = 0;
}

// ── Multipart parser: feed one payload byte ──
// (Called from update() with chunk-decoded bytes)
void MjpegHttpClient::_mpFeed(uint8_t c) {
  if (_state == ST_MULTIPART_HEADERS) {
    if (_feedLine(c, _line, _lineIdx, sizeof(_line))) {
      if (_isBoundaryLine()) {
        _startFrame();  // new part begins
      } else if (strncasecmp(_line, "Content-Length:", 15) == 0) {
        _frameRemain = (size_t)atoi(_line + 15);
        // v0.8.7: 帧比缓冲大 → 整帧丢弃 (旧版 clamp 到 _bufSize 后照样 emit,
        // 交出的是被截断的半张 JPEG → drawJpg 静默失败, 且与流的后续字节错位)
        _frameTruncated = (_frameRemain > _bufSize);
        if (_frameTruncated) {
          _dropped++;
          Serial.printf("[mjpeg] frame %u > buf %u → dropped (#%u)\n",
                        (unsigned)_frameRemain, (unsigned)_bufSize, (unsigned)_dropped);
        }
      } else if (_line[0] == 0 && _frameRemain > 0) {
        // blank line AND we know the frame size → body follows
        _state = ST_FRAME_DATA;
      }
      // blank line before Content-Length is ignored (stream preamble CRLF)
    }
    return;
  }

  if (_state == ST_FRAME_DATA) {
    if (_frameRemain > 0) {
      if (!_frameTruncated && _dataLen < _bufSize) _buf[_dataLen++] = c;
      _frameRemain--;
    } else if (!_frameTruncated && onFrameReady && _dataLen > 100) {
      // Frame complete → emit, then STOP this update() round.
      // v0.8.7: 旧版 emit 后继续排空 socket → 同一次 update() 里的第二帧会覆写
      // 同一个缓冲, 而渲染在本轮 loop 后半段才发生 → 渲染器读到"半旧半新"的合成图
      // (偶发花屏/整帧跳过的最可能根因)。剩余字节留在 socket 缓冲区, 下轮继续, 不丢字节。
      size_t n = _dataLen;
      _dataLen = 0;
      _state   = ST_MULTIPART_HEADERS;
      _pendingFrame = true;
      onFrameReady(n);
      return;
    } else {
      // 完整帧但无回调, 或超缓冲被丢弃 → 只重置状态, 继续扫下一个 boundary
      _dataLen = 0;
      _frameTruncated = false;
      _state = ST_MULTIPART_HEADERS;
    }
    return;
  }

  // Unexpected state — restart multipart scan
  _state = ST_MULTIPART_HEADERS;
}

void MjpegHttpClient::update() {
  if (!_connected) return;
  if (capFreeze) return;   // v0.8.1: 拍照期间冻结 — 不吞字节, 解冻后状态机从原处继续

  while (_client.available() && !_pendingFrame) {   // v0.8.7: 交帧后立即收手
    uint8_t c = _client.read();

    // ═══ Level 0: HTTP response headers (one-time) ═══
    if (_state == ST_HTTP_HEADERS) {
      if (_feedLine(c, _line, _lineIdx, sizeof(_line))) {
        if (_line[0] == 0) {
          _state = _inChunk ? ST_CHUNK_SIZE : ST_MULTIPART_HEADERS;
        } else if (strncasecmp(_line, "Transfer-Encoding:", 18) == 0 &&
                   strstr(_line, "chunked")) {
          _inChunk = true;
        }
      }
      continue;
    }

    // ═══ Level 1: chunked transfer decoding ═══
    if (_inChunk) {
      if (_state == ST_CHUNK_SIZE) {
        if (_feedLine(c, _line, _lineIdx, sizeof(_line))) {
          if (_line[0] == 0) continue;  // ignore CRLF between chunks
          _chunkRemain = (size_t)strtoul(_line, nullptr, 16);
          if (_chunkRemain == 0) {
            _setError("Stream ended");
            _connected = false;
            return;
          }
          _state = ST_CHUNK_DATA;
        }
        continue;
      }
      // ST_CHUNK_DATA: feed payload bytes to multipart parser
      if (_chunkRemain > 0) {
        _chunkRemain--;
        _mpFeed(c);
      } else {
        // chunk done; next line is hex size (CRLF skipped by strtoul)
        _state = ST_CHUNK_SIZE;
      }
      continue;
    }

    // ═══ Level 2: non-chunked → feed bytes straight to multipart ═══
    _mpFeed(c);
  }
  _pendingFrame = false;   // v0.8.7: 本轮结束, 允许下一轮继续解析

  if (!_client.connected() && _connected) {
    _setError("Connection lost");
    _connected = false;
    Serial.printf("[mjpeg] LOST (state=%d, frames=%u)\n",
      (int)_state, (unsigned)_dataLen);
  }
}
