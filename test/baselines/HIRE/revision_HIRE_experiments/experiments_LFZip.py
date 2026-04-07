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
        
        normalization = np.vstack([np.max(data,axis=1), np.min(data,axis=1)])
        normalization = np.hstack([normalization, np.array([data.shape[1], data.shape[0]]).reshape(2,1)])

		#print(self.normalization.shape)
		#get all attrs between 0 and 1
        for i in range(data.shape[0]):
            data[i,:] = (data[i,:] - normalization[1,i])/(normalization[0,i] - normalization[1,i])
        
        original_size = data.size * data.itemsize
        np.save("temp_normalization.npy", normalization)
        np.save("temp_data.npy", data)

        start_load_subtract = timer()
        x = np.load("temp_data.npy")
        end_load_subtract = timer() - start_load_subtract

        #dats 
        compress_cmd = BASE_COMPRESS + ["temp_data.npy", "-o", "temp_codes.bsc", "-a"]
        decompress_cmd = BASE_DECOMPRESS + ["temp_codes.bsc", "-o", "temp_reconstructed.npy"]
        decompress_cmd = " ".join(decompress_cmd)
        for error_threshold in ERROR_THRESHOLDS:
            comress_cmd_thresh = compress_cmd + [str(error_threshold)]
            print(filename, error_threshold)
            comress_cmd_thresh = " ".join(comress_cmd_thresh)
            start_compress = timer()
            os.system(comress_cmd_thresh)
            compress_latency = timer() - start_compress - end_load_subtract

            recons = np.load("temp_codes.bsc.recon.npy")
            # subtract off error check time for fair comparison
            error_checks_start = timer()
            np.save("temp_codes.bsc.recon.npy", recons)
            for j in range(recons.shape[0]):
                print('j:',j)
                maxerror_observed = np.max(np.abs(data[j,:]-recons[j,:]))
                RMSE = np.sqrt(np.mean((data[j,:]-recons[j,:])**2))
                MAE = np.mean(np.abs(data[j,:]-recons[j,:]))
                print('maxerror_observed:',maxerror_observed)
                print('RMSE:',RMSE)
                print('MAE:',MAE)
            error_checks_end = timer() - error_checks_start
            compress_latency -= error_checks_end
            
            compressed_size = os.path.getsize("temp_codes.bsc") + os.path.getsize("temp_normalization.npy")
            compression_ratio = compressed_size / original_size
            
            start_decompress = timer()
            os.system(decompress_cmd)
            decompress_latency = timer() - start_decompress
            # subtract off flush time for fair comparison
            flush_start = timer()
            np.save("temp_codes.npy", recons)
            flush_end = timer() - flush_start

            decompress_latency -= flush_end

            result.append([filename, error_threshold, compression_ratio, compress_latency, decompress_latency])
            os.remove("temp_codes.bsc")
            os.remove("temp_reconstructed.npy")
        os.remove("temp_data.npy")
        os.remove("temp_normalization.npy")
    return result

results = run_experiment("datasets")

with open("LFZip_results_normalization_subtract_load_time.csv", "w") as file:
    writer = csv.writer(file)
    writer.writerows(results)
