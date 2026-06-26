#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <stdarg.h>

static constexpr uint32_t PRINT_PACKET_MAGIC = 0x52504247u;
static constexpr uint8_t PRINT_PACKET_VERSION = 1;
static constexpr uint32_t UART_BAUD = 921600;
static constexpr size_t MAX_PAYLOAD = 256;
static constexpr uint8_t STATUS_DONE = 6;
static constexpr uint8_t STATUS_CONNECTION_ERROR = 8;
static constexpr uint8_t STATUS_TRANSPORT_ERROR = 9;
static constexpr uint8_t STATUS_PRINTER_CONNECTED = 10;
static constexpr uint8_t STATUS_PRINTER_DISCONNECTED = 11;
static constexpr uint32_t LOG_BAUD = 115200;
static constexpr size_t PRINTER_BLE_CHUNK_SIZE = 20;
static constexpr uint32_t PRINTER_BLE_CHUNK_DELAY_US = 800;

#define SERVICE_UUID "0000ff00-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_TX "0000ff02-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_RX "0000ff03-0000-1000-8000-00805f9b34fb"
#define PRINTER_MAC_ADDRESS "3f:78:0f:5e:07:ef"
#define USE_MAC_ADDRESS false

#ifndef PRINT_UART_RX_PIN
#define PRINT_UART_RX_PIN 44
#endif

#ifndef PRINT_UART_TX_PIN
#define PRINT_UART_TX_PIN 43
#endif

enum PacketType : uint8_t {
  PKT_START = 1,
  PKT_DATA = 2,
  PKT_END = 3,
  PKT_CANCEL = 4,
};

struct __attribute__((packed)) PacketHeader {
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t seq;
  uint32_t jobId;
  uint16_t length;
  uint16_t crc16;
};

struct PrintJob {
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t bytesPerRow = 0;
  uint32_t expectedBytes = 0;
  uint32_t receivedBytes = 0;
  uint8_t *raster = nullptr;
  uint32_t jobId = 0;
};

static const char *PRINTER_NAME_HINTS[] = {"T02", "Phomemo"};
static BLEAdvertisedDevice *printerDevice = nullptr;
static BLERemoteCharacteristic *printerWriteChar = nullptr;
static BLERemoteCharacteristic *printerNotifyChar = nullptr;
static BLEClient *printerClient = nullptr;
static PrintJob job;
static bool printerConnected = false;
static unsigned long totalPrinterBytesSent = 0;
static unsigned long totalPrinterPacketsSent = 0;

static void logf(const char *fmt, ...) {
  char message[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  Serial.printf("[%10lu ms] %s\n", millis(), message);
}

static void sendRp2040Status(uint8_t status) {
  Serial1.write(status);
}

static uint16_t crc16CcittUpdate(uint16_t crc, const uint8_t *data, size_t len) {
  while (len--) {
    crc ^= static_cast<uint16_t>(*data++) << 8;
    for (int i = 0; i < 8; ++i) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

static void clearJob() {
  free(job.raster);
  job = PrintJob{};
}

class PrinterAdvertisedCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    const std::string name = advertisedDevice.getName();
    logf("BLE scan result: name='%s' addr=%s", name.c_str(), advertisedDevice.getAddress().toString().c_str());
    if (USE_MAC_ADDRESS && advertisedDevice.getAddress().toString() == std::string(PRINTER_MAC_ADDRESS)) {
      logf("BLE printer candidate matched by MAC");
      printerDevice = new BLEAdvertisedDevice(advertisedDevice);
      BLEDevice::getScan()->stop();
      return;
    }
    for (const char *hint : PRINTER_NAME_HINTS) {
      if (name.find(hint) != std::string::npos) {
        logf("BLE printer candidate matched by name hint '%s'", hint);
        printerDevice = new BLEAdvertisedDevice(advertisedDevice);
        BLEDevice::getScan()->stop();
        return;
      }
    }
  }
};

class PrinterClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient *) override {
    printerConnected = true;
    logf("BLE client callback: connected");
  }

  void onDisconnect(BLEClient *) override {
    printerConnected = false;
    printerWriteChar = nullptr;
    printerNotifyChar = nullptr;
    sendRp2040Status(STATUS_PRINTER_DISCONNECTED);
    logf("BLE client callback: disconnected");
  }
};

static void printerNotifyCallback(BLERemoteCharacteristic *, uint8_t *data, size_t len, bool isNotify) {
  char hex[64] = {0};
  const size_t shown = min<size_t>(len, 16);
  for (size_t i = 0; i < shown; ++i) {
    snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02x ", data[i]);
  }
  logf("BLE %s from printer: len=%u data=%s%s",
       isNotify ? "notify" : "indicate",
       static_cast<unsigned>(len),
       hex,
       len > shown ? "..." : "");
}

static bool discoverPrinterByName() {
  if (printerDevice) {
    logf("BLE using cached scan result: %s", printerDevice->getAddress().toString().c_str());
    return true;
  }

  logf("BLE scanning for printer, useMac=%d", USE_MAC_ADDRESS);
  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new PrinterAdvertisedCallbacks(), true);
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);
  scan->start(30, false);
  logf("BLE scan finished: %s", printerDevice ? "printer found" : "printer not found");
  return printerDevice != nullptr;
}

static bool connectPrinter() {
  if (printerClient && printerClient->isConnected() && printerConnected && printerWriteChar) {
    logf("BLE printer already connected");
    return true;
  }

  logf("BLE connecting printer");
  printerWriteChar = nullptr;
  printerNotifyChar = nullptr;
  if (printerClient) {
    logf("BLE disconnecting stale client");
    printerClient->disconnect();
    delete printerClient;
    printerClient = nullptr;
  }
  printerConnected = false;

  if (!discoverPrinterByName()) {
    return false;
  }

  printerClient = BLEDevice::createClient();
  printerClient->setClientCallbacks(new PrinterClientCallbacks());
  logf("BLE connect target: %s", printerDevice->getAddress().toString().c_str());
  const bool connected = printerClient->connect(printerDevice);
  if (!connected) {
    logf("ERROR BLE connect failed");
    sendRp2040Status(STATUS_PRINTER_DISCONNECTED);
    delete printerClient;
    printerClient = nullptr;
    return false;
  }
  logf("BLE connected");

  const int mtu = printerClient->setMTU(512);
  logf("BLE negotiated MTU: %d", mtu);
  delay(1000);

  BLERemoteService *service = printerClient->getService(BLEUUID(SERVICE_UUID));
  if (!service) {
    logf("ERROR BLE service not found: %s", SERVICE_UUID);
    sendRp2040Status(STATUS_PRINTER_DISCONNECTED);
    printerClient->disconnect();
    return false;
  }
  logf("BLE service found: %s", SERVICE_UUID);

  printerWriteChar = service->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID_TX));
  if (!printerWriteChar || !(printerWriteChar->canWrite() || printerWriteChar->canWriteNoResponse())) {
    logf("ERROR BLE TX characteristic missing/not writable: %s", CHARACTERISTIC_UUID_TX);
    sendRp2040Status(STATUS_PRINTER_DISCONNECTED);
    printerClient->disconnect();
    printerWriteChar = nullptr;
    return false;
  }
  logf("BLE TX characteristic ready: %s write=%d writeNoResp=%d",
       CHARACTERISTIC_UUID_TX,
       printerWriteChar->canWrite(),
       printerWriteChar->canWriteNoResponse());

  printerNotifyChar = service->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID_RX));
  if (printerNotifyChar && printerNotifyChar->canNotify()) {
    printerNotifyChar->registerForNotify(printerNotifyCallback);
    logf("BLE RX notify characteristic subscribed: %s", CHARACTERISTIC_UUID_RX);
  } else if (printerNotifyChar) {
    logf("BLE RX characteristic found but cannot notify: %s", CHARACTERISTIC_UUID_RX);
  } else {
    logf("BLE RX notify characteristic not found: %s", CHARACTERISTIC_UUID_RX);
  }

  sendRp2040Status(STATUS_PRINTER_CONNECTED);
  return true;
}

static bool writePrinterBytes(const uint8_t *data, size_t len) {
  if (!connectPrinter()) {
    logf("ERROR BLE write aborted: printer not connected");
    return false;
  }

  while (len) {
    if (!printerClient || !printerClient->isConnected() || !printerConnected || !printerWriteChar) {
      logf("ERROR BLE disconnected during write, remaining=%u", static_cast<unsigned>(len));
      return false;
    }
    const size_t chunk = min<size_t>(len, PRINTER_BLE_CHUNK_SIZE);
    printerWriteChar->writeValue(const_cast<uint8_t *>(data), chunk, false);
    totalPrinterBytesSent += chunk;
    totalPrinterPacketsSent++;
    data += chunk;
    len -= chunk;
    delayMicroseconds(PRINTER_BLE_CHUNK_DELAY_US);
  }
  return true;
}

static bool feedBlankRasterRows(uint16_t bytesPerRow, uint16_t rows) {
  uint8_t blankRow[64] = {0};
  if (bytesPerRow > sizeof(blankRow)) {
    logf("ERROR feed bytesPerRow too large: %u", bytesPerRow);
    return false;
  }

  while (rows) {
    const uint16_t blockRows = rows > 32 ? 32 : rows;
    const uint8_t blockHeader[] = {
        0x1d, 0x76, 0x30, 0x00,
        static_cast<uint8_t>(bytesPerRow & 0xff),
        static_cast<uint8_t>(bytesPerRow >> 8),
        static_cast<uint8_t>(blockRows & 0xff),
        static_cast<uint8_t>(blockRows >> 8),
    };

    if (!writePrinterBytes(blockHeader, sizeof(blockHeader))) {
      return false;
    }
    for (uint16_t y = 0; y < blockRows; ++y) {
      if (!writePrinterBytes(blankRow, bytesPerRow)) {
        return false;
      }
    }
    rows -= blockRows;
  }

  return true;
}

static bool sendPhomemoRaster(const PrintJob &j) {
  if (!j.raster || !j.width || !j.height || j.receivedBytes != j.expectedBytes) {
    logf("ERROR print job invalid: raster=%p width=%u height=%u received=%lu expected=%lu",
         j.raster,
         j.width,
         j.height,
         static_cast<unsigned long>(j.receivedBytes),
         static_cast<unsigned long>(j.expectedBytes));
    return false;
  }

  const uint8_t printerHeader[] = {
      0x1b, 0x40,
      0x1b, 0x61, 0x01,
      0x1f, 0x11, 0x02, 0x04,
  };
  const uint8_t printerFooter[] = {
      0x1f, 0x11, 0x08,
      0x1f, 0x11, 0x0e,
      0x1f, 0x11, 0x07,
      0x1f, 0x11, 0x09,
  };

  logf("PRINT start: job=%lu width=%u height=%u bytesPerRow=%u bytes=%lu",
       static_cast<unsigned long>(j.jobId),
       j.width,
       j.height,
       j.bytesPerRow,
       static_cast<unsigned long>(j.expectedBytes));

  totalPrinterBytesSent = 0;
  totalPrinterPacketsSent = 0;
  const unsigned long printStartedAt = millis();

  logf("PRINT send header");
  if (!writePrinterBytes(printerHeader, sizeof(printerHeader))) {
    logf("ERROR print header write failed");
    return false;
  }
  delay(100);

  for (uint16_t y = 0; y < j.height; y += 2) {
    uint8_t blockBuffer[8 + (64 * 2)];
    size_t blockLen = 0;
    const uint8_t blockMarker[] = {
        0x1d, 0x76, 0x30, 0x00,
        static_cast<uint8_t>(j.bytesPerRow & 0xff),
        static_cast<uint8_t>(j.bytesPerRow >> 8),
    };
    uint8_t blankRow[64] = {0};

    if (j.bytesPerRow > sizeof(blankRow)) {
      logf("ERROR bytesPerRow too large: %u > %u", j.bytesPerRow, static_cast<unsigned>(sizeof(blankRow)));
      return false;
    }
    if (sizeof(blockMarker) + 2 + j.bytesPerRow * 2 > sizeof(blockBuffer)) {
      logf("ERROR print block buffer too small");
      return false;
    }
    if ((y % 32) == 0) {
      const uint16_t rowEnd = (y + 31 < j.height) ? y + 31 : j.height - 1;
      logf("PRINT rows %u-%u / %u", y, rowEnd, j.height);
    }

    memcpy(blockBuffer + blockLen, blockMarker, sizeof(blockMarker));
    blockLen += sizeof(blockMarker);
    blockBuffer[blockLen++] = 0x02;
    blockBuffer[blockLen++] = 0x00;
    memcpy(blockBuffer + blockLen, j.raster + y * j.bytesPerRow, j.bytesPerRow);
    blockLen += j.bytesPerRow;
    if (y + 1 < j.height) {
      memcpy(blockBuffer + blockLen, j.raster + (y + 1) * j.bytesPerRow, j.bytesPerRow);
    } else {
      memcpy(blockBuffer + blockLen, blankRow, j.bytesPerRow);
    }
    blockLen += j.bytesPerRow;

    if (!writePrinterBytes(blockBuffer, blockLen)) {
      logf("ERROR print block header failed at row=%u", y);
      return false;
    }
    yield();
  }

  logf("PRINT feed paper with blank raster rows");
  if (!feedBlankRasterRows(j.bytesPerRow, 96)) {
    logf("ERROR print blank raster feed failed");
    return false;
  }

  logf("PRINT send footer");
  if (!writePrinterBytes(printerFooter, sizeof(printerFooter))) {
    logf("ERROR print footer write failed");
    return false;
  }
  delay(500);
  const unsigned long duration = millis() - printStartedAt;
  logf("PRINT completed: job=%lu bytes=%lu packets=%lu durationMs=%lu",
       static_cast<unsigned long>(j.jobId),
       totalPrinterBytesSent,
       totalPrinterPacketsSent,
       duration);
  return true;
}

static bool readExact(uint8_t *dst, size_t len, uint32_t timeoutMs) {
  const uint32_t started = millis();
  size_t got = 0;
  while (got < len && millis() - started < timeoutMs) {
    if (Serial1.available()) {
      dst[got++] = static_cast<uint8_t>(Serial1.read());
    } else {
      delay(1);
    }
  }
  return got == len;
}

static bool readPacket(PacketHeader &hdr, uint8_t *payload) {
  if (!readExact(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr), 1000)) {
    logf("ERROR UART packet header timeout");
    return false;
  }

  if (hdr.magic != PRINT_PACKET_MAGIC || hdr.version != PRINT_PACKET_VERSION || hdr.length > MAX_PAYLOAD) {
    logf("ERROR UART packet header invalid: magic=0x%08lx version=%u type=%u seq=%u job=%lu len=%u",
         static_cast<unsigned long>(hdr.magic),
         hdr.version,
         hdr.type,
         hdr.seq,
         static_cast<unsigned long>(hdr.jobId),
         hdr.length);
    return false;
  }

  if (hdr.length && !readExact(payload, hdr.length, 1000)) {
    logf("ERROR UART packet payload timeout: type=%u seq=%u job=%lu len=%u",
         hdr.type,
         hdr.seq,
         static_cast<unsigned long>(hdr.jobId),
         hdr.length);
    return false;
  }

  uint16_t crc = 0xffff;
  crc = crc16CcittUpdate(crc, &hdr.version, sizeof(hdr) - sizeof(hdr.magic) - sizeof(hdr.crc16));
  crc = crc16CcittUpdate(crc, payload, hdr.length);
  if (crc != hdr.crc16) {
    logf("ERROR UART packet CRC mismatch: type=%u seq=%u job=%lu len=%u got=0x%04x expected=0x%04x",
         hdr.type,
         hdr.seq,
         static_cast<unsigned long>(hdr.jobId),
         hdr.length,
         hdr.crc16,
         crc);
    return false;
  }

  return true;
}

static void handlePacket(const PacketHeader &hdr, const uint8_t *payload) {
  switch (hdr.type) {
    case PKT_START:
      logf("UART START packet: seq=%u job=%lu len=%u", hdr.seq, static_cast<unsigned long>(hdr.jobId), hdr.length);
      if (hdr.length != 12) {
        logf("ERROR START payload length invalid: %u", hdr.length);
        Serial1.write(STATUS_TRANSPORT_ERROR);
        clearJob();
        return;
      }
      clearJob();
      job.width = payload[0] | (payload[1] << 8);
      job.height = payload[2] | (payload[3] << 8);
      job.bytesPerRow = payload[4] | (payload[5] << 8);
      job.expectedBytes = payload[8] | (payload[9] << 8) | (payload[10] << 16) | (payload[11] << 24);
      job.jobId = hdr.jobId;
      logf("UART job metadata: job=%lu width=%u height=%u bytesPerRow=%u expectedBytes=%lu flags=0x%02x",
           static_cast<unsigned long>(job.jobId),
           job.width,
           job.height,
           job.bytesPerRow,
           static_cast<unsigned long>(job.expectedBytes),
           payload[6]);
      job.raster = static_cast<uint8_t *>(malloc(job.expectedBytes));
      if (!job.raster) {
        logf("ERROR malloc failed for job bytes=%lu", static_cast<unsigned long>(job.expectedBytes));
        Serial1.write(STATUS_TRANSPORT_ERROR);
        clearJob();
      }
      break;

    case PKT_DATA:
      if (!job.raster || job.jobId != hdr.jobId || job.receivedBytes + hdr.length > job.expectedBytes) {
        logf("ERROR DATA rejected: raster=%p hdrJob=%lu activeJob=%lu received=%lu len=%u expected=%lu",
             job.raster,
             static_cast<unsigned long>(hdr.jobId),
             static_cast<unsigned long>(job.jobId),
             static_cast<unsigned long>(job.receivedBytes),
             hdr.length,
             static_cast<unsigned long>(job.expectedBytes));
        Serial1.write(STATUS_TRANSPORT_ERROR);
        clearJob();
        return;
      }
      memcpy(job.raster + job.receivedBytes, payload, hdr.length);
      job.receivedBytes += hdr.length;
      if ((job.receivedBytes % 2048) < hdr.length || job.receivedBytes == job.expectedBytes) {
        logf("UART DATA progress: job=%lu received=%lu/%lu",
             static_cast<unsigned long>(job.jobId),
             static_cast<unsigned long>(job.receivedBytes),
             static_cast<unsigned long>(job.expectedBytes));
      }
      break;

    case PKT_END:
      logf("UART END packet: seq=%u job=%lu received=%lu/%lu",
           hdr.seq,
           static_cast<unsigned long>(hdr.jobId),
           static_cast<unsigned long>(job.receivedBytes),
           static_cast<unsigned long>(job.expectedBytes));
      if (!connectPrinter()) {
        logf("ERROR END cannot connect printer");
        Serial1.write(STATUS_CONNECTION_ERROR);
        clearJob();
        return;
      }
      if (sendPhomemoRaster(job)) {
        logf("UART status -> DONE");
        Serial1.write(STATUS_DONE);
      } else {
        logf("UART status -> TRANSPORT_ERROR");
        Serial1.write(STATUS_TRANSPORT_ERROR);
      }
      clearJob();
      break;

    case PKT_CANCEL:
      logf("UART CANCEL packet: seq=%u job=%lu", hdr.seq, static_cast<unsigned long>(hdr.jobId));
      clearJob();
      break;

    default:
      logf("ERROR unknown UART packet type: %u seq=%u job=%lu", hdr.type, hdr.seq, static_cast<unsigned long>(hdr.jobId));
      Serial1.write(STATUS_TRANSPORT_ERROR);
      clearJob();
      break;
  }
}

void setup() {
  Serial.begin(LOG_BAUD);
  delay(250);
  logf("BOOT XIAO ESP32S3 Phomemo bridge");
  logf("UART RP2040: baud=%lu rx=%d tx=%d", static_cast<unsigned long>(UART_BAUD), PRINT_UART_RX_PIN, PRINT_UART_TX_PIN);
  logf("BLE config: mac=%s useMac=%d service=%s tx=%s rx=%s",
       PRINTER_MAC_ADDRESS,
       USE_MAC_ADDRESS,
       SERVICE_UUID,
       CHARACTERISTIC_UUID_TX,
       CHARACTERISTIC_UUID_RX);
  Serial1.begin(UART_BAUD, SERIAL_8N1, PRINT_UART_RX_PIN, PRINT_UART_TX_PIN);
  BLEDevice::init("GB-DMG-Printer-Bridge");
  logf("READY waiting for RP2040 packets");
}

void loop() {
  if (Serial1.available() < static_cast<int>(sizeof(PacketHeader))) {
    delay(1);
    return;
  }

  PacketHeader hdr;
  uint8_t payload[MAX_PAYLOAD];
  if (!readPacket(hdr, payload)) {
    Serial1.write(STATUS_TRANSPORT_ERROR);
    clearJob();
    return;
  }

  handlePacket(hdr, payload);
}
