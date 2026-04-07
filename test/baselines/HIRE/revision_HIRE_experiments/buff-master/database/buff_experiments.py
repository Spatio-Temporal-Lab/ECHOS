import os
from timeit import default_timer as timer
import numpy as np
import csv
import subprocess
# import pandas as pd

CMD_PREFIX = ['cargo', '+nightly', 'run', '--release', '--package', 'buff', '--bin', 'comp_profiler']
CMD_MIDFIX = ['buff-simd']
CMD_SUFFIX = ['1.1509']
ERROR_THRESHOLDS = [0.15,0.1,0.075,0.05,0.025,0.01,0.0075,0.005,0.0025,0.001]
DATASET_ROWS = {"bitcoin.npy": 1048576, "phones_accelerometer.npy": 262144, "phones_gyroscope.npy":262144, \
                "power.npy":524288, "sensor_ht.npy": 524288, "watch_accelerometer.npy": 262144, "watch_gyroscope.npy":262144}

# python nlms_compress.py -m c -i ../data/evaluation_datasets/dna/nanopore_test.npy -o nanopore_test_compressed.bsc -a 0.01
# python nlms_compress.py -m d -i nanopore_test_compressed.bsc -o nanopore_test.decompressed.npy
def parse_log(log_text):
    lines = log_text.splitlines()
    for line in lines:
        if not line.startswith("Performance:"):
            continue
        metrics = line.split(',')[3:6]
    return metrics[0], metrics[1], metrics[2]


def run_experiment(dataset_path, precision = 6):
    result = []
    for filename in os.listdir(dataset_path):
        if not filename.endswith('.npy'):
            continue
        full_path = os.path.join(dataset_path, filename)
        data = np.load(full_path, allow_pickle = True)[:DATASET_ROWS[filename],:].astype(np.float32).T
        np.savetxt(filename+".csv", data, fmt="%.{}f".format(precision), delimiter=',')
        # df = pd.DataFrame(data)
        # df.to_csv(filename+".csv",index=False, header=False) 
        output = subprocess.run(CMD_PREFIX + [filename+".csv"] + CMD_MIDFIX + [str(10**precision)] + CMD_SUFFIX, stdout=subprocess.PIPE)
        log_text = output.stdout.decode('utf-8')
        try:
            compression_ratio, compress_ltncy, decompress_ltncy = parse_log(log_text)
        except:
            continue
        result.append([filename, compression_ratio, float(compress_ltncy)/1e9, float(decompress_ltncy)/1e9])
        print(filename, data.shape)
        # os.remove("temp_data.csv")
    return result

results = run_experiment("../../datasets", 6)

with open("buff_results_latencies.csv", "w") as file:
    writer = csv.writer(file)
    writer.writerows(results)
