#!/usr/bin/env python3

import sqlite3
import random
import numpy as np

conn = sqlite3.connect(":memory:")
conn.enable_load_extension(True)
conn.load_extension("./sqlite_puf.so")

conn.execute("CREATE TABLE data (id INTEGER, vec BLOB)")

NUM_SAMPLES = 100
NUM_BYTES = 512

# Generate random data to test the functions
data = []
for i in range(NUM_SAMPLES):
    b = bytearray(random.getrandbits(8) for _ in range(NUM_BYTES))
    data.append((i, b))

conn.executemany("INSERT INTO data VALUES (?,?)", data)

print("Hamming Weight")
res = conn.execute("SELECT id, HW(vec) from data")
print(res.fetchall())

print("Hamming Distance")
res = conn.execute("SELECT id, HD(vec, ?) from data", (data[0][1],))
print(res.fetchall())

print("Fractional Hamming Distance")
res = conn.execute("SELECT id, FHD(vec, ?) from data", (data[0][1],))
print(res.fetchall())

print("Shannon Entropy")
res = conn.execute("SELECT id, entropy(vec) FROM data")
print(res.fetchall())

print("Computing Bitaliasing")
res = conn.execute("SELECT bitaliasing(vec) FROM data")
blob_result = res.fetchone()[0]

# As of now, the bitaliasing function returns a blob, instead of for example, a JSON array
# We can load the blob into an array of doubles
probabilities = np.frombuffer(blob_result, dtype=np.float64)

print(probabilities.shape)  # (NUM_BYTES * 8,)
print(probabilities)

print("Computing Reliability")
res = conn.execute("SELECT reliability(?, vec) FROM data", (data[0][1]))
blob_result = res.fetchone()[0]
reliability = np.frombuffer(blob_result, dtype=np.float64)
print(reliability)
