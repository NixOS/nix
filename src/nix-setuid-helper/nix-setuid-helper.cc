#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <pwd.h>
#include <grp.h>

#include <iostream>
#include <vector>

#include "config.h"
#include "util.hh"

using namespace nix;


extern char * * environ;


/* Recursively change the ownership of `path' to user `uidTo' and
   group `gidTo'.  `path' must currently be owned by user `uidFrom',
   or, if `uidFrom' is -1, by group `gidFrom'. */
static void secureChown(uid_t uidFrom, gid_t gidFrom,
    uid_t uidTo, gid_t gidTo, const Path & path)
{
    format error = format("cannot change ownership of `%1%'") % path;
    
    struct stat st;
    if (lstat(path.c_str(), &st) == -1)
        /* Important: don't give any detailed error messages here.
           Otherwise, the Nix account can discover information about
           the existence of paths that it doesn't normally have access
           to. */
        throw Error(error);

    if (uidFrom != -1) {
        assert(uidFrom != 0);
        if (st.st_uid != uidFrom)
            throw Error(error);
    } else {
        assert(gidFrom != 0);
        if (st.st_gid != gidFrom)
            throw Error(error);
    }

    assert(uidTo != 0 && gidTo != 0);

#if HAVE_LCHOWN
    if (lchown(path.c_str(), uidTo, gidTo) == -1)
        throw Error(error);
#else
    if (!S_ISLNK(st.st_mode) &&
        chown(path.c_str(), uidTo, gidTo) == -1)
        throw Error(error);
#endif

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            /* !!! recursion; check stack depth */
	    secureChown(uidFrom, gidFrom, uidTo, gidTo, path + "/" + *i);
    }
}


static uid_t nameToUid(const string & userName)
{
    struct passwd * pw = getpwnam(userName.c_str());
    if (!pw)
        throw Error(format("user `%1%' does not exist") % userName);
    return pw->pw_uid;
}


static void checkIfBuildUser(const StringSet & buildUsers,
    const string & userName)
{
    if (buildUsers.find(userName) == buildUsers.end())
        throw Error(format("user `%1%' is not a member of the build users group")
            % userName);
}


/* Run `program' under user account `targetUser'.  `targetUser' should
   be a member of `buildUsersGroup'.  The ownership of the current
   directory is changed from the Nix user (uidNix) to the target
   user. */
static void runBuilder(uid_t uidNix, gid_t gidBuildUsers,
    const StringSet & buildUsers, const string & targetUser,
    string program, int argc, char * * argv, char * * env)
{
    uid_t uidTargetUser = nameToUid(targetUser);

    /* Sanity check. */
    if (uidTargetUser == 0)
        throw Error("won't setuid to root");

    /* Verify that the target user is a member of the build users
       group. */
    checkIfBuildUser(buildUsers, targetUser);
    
    /* Chown the current directory, *if* it is owned by the Nix
       account.  The idea is that the current directory is the
       temporary build directory in /tmp or somewhere else, and we
       don't want to create that directory here. */
    secureChown(uidNix, -1, uidTargetUser, gidBuildUsers, ".");

    /* Set the real, effective and saved gid.  Must be done before
       setuid(), otherwise it won't set the real and saved gids. */
    if (setgroups(0, 0) == -1)
        throw SysError("cannot clear the set of supplementary groups");

    if (setgid(gidBuildUsers) == -1 ||
        getgid() != gidBuildUsers ||
        getegid() != gidBuildUsers)
        throw SysError("setgid failed");

    /* Set the real, effective and saved uid. */
    if (setuid(uidTargetUser) == -1 ||
        getuid() != uidTargetUser ||
        geteuid() != uidTargetUser)
        throw SysError("setuid failed");

    /* Execute the program. */
    std::vector<const char *> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(0);
    
    if (execve(program.c_str(), (char * *) &args[0], env) == -1)
        throw SysError(format("cannot execute `%1%'") % program);
}


void killBuildUser(gid_t gidBuildUsers,
    const StringSet & buildUsers, const string & userName)
{
    uid_t uid = nameToUid(userName);
    
    /* Verify that the user whose processes we are to kill is a member
       of the build users group. */
    checkIfBuildUser(buildUsers, userName);

    assert(uid != 0);

    killUser(uid);
}


#ifndef NIX_SETUID_CONFIG_FILE
#define NIX_SETUID_CONFIG_FILE "/etc/nix-setuid.conf"
#endif


static void run(int argc, char * * argv) 
{
    char * * oldEnviron = environ;
    
    setuidCleanup();

    if (geteuid() != 0)
        throw Error("nix-setuid-wrapper must be setuid root");


    /* Read the configuration file.  It should consist of two words:
       
       <nix-user-name> <nix-builders-group>

       The first is the privileged account under which the main Nix
       processes run (i.e., the supposed caller).  It should match our
       real uid.  The second is the Unix group to which the Nix
       builders belong (and nothing else!). */
    string configFile = NIX_SETUID_CONFIG_FILE;
    AutoCloseFD fdConfig = open(configFile.c_str(), O_RDONLY);
    if (fdConfig == -1)
        throw SysError(format("opening `%1%'") % configFile);

    /* Config file should be owned by root. */
    struct stat st;
    if (fstat(fdConfig, &st) == -1) throw SysError("statting file");
    if (st.st_uid != 0)
        throw Error(format("`%1%' not owned by root") % configFile);
    if (st.st_mode & (S_IWGRP | S_IWOTH))
        throw Error(format("`%1%' should not be group or world-writable") % configFile);

    Strings tokens = tokenizeString(readFile(fdConfig));

    fdConfig.close();

    if (tokens.size() != 2)
        throw Error(format("parse error in `%1%'") % configFile);

    Strings::iterator i = tokens.begin();
    string nixUser = *i++;
    string buildUsersGroup = *i++;


    /* Check that the caller (real uid) is the one allowed to call
       this program. */
    uid_t uidNix = nameToUid(nixUser);
    if (uidNix != getuid())
        throw Error("you are not allowed to call this program, go away");
    
    
    /* Get the gid and members of buildUsersGroup. */
    struct group * gr = getgrnam(buildUsersGroup.c_str());
    if (!gr)
        throw Error(format("group `%1%' does not exist") % buildUsersGroup);
    gid_t gidBuildUsers = gr->gr_gid;

    StringSet buildUsers;
    for (char * * p = gr->gr_mem; *p; ++p)
        buildUsers.insert(*p);

    
    /* Perform the desired command. */
    if (argc < 2)
        throw Error("invalid arguments");

    string command(argv[1]);

    if (command == "run-builder") {
        /* Syntax: nix-setuid-helper run-builder <username> <program>
             <arg0 arg1...> */
        if (argc < 4) throw Error("missing user name / program name");
        runBuilder(uidNix, gidBuildUsers, buildUsers,
            argv[2], argv[3], argc - 4, argv + 4, oldEnviron);
    }

    else if (command == "get-ownership") {
        /* Syntax: nix-setuid-helper get-ownership <path> */
        if (argc != 3) throw Error("missing path");
        secureChown(-1, gidBuildUsers, uidNix, gidBuildUsers, argv[2]);
    }

    else if (command == "kill") {
        /* Syntax: nix-setuid-helper kill <username> */
        if (argc != 3) throw Error("missing user name");
        killBuildUser(gidBuildUsers, buildUsers, argv[2]);
    }

    else throw Error ("invalid command");
}


int main(int argc, char * * argv)
{
    try {
        run(argc, argv);
    } catch (Error & e) {
        std::cerr << e.msg() << std::endl;
        return 1;
    }
}
