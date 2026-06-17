#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

static constexpr uint32_t PRINT_PACKET_MAGIC = 0x52504247u;
static constexpr uint8_t PRINT_PACKET_VERSION = 1;
static constexpr uint32_t UART_BAUD = 921600;
static constexpr size_t MAX_PAYLOAD = 256;
static constexpr uint8_t STATUS_DONE = 6;
static constexpr uint8_t STATUS_CONNECTION_ERROR = 8;
static constexpr uint8_t STATUS_TRANSPORT_ERROR = 9;

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
static BLEClient *printerClient = nullptr;
static PrintJob job;

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
    for (const char *hint : PRINTER_NAME_HINTS) {
      if (name.find(hint) != std::string::npos) {
        printerDevice = new BLEAdvertisedDevice(advertisedDevice);
        BLEDevice::getScan()->stop();
        return;
      }
    }
  }
};

static BLERemoteCharacteristic *findWritableCharacteristic(BLERemoteService *service) {
  const auto *chars = service->getCharacteristics();
  for (auto it = chars->begin(); it != chars->end(); ++it) {
    BLERemoteCharacteristic *ch = it->second;
    if (ch->canWrite() || ch->canWriteNoResponse()) {
      return ch;
    }
  }
  return nullptr;
}

static bool connectPrinter() {
  if (printerClient && printerClient->isConnected() && printerWriteChar) {
    return true;
  }

  printerWriteChar = nullptr;
  if (printerClient) {
    printerClient->disconnect();
    delete printerClient;
    printerClient = nullptr;
  }

  if (!printerDevice) {
    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new PrinterAdvertisedCallbacks(), true);
    scan->setActiveScan(true);
    scan->start(8, false);
  }

  if (!printerDevice) {
    return false;
  }

  printerClient = BLEDevice::createClient();
  if (!printerClient->connect(printerDevice)) {
    return false;
  }

  const auto *services = printerClient->getServices();
  for (auto it = services->begin(); it != services->end(); ++it) {
    printerWriteChar = findWritableCharacteristic(it->second);
    if (printerWriteChar) {
      return true;
    }
  }

  return false;
}

static bool writePrinterBytes(const uint8_t *data, size_t len) {
  if (!connectPrinter()) {
    return false;
  }

  while (len) {
    const size_t chunk = min<size_t>(len, 180);
    printerWriteChar->writeValue(const_cast<uint8_t *>(data), chunk, false);
    data += chunk;
    len -= chunk;
    delay(8);
  }
  return true;
}

static bool sendPhomemoRaster(const PrintJob &j) {
  if (!j.raster || !j.width || !j.height || j.receivedBytes != j.expectedBytes) {
    return false;
  }

  const uint8_t printerHeader[] = {
      0x1b, 0x40,
      0x1b, 0x61, 0x01,
      0x1f, 0x11, 0x02, 0x04,
  };
  const uint8_t printerFooter[] = {
      0x1b, 0x64, 0x08,
      0x1f, 0x11, 0x08,
      0x1f, 0x11, 0x0e,
      0x1f, 0x11, 0x07,
      0x1f, 0x11, 0x09,
  };

  if (!writePrinterBytes(printerHeader, sizeof(printerHeader))) {
    return false;
  }

  for (uint16_t y = 0; y < j.height; y += 2) {
    const uint8_t blockHeader[] = {
        0x1d, 0x76, 0x30, 0x00,
        static_cast<uint8_t>(j.bytesPerRow & 0xff),
        static_cast<uint8_t>(j.bytesPerRow >> 8),
        0x02, 0x00,
    };
    uint8_t blankRow[64] = {0};

    if (j.bytesPerRow > sizeof(blankRow)) {
      return false;
    }
    if (!writePrinterBytes(blockHeader, sizeof(blockHeader))) {
      return false;
    }
    if (!writePrinterBytes(j.raster + y * j.bytesPerRow, j.bytesPerRow)) {
      return false;
    }
    if (y + 1 < j.height) {
      if (!writePrinterBytes(j.raster + (y + 1) * j.bytesPerRow, j.bytesPerRow)) {
        return false;
      }
    } else if (!writePrinterBytes(blankRow, j.bytesPerRow)) {
      return false;
    }
  }

  return writePrinterBytes(printerFooter, sizeof(printerFooter));
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
    return false;
  }

  if (hdr.magic != PRINT_PACKET_MAGIC || hdr.version != PRINT_PACKET_VERSION || hdr.length > MAX_PAYLOAD) {
    return false;
  }

  if (hdr.length && !readExact(payload, hdr.length, 1000)) {
    return false;
  }

  uint16_t crc = 0xffff;
  crc = crc16CcittUpdate(crc, &hdr.version, sizeof(hdr) - sizeof(hdr.magic) - sizeof(hdr.crc16));
  crc = crc16CcittUpdate(crc, payload, hdr.length);
  return crc == hdr.crc16;
}

static void handlePacket(const PacketHeader &hdr, const uint8_t *payload) {
  switch (hdr.type) {
    case PKT_START:
      if (hdr.length != 12) {
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
      job.raster = static_cast<uint8_t *>(malloc(job.expectedBytes));
      if (!job.raster) {
        Serial1.write(STATUS_TRANSPORT_ERROR);
        clearJob();
      }
      break;

    case PKT_DATA:
      if (!job.raster || job.jobId != hdr.jobId || job.receivedBytes + hdr.length > job.expectedBytes) {
        Serial1.write(STATUS_TRANSPORT_ERROR);
        clearJob();
        return;
      }
      memcpy(job.raster + job.receivedBytes, payload, hdr.length);
      job.receivedBytes += hdr.length;
      break;

    case PKT_END:
      if (!connectPrinter()) {
        Serial1.write(STATUS_CONNECTION_ERROR);
        clearJob();
        return;
      }
      Serial1.write(sendPhomemoRaster(job) ? STATUS_DONE : STATUS_TRANSPORT_ERROR);
      clearJob();
      break;

    case PKT_CANCEL:
      clearJob();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(UART_BAUD, SERIAL_8N1, 20, 21);
  BLEDevice::init("GB-DMG-Printer-Bridge");
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
