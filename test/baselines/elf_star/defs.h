#pragma once

#include <cstdint>

union DOUBLE {
  double d;
  uint64_t i;
};

union FLOAT {
  float f;
  uint32_t i;
};

// Utils - 使用命名空间避免符号冲突
namespace elfstar_utils {
  int getFAlpha(int alpha);
  int *getAlphaAndBetaStar(double v, int lastBetaStar);
  int *getAlphaAndBetaStar_32(float v, int lastBetaStar);
  double roundUp(double v, int alpha);
  float roundUp_32(float v, int alpha);
  double get10iN(int i);
  float get10iN_32(int i);
  int getSP(double v);

  // 添加缺失的函数声明
  double get10iP(int i);
  float get10iP_32(int i);
  int *getSPAnd10iNFlag(double v);
  int *getSPAnd10iNFlag_32(float v);
  int getSignificantCount(double v, int sp, int lastBetaStar);
  int getSignificantCount_32(float v, int sp, int lastBetaStar);
}