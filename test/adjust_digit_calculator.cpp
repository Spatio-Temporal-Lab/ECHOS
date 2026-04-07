#include <iostream>
#include <cmath>

int cal_adjust_digit(double min, double max) {
  int u = std::ceil(std::log2(std::floor(max) - std::floor(min) + 1));
  int lambda = (1 << u) - static_cast<int>(std::floor(min));
  return std::max(0, lambda);
}

int main() {
  double min, max;
  std::cin >> min >> max;
  std::cout << cal_adjust_digit(min, max) << std::endl;
  return 0;
}