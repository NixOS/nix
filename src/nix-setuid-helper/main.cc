#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <pwd.h>
#include <grp.h>

#include <iostream>
#include <vector>

#include "util.hh"

#include "../libmain/setuid-common.hh"

using namespace nix;


static void secureChown(uid_t uidTarget, gid_t gidTarget,
    const Path & path)
{
    /* Recursively chown `path' to the specified uid and gid, but only
       if it is currently owned by the Nix account. */
    /* !!! */
}


static uid_t nameToUid(const string & userName)
{
    struct passwd * pw = getpwnam(userName.c_str());
    if (!pw)
        throw Error(format("user `%1%' does not exist") % userName);
    return pw->pw_uid;
}


static void runBuilder(const string & targetUser,
    string program, int argc, char * * argv)
{
    uid_t uidTargetUser = nameToUid(targetUser);
    gid_t gidBuilders = 1234;
    
    /* Chown the current directory, *if* it is owned by the Nix
       account.  The idea is that the current directory is the
       temporary build directory in /tmp or somewhere else, and we
       don't want to create that directory here. */
    secureChown(uidTargetUser, gidBuilders, ".");

                
    /* Set the real, effective and saved gid.  Must be done before
       setuid(), otherwise it won't set the real and saved gids. */
    if (setgroups(0, 0) == -1)
        throw SysError("cannot clear the set of supplementary groups");
    //setgid(gidBuilders);

    /* Set the real, effective and saved uid. */
    if (setuid(uidTargetUser) == -1 ||
        getuid() != uidTargetUser ||
        geteuid() != uidTargetUser)
        throw SysError("setuid failed");

    /* Execute the program. */
    std::vector<const char *> args;
    args.push_back(program.c_str());
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(0);
    
    if (execve(program.c_str(), (char * *) &args[0], 0) == -1)
        throw SysError(format("cannot execute `%1%'") % program);
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
    string allowedUser = *i++;
    string buildUsersGroup = *i++;


    /* Check that the caller (real uid) is the one allowed to call
       this program. */
    uid_t uidAllowedUser = nameToUid(allowedUser);
    if (uidAllowedUser != getuid())
        throw Error("you are not allowed to call this program, go away");
    
    
    /* Perform the desired command. */
    if (argc < 2)
        throw Error("invalid arguments");

    string command(argv[1]);

    if (command == "run-builder") {
        /* Syntax: nix-setuid-helper run-builder <username> <program>
             <args...> */
        if (argc < 4) throw Error("missing user name / program name");
        runBuilder(argv[2], argv[3], argc - 4, argv + 4);
    }

    else if (command == "fix-ownership") {
        /* Syntax: nix-setuid-helper <fix-ownership> <path> */
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
