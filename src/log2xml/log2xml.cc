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
            } else if (c == '<')
                line += "&lt;";
            else if (c == '&')
                line += "&amp;";
            else
                line += c;
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
    string tag = inHeader ? "head" : "line";
    cout << "<" << tag << ">";
    cout << line;
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
