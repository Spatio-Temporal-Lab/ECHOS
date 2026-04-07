from hier_splitting_point_ablation import MultivariateHierarchical as spqmh
import numpy as np
from hier import MultivariateHierarchical as nqmh


data = np.load('datasets/power.npy')[:4096,0:1]
for size in [True, False]:
    nn1 = spqmh('hier', error_thresh = 0.01, blocksize=4096, start_level = 0, trc = True)
    nn1.load(data)
    nn1.compress(size=size)
    print('Optimal Splitting point, size=', size)
    print(nn1.compression_stats)

nn2 = nqmh('hier', error_thresh = 0.01, blocksize=4096, start_level = 0, trc = True)
nn2.load(data)
nn2.compress()
print('Standard Hire')
print(nn2.compression_stats)

