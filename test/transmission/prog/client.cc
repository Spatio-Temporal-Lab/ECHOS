#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>

#include "serf/compressor/net_serf_xor_compressor.h"

#define PORT 8080
#define SERVER_ADDRESS "127.0.0.1"

int main() {
  int sock = 0;
  struct sockaddr_in serv_addr;

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    std::cerr << "Socket creation error" << std::endl;
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);

  if (inet_pton(AF_INET, SERVER_ADDRESS, &serv_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address/ Address not supported" << std::endl;
    return -1;
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    std::cerr << "Connection failed" << std::endl;
    return -1;
  }

  std::ifstream data_set_input("../../test/data_set/Air-pressure.csv");
  NetSerfXORCompressor compressor(100, 1.0E-4, 0);
  double input_buffer;
  while (!data_set_input.eof()) {
    data_set_input >> input_buffer;
    Array<uint8_t> compressed_output = compressor.Compress(input_buffer);
    compressed_output[0] = compressed_output[0] | (static_cast<uint8_t>(compressed_output.length()) << 4);
    send(sock, compressed_output.begin(), compressed_output.length(), 0);
  }

  close(sock);

  return 0;
}
