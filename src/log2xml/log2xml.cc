#include <iostream>
#include <cstdio>
#include <string>

using namespace std;


struct Decoder
{
    enum { stTop, stEscape, stCSI } state;
    string line;
    bool inHeader;
    int level;

    Decoder()
    {
        state = stTop;
        line = "";
        inHeader = false;
        level = 0;
    }

    void pushChar(char c);

    void finishLine();
};


void Decoder::pushChar(char c)
{
    switch (state) {
        
        case stTop:
            if (c == '\e') {
                state = stEscape;
            } else if (c == '\n') {
                finishLine();
            } else line += c;
            break;

        case stEscape:
            if (c == '[')
                state = stCSI;
            else
                state = stTop; /* !!! wrong */
            break;

        case stCSI:
            if (c >= 0x40 && c != 0x7e) {
                state = stTop;
                switch (c) {
                    case 'p':
                        if (line.size()) finishLine();
                        level++;
                        inHeader = true;
                        cout << "<nest>" << endl;
                        break;
                    case 'q':
                        if (line.size()) finishLine();
                        if (level > 0) {
                            level--;
                            cout << "</nest>" << endl;
                        } else
                            cerr << "not enough nesting levels" << endl;
                        break;
                }
            }
            break;
            
    }
}


void Decoder::finishLine()
{
    string storeDir = "/nix/store/";
    int sz = storeDir.size();
    string tag = inHeader ? "head" : "line";
    cout << "<" << tag << ">";

    for (int i = 0; i < line.size(); i++) {

        if (line[i] == '<') cout << "&lt;";
        else if (line[i] == '&') cout << "&amp;";
        else if (i + sz + 33 < line.size() &&
            string(line, i, sz) == storeDir &&
            line[i + sz + 32] == '-')
        {
            int j = i + sz + 32;
            /* skip name */
            while (!strchr("/\n\r\t ()[]:;?<>", line[j])) j++;
            int k = j;
            while (!strchr("\n\r\t ()[]:;?<>", line[k])) k++;
            // !!! escaping
            cout << "<storeref>"
                 << "<storedir>"
                 << string(line, i, sz)
                 << "</storedir>"
                 << "<hash>"
                 << string(line, i + sz, 32)
                 << "</hash>"
                 << "<name>"
                 << string(line, i + sz + 32, j - (i + sz + 32))
                 << "</name>"
                 << "<path>"
                 << string(line, j, k - j)
                 << "</path>"
                 << "</storeref>";
            i = k - 1;
        } else cout << line[i];
    }
    
    cout << "</" << tag << ">" << endl;
    line = "";
    inHeader = false;
}


int main(int argc, char * * argv)
{
    Decoder dec;
    int c;

    cout << "<logfile>" << endl;
    
    while ((c = getchar()) != EOF) {
        dec.pushChar(c);
    }

    cout << "</logfile>" << endl;
}
