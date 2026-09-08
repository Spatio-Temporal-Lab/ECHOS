#include <algorithm>
#include <limits>

#include "post_office_solver_32.h"

Array<int> PostOfficeSolver32::InitRoundAndRepresentation(Array<int> &distribution,
                                                          Array<int> &representation,
                                                          Array<int> &round) {
  Array<int> pre_non_zeros_count(distribution.length());
  Array<int> post_non_zeros_count(distribution.length());
  Array<int> total_count_and_non_zeros_counts = CalTotalCountAndNonZerosCounts(
      distribution, pre_non_zeros_count, post_non_zeros_count);

  int max_z = std::min(kPositionLength2Bits[total_count_and_non_zeros_counts[1]], 4);
  int total_cost = std::numeric_limits<int>::max();
  Array<int> positions = {};

  int present_cost;
  for (int z = 0; z <= max_z && (present_cost = total_count_and_non_zeros_counts[0] * z) < total_cost; ++z) {
    int num = PostOfficeSolver32::kPow2z[z];
    PostOfficeResult por = PostOfficeSolver32::BuildPostOffice(distribution,
                                                               num,
                                                               total_count_and_non_zeros_counts[1],
                                                               pre_non_zeros_count,
                                                               post_non_zeros_count);
    int temp_total_cost = por.total_app_cost() + present_cost;
    if (temp_total_cost < total_cost) {
      total_cost = temp_total_cost;
      positions = por.office_positions();
    }
  }

  if (representation.length() > 0) {
    representation[0] = 0;
    round[0] = 0;
  }
  int i = 1;
  for (int j = 1; j < distribution.length(); ++j) {
    int magic_code = (i < positions.length() && j == positions[i]);
    representation[j] = representation[j - 1] + magic_code;
    round[j] = magic_code ? j : round[j - 1];
    i += magic_code;
  }

  return positions;
}

Array<int> PostOfficeSolver32::CalTotalCountAndNonZerosCounts(Array<int> &arr,
                                                              Array<int> &out_pre_non_zeros_count,
                                                              Array<int> &out_post_non_zeros_count) {
  int non_zeros_count = arr.length();
  int total_count = arr[0];
  if (out_pre_non_zeros_count.length() > 0) {
    out_pre_non_zeros_count[0] = 1;
  }
  for (int i = 1; i < arr.length(); ++i) {
    total_count += arr[i];
    int magic_code = (arr[i] == 0);
    non_zeros_count -= magic_code;
    out_pre_non_zeros_count[i] = out_pre_non_zeros_count[i - 1] + !magic_code;
  }
  for (int i = 0; i < arr.length(); ++i) {
    out_post_non_zeros_count[i] = non_zeros_count - out_pre_non_zeros_count[i];
  }
  return Array<int>{total_count, non_zeros_count};
}

PostOfficeResult PostOfficeSolver32::BuildPostOffice(Array<int> &arr,
                                                     int num,
                                                     int non_zeros_count,
                                                     Array<int> &pre_non_zeros_count,
                                                     Array<int> &post_non_zeros_count) {
  int original_num = num;
  num = std::min(num, non_zeros_count);

  int dp[arr.length()][num];
  int pre[arr.length()][num];

  dp[0][0] = 0;
  pre[0][0] = -1;

  for (int i = 1; i < arr.length(); ++i) {
    if (arr[i] == 0) {
      continue;
    }
    for (int j = std::max(1, num + i - arr.length()); j <= i && j < num; ++j) {
      if (i > 1 && j == 1) {
        dp[i][j] = 0;
        for (int k = 1; k < i; ++k) {
          dp[i][j] += arr[k] * k;
        }
        pre[i][j] = 0;
      } else {
        if (pre_non_zeros_count[i] < j + 1 || post_non_zeros_count[i] < num - 1 - j) {
          continue;
        }
        int app_cost = std::numeric_limits<int>::max();
        int pre_k = 0;
        for (int k = j - 1; k <= i - 1; ++k) {
          if ((arr[k] == 0 && k > 0) ||
              pre_non_zeros_count[k] < j ||
              post_non_zeros_count[k] < num - j) {
            continue;
          }
          int sum = dp[k][j - 1];
          for (int p = k + 1; p <= i - 1; ++p) {
            sum += arr[p] * (p - k);
          }
          if (app_cost > sum) {
            app_cost = sum;
            pre_k = k;
            if (sum == 0) {
              break;
            }
          }
        }
        if (app_cost != std::numeric_limits<int>::max()) {
          dp[i][j] = app_cost;
          pre[i][j] = pre_k;
        }
      }
    }
  }

  int temp_total_app_cost = std::numeric_limits<int>::max();
  int temp_best_last = std::numeric_limits<int>::max();
  for (int i = num - 1; i < arr.length(); ++i) {
    if (num - 1 == 0 && i > 0) {
      break;
    }
    if ((arr[i] == 0 && i > 0) || pre_non_zeros_count[i] < num) {
      continue;
    }
    int sum = dp[i][num - 1];
    for (int j = i + 1; j < arr.length(); ++j) {
      sum += arr[j] * (j - i);
    }
    if (temp_total_app_cost > sum) {
      temp_total_app_cost = sum;
      temp_best_last = i;
    }
  }

  Array<int> office_positions(num);
  int i = 1;

  while (temp_best_last != -1) {
    office_positions[num - i] = temp_best_last;
    temp_best_last = pre[temp_best_last][num - i];
    ++i;
  }

  if (original_num > non_zeros_count) {
    Array<int> modifying_office_positions(original_num);
    int j = 0;
    int k = 0;
    while (j < original_num && k < num) {
      if (j - k < original_num - num && j < office_positions[k]) {
        modifying_office_positions[j] = j;
        ++j;
      } else {
        modifying_office_positions[j] = office_positions[k];
        ++j;
        ++k;
      }
    }
    office_positions = modifying_office_positions;
  }

  return {office_positions, temp_total_app_cost};
}

int PostOfficeSolver32::WritePositions(const Array<int> &positions, BitWriter *writer) {
  int encoded_length = positions.length() & 0xf;
  write(writer, encoded_length, 4);
  int this_size = 4;
  for (const auto &position : positions) {
    write(writer, position, 5);
    this_size += 5;
  }
  return this_size;
}
