#!/usr/bin/env bash

((NEW_NIX_FIRST_BUILD_UID=301))

id_available(){
	dscl . list /Users UniqueID | grep -E '\b'$1'\b' >/dev/null
}

change_nixbld_names_and_ids(){
	local name uid next_id
	((next_id=NEW_NIX_FIRST_BUILD_UID))
	echo "Attempting to migrate nixbld users."
	echo "Each user should change from nixbld# to _nixbld#"
	echo "and their IDs relocated to $next_id+"
	while read -r name uid; do
		echo "   Checking $name (uid: $uid)"
		# iterate for a clean ID
		while id_available "$next_id"; do
			((next_id++))
			if ((next_id >= 400)); then
				echo "We've hit UID 400 without placing all of your users :("
				echo "You should use the commands in this script as a starting"
				echo "point to review your UID-space and manually move the"
				echo "remaining users (or delete them, if you don't need them)."
				exit 1
			fi
		done

		if [[ $name == _* ]]; then
			echo "      It looks like $name has already been renamed--skipping."
		else
			# first 3 are cleanup, it's OK if they aren't here
			sudo dscl . delete /Users/$name dsAttrTypeNative:_writers_passwd &>/dev/null || true
			sudo dscl . change /Users/$name NFSHomeDirectory "/private/var/empty 1" "/var/empty" &>/dev/null || true
			# remove existing user from group
			sudo dseditgroup -o edit -t user -d $name nixbld || true
			sudo dscl . change /Users/$name UniqueID $uid $next_id
			sudo dscl . change /Users/$name RecordName $name _$name
			# add renamed user to group
			sudo dseditgroup -o edit -t user -a _$name nixbld
			echo "      $name migrated to _$name (uid: $next_id)"
		fi
	done < <(dscl . list /Users UniqueID | grep nixbld | sort -n -k2)
}

change_nixbld_names_and_ids
