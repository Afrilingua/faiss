# @lint-ignore-every LICENSELINT
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#
# Copyright (c) 2025, NVIDIA CORPORATION.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import numpy as np
import unittest

import faiss

from faiss.contrib import evaluation

@unittest.skipIf(
    "CUVS" not in faiss.get_compile_options(),
    "only if cuVS is compiled in")
class TestInterop(unittest.TestCase):

    def do_interop(self):
        d = 64 * 8
        k = 12

        res = faiss.StandardGpuResources()

        index = faiss.GpuIndexBinaryCagra(res, d)
        xb = np.random.randint(
            low=0, high=256, size=(1000000, d // 8), dtype=np.uint8)
        index.train(xb)
        queries = np.random.randint(
            low=0, high=256, size=(1000, d // 8), dtype=np.uint8)
        Dnew, Inew = index.search(queries, k)

        cpu_index = faiss.index_binary_gpu_to_cpu(index)
        Dref, Iref = cpu_index.search(queries, k)

        evaluation.check_ref_knn_with_draws(Dref, Iref, Dnew, Inew, k)

        deserialized_index = faiss.deserialize_index_binary(
            faiss.serialize_index_binary(cpu_index))

        gpu_index = faiss.index_binary_cpu_to_gpu(res, 0, deserialized_index)
        Dnew2, Inew2 = gpu_index.search(queries, k)

        evaluation.check_ref_knn_with_draws(Dnew2, Inew2, Dnew, Inew, k)

    def test_interop(self):
        self.do_interop()


@unittest.skipIf(
    "CUVS" not in faiss.get_compile_options(),
    "only if cuVS is compiled in")
class TestComputeGT(unittest.TestCase):

    def do_compute_GT(self, build_algo=None):
        d = 64 * 8
        k = 12
        flat_index = faiss.IndexBinaryFlat(d)
        xb = np.random.randint(
            low=0, high=256, size=(1000000, d // 8), dtype=np.uint8)
        flat_index.add(xb)
        queries = np.random.randint(
            low=0, high=256, size=(1000, d // 8), dtype=np.uint8)
        Dref, Iref = flat_index.search(queries, k)

        res = faiss.StandardGpuResources()

        # Configure the index with specified build algorithm
        config = faiss.GpuIndexCagraConfig()
        if build_algo is not None:
            config.build_algo = build_algo
            if build_algo == faiss.graph_build_algo_NN_DESCENT:
                config.nn_descent_niter = 20

        index = faiss.GpuIndexBinaryCagra(res, d, config)
        index.train(xb)
        Dnew, Inew = index.search(queries, k)

        evaluation.check_ref_knn_with_draws(Dref, Iref, Dnew, Inew, k)

    def test_compute_GT_nn_descent(self):
        self.do_compute_GT(faiss.graph_build_algo_NN_DESCENT)

    def test_compute_GT_iterative_search(self):
        self.do_compute_GT(faiss.graph_build_algo_ITERATIVE_SEARCH)


@unittest.skipIf(
    "CUVS" not in faiss.get_compile_options(),
    "only if cuVS is compiled in")
class TestIndexBinaryIDMap(unittest.TestCase):
    """Test IndexBinaryIDMap functionality with GpuIndexBinaryCagra"""

    def test_add_with_ids(self):
        d = 128 * 8
        k = 10
        n = 100000

        res = faiss.StandardGpuResources()

        # Create GpuIndexBinaryCagra with IDMap
        index_gpu = faiss.GpuIndexBinaryCagra(res, d)
        index_idmap = faiss.IndexBinaryIDMap(index_gpu)

        xb = np.random.randint(
            low=0, high=256, size=(n, d // 8), dtype=np.uint8)
        ids = np.arange(1000000, 1000000 + n).astype(np.int64)

        index_idmap.add_with_ids(xb, ids)

        nq = 1000
        xq = np.random.randint(
            low=0, high=256, size=(nq, d // 8), dtype=np.uint8)
        D, I = index_idmap.search(xq, k)

        self.assertTrue(np.all(I >= 1000000))
        self.assertTrue(np.all(I < 1000000 + n))

        D_exact, I_exact = index_idmap.search(xb[:10], 1)
        expected_ids = ids[:10].reshape(-1, 1)
        np.testing.assert_array_equal(I_exact, expected_ids)
        np.testing.assert_array_equal(D_exact, np.zeros((10, 1)))
