#include <iostream>

#include "hash.hh"

int main(int argc, char * * argv)
{
    Hash h = hashFile("/etc/passwd");
    
    cout << (string) h << endl;

    h = parseHash("0b0ffd0538622bfe20b92c4aa57254d9");
    
    cout << (string) h << endl;

    return 0;
}
