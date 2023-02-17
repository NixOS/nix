#include "nix/util/xml-writer.hh"
#include <gtest/gtest.h>
#include <sstream>

namespace nix {

    /* ----------------------------------------------------------------------------
     * XMLWriter
     * --------------------------------------------------------------------------*/

    TEST(XMLWriter, emptyObject) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n");
    }

    TEST(XMLWriter, objectWithEmptyElement) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
            t.openElement("foobar");
        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar></foobar>");
    }

    TEST(XMLWriter, objectWithElementWithAttrs) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
            XMLAttrs attrs = {
                { "foo", "bar" }
            };
            t.openElement("foobar", attrs);
        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar foo=\"bar\"></foobar>");
    }

    TEST(XMLWriter, objectWithElementWithEmptyAttrs) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
            XMLAttrs attrs = {};
            t.openElement("foobar", attrs);
        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar></foobar>");
    }

    TEST(XMLWriter, objectWithElementWithAttrsEscaping) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
            XMLAttrs attrs = {
                { "<key>", "<value>" }
            };
            t.openElement("foobar", attrs);
        }

        // XXX: While "<value>" is escaped, "<key>" isn't which I think is a bug.
        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar <key>=\"&lt;value&gt;\"></foobar>");
    }

    TEST(XMLWriter, objectWithElementWithAttrsIndented) {
        std::stringstream out;
        {
            XMLWriter t(true, out);
            XMLAttrs attrs = {
                { "foo", "bar" }
            };
            t.openElement("foobar", attrs);
        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar foo=\"bar\">\n</foobar>\n");
    }

    TEST(XMLWriter, writeEmptyElement) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
            t.writeEmptyElement("foobar");
        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar />");
    }

    TEST(XMLWriter, writeEmptyElementWithAttributes) {
        std::stringstream out;
        {
            XMLWriter t(false, out);
            XMLAttrs attrs = {
                { "foo", "bar" }
            };
            t.writeEmptyElement("foobar", attrs);

        }

        ASSERT_EQ(out.str(), "<?xml version='1.0' encoding='utf-8'?>\n<foobar foo=\"bar\" />");
    }

}
