#ifndef SERF_POST_OFFICE_SOLVER_32_H
#define SERF_POST_OFFICE_SOLVER_32_H

#include "post_office_result.h"
#include "array.h"
#include "BitStream/BitWriter.h"

class PostOfficeSolver32 {
 public:
  constexpr static int kPositionLength2Bits[] = {
      0, 0, 1, 2, 2, 3, 3, 3, 3,
      4, 4, 4, 4, 4, 4, 4, 4,
      5, 5, 5, 5, 5, 5, 5, 5,
      5, 5, 5, 5, 5, 5, 5, 5
  };

  static Array<int> InitRoundAndRepresentation(Array<int> &distribution,
                                               Array<int> &representation,
                                               Array<int> &round);

    static int WritePositions(const Array<int> &positions, BitWriter *writer);

 private:
  constexpr static int kPow2z[] = {1, 2, 4, 8, 16};

  static Array<int> CalTotalCountAndNonZerosCounts(Array<int> &arr,
                                                   Array<int> &out_pre_non_zeros_count,
                                                   Array<int> &out_post_non_zeros_count);

  static PostOfficeResult BuildPostOffice(Array<int> &arr,
                                          int num,
                                          int non_zeros_count,
                                          Array<int> &pre_non_zeros_count,
                                          Array<int> &post_non_zeros_count);
};

#endif  // SERF_POST_OFFICE_SOLVER_32_H
