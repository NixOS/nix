#ifndef __XML_WRITER_H
#define __XML_WRITER_H

#include <iostream>
#include <string>
#include <list>
#include <map>

using namespace std;


typedef map<string, string> XMLAttrs;


class XMLWriter
{
private:
    
    ostream & output;

    bool indent;
    bool closed;

    list<string> pendingElems;

public:

    XMLWriter(bool indent, ostream & output);
    ~XMLWriter();

    void close();

    void openElement(const string & name,
        const XMLAttrs & attrs = XMLAttrs());
    void closeElement();

    void writeEmptyElement(const string & name,
        const XMLAttrs & attrs = XMLAttrs());
    
    void writeCharData(const string & data);

private:
    void writeAttrs(const XMLAttrs & attrs);

    void indent_(unsigned int depth);
};


class XMLOpenElement
{
private:
    XMLWriter & writer;
public:
    XMLOpenElement(XMLWriter & writer, const string & name,
        const XMLAttrs & attrs = XMLAttrs())
        : writer(writer)
    {
        writer.openElement(name, attrs);
    }
    ~XMLOpenElement()
    {
        writer.closeElement();
    }
};


#endif /* !__XML_WRITER_H */
