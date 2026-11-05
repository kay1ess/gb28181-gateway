#include <chrono>
#include <future>
#include <iostream>

#include "gb28181/client.h"

int main(int argc, char** argv) {
  gb28181::Config config;
  if (argc > 1)
    config.database_path = argv[1];
  auto client = gb28181::CreateClient();
  const auto started = client->Start(config);
  if (!started) {
    std::cerr << started.error().message << '\n';
    return 1;
  }
  std::promise<gb28181::Result<std::vector<gb28181::Resource>>> promise;
  auto result = promise.get_future();
  const auto accepted = client->ListResources(
      {}, [&](auto resources) { promise.set_value(std::move(resources)); });
  if (!accepted ||
      result.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
    client->Stop();
    return 1;
  }
  auto resources = result.get();
  if (!resources) {
    std::cerr << resources.error().message << '\n';
    client->Stop();
    return 1;
  }
  for (const auto& resource : resources.value())
    std::cout << resource.id << " " << resource.name << '\n';
  return client->Stop().ok() ? 0 : 1;
}
