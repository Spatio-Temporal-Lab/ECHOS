#from curses import window
import numpy as np
from timeit import default_timer as timer
from scipy.interpolate import interp1d
import os
from core import *
import time

import warnings
warnings.filterwarnings("ignore")


# HIRE for univariate time series
class HierarchicalSketch():                                                  
    """
    parameters:
    - min_error_thresh: epsilon, the required L-Infinity error bound
    - blocksize: defines size of blocks to be compressed
    - pfn: pool function (see below)
    - sfn: spline function (see below)
    - start_level: the inital level compression is initiated    
    """
    def __init__(self, min_error_thresh, blocksize, pfn, sfn, start_level):
        self.error_thresh = min_error_thresh
        # for quantization
        #self.TURBO_CODE_LOCATION = "./../Turbo-Range-Coder/turborc" 
        #self.TURBO_CODE_PARAMETER = "-20" #on my laptop run -e0 and find best solution
        self.coderange = np.ceil(1.0/(min_error_thresh*2))
        self.blocksize = blocksize #must be a power of 2
        self.d = int(np.log2(blocksize))
        self.pfn = pfn
        self.sfn = sfn
        self.start_level = start_level
    # quantize each vector to enforce L-infty 
    def quantize(self, x):
        #x = x.copy()
        x = np.rint(x*self.coderange)
        x = x.astype(np.int16)
        return x
    # inverse of quantization
    def dequantize(self, x_quant):
        #x_quant = x_quant.copy()
        x_quant = x_quant/self.coderange
        #x_quant = x_quant.astype(np.float16)
        return x_quant
    # unused for now, split somewhere besides middle
    def findOptPartition(self, x: np.array):
        N = x.shape[0]
        # compute prefix sums P_i
        P = np.cumsum(x)
        # compute squared prefix sums R_i
        R = np.cumsum(np.square(x))

        mxs = []
        for l in range(0,N-1):
            SSE_L = (l+1)*((R[l] / (l+1)) - (P[l]/(l+1))**2)
            SSE_R = (N-l-1)*((R[N-1] - R[l])/(N-l-1) - ((P[N-1] - P[l])/(N-l-1))**2)
            mxs.append(max(SSE_L, SSE_R))
        return np.argmin(np.array(mxs))
    # optimized version of mean pooling
    def mean_poool_optim(self, x, width):
        if width == 1:
            return x.copy()

        N = x.shape[0]
        P = np.cumsum(x)

        result = np.zeros((N // width))

        result[0] = P[width-1]
        for i in range(width, N, width):
            result[i//width] = P[i+width-1] - P[i-1]
        result /= width
        return result
    # general function-agnostic pooling 
    def pool(self, x, fn, width):
        slices = x.reshape(-1,width)
        N,_ = slices.shape
        return np.array([fn(slices[i]) for i in range(N)])
    # standard pooling for max, mean, median, percentile, midrank
    def pool_max(self, x, width):
        return np.max(x.reshape(-1,width), axis=1)

    def pool_mean(self, x, width):
        return np.mean(x.reshape(-1,width), axis=1)

    def pool_median(self, x, width):
        return np.median(x.reshape(-1,width), axis=1)

    def pool_percentile(self, x, width, p):
        return np.percentile(x.reshape(-1,width),p, axis=1)
    
    def pool_midrank(self, x, width):
        return 0.5*(np.max(x.reshape(-1,width), axis=1) + np.min(x.reshape(-1,width), axis=1))
    # general spline function with various interpolation schemes
    def spline(self, p, width, inter):
        N = p.shape[0]
        
        #degenerate case
        if N == 1:
            return np.ones(N*width)*p[0]
        
        #treat every obs as midpoint of its range
        fn = interp1d(np.arange(0,N*width, width) + (width-1)/2, p, \
                      kind=inter, fill_value="extrapolate")
                      
        return fn(np.arange(0,N*width,1))

    def spline_optim(self, p, width):
        N = p.shape[0]

        #degenerate case
        if N == 1:
            return np.ones(N*width)*p[0]
        return np.repeat(p, width)
  
    #only works on univariate data
    def encode(self, data):
        cpy = data.copy()
        N = data.shape[0]
        self.nblks = int(np.rint(N / self.blocksize))
        hierarchies = []
        # for each block
        for j in range(self.nblks):
            curr = cpy[j*self.blocksize:(j+1)*self.blocksize]
            hierarchy = [] 
            residuals = []
            # for each level from start to leaf nodes
            for i in range(self.start_level, self.d + 1):
                
                w = self.blocksize // 2**i
                
                #v = self.mean_poool_optim(curr, w)
                v = self.pool_mean(curr, w)
                #v = self.pool_percentile(curr, w, 90)
                # v = self.pool_median(curr, w)
                #v = self.pool_midrank(curr, w)
                v_quant = self.quantize(v)
                # print(v_quant)
                # time.sleep(2)
                # print(v_quant)
                v = self.dequantize(v_quant)
                vp = self.spline_optim(v, w)

                curr -= vp 
                r = self.pool_max(np.abs(curr), w)
                #curr[np.repeat(r < self.error_thresh, w)] = 0
                
                hierarchy.append(v_quant)

                residuals.append(np.max(r))
            #print(list(residuals))
            hierarchies.append(list(zip(hierarchy, residuals)))
        return hierarchies

    def decode(self, sketch, error_thresh=0):
        
        #start = timer()
        W = np.zeros((len(sketch), self.blocksize)) #preallocate 0 vector
        for i, (h,r) in enumerate(sketch, start = self.start_level):
            
            dims = h.shape[0]
            # recompute the spline at each level 
            W[i-self.start_level,:] = self.spline_optim(h, self.blocksize // dims)
            # break when closest subencoding to desired error is reached   
            if r < self.error_thresh:
                break
        #print('time:', timer()-start, 'error:', r)
        # invert the residualizing step
        return self.dequantize(np.sum(W,axis=0))


    """
    Packs error (r) and encoding (h) from each level into one array
    
    Parameters:
    - sketch: List[(h, r)]
    """
    def pack(self, sketch):
        vectors = []
        for h,r in sketch:
            vector = np.concatenate([np.array([r]), h])
            vectors.append(vector)
        return np.concatenate(vectors)
    
    """
    Unpacks all of the data from single array to original format
    
    Parameters:
    - array: 1 dimensional packed data
    - error_thresh: desired (decompression time) error
    """
    def unpack(self, array, error_thresh=0):
        sketch = []
        for i in range(self.start_level,self.d+1):
            
            r = array[0]
            h = array[1:2**i+1]
            array = array[2**i+1:]
                
            sketch.append((h,r))
            
            if r < error_thresh:
                break

        return sketch

# HIRE for multivariate time series
class MultivariateHierarchical(CompressionAlgorithm):
    '''
    Parameters:
    - target: HierarchicalSketch class
    - pfn: pool function
    - error_thresh: error threshold across all columns
    - blocksize: size of blocks to be compressed
    - start_level: level of compression to start at
    - trc: use the Turbo Range Coder or not 
    '''
    def __init__(self, target,pfn = np.mean, error_thresh=1e-5, blocksize=4096, start_level = 0, trc = False):

        super().__init__(target, error_thresh)
        self.trc = trc
        self.blocksize = blocksize
        self.start_level =start_level
        self.TURBO_CODE_PARAMETER = "20"
        # TURBO RANGE CODER PATH HERE
        self.TURBO_CODE_LOCATION = "./turborc" 
        #self.TURBO_CODE_PARAMETER = "-20" #on my laptop run -e0 and find best solution

        self.pfn = pfn

        self.sketch = HierarchicalSketch(self.error_thresh, blocksize, start_level = self.start_level, pfn=self.pfn, sfn='nearest')

    def compress(self):

        start = timer()
        arrays = []
        # for each column
        for j in range(self.p):
            vector = self.data[:,j].reshape(-1)
            # generate encodings
            ens = self.sketch.encode(vector)

            # pack each encoding
            for en in ens:
                #cumulative_gap = min(self.error_thresh - en[-1][1], cumulative_gap)
                arrays.append(self.sketch.pack(en))
        

        codes = np.vstack(arrays).astype(np.float16)

        
        #fname = self.CODES
        # run the Turbo Range Coder
        trc_flag = '-' + self.TURBO_CODE_PARAMETER
        # flush to .npy file
        self.path = self.CODES + '.npy'
        np.save(self.path, codes.flatten(order='F'))
        self.CODES += '.rc'
        self.DATA_FILES[0] = self.CODES
        print('\n')
        
        # run TRC [compression]
        # best performing function should be the the int trc_flag after run of turborc -e0
        #subprocess.run(['./../Turbo-Range-Coder/turborc', trc_flag, self.path, self.CODES])
        # command = " ".join(['./../Turbo-Range-Coder/turborc', trc_flag, self.path, self.CODES])
        command = " ".join([self.TURBO_CODE_LOCATION, trc_flag, self.path, self.CODES])
        os.system(command)
        # compute statistics
        self.compression_stats['compression_latency'] = timer() - start
        self.compression_stats['compressed_size'] = self.getSize()
        self.compression_stats['compressed_ratio'] = self.getSize()/self.compression_stats['original_size']
        #self.compression_stats.update(struct.additional_stats)


    def decompress(self, original=None, error_thresh=1e-4):
        start = timer()
        
        #subprocess.run(['./../Turbo-Range-Coder/turborc', '-d', self.CODES, self.path])
        # decompress TRC
        command = " ".join([self.TURBO_CODE_LOCATION, "-d", self.CODES, self.path])
        os.system(command)
        
        packed = np.load(self.path)
        print('\n')

        #unpack_time = timer() - start
        #print('trc time: ', unpack_time)
        packed = packed.reshape(self.p*self.sketch.nblks, -1, order='F')
        
        #start = timer()


        normalization = np.load(self.NORMALIZATION)
        _, P2 = normalization.shape

        p = int(P2 - 1)
        N = int(normalization[0,p])
        codes = np.zeros((N,p))

        #normalize_time = timer() - start
        #print('normalize time: ', normalize_time)
        #start = timer()

        j = -1
        #k=0
        #index the vstacked arrs
        start = timer()
        for i in range(self.p*self.sketch.nblks):
            # detects new column
            if i % self.sketch.nblks == 0:
                # index original codes
                k = 0
                # index blocks
                j += 1
            #start = timer()
            # unpack
            sk = self.sketch.unpack(packed[i,:], error_thresh)
            #unpack_time = timer() - start
            #print('unpack time: ', unpack_time)

            # print('decompress end', sk)
            #start = timer()
            codes[k*self.blocksize:(k+1)*self.blocksize, j] = self.sketch.decode(sk, error_thresh)
            #decode_time = timer() - start
            #print('decode time: ', decode_time)
            k += 1

        #unpack_time = timer() - start
        #print('unpack time: ', unpack_time)
        
        #decode_time = timer() - start
        #print('decode time: ', decode_time)

        #start = timer()
        # undo normalization
        for i in range(p):
            codes[:,i] = (codes[:,i])*(normalization[0,i] - normalization[1,i]) + normalization[1,i]


        #denormalize_time = timer() - start
        #print('denormalize time: ', denormalize_time)
        # report latency
        self.compression_stats['decompression_latency'] = timer() - start
        #self.compression_stats['decompression_ratio'] = (codes.size * codes.itemsize)/self.compression_stats['original_size']
        if not original is None:
            #print(original-codes)
            self.compression_stats['errors'] = self.verify(original, codes)

        return codes

def bisect(x):
    N = x.shape[0]
    return x[N // 2]
