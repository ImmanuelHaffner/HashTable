#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>


namespace {

template<typename T>
T ceil_to_pow2(T n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    if constexpr (sizeof(T) >= 2) {
        n |= n >> 8;
        if constexpr (sizeof(T) >= 4) {
            n |= n >> 16;
            if constexpr (sizeof(T) >= 8) {
                n |= n >> 32;
            }
        }
    }
    n++;
    return n;
}

}

template<
    typename Key,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
struct HashTable
{
    using key_type = Key;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using size_type = std::size_t;

    using reference = Key&;
    using const_reference = const Key&;
    using pointer = Key*;
    using const_pointer = const Key*;

    private:
    template<bool C>
    struct the_iterator
    {
        friend struct HashTable;

        static constexpr bool is_const = C;
        using pointer_type = std::conditional_t<is_const, const_pointer, pointer>;
        using reference_type = std::conditional_t<is_const, const_reference, reference>;

        the_iterator(key_type *ptr, key_type *end, key_type empty, bool advance = true)
            : ptr(ptr)
            , end(end)
            , EMPTY(empty)
        {
            key_equal eq;
            if (advance and ptr != end and eq(*ptr, EMPTY))
                this->operator++();
        }

        /** Compare this iterator with an other iterator for equality. */
        bool operator==(the_iterator other) const { return this->ptr == other.ptr; }
        bool operator!=(the_iterator other) const { return not this->operator==(other); }

        /** Advance the iterator to the next element. */
        the_iterator & operator++() {
            key_equal eq;
            if (ptr != end) {
                do
                    ++ptr;
                while (ptr != end and eq(*ptr, EMPTY));
            }
            assert(ptr == end or not eq(*ptr, EMPTY));
            return *this;
        }

        the_iterator operator++(int) {
            the_iterator cpy(*this);
            this->operator++();
            return cpy;
        }

        /** Return a reference to the designated element */
        reference_type operator*() const { return *ptr; }
        /** Return a pointer to the designated element. */
        pointer_type operator->() const { return ptr; }

        private:
        pointer_type ptr;
        pointer_type end;
        key_type EMPTY;
    };
    public:
    using iterator = the_iterator<false>;
    using const_iterator = the_iterator<true>;

    iterator begin() { return mkiter(table_); }
    iterator end()   { return mkiter(table_ + capacity_); }
    const_iterator begin() const { return mkiter(table_); }
    const_iterator end()   const { return mkiter(table_ + capacity_); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend()   const { return end(); }

    HashTable(key_type empty = key_type(), size_type capacity = 1024)
        : EMPTY(empty)
        , table_(nullptr)
        , size_(0)
        , capacity_(ceil_to_pow2(capacity))
        , watermark_(capacity_ * MIN_LOADFACTOR_RESIZE)
    {
        table_ = static_cast<pointer>(malloc(capacity_ * sizeof(key_type)));
        for (pointer p = table_, end = table_ + capacity_; p != end; ++p)
            new (p) key_type(EMPTY);
    }

    HashTable(const HashTable &other) = delete;
    HashTable(HashTable&&) = default;

    ~HashTable() {
        key_equal eq;
        for (key_type *p = table_, *end = table_ + capacity_; p != end; ++p)
            if (not eq(*p, EMPTY)) p->~key_type();
        free(table_);
    }

    size_type size() const { return size_; }
    size_type capacity() const { return capacity_; }

    /** If an element with key exists, returns an iterator to that element.  Otherwise, returns end(). */
    iterator find(const key_type &key) {
        key_equal eq;
        pointer p = hint(key);
        assert(p < table_ + capacity_);
        if (not eq(*p, EMPTY)) {
            for (;;) {
                if (eq(*p, EMPTY)) goto hint;
                if (eq(*p, key)) return mkiter(p);
                if (++p == table_ + capacity_) p = table_; /* CONTINUE */
            }
            __builtin_unreachable();
        }
hint:   return mkhint(p);
    }
    const_iterator find(const key_type &key) const { return mkiter(const_cast<HashTable*>(this)->find(key).ptr); }

    bool has(const key_type &key) const { return find(key) != end(); }
    bool operator()(const key_type &key) const { return has(key); }

    /** Returns an iterator to the element in the table and a flag whether insertion succeeded.  The flag is true, if
     * the element was newly inserted into the table, and false otherwise.  The iterator designates the newly inserted
     * element respectively the element already present in the table. */
    std::pair<iterator, bool> insert(key_type key) { return insert_hint(hint(key), key); }
    std::pair<iterator, bool> insert_hint(iterator hint, key_type key) { return insert_hint(hint.end, key); }

    /** Increase the capacity of the table to accomodate for at least new_cap many elements. */
    void reserve(const size_type new_cap) {
        key_equal eq;
        if (new_cap <= capacity_) return;

        /* Grow the table. */
        size_type alt_cap = ceil_to_pow2(new_cap);
        table_ = static_cast<key_type*>(realloc(table_, alt_cap * sizeof(key_type)));
        for (pointer p = table_ + capacity_, end = table_ + alt_cap; p != end; ++p)
            new (p) key_type(EMPTY);

        /* Find the first element in it's optimal slot. */
        pointer p = table_;
        if (not eq(table_[capacity_ - 1], EMPTY))
            while (not eq(*p, EMPTY)) ++p;
        while (eq(*p, EMPTY)) ++p;
        assert(not eq(*p, EMPTY));
        assert(hint(*p) == p);

        /* Rehash all elements. */
        using std::swap;
        size_ = 0;
        probe_distance_ = 0;
        watermark_ = MIN_LOADFACTOR_RESIZE * alt_cap;
        swap(capacity_, alt_cap);
        const auto end = p;
        for (;;) {
            assert(table_ <= p);
            assert(p < table_ + alt_cap);
            assert(not eq(*p, EMPTY));

            key_type key = EMPTY;
            swap(key, *p);
            assert(not eq(key, EMPTY));
            assert(eq(*p, EMPTY));
#ifndef NDEBUG
            key_equal eq;
            auto res =
#endif
            insert(key);
            assert(res.second);
            assert(eq(*res.first, key));

loop:
            if (++p == table_ + alt_cap) p = table_; /* wrap at end of old table */
            if (p == end) break;
            if (eq(*p, EMPTY)) goto loop;
        }
    }

    const key_type EMPTY;
    private:
    static constexpr float MAX_PROBE_DISTANCE = 5;
    static constexpr float MIN_LOADFACTOR_RESIZE = .5;
    key_type *table_;
    size_type size_;
    size_type capacity_;
    size_type probe_distance_ = 0;
    size_type watermark_;

    iterator mkiter(key_type *ptr) { return iterator(ptr, table_ + capacity_, EMPTY); }
    const_iterator mkiter(key_type *ptr) const { return const_iterator(ptr, table_ + capacity_, EMPTY); }
    iterator mkhint(key_type *ptr) { return iterator(table_ + capacity_, ptr, EMPTY, false); }

    key_type * hint(const key_type &key) {
        hasher h;
        const size_type hash = h(key);
        assert((hash & (capacity_ - 1)) == hash % capacity_ and "invalid capacity");
        return table_ + (hash & (capacity_ - 1));
    }

    std::pair<iterator, bool> insert_hint(pointer p, key_type key) {
        /* TODO 3.1 */
        if ((probe_distance_ >= MAX_PROBE_DISTANCE and size_ >= watermark_) or size_ + 1 == capacity_) {
            reserve(2 * capacity_);
            p = hint(key); // update hint after resizing
        }

        hasher h;
        key_equal eq;
        assert(p < table_ + capacity_);
        size_type probe_distance = 0;

        if (not eq(*p, EMPTY)) {
            for (;;) {
                if (eq(*p, EMPTY)) goto place_free;
                if (eq(*p, key)) goto failed;

                const size_type others_dist = ((p - table_) - h(*p)) & (capacity_ - 1);
                if (others_dist < probe_distance)
                    goto place_reserved;
                ++p;
                ++probe_distance;
                if (p == table_ + capacity_) p = table_; /* CONTINUE */
            }
failed:
            return std::make_pair(mkiter(p), false);

place_reserved:
            probe_distance_ = std::max(probe_distance_, probe_distance);
            auto res = std::make_pair(mkiter(p), true);
            using std::swap;
            while (not eq(key, EMPTY)) { // shift elements by 1 until we reach a free slot
                swap(*p, key);
                if (++p == table_ + capacity_) p = table_; /* CONTINUE */
            }
            assert(eq(key, EMPTY));
            ++size_;
            return res;
        }

place_free:
        probe_distance_ = std::max(probe_distance_, probe_distance);
        assert(eq(*p, EMPTY));
        new (p) key_type(key);
        ++size_;
        return std::make_pair(mkiter(p), true);
    }
};

template<
    typename Key,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
using hash_set = HashTable<Key, Hash, KeyEqual>;

namespace {

template<typename Key, typename Value, typename Hash>
struct _key_hash
{
    std::size_t operator()(const std::pair<Key, Value> &pair) const {
        return Hash{}(pair.first);
    }
};

template<typename Key, typename Value, typename KeyEqual>
struct _key_equal
{
    bool operator()(const std::pair<Key, Value> &first, const std::pair<Key, Value> &second) const {
        return KeyEqual{}(first.first, second.first);
    }
};

}

template<
    typename Key,
    typename Value,
    typename Hash = std::hash<Key>,
    typename KeyEqual = std::equal_to<Key>>
struct hash_map
{
    public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using hasher = _key_hash<const Key, Value, Hash>;
    using key_equal = _key_equal<const Key, Value, KeyEqual>;
    using size_type = std::size_t;

    using reference = Key&;
    using const_reference = const Key&;
    using pointer = Key*;
    using const_pointer = const Key*;

    private:
    using table_type = HashTable<std::pair<key_type, mapped_type>, hasher, key_equal>;

    public:
    using iterator = typename table_type::iterator;
    using const_iterator = typename table_type::const_iterator;

    public:
    hash_map(Key empty = Key()) : table_(std::make_pair(empty, Value())) { }
    hash_map(Key empty, std::size_t capacity) : table_(std::make_pair(empty, Value()), capacity) { }

    iterator begin() { return table_.begin(); }
    iterator end() { return table_.end(); }
    const_iterator begin() const { return table_.begin(); }
    const_iterator end() const { return table_.end(); }
    const_iterator cbegin() const { return table_.cbegin(); }
    const_iterator cend() const { return table_.cend(); }

    size_type size() const { return table_.size(); }
    size_type capacity() const { return table_.capacity(); }

    iterator find(const key_type &key) { return table_.find({key, mapped_type()}); }
    const_iterator find(const key_type &key) const { return table_.find({key, mapped_type()}); }

    std::pair<iterator, bool> insert(const value_type &value) { return table_.insert(value); }

    mapped_type & operator[](const key_type& key) {
        auto probe = std::make_pair(key, mapped_type());
        auto it = table_.find(probe);
        if (it == table_.end())
            it = table_.insert_hint(it, probe).first;
        return it->second;
    }

    private:
    table_type table_;
};
