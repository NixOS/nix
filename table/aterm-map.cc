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
    void remove(const string & key);

private:
    void init(unsigned int expectedCount);

    void resizeTable(unsigned int expectedCount);

    unsigned int hash1(ATerm key) const;
    unsigned int hash2(ATerm key) const;
};


ATermMap::ATermMap(unsigned int expectedCount)
{
    init(expectedCount);
}


ATermMap::~ATermMap()
{
    if (hashTable) free(hashTable); 
}


void ATermMap::init(unsigned int expectedCount)
{
    size = 0;
    count = 0;
    maxCount = 0;
    hashTable = 0;
    resizeTable(expectedCount);
}


void ATermMap::resizeTable(unsigned int expectedCount)
{
    assert(size == 0);

    this->maxCount = expectedCount;

    unsigned int newSize = 128;

    hashTable = (KeyValue *) calloc(sizeof(KeyValue), newSize);

    size = newSize;
}


unsigned int ATermMap::hash1(ATerm key) const
{
    /* Don't care about the least significant bits of the ATerm
       pointer since they're always 0. */
    unsigned int key2 = ((unsigned int) key) >> 2;

#if 0
    double d1 = key2 * 0.6180339887;
    unsigned int h1 = (int) (size * (d1 - floor(d1)));
#endif

#if 0
    unsigned int h1 = size * (key2 * 61803 % 100000);
#endif

    unsigned int h1 = (size * ((key2 * 40503) & 0xffff)) >> 16;

//     cout << key2 << " " << h1 << endl;
    
//     unsigned int h1 = (key2 * 134217689) & (size - 1);

    return h1 % size;
}


unsigned int ATermMap::hash2(ATerm key) const
{
    unsigned int key2 = ((unsigned int) key) >> 2;

#if 0    
    double d2 = key2 * 0.6180339887;
    unsigned int h2 = 1 | (int) (size * (d2 - floor(d2)));
#endif

    unsigned int h3 = ((key2 * 134217689) & (size - 1)) | 1;
    return h3;
}


unsigned int nrItemsSet = 0;
unsigned int nrSetProbes = 0;
unsigned int nrMaxProbes = 0;


void ATermMap::set(ATerm key, ATerm value)
{
    unsigned int probes = 0;
    nrItemsSet++;
    for (unsigned int i = 0, h = hash1(key); i < size;
         ++i, h = (h + hash2(key)) & (size - 1))
    {
        assert(h < size);
        probes++;
        nrSetProbes++;
        if (hashTable[h].key == 0) {
            if (probes > nrMaxProbes) nrMaxProbes = probes;
            hashTable[h].key = key;
            hashTable[h].value = value;
            count++;
            return;
        }
    }
    abort();
}


unsigned int nrItemsGet = 0;
unsigned int nrGetProbes = 0;


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
        ATermMap map(100);
        for (int i = 0; i < 30; ++i) 
            map.set(someTerm(), someTerm());
        for (int i = 0; i < 100; ++i)
            map.get(someTerm());
    }

    cout << "SET: "
         << nrItemsSet << " "
         << nrSetProbes << " "
         << (double) nrSetProbes / nrItemsSet << " "
         << nrMaxProbes << endl;

    cout << "GET: "
         << nrItemsGet << " "
         << nrGetProbes << " "
         << (double) nrGetProbes / nrItemsGet << endl;
}
