#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

#include "db.hh"

using namespace std;


extern Database nixDB;


/* Database tables. */


/* dbValidPaths :: Path -> ()

   The existence of a key $p$ indicates that path $p$ is valid (that
   is, produced by a succesful build). */
extern TableId dbValidPaths;


/* dbSuccessors :: Path -> Path

   Each pair $(p_1, p_2)$ in this mapping records the fact that the
   Nix expression stored at path $p_1$ has a successor expression
   stored at path $p_2$.

   Note that a term $y$ is a successor of $x$ iff there exists a
   sequence of rewrite steps that rewrites $x$ into $y$.
*/
extern TableId dbSuccessors;


/* dbSubstitutes :: Path -> [Path]

   Each pair $(p, [ps])$ tells Nix that it can realise any of the
   Nix expressions stored at paths $ps$ to produce a path $p$.

   The main purpose of this is for distributed caching of derivates.
   One system can compute a derivate and put it on a website (as a Nix
   archive), for instance, and then another system can register a
   substitute for that derivate.  The substitute in this case might be
   a Nix expression that fetches the Nix archive.
*/
extern TableId dbSubstitutes;


/* Path names. */

/* nixStore is the directory where we generally store atomic and
   derived files. */
extern string nixStore;

extern string nixDataDir; /* !!! fix */

/* nixLogDir is the directory where we log various operations. */ 
extern string nixLogDir;

/* nixDBPath is the path name of our Berkeley DB environment. */
extern string nixDBPath;


/* Misc. global flags. */

/* Whether to keep temporary directories of failed builds. */
extern bool keepFailed;


/* Open the database environment. */
void openDB();

/* Create the required database tables. */
void initDB();


#endif /* !__GLOBALS_H */
