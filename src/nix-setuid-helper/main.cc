#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
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


static void runBuilder(string userName,
    string program, int argc, char * * argv)
{
    struct passwd * pw = getpwnam(userName.c_str());
    if (!pw)
        throw Error(format("the user `%1%' does not exist") % userName);

    gid_t gidBuilders = 1234;
    
    /* Chown the current directory, *if* it is owned by the Nix
       account.  The idea is that the current directory is the
       temporary build directory in /tmp or somewhere else, and we
       don't want to create that directory here. */
    secureChown(pw->pw_uid, gidBuilders, ".");

    /* Set the real, effective and saved gid.  Must be done before
       setuid(), otherwise it won't set the real and saved gids. */
    //setgid(gidBuilders);

    /* Set the real, effective and saved uid. */
    setuid(pw->pw_uid);
    if (getuid() != pw->pw_uid || geteuid() != pw->pw_uid)
        throw Error("cannot setuid");

    /* Execute the program. */
    std::vector<const char *> args;
    args.push_back(program.c_str());
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(0);
    
    if (execve(program.c_str(), (char * *) &args[0], 0) == -1)
        throw SysError(format("cannot execute `%1%'") % program);
}


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
    /* !!! */
    
    
    /* Make sure that we are called by the Nix account, not by someone
       else. */
    // ...

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
