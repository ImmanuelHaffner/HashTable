# HashTable

This repository provides a single header file, that implements a hash table.  The hash table uses linear probing for
conflict resolution and provides an interface similar to `std::map` and `std::unordered_map` from the STL containers
library.


## Features

The hash table uses [Robin Hood Hashing](https://en.wikipedia.org/wiki/Hash_table#Robin_Hood_hashing) to minimize
variance of probe distances.  Contrary to common hash table implementations, the table does not resize and rehash based
on the load factor alone.  Additionally, the maximum probe distance during inserts is considered to decide whether
rehashing is necessary.  (If the hash function is *perfect* and no collisions occur, the table gladly fills up to 100%
load factor before growing.)

I implement a single, generic class `HashTable`, and provide convenience wrappers for set and map functionality.  This
avoids code duplication and minimizes sources of error.  The wrappers are `hash_set` and `hash_map`, and are dualities
of `std::unordered_set` and `std::unordered_map`.


## Example

TODO


## Note

The class `HashTable` and its wrappers `hash_set` and `hash_map` do not provide all the functionality of their STL
container library counter parts.  Nonetheless, it should be easy to extend the interfaces to your liking.
