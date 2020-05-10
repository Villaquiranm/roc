/*
 * Copyright (c) 2020 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_core/seqlock.h
//! @brief Seqlock.

#ifndef ROC_CORE_SEQLOCK_H_
#define ROC_CORE_SEQLOCK_H_

#include "roc_core/atomic.h"
#include "roc_core/atomic_ops.h"
#include "roc_core/cpu_ops.h"
#include "roc_core/cpu_traits.h"
#include "roc_core/noncopyable.h"

namespace roc {
namespace core {

//! Seqlock.
//!
//! Provides safe concurrent access to a single value.
//! Supports single writer and multiple readers.
//! Writes are lock-free and take priority over reades.
//!
//! See details on the barriers here:
//!  https://elixir.bootlin.com/linux/latest/source/include/linux/seqlock.h
//!  https://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf
template <class T> class Seqlock : public NonCopyable<> {
public:
    //! Initialize with given value.
    explicit Seqlock(T value = T())
        : val_(value) {
    }

    //! Store value.
    //! Should NOT be called concurrently.
    //! Is both lock-free and wait-free, i.e. it never waits for sleeping threads
    //! and never spins.
    void store(const T& value) {
        const int seq0 = seq_.load_relaxed();
        seq_.store_relaxed(seq0 + 1);
        AtomicOps::barrier_release();

        val_ = value;
        AtomicOps::barrier_release();

        seq_.store_relaxed(seq0 + 2);
    }

    //! Try to load value.
    //! Returns true if the value was loaded.
    //! May return false if concurrent store() is currently in progress.
    //! Is both lock-free and wait-free, i.e. it never waits for sleeping threads
    //! and never spins.
    //! If the concurrent store() is running and is not sleeping, retrying 3 times
    //! should be enough and try_load() will succeed. However in the rare case when
    //! the thread was preempted inside store() and is sleeping, try_load() will
    //! likely fail.
    bool try_load(T& ret) const {
        if (try_load_(ret)) {
            return true;
        }
        if (try_load_(ret)) {
            return true;
        }
        if (try_load_(ret)) {
            return true;
        }
        return false;
    }

    //! Load value.
    //! May spin until concurrent store() call completes.
    //! Is NOT lock-free (or wait-free).
    T load() const {
        T ret;
        while (!try_load_(ret)) {
            cpu_relax();
        }
        return ret;
    }

private:
    bool try_load_(T& ret) const {
        const int seq0 = seq_.load_relaxed();
        AtomicOps::barrier_acquire();

        if (seq0 & 1) {
            return false;
        }

        ret = val_;
        AtomicOps::barrier_acquire();

        const int seq1 = seq_.load_relaxed();

        return (seq0 == seq1);
    }

    Atomic<int> seq_;
    T val_;
};

//! Seqlock specialization for hardware-supported atomic types.
//!
//! Seqlock<T> is useful when not all platforms have native atomic operations
//! for given type T, e.g. int64_t. In this case, using seqlock allows to
//! write portable lock-free code.
//!
//! However, on CPUs that do support atomic operations for type T, we can
//! effectively replace seqlock with an atomic.
//!
//! This class implements Seqlock<T> interface using atomic operations.
template <class T> class AtomicSeqlock : public NonCopyable<> {
public:
    //! Initialize with given value.
    explicit AtomicSeqlock(T value)
        : val_(value) {
    }

    //! Store value.
    void store(const T& value) {
        AtomicOps::store_release(val_, value);
    }

    //! Try to load value.
    bool try_load(T& ret) const {
        ret = AtomicOps::load_acquire(val_);
        return true;
    }

    //! Load value.
    T load() const {
        return AtomicOps::load_acquire(val_);
    }

private:
    T val_;
};

// Seqlock<> specializations for platforms with native 64-bit atomics.
#if ROC_CPU_64BIT

//! Seqlock specialization for uint64_t.
template <> class Seqlock<uint64_t> : public AtomicSeqlock<uint64_t> {
public:
    //! Initialize with given value.
    explicit Seqlock(uint64_t value = 0)
        : AtomicSeqlock<uint64_t>(value) {
    }
};

//! Seqlock specialization for int64_t.
template <> class Seqlock<int64_t> : public AtomicSeqlock<int64_t> {
public:
    //! Initialize with given value.
    explicit Seqlock(int64_t value = 0)
        : AtomicSeqlock<int64_t>(value) {
    }
};

#endif // ROC_CPU_64BIT

} // namespace core
} // namespace roc

#endif // ROC_CORE_SEQLOCK_H_
