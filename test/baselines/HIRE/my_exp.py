from hier import *
from tqdm import tqdm

import numpy as np

DATA_SET_LIST = {"AP": "Air-pressure.csv", "BT": "Basel-temp.csv",
                 "BW": "Basel-wind.csv", "CDT": "Chengdu-traj.csv",
                 "CT": "City-temp.csv", "DT": "Dew-point-temp.csv",
                 "IR": "IR-bio-temp.csv", "MT": "Motor-temp.csv",
                 "PM10": "PM10-dust.csv", "SG": "Smart-grid.csv",
                 "SUSA": "Stocks-USA.csv", "TD": "T-drive.csv",
                 "WS": "Wind-Speed.csv"}

BLOCK_SIZE_LIST = [128, 256, 512, 1024, 2048]

DATA_PATH_PREFIX = "../../data_set/"

ERROR_BOUND = 0.001


def read_blocks(file_path, each_block_size):
    with open(file_path, 'r+', encoding='utf-8') as file:
        block = []
        for line in file:
            block.append(float(line.rstrip()))
            if len(block) == each_block_size:
                yield block
                block = []
        if block:
            yield block


if __name__ == "__main__":
    for block_size in BLOCK_SIZE_LIST:
        for alias, file_path in DATA_SET_LIST.items():
            block_count = 0
            compressed_file_size_in_bytes = 0
            delimiter = max(9 + len(str(alias)), 13 +
                            len(str(ERROR_BOUND)), 12 + len(str(block_size))) * "="
            print(delimiter)
            print(f"DataSet: {alias}")
            print(f"Error Bound: {ERROR_BOUND}")
            print(f"Block Size: {block_size}")
            print(delimiter)
            for block in tqdm(read_blocks(DATA_PATH_PREFIX + file_path, block_size)):
                if len(block) != block_size:
                    break
                block_count += 1
                block_np = np.array(block).reshape(-1, 1)
                hire = MultivariateHierarchical(
                    "hire", error_thresh=ERROR_BOUND, blocksize=block_size, start_level=2, trc=True)
                hire.load(block_np)
                hire.compress()
                compressed_file_size_in_bytes += hire.compression_stats["compressed_size"]
            print(f"Compression Ratio: {
                compressed_file_size_in_bytes / (block_count * block_size * 8)}")
