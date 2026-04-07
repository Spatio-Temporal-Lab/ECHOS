#include <iostream>
#include <chrono>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "serf/decompressor/net_serf_xor_decompressor.h"

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
  int server_fd, new_socket;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);
  unsigned char buffer[BUFFER_SIZE] = {0};

  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
    perror("accept");
    exit(EXIT_FAILURE);
  }

  NetSerfXORDecompressor decompressor(100, 0);
  std::chrono::microseconds total_time = std::chrono::microseconds::zero();
  auto transmission_start_time = std::chrono::steady_clock::now();
  while (true) {
    read(new_socket, buffer, 1);
    int byte_count = buffer[0] >> 4;
    if (byte_count == 0) {
      auto transmission_end_time = std::chrono::steady_clock::now();
      total_time += std::chrono::duration_cast<std::chrono::microseconds>(transmission_end_time -
          transmission_start_time);
      break;
    }
    read(new_socket, buffer + 1, byte_count - 1);
    Array<uint8_t> pack(byte_count);
    std::copy(buffer, buffer + byte_count, pack.begin());
    decompressor.Decompress(pack);
  }

  std::cout << total_time.count() << std::endl;

  close(new_socket);
  close(server_fd);

  return 0;
}
