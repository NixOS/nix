#pragma once

#include <iostream>
#include <string>
#include <list>
#include <map>


namespace nix {

using std::string;
using std::map;
using std::list;


typedef map<string, string> XMLAttrs;


class XMLWriter
{
private:

    std::ostream & output;

    bool indent;
    bool closed;

    list<string> pendingElems;

public:

    XMLWriter(bool indent, std::ostream & output);
    ~XMLWriter();

    void close();

    void openElement(const string & name,
        const XMLAttrs & attrs = XMLAttrs());
    void closeElement();

    void writeEmptyElement(const string & name,
        const XMLAttrs & attrs = XMLAttrs());

private:
    void writeAttrs(const XMLAttrs & attrs);

    void indent_(size_t depth);
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


}
