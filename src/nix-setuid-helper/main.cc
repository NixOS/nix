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


/* Recursively change the ownership of `path' from `uidFrom' to
   `uidTo' and `gidTo'.  Barf if we encounter a file not owned by
   `uidFrom'. */
static void secureChown(uid_t uidFrom, uid_t uidTo, gid_t gidTo,
    const Path & path)
{
    struct stat st;
    if (lstat(path.c_str(), &st) == -1)
        throw SysError(format("statting of `%1%'") % path);

    if (st.st_uid != uidFrom)
        throw Error(format("path `%1%' owned by the wrong owner") % path);

    if (lchown(path.c_str(), uidTo, gidTo) == -1)
        throw SysError(format("changing ownership of `%1%'") % path);

    if (S_ISDIR(st.st_mode)) {
        Strings names = readDirectory(path);
	for (Strings::iterator i = names.begin(); i != names.end(); ++i)
            /* !!! recursion; check stack depth */
	    secureChown(uidFrom, uidTo, gidTo, path + "/" + *i);
    }
}


static uid_t nameToUid(const string & userName)
{
    struct passwd * pw = getpwnam(userName.c_str());
    if (!pw)
        throw Error(format("user `%1%' does not exist") % userName);
    return pw->pw_uid;
}


/* Run `program' under user account `targetUser'.  `targetUser' should
   be a member of `buildUsersGroup'.  The ownership of the current
   directory is changed from the Nix user (uidNix) to the target
   user. */
static void runBuilder(uid_t uidNix,
    const string & buildUsersGroup, const string & targetUser,
    string program, int argc, char * * argv, char * * env)
{
    uid_t uidTargetUser = nameToUid(targetUser);

    /* Sanity check. */
    if (uidTargetUser == 0)
        throw Error("won't setuid to root");

    /* Get the gid and members of buildUsersGroup. */
    struct group * gr = getgrnam(buildUsersGroup.c_str());
    if (!gr)
        throw Error(format("group `%1%' does not exist") % buildUsersGroup);
    gid_t gidBuildUsers = gr->gr_gid;

    /* Verify that the target user is a member of that group. */
    Strings users;
    bool found = false;
    for (char * * p = gr->gr_mem; *p; ++p)
        if (string(*p) == targetUser) {
            found = true;
            break;
        }
    if (!found)
        throw Error(format("user `%1%' is not a member of `%2%'")
            % targetUser % buildUsersGroup);
    
    /* Chown the current directory, *if* it is owned by the Nix
       account.  The idea is that the current directory is the
       temporary build directory in /tmp or somewhere else, and we
       don't want to create that directory here. */
    secureChown(uidNix, uidTargetUser, gidBuildUsers, ".");

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
    
    
    /* Perform the desired command. */
    if (argc < 2)
        throw Error("invalid arguments");

    string command(argv[1]);

    if (command == "run-builder") {
        /* Syntax: nix-setuid-helper run-builder <username> <program>
             <arg0 arg1...> */
        if (argc < 4) throw Error("missing user name / program name");
        runBuilder(uidNix, buildUsersGroup,
            argv[2], argv[3], argc - 4, argv + 4, oldEnviron);
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
