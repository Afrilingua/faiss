/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef FAISS_INDEX_BINARY_H
#define FAISS_INDEX_BINARY_H

#include <cstdint>
#include <cstdio>

#include <faiss/Index.h>

namespace faiss {

/// Forward declarations see AuxIndexStructures.h
struct IDSelector;
struct RangeSearchResult;

/** Abstract structure for a binary index.
 *
 * Supports adding vertices and searching them.
 *
 * All queries are symmetric because there is no distinction between codes and
 * vectors.
 */
struct IndexBinary {
    using component_t = uint8_t;
    using distance_t = int32_t;

    int d = 0;            ///< vector dimension
    int code_size = 0;    ///< number of bytes per vector ( = d / 8 )
    idx_t ntotal = 0;     ///< total nb of indexed vectors
    bool verbose = false; ///< verbosity level

    /// set if the Index does not require training, or if training is done
    /// already
    bool is_trained = true;

    /// type of metric this index uses for search
    MetricType metric_type = METRIC_L2;

    explicit IndexBinary(idx_t d = 0, MetricType metric = METRIC_L2);

    virtual ~IndexBinary();

    /** Perform training on a representative set of vectors.
     *
     * @param n      nb of training vectors
     * @param x      training vecors, size n * d / 8
     */
    virtual void train(idx_t n, const uint8_t* x);
    virtual void train(idx_t n, const void* x, NumericType numeric_type) {
        if (numeric_type == NumericType::UInt8) {
            train(n, static_cast<const uint8_t*>(x));
        } else {
            FAISS_THROW_MSG("IndexBinary::train: unsupported numeric type");
        }
    };

    /** Add n vectors of dimension d to the index.
     *
     * Vectors are implicitly assigned labels ntotal .. ntotal + n - 1
     * @param x      input matrix, size n * d / 8
     */
    virtual void add(idx_t n, const uint8_t* x) = 0;
    virtual void add(idx_t n, const void* x, NumericType numeric_type) {
        if (numeric_type == NumericType::UInt8) {
            add(n, static_cast<const uint8_t*>(x));
        } else {
            FAISS_THROW_MSG("IndexBinary::add: unsupported numeric type");
        }
    };

    /** Same as add, but stores xids instead of sequential ids.
     *
     * The default implementation fails with an assertion, as it is
     * not supported by all indexes.
     *
     * @param xids if non-null, ids to store for the vectors (size n)
     */
    virtual void add_with_ids(idx_t n, const uint8_t* x, const idx_t* xids);
    virtual void add_with_ids(
            idx_t n,
            const void* x,
            NumericType numeric_type,
            const idx_t* xids) {
        if (numeric_type == NumericType::UInt8) {
            add_with_ids(n, static_cast<const uint8_t*>(x), xids);
        } else {
            FAISS_THROW_MSG(
                    "IndexBinary::add_with_ids: unsupported numeric type");
        }
    };

    /** Query n vectors of dimension d to the index.
     *
     * return at most k vectors. If there are not enough results for a
     * query, the result array is padded with -1s.
     *
     * @param x           input vectors to search, size n * d / 8
     * @param labels      output labels of the NNs, size n*k
     * @param distances   output pairwise distances, size n*k
     */
    virtual void search(
            idx_t n,
            const uint8_t* x,
            idx_t k,
            int32_t* distances,
            idx_t* labels,
            const SearchParameters* params = nullptr) const = 0;
    virtual void search(
            idx_t n,
            const void* x,
            NumericType numeric_type,
            idx_t k,
            int32_t* distances,
            idx_t* labels,
            const SearchParameters* params = nullptr) const {
        if (numeric_type == NumericType::UInt8) {
            search(n,
                   static_cast<const uint8_t*>(x),
                   k,
                   distances,
                   labels,
                   params);
        } else {
            FAISS_THROW_MSG("IndexBinary::search: unsupported numeric type");
        }
    };

    /** Query n vectors of dimension d to the index.
     *
     * return all vectors with distance < radius. Note that many indexes
     * do not implement the range_search (only the k-NN search is
     * mandatory). The distances are converted to float to reuse the
     * RangeSearchResult structure, but they are integer. By convention,
     * only distances < radius (strict comparison) are returned,
     * ie. radius = 0 does not return any result and 1 returns only
     * exact same vectors.
     *
     * @param x           input vectors to search, size n * d / 8
     * @param radius      search radius
     * @param result      result table
     */
    virtual void range_search(
            idx_t n,
            const uint8_t* x,
            int radius,
            RangeSearchResult* result,
            const SearchParameters* params = nullptr) const;

    /** Return the indexes of the k vectors closest to the query x.
     *
     * This function is identical to search but only returns labels of
     * neighbors.
     * @param x           input vectors to search, size n * d / 8
     * @param labels      output labels of the NNs, size n*k
     */
    void assign(idx_t n, const uint8_t* x, idx_t* labels, idx_t k = 1) const;

    /// Removes all elements from the database.
    virtual void reset() = 0;

    /** Removes IDs from the index. Not supported by all indexes.
     */
    virtual size_t remove_ids(const IDSelector& sel);

    /** Reconstruct a stored vector.
     *
     * This function may not be defined for some indexes.
     * @param key         id of the vector to reconstruct
     * @param recons      reconstucted vector (size d / 8)
     */
    virtual void reconstruct(idx_t key, uint8_t* recons) const;

    /** Reconstruct vectors i0 to i0 + ni - 1.
     *
     * This function may not be defined for some indexes.
     * @param recons      reconstucted vectors (size ni * d / 8)
     */
    virtual void reconstruct_n(idx_t i0, idx_t ni, uint8_t* recons) const;

    /** Similar to search, but also reconstructs the stored vectors (or an
     * approximation in the case of lossy coding) for the search results.
     *
     * If there are not enough results for a query, the resulting array
     * is padded with -1s.
     *
     * @param recons      reconstructed vectors size (n, k, d)
     **/
    virtual void search_and_reconstruct(
            idx_t n,
            const uint8_t* x,
            idx_t k,
            int32_t* distances,
            idx_t* labels,
            uint8_t* recons,
            const SearchParameters* params = nullptr) const;

    /** Display the actual class name and some more info. */
    void display() const;

    /** moves the entries from another dataset to self.
     * On output, other is empty.
     * add_id is added to all moved ids
     * (for sequential ids, this would be this->ntotal) */
    virtual void merge_from(IndexBinary& otherIndex, idx_t add_id = 0);

    /** check that the two indexes are compatible (ie, they are
     * trained in the same way and have the same
     * parameters). Otherwise throw. */
    virtual void check_compatible_for_merge(
            const IndexBinary& otherIndex) const;

    /** size of the produced codes in bytes */
    virtual size_t sa_code_size() const;

    /** Same as add_with_ids for IndexBinary. */
    virtual void add_sa_codes(idx_t n, const uint8_t* codes, const idx_t* xids);
};

} // namespace faiss

#endif // FAISS_INDEX_BINARY_H
