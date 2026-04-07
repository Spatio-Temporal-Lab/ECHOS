import os
from timeit import default_timer as timer
import numpy as np
import csv

BASE_COMPRESS = ["lfzip-nlms", "-m", "c", "-i"]
BASE_DECOMPRESS = ["lfzip-nlms", "-m", "d", "-i"]
ERROR_THRESHOLDS = [0.15,0.1,0.075,0.05,0.025,0.01,0.0075,0.005,0.0025,0.001]
DATASET_ROWS = {"bitcoin.npy": 1048576, "phones_accelerometer.npy": 262144, "phones_gyroscope.npy":262144, \
                "power.npy":524288, "sensor_ht.npy": 524288, "watch_accelerometer.npy": 262144, "watch_gyroscope.npy":262144}

# python nlms_compress.py -m c -i ../data/evaluation_datasets/dna/nanopore_test.npy -o nanopore_test_compressed.bsc -a 0.01
# python nlms_compress.py -m d -i nanopore_test_compressed.bsc -o nanopore_test.decompressed.npy

def run_experiment(dataset_path):
    result = []
    for filename in os.listdir(dataset_path):
        if not filename.endswith('.npy'):
            continue
        full_path = os.path.join(dataset_path, filename)
        data = np.load(full_path, allow_pickle = True)[:DATASET_ROWS[filename],:].astype(np.float32).T
        original_size = data.size * data.itemsize

        np.save("temp_data.npy", data) 
        compress_cmd = BASE_COMPRESS + ["temp_data.npy", "-o", "temp_codes.bsc", "-a"]
        decompress_cmd = BASE_DECOMPRESS + ["temp_codes.bsc", "-o", "temp_reconstructed.npy"]
        decompress_cmd = " ".join(decompress_cmd)
        for error_threshold in ERROR_THRESHOLDS:
            comress_cmd_thresh = compress_cmd + [str(error_threshold)]
            print(filename, error_threshold)
            comress_cmd_thresh = " ".join(comress_cmd_thresh)
            start_compress = timer()
            os.system(comress_cmd_thresh)
            compress_latency = timer() - start_compress

            compressed_size = os.path.getsize("temp_codes.bsc")
            compression_ratio = compressed_size / original_size

            start_decompress = timer()
            os.system(decompress_cmd)
            decompress_latency = timer() - start_decompress
            
            result.append([filename, error_threshold, compression_ratio, compress_latency, decompress_latency])

            os.remove("temp_codes.bsc")
            os.remove("temp_reconstructed.npy")
        os.remove("temp_data.npy")
    return result

results = run_experiment("datasets")

with open("LFZip_results.csv", "w") as file:
    writer = csv.writer(file)
    writer.writerows(results)
