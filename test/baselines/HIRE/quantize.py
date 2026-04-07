import numpy as np
import os
from timeit import default_timer as timer
import math
from core import *


class Quantize(CompressionAlgorithm):
	'''Applies a basic quantization algorithm to compress the data
	'''


	'''
	The compression codec is initialized with a per
	attribute error threshold.
	'''
	def __init__(self, target, error_thresh=0.005):

		super().__init__(target, error_thresh)

		self.coderange = int(math.ceil(1.0/error_thresh))


	"""The main compression loop
	"""
	def compress(self):
		start = timer()

		codes = np.ones((self.N, self.p))*-1#set all to negative one

		for i in range(self.N):
			for j in range(self.p):
				codes[i,j] = int(self.data[i,j]*self.coderange)

		#bit_start = timer()
		struct = iarray_bitpacking(codes)
		#bit_total = timer() - bit_start
		struct.flush(self.CODES)
		struct.flushmeta(self.METADATA)

		self.compression_stats['compression_latency'] = timer() - start
		self.compression_stats['compressed_size'] = self.getSize()
		self.compression_stats['compressed_ratio'] = self.getSize()/self.compression_stats['original_size']

		

	def decompress(self, original=None):

		start = timer()

		struct = BitPackedStruct.loadmeta(self.METADATA)
		codes = struct.load(self.CODES)
		coderange = np.max(codes)

		normalization = np.load(self.NORMALIZATION)
		_, P2 = normalization.shape

		p = int(P2 - 1)
		N = int(normalization[0,p])
		bit_length = struct.bit_length

		for i in range(p):
			codes[:,i] = (codes[:,i]/coderange + normalization[1,i])*(normalization[0,i] - normalization[1,i])

		self.compression_stats['decompression_latency'] = timer() - start

		if not original is None:
			self.compression_stats['errors'] = self.verify(original, codes)

		return codes



class QuantizeGZ(CompressionAlgorithm):
	'''Applies a basic quantization algorithm to compress the data
	'''


	'''
	The compression codec is initialized with a per
	attribute error threshold.
	'''
	def __init__(self, target, error_thresh=0.005):

		super().__init__(target, error_thresh)

		self.coderange = int(math.ceil(1.0/error_thresh))

	def quantize(self, x):
		x = x*self.coderange
		return x
		
	def dequantize(self, x_quant):
		x_quant = x_quant/self.coderange
		return x_quant

	"""The main compression loop
	"""
	def compress(self):
		start = timer()

		codes = np.ones((self.N, self.p))*-1#set all to negative one

		#for i in range(self.N):
		#	for j in range(self.p):
		#		codes[i,j] = int(self.data[i,j]*self.coderange)
		codes = self.quantize(self.data)

		#bit_start = timer()
		struct = iarray_bitpacking(codes)
		#bit_total = timer() - bit_start
		struct.flushz(self.CODES)
		struct.flushmeta(self.METADATA)

		self.compression_stats['compression_latency'] = timer() - start
		self.compression_stats['compressed_size'] = self.getSize()
		self.compression_stats['compressed_ratio'] = self.getSize()/self.compression_stats['original_size']
		self.compression_stats.update(struct.additional_stats)
		

	def decompress(self, original=None):

		start = timer()

		struct = BitPackedStruct.loadmeta(self.METADATA)
		codes = struct.loadz(self.CODES)
		coderange = np.max(codes)

		normalization = np.load(self.NORMALIZATION)
		_, P2 = normalization.shape

		p = int(P2 - 1)
		N = int(normalization[0,p])
		#bit_length = struct.bit_length

		# for i in range(p):
		# 	codes[:,i] = (codes[:,i]/coderange + normalization[1,i])*(normalization[0,i] - normalization[1,i])
		codes = self.dequantize(codes)
		for i in range(p):
			codes[:,i] = (codes[:,i])*(normalization[0,i] - normalization[1,i]) + normalization[1,i]

		self.compression_stats['decompression_latency'] = timer() - start

		if not original is None:
			self.compression_stats['errors'] = self.verify(original, codes)


		self.compression_stats.update(struct.additional_stats)

		return codes