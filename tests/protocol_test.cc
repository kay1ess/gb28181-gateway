#include <iostream>
#include <string>

#include "src/adapters/sip/sdp_codec.h"
#include "src/adapters/sip/xml_codec.h"
#include "src/application/catalog_accumulator.h"
#include "tests/test_support.h"

namespace {
using namespace gb28181;
using test::Require;
using test::RequireOk;
const char kPlatform[] = "34020000002000000002";
const char kCamera[] = "34020000001320000001";

void TestXml() {
  auto query = sip::BuildQuery("Catalog", 123, kPlatform);
  RequireOk(query, "build query");
  auto parsed = sip::ParseXml(query.value());
  RequireOk(parsed, "parse generated query");
  Require(parsed.value().sn == 123 && parsed.value().device_id == kPlatform,
          "query identity round trip");
  Require(!sip::BuildQuery("Catalog", 0, kPlatform).ok(), "zero SN rejected");
  Require(!sip::BuildQuery("Catalog", 1, "bad<&id").ok(),
          "invalid identity rejected");
  const std::string header =
      "<Response><CmdType>Catalog</CmdType><SN>1</SN><DeviceID>" +
      std::string(kPlatform) + "</DeviceID><SumNum>1</SumNum>";
  const std::string item =
      "<Item><DeviceID>" + std::string(kCamera) +
      "</DeviceID><Name>摄像头 &amp; 1</Name><Status>ON</Status></Item>";
  auto catalog = sip::ParseXml(header + "<DeviceList Num=\"1\">" + item +
                               "</DeviceList></Response>");
  RequireOk(catalog, "UTF-8 catalog");
  Require(catalog.value().resources[0].name == "摄像头 & 1",
          "XML entities decoded");
  Require(catalog.value().resources[0].state == OnlineState::kOnline,
          "catalog liveness decoded");
  auto attributes = sip::ParseXml(
      "<Response CmdType=\"Catalog\" SN=\"17\"><DeviceID>" +
      std::string(kPlatform) + "</DeviceID><SumNum>1</SumNum>" +
      "<DeviceList Num=\"1\">" + item + "</DeviceList></Response>");
  RequireOk(attributes, "independent device attribute-form catalog");
  Require(
      attributes.value().sn == 17 && attributes.value().resources.size() == 1,
      "attribute envelope preserves query identity");
  Require(!sip::ParseXml(
               "<Notify CmdType=\"Keepalive\" SN=\"17\"><SN>18</SN><DeviceID>" +
               std::string(kPlatform) + "</DeviceID></Notify>")
               .ok(),
          "conflicting attribute and child identities rejected");
  Require(!sip::ParseXml(header + "<DeviceList Num=\"2\">" + item +
                         "</DeviceList></Response>")
               .ok(),
          "mismatched item count rejected");
  Require(!sip::ParseXml("<Response><broken>").ok(), "malformed XML rejected");
  Require(!sip::ParseXml(std::string(1024 * 1024 + 1, 'a')).ok(),
          "large body rejected");
  Require(
      !sip::ParseXml("<!DOCTYPE a [<!ENTITY x 'bad'>]>" + query.value()).ok(),
      "DTD rejected");
  Require(!sip::ParseXml(query.value() + "<Query/>").ok(),
          "multiple roots rejected");
  std::string legacy =
      "<?xml version=\"1.0\" "
      "encoding=\"GB2312\"?><Response><CmdType>DeviceInfo</CmdType><SN>9</"
      "SN><DeviceID>";
  legacy += kCamera;
  legacy +=
      "</DeviceID><DeviceName>\xc9\xe3\xcf\xf1\xcd\xb7</DeviceName></Response>";
  auto converted = sip::ParseXml(legacy);
  RequireOk(converted, "GB2312 device info");
  Require(converted.value().info.name == "摄像头", "GB2312 converted to UTF-8");
}

void TestSdp() {
  for (auto mode : {RtpReceiveMode::kUdp, RtpReceiveMode::kTcpPassive,
                    RtpReceiveMode::kTcpActive}) {
    auto offer = sip::BuildLiveSdp(kPlatform, "127.0.0.1", 30000, 123, mode);
    RequireOk(offer, "build SDP");
    Require(offer.value().find("y=0000000123\r\n") != std::string::npos,
            "SSRC formatted as ten decimal digits");
    const auto sender =
        mode == RtpReceiveMode::kTcpActive    ? RtpReceiveMode::kTcpPassive
        : mode == RtpReceiveMode::kTcpPassive ? RtpReceiveMode::kTcpActive
                                              : RtpReceiveMode::kUdp;
    auto answer = sip::BuildLiveSdp(kCamera, "127.0.0.1", 30002, 123, sender);
    auto parsed = sip::ParseLiveAnswer(answer.value(), mode);
    RequireOk(parsed, "parse matching negotiated mode");
    Require(parsed.value().port == 30002 && parsed.value().ssrc == 123,
            "negotiated endpoint and SSRC");
    if (mode != RtpReceiveMode::kUdp)
      Require(!sip::ParseLiveAnswer(offer.value(), mode).ok(),
              "same TCP role rejected");
  }
  Require(!sip::BuildLiveSdp(kPlatform, "127.0.0.1\r\na=evil", 30000, 1,
                             RtpReceiveMode::kUdp)
               .ok(),
          "SDP address injection rejected");
  auto answer =
      sip::BuildLiveSdp(kCamera, "127.0.0.1", 30002, 1, RtpReceiveMode::kUdp)
          .value();
  Require(!sip::ParseLiveAnswer(answer, RtpReceiveMode::kTcpActive).ok(),
          "transport mismatch rejected");
  answer.replace(answer.find("PS/90000"), 8, "H264/90000");
  Require(!sip::ParseLiveAnswer(answer, RtpReceiveMode::kUdp).ok(),
          "unnegotiated media encoding rejected");
}

void TestAggregation() {
  Resource a;
  a.gb_id = kCamera;
  Resource b = a;
  b.gb_id = "34020000001320000002";
  CatalogAccumulator catalog;
  Require(!catalog.Add(2, {b}).value(), "partial catalog stays pending");
  Require(!catalog.Add(2, {b}).value() && catalog.received() == 1,
          "retransmitted batch deduplicated");
  Require(catalog.Add(2, {a}).value(),
          "out of order catalog completes once unique count reached");
  Require(catalog.Snapshot().size() == 2, "snapshot has unique resources");
  CatalogAccumulator inconsistent;
  RequireOk(inconsistent.Add(3, {a}), "initial partial batch");
  Require(!inconsistent.Add(4, {b}).ok(), "changed total fails");
  Require(!inconsistent.Add(3, {b}).ok(), "failed catalog cannot revive");
  CatalogAccumulator changed;
  changed.Add(2, {a});
  a.name = "changed";
  Require(!changed.Add(2, {a}).ok(), "conflicting duplicate fails");
  CatalogAccumulator empty;
  Require(empty.Add(0, {}).value(), "zero resource catalog completes");
}
}  // namespace

int main() {
  TestXml();
  TestSdp();
  TestAggregation();
  std::cout << "protocol tests passed\n";
}
