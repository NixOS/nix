/*
 * JSON schema validator for JSON for modern C++
 *
 * Copyright (c) 2016-2019 Patrick Boettcher <p@yai.se>.
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include <nlohmann/json-schema.hpp>

#include <algorithm>
#include <exception>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * Many of the RegExes are from @see http://jmrware.com/articles/2009/uri_regexp/URI_regex.html
 */

namespace
{
template <typename T> void range_check(const T value, const T min, const T max)
{
    if (!((value >= min) && (value <= max)))
    {
        std::stringstream out;
        out << "Value " << value << " should be in interval [" << min << "," << max << "] but is not!";
        throw std::invalid_argument(out.str());
    }
}

/** @see date_time_check */
void rfc3339_date_check(const std::string &value)
{
    const static std::regex dateRegex{R"(^([0-9]{4})\-([0-9]{2})\-([0-9]{2})$)"};

    std::smatch matches;
    if (!std::regex_match(value, matches, dateRegex))
    {
        throw std::invalid_argument(value + " is not a date string according to RFC 3339.");
    }

    const auto year = std::stoi(matches[1].str());
    const auto month = std::stoi(matches[2].str());
    const auto mday = std::stoi(matches[3].str());

    const auto isLeapYear = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));

    range_check(month, 1, 12);
    if (month == 2)
    {
        range_check(mday, 1, isLeapYear ? 29 : 28);
    }
    else if (month <= 7)
    {
        range_check(mday, 1, month % 2 == 0 ? 30 : 31);
    }
    else
    {
        range_check(mday, 1, month % 2 == 0 ? 31 : 30);
    }
}

/** @see date_time_check */
void rfc3339_time_check(const std::string &value)
{
    const static std::regex timeRegex{
        R"(^([0-9]{2})\:([0-9]{2})\:([0-9]{2})(\.[0-9]+)?(?:[Zz]|((?:\+|\-)[0-9]{2})\:([0-9]{2}))$)"};

    std::smatch matches;
    if (!std::regex_match(value, matches, timeRegex))
    {
        throw std::invalid_argument(value + " is not a time string according to RFC 3339.");
    }

    auto hour = std::stoi(matches[1].str());
    auto minute = std::stoi(matches[2].str());
    auto second = std::stoi(matches[3].str());
    // const auto secfrac      = std::stof( matches[4].str() );

    range_check(hour, 0, 23);
    range_check(minute, 0, 59);

    int offsetHour = 0, offsetMinute = 0;

    /* don't check the numerical offset if time zone is specified as 'Z' */
    if (!matches[5].str().empty())
    {
        offsetHour = std::stoi(matches[5].str());
        offsetMinute = std::stoi(matches[6].str());

        range_check(offsetHour, -23, 23);
        range_check(offsetMinute, 0, 59);
        if (offsetHour < 0)
            offsetMinute *= -1;
    }

    /**
     * @todo Could be made more exact by querying a leap second database and choosing the
     *       correct maximum in {58,59,60}. This current solution might match some invalid dates
     *       but it won't lead to false negatives. This only works if we know the full date, however
     */

    auto day_minutes = hour * 60 + minute - (offsetHour * 60 + offsetMinute);
    if (day_minutes < 0)
        day_minutes += 60 * 24;
    hour = day_minutes % 24;
    minute = day_minutes / 24;

    if (hour == 23 && minute == 59)
        range_check(second, 0, 60); // possible leap-second
    else
        range_check(second, 0, 59);
}

/**
 * @see https://tools.ietf.org/html/rfc3339#section-5.6
 *
 * @verbatim
 * date-fullyear   = 4DIGIT
 * date-month      = 2DIGIT  ; 01-12
 * date-mday       = 2DIGIT  ; 01-28, 01-29, 01-30, 01-31 based on
 *                          ; month/year
 * time-hour       = 2DIGIT  ; 00-23
 * time-minute     = 2DIGIT  ; 00-59
 * time-second     = 2DIGIT  ; 00-58, 00-59, 00-60 based on leap second
 *                          ; rules
 * time-secfrac    = "." 1*DIGIT
 * time-numoffset  = ("+" / "-") time-hour ":" time-minute
 * time-offset     = "Z" / time-numoffset
 *
 * partial-time    = time-hour ":" time-minute ":" time-second
 *                  [time-secfrac]
 * full-date       = date-fullyear "-" date-month "-" date-mday
 * full-time       = partial-time time-offset
 *
 * date-time       = full-date "T" full-time
 * @endverbatim
 * NOTE: Per [ABNF] and ISO8601, the "T" and "Z" characters in this
 *       syntax may alternatively be lower case "t" or "z" respectively.
 */
void rfc3339_date_time_check(const std::string &value)
{
    const static std::regex dateTimeRegex{
        R"(^([0-9]{4}\-[0-9]{2}\-[0-9]{2})[Tt]([0-9]{2}\:[0-9]{2}\:[0-9]{2}(?:\.[0-9]+)?(?:[Zz]|(?:\+|\-)[0-9]{2}\:[0-9]{2}))$)"};

    std::smatch matches;
    if (!std::regex_match(value, matches, dateTimeRegex))
    {
        throw std::invalid_argument(value + " is not a date-time string according to RFC 3339.");
    }

    rfc3339_date_check(matches[1].str());
    rfc3339_time_check(matches[2].str());
}

const std::string decOctet{R"((?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9]))"}; // matches numbers 0-255
const std::string ipv4Address{"(?:" + decOctet + R"(\.){3})" + decOctet};
const std::string h16{R"([0-9A-Fa-f]{1,4})"};
const std::string h16Left{"(?:" + h16 + ":)"};
const std::string ipv6Address{"(?:"
                              "(?:" +
                              h16Left +
                              "{6}"
                              "|::" +
                              h16Left +
                              "{5}"
                              "|(?:" +
                              h16 + ")?::" + h16Left +
                              "{4}"
                              "|(?:" +
                              h16Left + "{0,1}" + h16 + ")?::" + h16Left +
                              "{3}"
                              "|(?:" +
                              h16Left + "{0,2}" + h16 + ")?::" + h16Left +
                              "{2}"
                              "|(?:" +
                              h16Left + "{0,3}" + h16 + ")?::" + h16Left + "|(?:" + h16Left + "{0,4}" + h16 +
                              ")?::"
                              ")(?:" +
                              h16Left + h16 + "|" + ipv4Address +
                              ")"
                              "|(?:" +
                              h16Left + "{0,5}" + h16 + ")?::" + h16 + "|(?:" + h16Left + "{0,6}" + h16 +
                              ")?::"
                              ")"};
const std::string ipvFuture{R"([Vv][0-9A-Fa-f]+\.[A-Za-z0-9\-._~!$&'()*+,;=:]+)"};
const std::string regName{R"((?:[A-Za-z0-9\-._~!$&'()*+,;=]|%[0-9A-Fa-f]{2})*)"};
const std::string host{"(?:"
                       R"(\[(?:)" +
                       ipv6Address + "|" + ipvFuture + R"()\])" + "|" + ipv4Address + "|" + regName + ")"};

const std::string uuid{R"([0-9a-fA-F]{8}\-[0-9a-fA-F]{4}\-[0-9a-fA-F]{4}\-[0-9a-fA-F]{4}\-[0-9a-fA-F]{12})"};

// from http://stackoverflow.com/questions/106179/regular-expression-to-match-dns-hostname-or-ip-address
const std::string hostname{
    R"(^([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])(\.([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\-]{0,61}[a-zA-Z0-9]))*$)"};

/**
 * @see https://tools.ietf.org/html/rfc5322#section-4.1
 *
 * @verbatim
 * atom            =   [CFWS] 1*atext [CFWS]
 * word            =   atom / quoted-string
 * phrase          =   1*word / obs-phrase
 * obs-FWS         =   1*WSP *(CRLF 1*WSP)
 * FWS             =   ([*WSP CRLF] 1*WSP) /  obs-FWS
 *                                       ; Folding white space
 * ctext           =   %d33-39 /          ; Printable US-ASCII
 *                     %d42-91 /          ;  characters not including
 *                     %d93-126 /         ;  "(", ")", or "\"
 *                     obs-ctext
 * ccontent        =   ctext / quoted-pair / comment
 * comment         =   "(" *([FWS] ccontent) [FWS] ")"
 * CFWS            =   (1*([FWS] comment) [FWS]) / FWS
 * obs-local-part  =   word *("." word)
 * obs-domain      =   atom *("." atom)
 * obs-dtext       =   obs-NO-WS-CTL / quoted-pair
 * quoted-pair     =   ("\" (VCHAR / WSP)) / obs-qp
 * obs-NO-WS-CTL   =   %d1-8 /            ; US-ASCII control
 *                     %d11 /             ;  characters that do not
 *                     %d12 /             ;  include the carriage
 *                     %d14-31 /          ;  return, line feed, and
 *                     %d127              ;  white space characters
 * obs-ctext       =   obs-NO-WS-CTL
 * obs-qtext       =   obs-NO-WS-CTL
 * obs-utext       =   %d0 / obs-NO-WS-CTL / VCHAR
 * obs-qp          =   "\" (%d0 / obs-NO-WS-CTL / LF / CR)
 * obs-body        =   *((*LF *CR *((%d0 / text) *LF *CR)) / CRLF)
 * obs-unstruct    =   *((*LF *CR *(obs-utext *LF *CR)) / FWS)
 * obs-phrase      =   word *(word / "." / CFWS)
 * obs-phrase-list =   [phrase / CFWS] *("," [phrase / CFWS])
 * qtext           =   %d33 /             ; Printable US-ASCII
 *                     %d35-91 /          ;  characters not including
 *                     %d93-126 /         ;  "\" or the quote character
 *                     obs-qtext
 * qcontent        =   qtext / quoted-pair
 * quoted-string   =   [CFWS]
 *                     DQUOTE *([FWS] qcontent) [FWS] DQUOTE
 *                     [CFWS]
 * atext           =   ALPHA / DIGIT /    ; Printable US-ASCII
 *                     "!" / "#" /        ;  characters not including
 *                     "$" / "%" /        ;  specials.  Used for atoms.
 *                     "&" / "'" /
 *                     "*" / "+" /
 *                     "-" / "/" /
 *                     "=" / "?" /
 *                     "^" / "_" /
 *                     "`" / "{" /
 *                     "|" / "}" /
 *                     "~"
 * dot-atom-text   =   1*atext *("." 1*atext)
 * dot-atom        =   [CFWS] dot-atom-text [CFWS]
 * addr-spec       =   local-part "@" domain
 * local-part      =   dot-atom / quoted-string / obs-local-part
 * domain          =   dot-atom / domain-literal / obs-domain
 * domain-literal  =   [CFWS] "[" *([FWS] dtext) [FWS] "]" [CFWS]
 * dtext           =   %d33-90 /          ; Printable US-ASCII
 *                     %d94-126 /         ;  characters not including
 *                     obs-dtext          ;  "[", "]", or "\"
 * @endverbatim
 * @todo Currently don't have a working tool for this larger ABNF to generate a regex.
 *       Other options:
 *         - https://github.com/ldthomas/apg-6.3
 *         - https://github.com/akr/abnf
 *
 * The problematic thing are the allowed whitespaces (even newlines) in the email.
 * Ignoring those and starting with
 * @see https://stackoverflow.com/questions/13992403/regex-validation-of-email-addresses-according-to-rfc5321-rfc5322
 * and trying to divide up the complicated regex into understandable ABNF definitions from rfc5322 yields:
 */
const std::string obsnowsctl{R"([\x01-\x08\x0b\x0c\x0e-\x1f\x7f])"};
const std::string obsqp{R"(\\[\x01-\x09\x0b\x0c\x0e-\x7f])"};
const std::string qtext{R"((?:[\x21\x23-\x5b\x5d-\x7e]|)" + obsnowsctl + ")"};
const std::string dtext{R"([\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f])"};
const std::string quotedString{R"("(?:)" + qtext + "|" + obsqp + R"()*")"};
const std::string atext{R"([A-Za-z0-9!#$%&'*+/=?^_`{|}~-])"};
const std::string domainLiteral{R"(\[(?:(?:)" + decOctet + R"()\.){3}(?:)" + decOctet +
                                R"(|[A-Za-z0-9-]*[A-Za-z0-9]:(?:)" + dtext + "|" + obsqp + R"()+)\])"};

const std::string dotAtom{"(?:" + atext + R"(+(?:\.)" + atext + "+)*)"};
const std::string stackoverflowMagicPart{R"((?:[[:alnum:]](?:[[:alnum:]-]*[[:alnum:]])?\.)+)"
                                         R"([[:alnum:]](?:[[:alnum:]-]*[[:alnum:]])?)"};
const std::string email{"(?:" + dotAtom + "|" + quotedString + ")@(?:" + stackoverflowMagicPart + "|" + domainLiteral +
                        ")"};
} // namespace

namespace nlohmann
{
namespace json_schema
{
/**
 * Checks validity for built-ins by converting the definitions given as ABNF in the linked RFC from
 * @see https://json-schema.org/understanding-json-schema/reference/string.html#built-in-formats
 * into regular expressions using @see https://www.msweet.org/abnf/ and some manual editing.
 *
 * @see https://json-schema.org/latest/json-schema-validation.html
 */
void default_string_format_check(const std::string &format, const std::string &value)
{
    if (format == "date-time")
    {
        rfc3339_date_time_check(value);
    }
    else if (format == "date")
    {
        rfc3339_date_check(value);
    }
    else if (format == "time")
    {
        rfc3339_time_check(value);
    }
    else if (format == "email")
    {
        static const std::regex emailRegex{email};
        if (!std::regex_match(value, emailRegex))
        {
            throw std::invalid_argument(value + " is not a valid email according to RFC 5322.");
        }
    }
    else if (format == "hostname")
    {
        static const std::regex hostRegex{hostname};
        if (!std::regex_match(value, hostRegex))
        {
            throw std::invalid_argument(value + " is not a valid hostname according to RFC 3986 Appendix A.");
        }
    }
    else if (format == "ipv4")
    {
        const static std::regex ipv4Regex{"^" + ipv4Address + "$"};
        if (!std::regex_match(value, ipv4Regex))
        {
            throw std::invalid_argument(value + " is not an IPv4 string according to RFC 2673.");
        }
    }
    else if (format == "ipv6")
    {
        static const std::regex ipv6Regex{ipv6Address};
        if (!std::regex_match(value, ipv6Regex))
        {
            throw std::invalid_argument(value + " is not an IPv6 string according to RFC 5954.");
        }
    }
    else if (format == "uuid")
    {
        static const std::regex uuidRegex{uuid};
        if (!std::regex_match(value, uuidRegex))
        {
            throw std::invalid_argument(value + " is not an uuid string according to RFC 4122.");
        }
    }
    else if (format == "regex")
    {
        try
        {
            std::regex re(value, std::regex::ECMAScript);
        }
        catch (std::exception &exception)
        {
            throw exception;
        }
    }
    else
    {
        /* yet unsupported JSON schema draft 7 built-ins */
        static const std::vector<std::string> jsonSchemaStringFormatBuiltIns{
            "date-time",     "time",         "date",          "email",
            "idn-email",     "hostname",     "idn-hostname",  "ipv4",
            "ipv6",          "uri",          "uri-reference", "iri",
            "iri-reference", "uri-template", "json-pointer",  "relative-json-pointer",
            "regex"};
        if (std::find(jsonSchemaStringFormatBuiltIns.begin(), jsonSchemaStringFormatBuiltIns.end(), format) !=
            jsonSchemaStringFormatBuiltIns.end())
        {
            throw std::logic_error("JSON schema string format built-in " + format + " not yet supported. " +
                                   "Please open an issue or use a custom format checker.");
        }

        throw std::logic_error("Don't know how to validate " + format);
    }
}
} // namespace json_schema
} // namespace nlohmann
