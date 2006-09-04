#include "aterm-map.hh"

#include <iostream>

#include <assert.h>
#include <stdlib.h>


namespace nix {


static const unsigned int maxLoadFactor = /* 1 / */ 3;
static unsigned int nrResizes = 0;
static unsigned int sizeTotalAlloc = 0;
static unsigned int sizeCurAlloc = 0;
static unsigned int sizeMaxAlloc = 0;


ATermMap::ATermMap(unsigned int expectedCount)
{
    init(expectedCount);
}


ATermMap::ATermMap(const ATermMap & map)
{
    init(map.maxCount);
    copy(map.hashTable, map.capacity);
}


ATermMap & ATermMap::operator = (const ATermMap & map)
{
    if (this == &map) return *this;
    free();
    init(map.maxCount);
    copy(map.hashTable, map.capacity);
    return *this;
}


ATermMap::~ATermMap()
{
    free();
}


void ATermMap::init(unsigned int expectedCount)
{
    assert(sizeof(ATerm) * 2 == sizeof(KeyValue));
    capacity = 0;
    count = 0;
    maxCount = 0;
    hashTable = 0;
    resizeTable(expectedCount);
}


void ATermMap::free()
{
    if (hashTable) {
        ATunprotectArray((ATerm *) hashTable);
        ::free(hashTable);
        sizeCurAlloc -= sizeof(KeyValue) * capacity;
        hashTable = 0;
    }
}


static unsigned int roundToPowerOf2(unsigned int x)
{
    x--;
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    x++;
    return x;
}


void ATermMap::resizeTable(unsigned int expectedCount)
{
    if (expectedCount == 0) expectedCount = 1;
//     cout << maxCount << " -> " << expectedCount << endl;
//     cout << maxCount << " " << size << endl;
//     cout << (double) size / maxCount << endl;

    unsigned int oldCapacity = capacity;
    KeyValue * oldHashTable = hashTable;

    maxCount = expectedCount;
    capacity = roundToPowerOf2(maxCount * maxLoadFactor);
    hashTable = (KeyValue *) calloc(sizeof(KeyValue), capacity);
    sizeTotalAlloc += sizeof(KeyValue) * capacity;
    sizeCurAlloc += sizeof(KeyValue) * capacity;
    if (sizeCurAlloc > sizeMaxAlloc) sizeMaxAlloc = sizeCurAlloc;
    ATprotectArray((ATerm *) hashTable, capacity * 2);
    
//     cout << capacity << endl;

    /* Re-hash the elements in the old table. */
    if (oldCapacity != 0) {
        count = 0;
        copy(oldHashTable, oldCapacity);
        ATunprotectArray((ATerm *) oldHashTable);
        ::free(oldHashTable);
        sizeCurAlloc -= sizeof(KeyValue) * oldCapacity;
        nrResizes++;
    }
}


void ATermMap::copy(KeyValue * elements, unsigned int capacity)
{
    for (unsigned int i = 0; i < capacity; ++i)
        if (elements[i].value) /* i.e., non-empty, non-deleted element */
            set(elements[i].key, elements[i].value);
}


/* !!! use a bigger shift for 64-bit platforms? */
static const unsigned int shift = 16;
static const unsigned long knuth = (unsigned long) (0.6180339887 * (1 << shift));


unsigned long ATermMap::hash1(ATerm key) const
{
    /* Don't care about the least significant bits of the ATerm
       pointer since they're always 0. */
    unsigned long key2 = ((unsigned long) key) >> 2;

    /* Approximately equal to:
    double d = key2 * 0.6180339887;
    unsigned int h = (int) (capacity * (d - floor(d)));
    */
 
    unsigned long h = (capacity * ((key2 * knuth) & ((1 << shift) - 1))) >> shift;

    return h;
}


unsigned long ATermMap::hash2(ATerm key) const
{
    unsigned long key2 = ((unsigned long) key) >> 2;
    /* Note: the result must be relatively prime to `capacity' (which
       is a power of 2), so we make sure that the result is always
       odd. */
    unsigned long h = ((key2 * 134217689) & (capacity - 1)) | 1;
    return h;
}


static unsigned int nrItemsSet = 0;
static unsigned int nrSetProbes = 0;


void ATermMap::set(ATerm key, ATerm value)
{
    if (count == maxCount) resizeTable(capacity * 2 / maxLoadFactor);
    
    nrItemsSet++;
    for (unsigned int i = 0, h = hash1(key); i < capacity;
         ++i, h = (h + hash2(key)) & (capacity - 1))
    {
        // assert(h < capacity);
        nrSetProbes++;
        /* Note: to see whether a slot is free, we check
           hashTable[h].value, not hashTable[h].key, since we use
           value == 0 to mark deleted slots. */
        if (hashTable[h].value == 0 || hashTable[h].key == key) {
            if (hashTable[h].value == 0) count++;
            hashTable[h].key = key;
            hashTable[h].value = value;
            return;
        }
    }
        
    abort();
}


static unsigned int nrItemsGet = 0;
static unsigned int nrGetProbes = 0;


ATerm ATermMap::get(ATerm key) const
{
    nrItemsGet++;
    for (unsigned int i = 0, h = hash1(key); i < capacity;
         ++i, h = (h + hash2(key)) & (capacity - 1))
    {
        nrGetProbes++;
        if (hashTable[h].key == 0) return 0;
        if (hashTable[h].key == key) return hashTable[h].value;
    }
    return 0;
}


void ATermMap::remove(ATerm key)
{
    for (unsigned int i = 0, h = hash1(key); i < capacity;
         ++i, h = (h + hash2(key)) & (capacity - 1))
    {
        if (hashTable[h].key == 0) return;
        if (hashTable[h].key == key) {
            if (hashTable[h].value != 0) {
                hashTable[h].value = 0;
                count--;
            }
            return;
        }
    }
}


unsigned int ATermMap::size()
{
    return count; /* STL nomenclature */
}


void printATermMapStats()
{
    using std::cerr;
    using std::endl;
    
    cerr << "RESIZES: " << nrResizes << " "
         << sizeTotalAlloc << " "
         << sizeCurAlloc << " "
         << sizeMaxAlloc << endl;
        
    cerr << "SET: "
         << nrItemsSet << " "
         << nrSetProbes << " "
         << (double) nrSetProbes / nrItemsSet << endl;

    cerr << "GET: "
         << nrItemsGet << " "
         << nrGetProbes << " "
         << (double) nrGetProbes / nrItemsGet << endl;
}


#if 0
int main(int argc, char * * argv)
{
    ATerm bottomOfStack;
    ATinit(argc, argv, &bottomOfStack);

    /* Make test terms. */
    int nrTestTerms = 100000;
    ATerm testTerms[nrTestTerms];

    for (int i = 0; i < nrTestTerms; ++i) {
        char name[10];
        sprintf(name, "%d", (int) random() % 37);

        int arity = i == 0 ? 0 : (random() % 37);
        ATerm kids[arity];
        for (int j = 0; j < arity; ++j)
            kids[j] = testTerms[random() % i];
        
        testTerms[i] = (ATerm) ATmakeApplArray(ATmakeAFun(name, arity, ATfalse), kids);
//         ATwriteToSharedTextFile(testTerms[i], stdout);
//         printf("\n");
    }


    cout << "testing...\n";

    
    #define someTerm() (testTerms[(int) random() % nrTestTerms])


    for (int test = 0; test < 100000; ++test) {
        //cerr << test << endl;
        unsigned int n = 300;
        ATermMap map(300);
        ATerm keys[n], values[n];
        for (unsigned int i = 0; i < n; ++i) {
            keys[i] = someTerm();
            values[i] = someTerm();
            map.set(keys[i], values[i]);
            //cerr << "INSERT: " << keys[i] << " " << values[i] << endl;
        }

        unsigned int size = map.size();
        assert(size <= n);
        values[n - 1] = 0;
        map.remove(keys[n - 1]);
        assert(map.size() == size - 1);

        unsigned int checksum;
        unsigned int count = 0;
        for (ATermMap::const_iterator i = map.begin(); i != map.end(); ++i, ++count) {
            assert(i->key);
            assert(i->value);
            checksum += (unsigned int) (*i).key;
            checksum += (unsigned int) (*i).value;
            // cout << (*i).key << " " << (*i).value << endl;
        }
        assert(count == size - 1);

        for (unsigned int i = 0; i < n; ++i) {
            for (unsigned int j = i + 1; j < n; ++j)
                if (keys[i] == keys[j]) goto x;
            if (map.get(keys[i]) != values[i]) {
                cerr << "MISMATCH: " << keys[i] << " " << values[i] << " " << map.get(keys[i]) << " " << i << endl;
                abort();
            }
            if (values[i] != 0) {
                checksum -= (unsigned int) keys[i];
                checksum -= (unsigned int) values[i];
            }
        x:  ;
        }

        assert(checksum == 0);
        
        for (unsigned int i = 0; i < 100; ++i)
            map.get(someTerm());
        
    }

    printATermMapStats();
}
#endif

 
}
