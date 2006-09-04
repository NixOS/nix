#ifndef __ATERM_MAP_H
#define __ATERM_MAP_H

#include <aterm2.h>
#include <assert.h>


namespace nix {


class ATermMap
{
public:

    struct KeyValue
    {
        ATerm key;
        ATerm value;
    };

private:

    /* Hash table for the map.  We use open addressing, i.e., all
       key/value pairs are stored directly in the table, and there are
       no pointers.  Collisions are resolved through probing. */
    KeyValue * hashTable;

    /* Current size of the hash table. */
    unsigned int capacity;

    /* Number of elements in the hash table. */
    unsigned int count;

    /* Maximum number of elements in the hash table.  If `count'
       exceeds this number, the hash table is expanded. */
    unsigned int maxCount;
    
public:

    /* Create a map.  `expectedCount' is the number of elements the
       map is expected to hold. */
    ATermMap(unsigned int expectedCount);
    
    ATermMap(const ATermMap & map);
    
    ~ATermMap();

    ATermMap & operator = (const ATermMap & map);
        
    void set(ATerm key, ATerm value);

    ATerm get(ATerm key) const;

    ATerm operator [](ATerm key) const
    {
        return get(key);
    }

    void remove(ATerm key);

    unsigned int size();

    struct const_iterator
    {
        const ATermMap & map;
        unsigned int pos;
        const_iterator(const ATermMap & map, int pos) : map(map)
        {
            this->pos = pos;
        }
        bool operator !=(const const_iterator & i)
        {
            return pos != i.pos;
        }
        void operator ++()
        {
            if (pos == map.capacity) return;
            do { ++pos; 
            } while (pos < map.capacity && map.hashTable[pos].value == 0);
        }
        const KeyValue & operator *()
        {
            assert(pos < map.capacity);
            return map.hashTable[pos];
        }
        const KeyValue * operator ->()
        {
            assert(pos < map.capacity);
            return &map.hashTable[pos];
        }
    };

    friend class ATermMap::const_iterator;
    
    const_iterator begin() const
    {
        unsigned int i = 0;
        while (i < capacity && hashTable[i].value == 0) ++i;
        return const_iterator(*this, i);
    }
    
    const_iterator end() const
    {
        return const_iterator(*this, capacity);
    }

private:
    
    void init(unsigned int expectedCount);

    void free();

    void resizeTable(unsigned int expectedCount);

    void copy(KeyValue * elements, unsigned int capacity);
    
    inline unsigned long hash1(ATerm key) const;
    inline unsigned long hash2(ATerm key) const;
};


/* Hack. */
void printATermMapStats();

 
}


#endif /* !__ATERM_MAP_H */
