#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <string>

#include "db.hh"

using namespace std;


extern Database nixDB;


/* Database tables. */

/* dbPath2Id :: Path -> FSId

   Each pair (p, id) records that path $p$ contains an expansion of
   $id$. */
extern string dbPath2Id;


/* dbId2Paths :: FSId -> [Path]

   A mapping from ids to lists of paths. */
extern string dbId2Paths;


/* dbSuccessors :: FSId -> FSId

   Each pair $(id_1, id_2)$ in this mapping records the fact that a
   successor of an fstate expression stored in a file with identifier
   $id_1$ is stored in a file with identifier $id_2$.

   Note that a term $y$ is successor of $x$ iff there exists a
   sequence of rewrite steps that rewrites $x$ into $y$.
*/
extern string dbSuccessors;


/* dbSubstitutes :: FSId -> [FSId]

   Each pair $(id, [ids])$ tells Nix that it can realise any of the
   fstate expressions referenced by the identifiers in $ids$ to
   generate a path with identifier $id$.

   The main purpose of this is for distributed caching of derivates.
   One system can compute a derivate with hash $h$ and put it on a
   website (as a Nix archive), for instance, and then another system
   can register a substitute for that derivate.  The substitute in
   this case might be an fstate expression that fetches the Nix
   archive. 
*/
extern string dbSubstitutes;


/* Path names. */

/* nixStore is the directory where we generally store atomic and
   derived files. */
extern string nixStore;

extern string nixDataDir; /* !!! fix */

/* nixLogDir is the directory where we log various operations. */ 
extern string nixLogDir;

/* nixDBPath is the path name of our Berkeley DB environment. */
extern string nixDBPath;


/* Open the database environment. */
void openDB();

/* Create the required database tables. */
void initDB();


#endif /* !__GLOBALS_H */
