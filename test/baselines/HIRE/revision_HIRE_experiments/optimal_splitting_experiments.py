from hier_splitting_point_quantize import MultivariateHierarchical as spqmh
from hier import MultivariateHierarchical as nqmh
import numpy as np

data = np.load('datasets/power.npy')[:4096,0:1]
nn1 = spqmh('hier', error_thresh = 0.01, blocksize=4096, start_level = 0, trc = True)
nn1.load(data)
nn1.compress()
print('Optimal Splitting point')
print(nn1.compression_stats)

nn2 = nqmh('hier', error_thresh = 0.01, blocksize=4096, start_level = 0, trc = True)
nn2.load(data)
nn2.compress()
print('Standard Hire')
print(nn2.compression_stats)

