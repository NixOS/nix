#pragma once

#include "types.hh"

namespace nix {

typedef enum {
    lvlError = 0,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

typedef enum {
    actUnknown = 0,
    actCopyPath = 100,
    actDownload = 101,
    actRealise = 102,
    actCopyPaths = 103,
} ActivityType;

class Activity
{
public:
    typedef uint64_t t;
    const t id;
    Activity();
    Activity(const Activity & act) : id(act.id) { };
    Activity(uint64_t id) : id(id) { };
    Activity(ActivityType type, std::string msg = "");
    ~Activity();

    void progress(uint64_t done = 0, uint64_t expected = 0, uint64_t running = 0, uint64_t failed = 0) const;
};

typedef enum {
    evBuildCreated = 0,
    evBuildStarted = 1,
    evBuildOutput = 2,
    evBuildFinished = 3,

    evStartActivity = 1000,
    evStopActivity = 1001,
    evProgress = 1002,
    evSetExpected = 1003,

} EventType;

struct Event
{
    struct Field
    {
        // FIXME: use std::variant.
        enum { tInt, tString } type;
        uint64_t i = 0;
        std::string s;
        Field(const std::string & s) : type(tString), s(s) { }
        Field(const char * s) : type(tString), s(s) { }
        Field(const uint64_t & i) : type(tInt), i(i) { }
        Field(const Activity & act) : type(tInt), i(act.id) { }
    };

    typedef std::vector<Field> Fields;

    EventType type;
    Fields fields;

    std::string getS(size_t n) const
    {
        assert(n < fields.size());
        assert(fields[n].type == Field::tString);
        return fields[n].s;
    }

    uint64_t getI(size_t n) const
    {
        assert(n < fields.size());
        assert(fields[n].type == Field::tInt);
        return fields[n].i;
    }
};

class Logger
{
    friend class Activity;

public:

    virtual ~Logger() { }

    virtual void log(Verbosity lvl, const FormatOrString & fs) = 0;

    void log(const FormatOrString & fs)
    {
        log(lvlInfo, fs);
    }

    virtual void warn(const std::string & msg);

    template<typename... Args>
    void event(EventType type, const Args & ... args)
    {
        Event ev;
        ev.type = type;
        nop{(ev.fields.emplace_back(Event::Field(args)), 1)...};
        event(ev);
    }

    virtual void event(const Event & ev) = 0;
};

extern Logger * logger;

Logger * makeDefaultLogger();

extern Verbosity verbosity; /* suppress msgs > this */

/* Print a message if the current log level is at least the specified
   level. Note that this has to be implemented as a macro to ensure
   that the arguments are evaluated lazily. */
#define printMsg(level, args...) \
    do { \
        if (level <= nix::verbosity) { \
            logger->log(level, fmt(args)); \
        } \
    } while (0)

#define printError(args...) printMsg(lvlError, args)
#define printInfo(args...) printMsg(lvlInfo, args)
#define printTalkative(args...) printMsg(lvlTalkative, args)
#define debug(args...) printMsg(lvlDebug, args)
#define vomit(args...) printMsg(lvlVomit, args)

template<typename... Args>
inline void warn(const std::string & fs, Args... args)
{
    boost::format f(fs);
    nop{boost::io::detail::feed(f, args)...};
    logger->warn(f.str());
}

void warnOnce(bool & haveWarned, const FormatOrString & fs);

void writeToStderr(const string & s);

}
