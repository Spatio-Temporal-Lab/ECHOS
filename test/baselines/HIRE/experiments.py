from identity import *
from quantize import Quantize, QuantizeGZ
#from quantize_bit import Quantize_bit, QuantizeGZ_bit
#from itcompress import *
from squish import *
from delta import *
#from delta_bit import *
from xor import *
from spartan import *
from apca import *
from hier import *
import numpy as np

error_thresholds = [0.001]

#ERROR_THRESHOLD = 0.00499

def initialize(ERROR_THRESH):
	#set up baslines
	BASELINES = []
	#BASELINES.append(IdentityGZ('gz', error_thresh=ERROR_THRESH))
	#BASELINES.append(Quantize_bit('q', error_thresh=ERROR_THRESH))
	#BASELINES.append(QuantizeGZ_bit('q+gz', error_thresh=ERROR_THRESH))
	#BASELINES.append(ItCompress('itcmp', error_thresh=ERROR_THRESH))
	#BASELINES.append(Spartan('sptn', error_thresh=ERROR_THRESH))
	#BASELINES.append(EntropyCoding('q+ent', error_thresh=ERROR_THRESH))
	#BASELINES.append(Sprintz('spz', error_thresh=ERROR_THRESH))
	#BASELINES.append(SprintzLearnedGzip('spz+ln', error_thresh = ERROR_THRESH))
	#BASELINES.append(SprintzGzip_bit('spz+gz', error_thresh=ERROR_THRESH))
	# BASELINES.append(Gorilla('grla', error_thresh=ERROR_THRESH))
	#BASELINES.append(GorillaLossy('grla+l', error_thresh=ERROR_THRESH))
	BASELINES.append(MultivariateHierarchical('hier', error_thresh = ERROR_THRESH, blocksize=50, start_level = 10,
											  trc = True))
	#BASELINES.append(AdaptivePiecewiseConstant('apca', error_thresh=ERROR_THRESH))
	#BASELINES.append(Quarc('mts',model = 'online_VAR', error_thresh = ERROR_THRESH, trc = True, col_sel_algo = 'randomK', sample_size_factor = 1, param_error_thresh = 0.001, number_columns=18, hybrid = False, first_diff= False))
	return BASELINES

def run(BASELINES,
		DATA_DIRECTORY = '/Users/-/Documents/Research/ts_compression/l2c/data/',
		FILENAME = 'bitcoin.npy',
		):
	data = np.load(DATA_DIRECTORY + FILENAME, allow_pickle = True)[:50,:]

	bresults = {}
	for nn in BASELINES:
		#print(data.shape)
		nn.load(data)
		nn.compress()
		nn.decompress(data)
		bresults[nn.target] = nn.compression_stats

	return bresults

#plotting

import matplotlib.pyplot as plt
plt.style.use('fivethirtyeight')
plt.rcParams["figure.figsize"] = (10,4)

for t in error_thresholds:

	ERROR_THRESHOLD = t
	print(t)
	BASELINES = initialize(t)
	FILENAME = 'gorillaz.npy'
	#SIZE_LIMIT = 1000
	bresults = run(BASELINES)


	#compressed size
	try:
		with open('/Users/-/Documents/Research/ts_compression/l2c/results/results_compression_r_' + FILENAME.split('.')[0] + '.txt', 'x') as f:
			f.write('[')
			[f.write(str(bresults[k]['compressed_ratio'])+',') if k != 'apca' else f.write(str(bresults[k]['compressed_ratio'])) for k in bresults]
			f.write(']')
			f.write(',')
			f.close()

	except:
		with open('/Users/-/Documents/Research/ts_compression/l2c/results/results_compression_r_' +FILENAME.split('.')[0] + '.txt','a') as f:
			f.write('[')
			[f.write(str(bresults[k]['compressed_ratio'])+',') if k != 'apca' else f.write(str(bresults[k]['compressed_ratio'])) for k in bresults]
			f.write(']')
			f.write(',')
			f.close()

	#decompression throughput (subtract bitpacking time)
	try:
		with open('/Users/-/Documents/Research/ts_compression/l2c/results/results_compression_l_' + FILENAME.split('.')[0] + '.txt', 'x') as f:
			f.write('[')
			[f.write(str(bresults[k]['compression_latency'])+',') if k != 'apca' else f.write(str(bresults[k]['compressed_ratio'])) for k in bresults]
			f.write(']')
			f.write(',')
			f.close()
	except:
		with open('/Users/-/Documents/Research/ts_compression/l2c/results/results_compression_l_' +FILENAME.split('.')[0] + '.txt','a') as f:
			f.write('[')
			[f.write(str(bresults[k]['compression_latency'])+',') if k != 'apca' else f.write(str(bresults[k]['compressed_ratio'])) for k in bresults]
			f.write(']')
			f.write(',')
			f.close()

	try:
		with open('/Users/-/Documents/Research/ts_compression/l2c/results/results_decompression_l_' + FILENAME.split('.')[0] + '.txt', 'x') as f:
			f.write('[')
			[f.write(str(bresults[k]['decompression_latency'])+',') if k != 'apca' else f.write(str(bresults[k]['compressed_ratio'])) for k in bresults]
			f.write(']')
			f.write(',')
			f.close()
	except:
		with open('/Users/-/Documents/Research/ts_compression/l2c/results/results_decompression_l_' +FILENAME.split('.')[0] + '.txt','a') as f:
			f.write('[')
			[f.write(str(bresults[k]['decompression_latency'])+',') if k != 'apca' else f.write(str(bresults[k]['compressed_ratio'])) for k in bresults]
			f.write(']')
			f.write(',')
			f.close()

