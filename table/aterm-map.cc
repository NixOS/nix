#include <string>
#include <iostream>
#include <assert.h>
#include <math.h>
#include <aterm2.h>

using namespace std;


class ATermMap
{
private:

    struct KeyValue
    {
        ATerm key;
        ATerm value;
    };

    /* Hash table for the map.  We use open addressing, i.e., all
       key/value pairs are stored directly in the table, and there are
       no pointers.  Collisions are resolved through probing. */
    KeyValue * hashTable;

    /* Current size of the hash table. */
    unsigned int size;

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

    void remove(ATerm key);

private:
    void init(unsigned int expectedCount);

    void free();

    void resizeTable(unsigned int expectedCount);

    void copy(KeyValue * elements, unsigned int size);
    
    inline unsigned int hash1(ATerm key) const;
    inline unsigned int hash2(ATerm key) const;
};


ATermMap::ATermMap(unsigned int expectedCount)
{
    init(expectedCount * 10 / 9); /* slight adjustment */
}


ATermMap::ATermMap(const ATermMap & map)
{
    init(map.maxCount);
    copy(map.hashTable, map.size);
}


ATermMap & ATermMap::operator = (const ATermMap & map)
{
    if (this == &map) return *this;
    free();
    init(map.maxCount);
    copy(map.hashTable, map.size);
    return *this;
}


ATermMap::~ATermMap()
{
    free();
}


void ATermMap::init(unsigned int expectedCount)
{
    assert(sizeof(ATerm) * 2 == sizeof(KeyValue));
    size = 0;
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


static const unsigned int maxLoadFactor = /* 1 / */ 3;
static unsigned int nrResizes = 0;


void ATermMap::resizeTable(unsigned int expectedCount)
{
    if (expectedCount == 0) expectedCount = 1;
//     cout << maxCount << " -> " << expectedCount << endl;
//     cout << maxCount << " " << size << endl;
//     cout << (double) size / maxCount << endl;

    unsigned int oldSize = size;
    KeyValue * oldHashTable = hashTable;

    maxCount = expectedCount;
    size = roundToPowerOf2(maxCount * maxLoadFactor);
    hashTable = (KeyValue *) calloc(sizeof(KeyValue), size);
    ATprotectArray((ATerm *) hashTable, size * 2);
    
//     cout << size << endl;

    /* Re-hash the elements in the old table. */
    if (oldSize != 0) {
        count = 0;
        copy(oldHashTable, oldSize);
        ATunprotectArray((ATerm *) oldHashTable);
        ::free(oldHashTable);
        nrResizes++;
    }
}


void ATermMap::copy(KeyValue * elements, unsigned int size)
{
    for (unsigned int i = 0; i < size; ++i)
        if (elements[i].value) /* i.e., non-empty, non-deleted element */
            set(elements[i].key, elements[i].value);
}


static const unsigned int shift = 16;
static const unsigned int knuth = (unsigned int) (0.6180339887 * (1 << shift));


unsigned int ATermMap::hash1(ATerm key) const
{
    /* Don't care about the least significant bits of the ATerm
       pointer since they're always 0. */
    unsigned int key2 = ((unsigned int) key) >> 2;

    /* Approximately equal to:
    double d = key2 * 0.6180339887;
    unsigned int h = (int) (size * (d - floor(d)));
    */
 
    unsigned int h = (size * ((key2 * knuth) & ((1 << shift) - 1))) >> shift;

    return h;
}


unsigned int ATermMap::hash2(ATerm key) const
{
    unsigned int key2 = ((unsigned int) key) >> 2;
    /* Note: the result must be relatively prime to `size' (which is a
       power of 2), so we make sure that the result is always odd. */
    unsigned int h = ((key2 * 134217689) & (size - 1)) | 1;
    return h;
}


static unsigned int nrItemsSet = 0;
static unsigned int nrSetProbes = 0;


void ATermMap::set(ATerm key, ATerm value)
{
    if (count == maxCount) resizeTable(size * 2 / maxLoadFactor);
    
    nrItemsSet++;
    for (unsigned int i = 0, h = hash1(key); i < size;
         ++i, h = (h + hash2(key)) & (size - 1))
    {
        // assert(h < size);
        nrSetProbes++;
        /* Note: to see whether a slot is free, we check
           hashTable[h].value, not hashTable[h].key, since we use
           value == 0 to mark deleted slots. */
        if (hashTable[h].value == 0 || hashTable[h].key == key) {
            hashTable[h].key = key;
            hashTable[h].value = value;
            count++;
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
    for (unsigned int i = 0, h = hash1(key); i < size;
         ++i, h = (h + hash2(key)) & (size - 1))
    {
        nrGetProbes++;
        if (hashTable[h].key == 0) return 0;
        if (hashTable[h].key == key) return hashTable[h].value;
    }
    return 0;
}


void ATermMap::remove(ATerm key)
{
    for (unsigned int i = 0, h = hash1(key); i < size;
         ++i, h = (h + hash2(key)) & (size - 1))
    {
        if (hashTable[h].key == 0) return;
        if (hashTable[h].key == key) {
            hashTable[h].value = 0;
            return;
        }
    }
}


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
        // cerr << test << endl;
        unsigned int n = 300;
        ATermMap map(300);
        ATerm keys[n], values[n];
        for (unsigned int i = 0; i < n; ++i) {
            keys[i] = someTerm();
            values[i] = someTerm();
            map.set(keys[i], values[i]);
            // cerr << "INSERT: " << keys[i] << " " << values[i] << endl;
        }
        values[n - 1] = 0;
        map.remove(keys[n - 1]);
        for (unsigned int i = 0; i < n; ++i) {
            if (map.get(keys[i]) != values[i]) {
                for (unsigned int j = i + 1; j < n; ++j)
                    if (keys[i] == keys[j]) goto x;
                cerr << "MISMATCH: " << keys[i] << " " << values[i] << " " << map.get(keys[i]) << " " << i << endl;
                abort();
            x: ;
            }
        }
        for (unsigned int i = 0; i < 100; ++i)
            map.get(someTerm());
    }

    cout << "RESIZES: " << nrResizes << endl;
        
    cout << "SET: "
         << nrItemsSet << " "
         << nrSetProbes << " "
         << (double) nrSetProbes / nrItemsSet << endl;

    cout << "GET: "
         << nrItemsGet << " "
         << nrGetProbes << " "
         << (double) nrGetProbes / nrItemsGet << endl;
}
