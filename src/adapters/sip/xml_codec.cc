#include "src/adapters/sip/xml_codec.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <string_view>

#include <tinyxml2.h>

namespace gb28181::sip {
namespace {
Error Invalid(const char* message) {
  return {ErrorCode::kInvalidArgument, message, {}};
}

bool Number(const std::string& text, uint32_t* number) {
  if (text.empty())
    return false;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), *number);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

std::string Text(const tinyxml2::XMLElement* parent, const char* key) {
  const auto* item = parent->FirstChildElement(key);
  const auto* attribute = parent->Attribute(key);
  if (item && item->GetText()) {
    if (attribute && std::string(item->GetText()) != attribute)
      return {};
    return item->GetText();
  }
  return attribute ? attribute : "";
}

bool GbId(const std::string& id) {
  return id.size() == 20 && std::all_of(id.begin(), id.end(), [](char c) {
           return c >= '0' && c <= '9';
         });
}

Result<std::string> Decode(const std::string& body) {
  if (body.empty() || body.size() > 1024 * 1024)
    return Invalid("XML size outside allowed range");
  auto declaration = body.substr(
      0, body.find("?>") == std::string::npos ? 0 : body.find("?>") + 2);
  std::transform(
      declaration.begin(), declaration.end(), declaration.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  const bool legacy = declaration.find("GB2312") != std::string::npos ||
                      declaration.find("GBK") != std::string::npos ||
                      declaration.find("GB18030") != std::string::npos;
  if (declaration.find("ENCODING") != std::string::npos && !legacy &&
      declaration.find("UTF-8") == std::string::npos &&
      declaration.find("UTF8") == std::string::npos)
    return Invalid("Unsupported XML encoding");
#ifdef _WIN32
  const UINT codepage =
      legacy ? (declaration.find("GB18030") != std::string::npos ? 54936 : 936)
             : CP_UTF8;
  const DWORD flags = codepage == 936 ? 0 : MB_ERR_INVALID_CHARS;
  const int count = MultiByteToWideChar(
      codepage, flags, body.data(), static_cast<int>(body.size()), nullptr, 0);
  if (!count)
    return Invalid("Invalid XML encoding");
  std::wstring wide(count, L'\0');
  if (!MultiByteToWideChar(codepage, flags, body.data(),
                           static_cast<int>(body.size()), wide.data(), count))
    return Invalid("Invalid XML encoding");
  const int bytes =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), count,
                          nullptr, 0, nullptr, nullptr);
  if (!bytes)
    return Invalid("Invalid XML encoding");
  std::string output(bytes, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), count,
                      output.data(), bytes, nullptr, nullptr);
  return output;
#else
  iconv_t converter = iconv_open("UTF-8", legacy ? "GB18030" : "UTF-8");
  if (converter == reinterpret_cast<iconv_t>(-1))
    return Invalid("Encoding conversion unavailable");
  std::string output(body.size() * 4, '\0');
  char* input = const_cast<char*>(body.data());
  char* destination = output.data();
  size_t remaining = body.size();
  size_t available = output.size();
  const auto converted =
      iconv(converter, &input, &remaining, &destination, &available);
  iconv_close(converter);
  if (converted == static_cast<size_t>(-1) || remaining)
    return Invalid("Invalid XML encoding");
  output.resize(output.size() - available);
  return output;
#endif
}

OnlineState State(const std::string& value) {
  if (value == "ON" || value == "ONLINE")
    return OnlineState::kOnline;
  if (value == "OFF" || value == "OFFLINE")
    return OnlineState::kOffline;
  return OnlineState::kUnknown;
}
}  // namespace

Result<XmlMessage> ParseXml(const std::string& body) {
  auto decoded = Decode(body);
  if (!decoded)
    return decoded.error();
  if (decoded.value().find("<!DOCTYPE") != std::string::npos ||
      decoded.value().find("<!ENTITY") != std::string::npos)
    return Invalid("DTD and entities are not supported");
  tinyxml2::XMLDocument document;
  if (document.Parse(decoded.value().data(), decoded.value().size()) !=
      tinyxml2::XML_SUCCESS)
    return Invalid("Malformed XML");
  const auto* root = document.RootElement();
  if (!root || root->NextSiblingElement())
    return Invalid("Expected a single XML root");
  XmlMessage message;
  message.root = root->Name();
  message.command = Text(root, "CmdType");
  message.device_id = Text(root, "DeviceID");
  if ((message.root != "Response" && message.root != "Notify" &&
       message.root != "Query") ||
      !GbId(message.device_id) || !Number(Text(root, "SN"), &message.sn) ||
      message.sn == 0)
    return Invalid("Invalid MANSCDP envelope");
  if (message.command == "Catalog") {
    if (message.root == "Query")
      return message;
    if (!Number(Text(root, "SumNum"), &message.total) || message.total > 100000)
      return Invalid("Invalid catalog total");
    const auto* list = root->FirstChildElement("DeviceList");
    if (!list && message.total)
      return Invalid("Missing DeviceList");
    if (list) {
      for (auto* item = list->FirstChildElement("Item"); item;
           item = item->NextSiblingElement("Item")) {
        Resource resource;
        resource.gb_id = Text(item, "DeviceID");
        resource.parent_gb_id = Text(item, "ParentID");
        if (!GbId(resource.gb_id))
          return Invalid("Invalid catalog device ID");
        resource.name = Text(item, "Name");
        resource.manufacturer = Text(item, "Manufacturer");
        resource.model = Text(item, "Model");
        resource.address = Text(item, "Address");
        resource.civil_code = Text(item, "CivilCode");
        resource.parental = Text(item, "Parental") == "1";
        resource.state = State(Text(item, "Status"));
        message.resources.push_back(std::move(resource));
        if (message.resources.size() > 4096)
          return Invalid("Catalog batch too large");
      }
      uint32_t count = 0;
      if (!list->Attribute("Num") || !Number(list->Attribute("Num"), &count) ||
          count != message.resources.size())
        return Invalid("Catalog Num does not match items");
    }
    if (message.resources.size() > message.total)
      return Invalid("Catalog batch exceeds total");
  } else if (message.command == "DeviceInfo") {
    message.info.name = Text(root, "DeviceName");
    message.info.manufacturer = Text(root, "Manufacturer");
    message.info.model = Text(root, "Model");
    message.info.firmware = Text(root, "Firmware");
  } else if (message.command == "DeviceStatus") {
    message.status.state = State(Text(root, "Online"));
    message.status.status = Text(root, "Status");
  } else if (message.command == "Keepalive") {
    message.status.status = Text(root, "Status");
  } else {
    return Invalid("Unsupported MANSCDP command");
  }
  return message;
}

Result<std::string> BuildQuery(const std::string& command,
                               uint32_t sn,
                               const std::string& device_id) {
  if (!GbId(device_id) || !sn ||
      (command != "Catalog" && command != "DeviceInfo" &&
       command != "DeviceStatus"))
    return Invalid("Invalid query parameters");
  tinyxml2::XMLDocument document;
  document.InsertEndChild(
      document.NewDeclaration("xml version=\"1.0\" encoding=\"UTF-8\""));
  auto* root = document.NewElement("Query");
  document.InsertEndChild(root);
  auto append = [&](const char* name, const std::string& value) {
    auto* node = document.NewElement(name);
    node->SetText(value.c_str());
    root->InsertEndChild(node);
  };
  append("CmdType", command);
  append("SN", std::to_string(sn));
  append("DeviceID", device_id);
  tinyxml2::XMLPrinter printer;
  document.Print(&printer);
  return std::string(printer.CStr());
}
}  // namespace gb28181::sip
