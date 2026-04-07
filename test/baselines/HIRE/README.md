# HIRE: Hierarchical Residual Encoding for Multiresolution Time Series Compression
Welcome to the repository accompanying our SIGMOD '23 paper [Hierarchical Residual Encoding for Multiresolution Time Series Compression](https://dl.acm.org/doi/10.1145/3588953). We propose a new type of compression algorithm called HIRE that produces an encoding for time series data that *can be decoded at multiple error bounds (thresholds)*. A sequence of pooling and spline operations is constructed, and these operations are applied recursively to an error vector. This repository can be used to reproduce the main results of the paper, or to explore how HIRE could work in your own system.  


## Dataset files
Download from the following Google Drive link: https://drive.google.com/drive/folders/1gxU9GskX9f60meHUnwcaqs0FvE75tuEN?usp=sharing

## Requirements:
- Create a new Python virtual environment 
- Install the dependencies via `pip install -r requirements.txt`
- We used TRC as our downstream compression as explained in the paper. It can be installed through the link: https://github.com/powturbo/Turbo-Range-Coder 

## Our method: HIRE
- Located in `hier.py`
- `HierarchicalSketch` class implements univariate HIRE
- `MultivariateHierarchical` class implements multivariate HIRE

Detailed comments explaining the correspondence beteween parts of the code and the paper can be found throughout.  
## Main experiment file
1) Navigate to `experiments.py` and choose baselines you would like to run in `initialize(...)`
2) Change data path to the dataset location in `run(..., DATA_DIRECTORY = "your_path", FILENAME = "data_file")`
3) Change results path for desired metrics in the `with open(...)` statements
4) Run file

## LFZip, Buff, and optimal split experiments
- `cd revision_HIRE_experiments/`
- Place the datasets in the `datasets/` directory (from the below Google Drive link)
1) To run LFZip, create a conda virtual environment and `conda install lfzip`. Then run `python3 experiments_LFZip.py`
2) To run Buff, first install the rust programming language on your local machine: https://www.rust-lang.org/tools/install. Then `cd buff-master/database` and `python3 buff_experiments.py`
3) To run the optimal splitting experiment `python3 optimal_splitting_experiments.py` For the ablation study of the optimal split, run `python3 optimal_splitting_experiments_ablation.py`
4) To run the experiments comparing the different error functions at various levels of the hierarchy, run `python3 experiments_other_errs.py`

# Citation
If you use HIRE in your paper, please use the following citation:

`@article{Barbarioli2023Hierarchical,
author = {Barbarioli, Bruno and Mersy, Gabriel and Sintos, Stavros and Krishnan, Sanjay},
title = {Hierarchical Residual Encoding for Multiresolution Time Series Compression},
year = {2023},
issue_date = {May 2023},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
volume = {1},
number = {1},
url = {https://doi.org/10.1145/3588953},
doi = {10.1145/3588953},
abstract = {Data compression is a key technique for reducing the cost of data transfer from storage to compute nodes. Increasingly, modern data scales necessitate lossy compression techniques, where exactness is sacrificed for a smaller compressed representation. One challenge in lossy compression is that different applications may have different accuracy demands. Today's compression techniques struggle in this setting either forcing the user to compress at the strictest accuracy demand, or to re-encode the data at multiple resolutions. This paper proposes a simple, but effective multiresolution compression algorithm for time series data, where a single encoding can effectively be decompressed at multiple output resolutions. There are a number of benefits over current state-of-the-art techniques for time series compression. (1) The storage footprint of this encoding is smaller than re-encoding the data at multiple resolutions. (2) Similarly, the compression latency is generally smaller than re-encoding at multiple resolutions. (3) Finally, the decompression latency of our encoding is significantly faster than single encodings at the strictest accuracy demand.},
journal = {Proc. ACM Manag. Data},
month = {may},
articleno = {99},
numpages = {26},
keywords = {compression, storage, temporal databases}
}`

