from hier_other_errs import *
import numpy as np

# data = np.load('datasets/power.npy')[:524288, :]
# data = np.load('datasets/power.npy')[:1024, 0:1]
data = np.load('datasets/power.npy')[:1024, 0:1]

nn = MultivariateHierarchical('hier', error_thresh = 0.01, blocksize=1024, start_level = 5, trc = True)
nn.load(data)
nn.compress()
