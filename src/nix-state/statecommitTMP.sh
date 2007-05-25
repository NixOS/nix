#! /bin/sh

#TODO EXCLUDE PATHS AND MAKE PARAMETERS AND STORE OBJECT!

svnbin=/nix/var/nix/profiles/per-user/root/profile/bin/svn
subversionedpaths=( /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/ )
subversionedpathsInterval=( 0 0 )
nonversionedpaths=( /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/cache/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/test/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/test2/test2/ /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/logging/ )
checkouts=( "/nix/var/nix/profiles/per-user/root/profile/bin/svn checkout file:///nix/staterepos/99dj5zg1ginj5as75nkb0psnp02krv2s-hellohardcodedstateworld-1.0 /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/" "/nix/var/nix/profiles/per-user/root/profile/bin/svn checkout file:///nix/staterepos/9ph3nd4irpvgs66h24xjvxrwpnrwy9n0-hellohardcodedstateworld-1.0 /nix/state/v6rr3yi5ilgn3k0kwxkk633ap4z0m1zi-hellohardcodedstateworld-1.0/log/" )

# if a dir exists, get his rev. number or check it out again (maybe the dir was deleted)

i=0
for path in ${subversionedpaths[@]}
do
   if test -d $path; then 
      cd $path;
      output=$($svnbin stat 2>&1 | grep "is not a working copy");
      if [ "$output" != "" ] ; then       										#if dirs exists but is not yet an svn dir: create repos
          ${checkouts[$i]};
      fi
        
	  repos=$(svn info | grep "Repository Root" | sed 's/Repository Root: //');	# get the revision number of the repository		
	  revision=$(svn info $repos | grep "Revision: " | sed 's/Revision: //');
      interval=${subversionedpathsInterval[$i]};
      
      #TODO BUG !!!!!!!! THE REVISION DOESNT GO UP WE NEED A DB CONNECTION OR A FILE TO HOLD A COUNTER ...!
      
      if [ "$interval" = "0" ] || [ "$($revision % $interval)" = "0" ]; then	# if we dont have an interval or the interval is due... commit
       															
		  allsubdirs=( $(echo *) );
		  subdirs=();
		  for subdir in ${allsubdirs[@]}										#add all, exlucding explicity stated direct versioned-subdirs or explicity stated nonversioned-subdirs
	      do																	#this is only to prevent some warnings, ultimately we would like svn add to have a option 'exclude dirs'
			  subdir="$(pwd)/$subdir/";
			  exclude=0;
			  
			  for svnp in ${subversionedpaths[@]}
			  do
			  	  if [ "$svnp" = "$subdir" ]; then
			  	    exclude=1;
			  	  fi
			  done
	
			  for nonvp in ${nonversionedpaths[@]}
			  do	
			  	  if [ "$nonvp" = "$subdir" ]; then
			  	    exclude=1;
			  	  fi
			  done
			  
			  if [ $exclude = 0 ]; then
	             subdirs[${#subdirs[*]}]=$subdir
	          fi
	      done  
		  
		  if [ "$subdirs" != "" ]; then
	         svn add $subdirs;
	      
	      	 for revpath in ${nonversionedpaths[@]}								#revert sub-sub* dirs that havent been excluded
		     do
		       if test -d $revpath; then
		         if [ "${revpath:0:${#path}}" == "$path" ]; then
			 	   #echo "$path revert $revpath";
			 	   svn revert $revpath;
		         fi
		       fi
		     done
		     svn -m "" commit;
	      fi
      
      fi
      
      cd - &> /dev/null;
   fi
  let "i+=1"
done

