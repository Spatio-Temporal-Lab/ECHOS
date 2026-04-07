import numpy as np
import pandas as pd

# from numpy import genfromtxt
# my_data = genfromtxt('data/randomwalkdatasample1k-1k', delimiter=',')
# print(my_data.shape)

data1 = pd.read_csv("/Users/gabemersy/Desktop/new_HIRE_experiments/buff-master/database/watch_accelerometer.npy.csv")
print(data1.shape, data1.head())
data2 = pd.read_csv("/Users/gabemersy/Desktop/new_HIRE_experiments/buff-master/database/phones_accelerometer.npy.csv")
print(data2.shape, data2.head())


