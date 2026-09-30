#ifndef __MJPEG_HTTP_CLIENT_H__
#define __MJPEG_HTTP_CLIENT_H__

#include <Arduino.h>
#include <WiFi.h>
#include <functional>

// MJPEG HTTP stream client for CAMS3 default AP firmware.
//
// Parses multipart/x-mixed-replace stream using Content-Length as the
// authoritative frame boundary (NOT SOI/EOI byte search — JPEG scan data
// can contain false EOI markers when byte-stuffing is not strict).
//
// Uses caller-provided buffer — no extra heap allocation.
//
// Stream format (CAMS3 factory firmware /api/v1/stream):
//   HTTP/1.1 200 OK
//   Content-Type: multipart/x-mixed-replace;boundary=123456789000000000000987654321
//   Transfer-Encoding: chunked
//
//   \r\n--123456789000000000000987654321\r\n
//   Content-Type: image/jpeg\r\n
//   Content-Length: <N>\r\n
//   \r\n
//   <N bytes JPEG>
//   \r\n--boundary...  (next frame)
//
// Two-layer parsing:
//   1. HTTP chunked transfer decoding (hex size lines)
//   2. multipart frame headers → Content-Length → exact JPEG body copy
class MjpegHttpClient {
public:
  // buffer: where to write parsed JPEG frames
  // bufferSize: max frame size (e.g. 64KB)
  MjpegHttpClient(uint8_t* buffer, size_t bufferSize);
  ~MjpegHttpClient();

  bool begin();          // connect to 192.168.4.1 and start GET /api/v1/stream
  void end();            // stop client + reset parser
  void update();         // pump: read socket, parse, call onFrameReady

  bool isConnected() const { return _connected; }
  const char* lastError() const { return _error; }

  // Called when a complete JPEG frame is ready in the buffer
  std::function<void(size_t len)> onFrameReady;

private:
  WiFiClient _client;
  bool _connected = false;
  char _error[64] = "";

  // External buffer (points to s_frame_buf in main.cpp)
  uint8_t* _buf;
  size_t   _bufSize;

  // ── Parser state machine ──
  enum State : uint8_t {
    ST_HTTP_HEADERS,      // reading initial HTTP response headers
    ST_CHUNK_SIZE,        // reading hex chunk-size line
    ST_CHUNK_DATA,        // inside chunk body (may contain multipart)
    ST_MULTIPART_HEADERS, // reading \r\n--boundary + Content-* headers
    ST_FRAME_DATA,        // copying JPEG body (Content-Length bytes)
  };
  State _state = ST_HTTP_HEADERS;

  char _line[160];        // line buffer for headers
  int  _lineIdx = 0;
  size_t _chunkRemain = 0; // bytes left in current chunk
  size_t _frameRemain = 0; // bytes left in current JPEG frame
  size_t _dataLen = 0;     // bytes copied into _buf for current frame
  bool _inChunk = false;   // saw Transfer-Encoding: chunked

  // ── v0.8.7 ──
  // _pendingFrame: 已交出一帧 → 本轮 update() 立刻收手, 剩下的字节留在 socket 里.
  //   旧版 emit 后继续排空 socket, 同一 update() 内解析到第二帧就会覆写同一个
  //   s_frame_buf[0], 而渲染发生在本轮 loop 的后半段 → 渲染器读到"半旧半新"合成图 (花屏).
  bool _pendingFrame = false;
  // _frameTruncated: Content-Length 大于缓冲 → 整帧丢弃 (仍按长度消费以保持流同步)
  bool _frameTruncated = false;
  uint32_t _dropped = 0;   // 丢弃帧计数

  // Feed one payload byte into the multipart parser (line-based until body)
  void _mpFeed(uint8_t c);

  static const char* BOUNDARY;  // "123456789000000000000987654321"

  void _resetParser();
  void _setError(const char* msg);
  bool _isBoundaryLine();   // line == "--<boundary>"
  void _startFrame();       // begin capturing frame body
};

#endif
